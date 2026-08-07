#include "gb/app/frontend/realtime/retroachievements_image_cache.hpp"

#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/app/runtime_paths.hpp"

#include "rhash/md5.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <bcrypt.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <stdlib.h>
#elif defined(__linux__)
#include <sys/random.h>
#endif
#endif

namespace gb::frontend {

namespace {

constexpr std::size_t maximumImageSize = 2U * 1024U * 1024U;
constexpr std::array<std::uint8_t, 8> pngSignature{0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
constexpr std::array<std::uint8_t, 3> jpegSignature{0xFFU, 0xD8U, 0xFFU};
std::atomic<std::uint64_t> nextImageRequestId{1};

bool startsWithIgnoringCase(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), value.begin(), [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs))
            == std::tolower(static_cast<unsigned char>(rhs));
    });
}

bool isHttpsUrl(std::string_view url) {
    return startsWithIgnoringCase(url, "https://");
}

std::optional<std::string_view> imageExtension(const std::vector<std::uint8_t>& body) {
    if (body.size() >= pngSignature.size()
        && std::equal(pngSignature.begin(), pngSignature.end(), body.begin())) {
        return ".png";
    }
    if (body.size() >= jpegSignature.size()
        && std::equal(jpegSignature.begin(), jpegSignature.end(), body.begin())) {
        return ".jpg";
    }
    return std::nullopt;
}

std::string hexEncode(const std::uint8_t* bytes, std::size_t size) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2U);
    for (std::size_t index = 0; index < size; ++index) {
        result.push_back(hex[(bytes[index] >> 4U) & 0x0FU]);
        result.push_back(hex[bytes[index] & 0x0FU]);
    }
    return result;
}

std::optional<std::string> secureNonce() {
    std::array<std::uint8_t, 16> bytes{};
#if defined(_WIN32)
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return std::nullopt;
    }
#elif defined(__APPLE__)
    arc4random_buf(bytes.data(), bytes.size());
#elif defined(__linux__)
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
        if (count == 0) {
            return std::nullopt;
        }
        offset += static_cast<std::size_t>(count);
    }
#else
    std::random_device randomDevice;
    for (std::uint8_t& byte : bytes) {
        byte = static_cast<std::uint8_t>(randomDevice());
    }
#endif
    return hexEncode(bytes.data(), bytes.size());
}

bool isSafeDestination(const std::filesystem::path& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    return (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0;
#else
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0) {
        return errno == ENOENT;
    }
    return S_ISREG(status.st_mode);
#endif
}

bool isSafeCacheDirectory(const std::filesystem::path& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    struct stat status {};
    return ::lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

std::optional<std::string_view> imageFileExtension(const std::filesystem::path& path) {
    if (!isSafeDestination(path)) {
        return std::nullopt;
    }
    std::array<std::uint8_t, pngSignature.size()> prefix{};
    std::size_t count = 0;
#if defined(_WIN32)
    HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    DWORD read = 0;
    const bool inspected = GetFileInformationByHandle(handle, &info) != 0
        && (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0
        && info.nFileSizeHigh == 0
        && info.nFileSizeLow <= maximumImageSize;
    const bool readSucceeded = inspected
        && ReadFile(handle, prefix.data(), static_cast<DWORD>(prefix.size()), &read, nullptr) != 0;
    const bool closeSucceeded = CloseHandle(handle) != 0;
    if (!readSucceeded || !closeSucceeded) {
        return std::nullopt;
    }
    count = read;
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0) {
        return std::nullopt;
    }
    struct stat status {};
    const ssize_t read = ::read(descriptor, prefix.data(), prefix.size());
    const bool inspected = ::fstat(descriptor, &status) == 0
        && S_ISREG(status.st_mode)
        && status.st_size >= 0
        && static_cast<std::uintmax_t>(status.st_size) <= maximumImageSize;
    const bool closeSucceeded = ::close(descriptor) == 0;
    if (!inspected || read < 0 || !closeSucceeded) {
        return std::nullopt;
    }
    count = static_cast<std::size_t>(read);
#endif
    const std::vector<std::uint8_t> bytes(prefix.begin(), prefix.begin() + count);
    return imageExtension(bytes);
}

struct TemporaryFile {
    std::filesystem::path path;
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int descriptor = -1;
#endif
};

void removeTemporaryFile(const std::filesystem::path& path) {
#if defined(_WIN32)
    DeleteFileW(path.c_str());
#else
    ::unlink(path.c_str());
#endif
}

std::optional<TemporaryFile> createTemporaryFile(const std::filesystem::path& finalPath) {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto nonce = secureNonce();
        if (!nonce.has_value()) {
            return std::nullopt;
        }
        const std::filesystem::path path = finalPath.string() + ".tmp." + *nonce;
#if defined(_WIN32)
        HANDLE handle = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr
        );
        if (handle != INVALID_HANDLE_VALUE) {
            return TemporaryFile{path, handle};
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
            return std::nullopt;
        }
#else
        const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (descriptor >= 0) {
            return TemporaryFile{path, descriptor};
        }
        if (errno != EEXIST) {
            return std::nullopt;
        }
#endif
    }
    return std::nullopt;
}

bool writeAndClose(TemporaryFile& temporary, const std::vector<std::uint8_t>& body) {
#if defined(_WIN32)
    std::size_t offset = 0;
    while (offset < body.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            body.size() - offset,
            static_cast<std::size_t>(MAXDWORD)
        ));
        DWORD written = 0;
        if (WriteFile(temporary.handle, body.data() + offset, chunk, &written, nullptr) == 0
            || written != chunk) {
            CloseHandle(temporary.handle);
            temporary.handle = INVALID_HANDLE_VALUE;
            return false;
        }
        offset += written;
    }
    const bool flushed = FlushFileBuffers(temporary.handle) != 0;
    const bool closed = CloseHandle(temporary.handle) != 0;
    temporary.handle = INVALID_HANDLE_VALUE;
    return flushed && closed;
#else
    std::size_t offset = 0;
    while (offset < body.size()) {
        const ssize_t written = ::write(
            temporary.descriptor,
            body.data() + offset,
            body.size() - offset
        );
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(temporary.descriptor);
            temporary.descriptor = -1;
            return false;
        }
        if (written == 0) {
            ::close(temporary.descriptor);
            temporary.descriptor = -1;
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    const bool flushed = ::fsync(temporary.descriptor) == 0;
    const bool closed = ::close(temporary.descriptor) == 0;
    temporary.descriptor = -1;
    return flushed && closed;
#endif
}

bool replaceWithTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
#if defined(_WIN32)
    return MoveFileExW(
        temporary.c_str(),
        destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != 0;
#else
    return ::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
}

std::optional<std::filesystem::path> cachedPathFor(
    const std::filesystem::path& cacheDirectory,
    std::string_view url
) {
    if (!isHttpsUrl(url) || !isSafeCacheDirectory(cacheDirectory)) {
        return std::nullopt;
    }
    const std::filesystem::path base = cacheDirectory / cacheKey(url);
    for (const char* extension : {".png", ".jpg"}) {
        const std::filesystem::path candidate = base.string() + extension;
        if (const auto kind = imageFileExtension(candidate); kind.has_value() && *kind == extension) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool writeImageAtomically(
    const std::filesystem::path& cacheDirectory,
    std::string_view url,
    const std::vector<std::uint8_t>& body
) {
    if (body.size() > maximumImageSize) {
        return false;
    }
    const auto extension = imageExtension(body);
    if (!extension.has_value()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(cacheDirectory, ec);
    if (ec || !isSafeCacheDirectory(cacheDirectory)) {
        return false;
    }

    const std::filesystem::path finalPath = cacheDirectory / (cacheKey(url) + std::string(*extension));
    if (!isSafeDestination(finalPath)) {
        return false;
    }
    if (const auto existing = imageFileExtension(finalPath);
        existing.has_value() && *existing == *extension) {
        return true;
    }
    const auto temporary = createTemporaryFile(finalPath);
    if (!temporary.has_value()) {
        return false;
    }
    TemporaryFile file = std::move(*temporary);
    if (!writeAndClose(file, body) || !isSafeDestination(finalPath)) {
        removeTemporaryFile(file.path);
        return false;
    }
    if (!replaceWithTemporaryFile(file.path, finalPath)) {
        removeTemporaryFile(file.path);
        return false;
    }
    return true;
}

void applyPath(std::string_view url, std::string& path, const RetroAchievementsImageCache& cache) {
    path = cache.localPath(url).value_or(std::string{});
}

} // namespace

struct RetroAchievementsImageCache::State {
    std::unordered_set<std::string> inflightUrls;
    std::unordered_set<std::string> failedUrls;
    std::unordered_set<std::string> probedUrls;
    std::unordered_map<std::string, std::string> localPaths;
    std::unordered_map<std::uint64_t, std::string> urlByRequestId;
    std::size_t filesystemProbes = 0;
};

std::optional<std::uint64_t> allocateImageRequestId() {
    std::uint64_t current = nextImageRequestId.load(std::memory_order_relaxed);
    while (current != 0) {
        if (nextImageRequestId.compare_exchange_weak(
                current,
                current + 1U,
                std::memory_order_relaxed,
                std::memory_order_relaxed
            )) {
            return current;
        }
    }
    return std::nullopt;
}

std::string cacheKey(std::string_view url) {
    md5_state_t state;
    md5_byte_t digest[16]{};
    md5_init(&state);
    std::size_t offset = 0;
    while (offset < url.size()) {
        const std::size_t count = std::min<std::size_t>(url.size() - offset, INT_MAX);
        md5_append(
            &state,
            reinterpret_cast<const md5_byte_t*>(url.data() + offset),
            static_cast<int>(count)
        );
        offset += count;
    }
    md5_finish(&state, digest);

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (const md5_byte_t byte : digest) {
        result.push_back(hex[(byte >> 4U) & 0x0FU]);
        result.push_back(hex[byte & 0x0FU]);
    }
    return result;
}

RetroAchievementsImageCache::RetroAchievementsImageCache(
    RaHttpTransport& transport,
    std::string cacheDirectory
)
    : transport_(transport)
    , cacheDirectory_(cacheDirectory.empty() ? gb::retroAchievementsCacheDirectory() : std::move(cacheDirectory))
    , state_(std::make_unique<State>()) {
}

RetroAchievementsImageCache::~RetroAchievementsImageCache() {
    shutdown();
}

void RetroAchievementsImageCache::request(std::string url) {
    if (stopping_ || !isHttpsUrl(url)
        || state_->inflightUrls.find(url) != state_->inflightUrls.end()
        || state_->failedUrls.find(url) != state_->failedUrls.end()
        || localPath(url).has_value()) {
        return;
    }
    if (!state_->inflightUrls.insert(url).second) {
        return;
    }

    const auto requestId = allocateImageRequestId();
    if (!requestId.has_value()) {
        state_->inflightUrls.erase(url);
        return;
    }
    state_->urlByRequestId.emplace(*requestId, url);
    if (!transport_.submit({*requestId, RaHttpChannel::Image, std::move(url), {}})) {
        state_->inflightUrls.erase(state_->urlByRequestId.at(*requestId));
        state_->urlByRequestId.erase(*requestId);
    }
}

void RetroAchievementsImageCache::processCompleted() {
    for (const RaHttpResponse& response : transport_.takeCompleted(RaHttpChannel::Image)) {
        const auto request = state_->urlByRequestId.find(response.id);
        if (request == state_->urlByRequestId.end()) {
            continue;
        }
        const std::string url = std::move(request->second);
        state_->urlByRequestId.erase(request);
        state_->inflightUrls.erase(url);
        if (!response.error.empty()) {
            state_->failedUrls.insert(url);
            continue;
        }
        if (writeImageAtomically(cacheDirectory_, url, response.body)) {
            if (const auto path = cachedPathFor(cacheDirectory_, url); path.has_value()) {
                state_->localPaths[url] = path->string();
            }
        } else {
            state_->failedUrls.insert(url);
        }
    }
}

std::optional<std::string> RetroAchievementsImageCache::localPath(std::string_view url) const {
    const std::string key(url);
    if (const auto found = state_->localPaths.find(key);
        found != state_->localPaths.end()) {
        return found->second;
    }
    if (!state_->probedUrls.insert(key).second) {
        return std::nullopt;
    }
    ++state_->filesystemProbes;
    const auto path = cachedPathFor(cacheDirectory_, url);
    if (!path.has_value()) {
        return std::nullopt;
    }
    state_->localPaths.emplace(key, path->string());
    return path->string();
}

std::size_t RetroAchievementsImageCache::filesystemProbeCount() const {
    return state_->filesystemProbes;
}

void RetroAchievementsImageCache::retryFailed() {
    state_->failedUrls.clear();
}

void RetroAchievementsImageCache::shutdown() {
    if (stopping_) {
        return;
    }
    stopping_ = true;
    state_->inflightUrls.clear();
    state_->urlByRequestId.clear();
}

void applyCachedImagePaths(RaSessionSnapshot& snapshot, const RetroAchievementsImageCache& cache) {
    applyPath(snapshot.profile.user.avatarUrl, snapshot.profile.user.avatarPath, cache);
    applyPath(snapshot.currentGame.badgeUrl, snapshot.currentGame.badgePath, cache);
    for (RaGameProgressSummary& game : snapshot.profile.library) {
        applyPath(game.badgeUrl, game.badgePath, cache);
    }
    for (RaAchievementSummary& achievement : snapshot.currentAchievements) {
        applyPath(achievement.badgeUrl, achievement.badgePath, cache);
    }
}

void applyCachedImagePathsForUrls(
    RaSessionSnapshot& snapshot,
    const RetroAchievementsImageCache& cache,
    const std::vector<std::string>& urls
) {
    const std::unordered_set<std::string> visible(urls.begin(), urls.end());
    const auto applyVisible = [&](std::string_view url, std::string& path) {
        if (visible.find(std::string(url)) != visible.end()) {
            applyPath(url, path, cache);
        }
    };
    applyVisible(snapshot.profile.user.avatarUrl, snapshot.profile.user.avatarPath);
    applyVisible(snapshot.currentGame.badgeUrl, snapshot.currentGame.badgePath);
    for (RaGameProgressSummary& game : snapshot.profile.library) {
        applyVisible(game.badgeUrl, game.badgePath);
    }
    for (RaAchievementSummary& achievement : snapshot.currentAchievements) {
        applyVisible(achievement.badgeUrl, achievement.badgePath);
    }
}

} // namespace gb::frontend
