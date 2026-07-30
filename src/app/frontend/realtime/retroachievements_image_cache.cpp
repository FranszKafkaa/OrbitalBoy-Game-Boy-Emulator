#include "gb/app/frontend/realtime/retroachievements_image_cache.hpp"

#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/app/runtime_paths.hpp"

#include "rhash/md5.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gb::frontend {

namespace {

constexpr std::size_t maximumImageSize = 2U * 1024U * 1024U;
constexpr std::array<std::uint8_t, 8> pngSignature{0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
constexpr std::array<std::uint8_t, 3> jpegSignature{0xFFU, 0xD8U, 0xFFU};

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

std::optional<std::string_view> imageFileExtension(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)
        || std::filesystem::file_size(path, ec) > maximumImageSize) {
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::array<std::uint8_t, pngSignature.size()> prefix{};
    input.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
    const std::size_t count = static_cast<std::size_t>(input.gcount());
    const std::vector<std::uint8_t> bytes(prefix.begin(), prefix.begin() + count);
    return imageExtension(bytes);
}

std::optional<std::filesystem::path> cachedPathFor(
    const std::filesystem::path& cacheDirectory,
    std::string_view url
) {
    if (!isHttpsUrl(url)) {
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
    const std::vector<std::uint8_t>& body,
    std::uint64_t requestId
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
    if (ec) {
        return false;
    }

    const std::filesystem::path finalPath = cacheDirectory / (cacheKey(url) + std::string(*extension));
    if (const auto existing = imageFileExtension(finalPath);
        existing.has_value() && *existing == *extension) {
        return true;
    }
    const std::filesystem::path temporaryPath = finalPath.string() + ".tmp." + std::to_string(requestId);
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
        output.flush();
        if (!output) {
            std::filesystem::remove(temporaryPath, ec);
            return false;
        }
    }

    std::filesystem::rename(temporaryPath, finalPath, ec);
    if (ec) {
        std::filesystem::remove(temporaryPath, ec);
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
    std::unordered_map<std::uint64_t, std::string> urlByRequestId;
};

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
    if (stopping_ || !isHttpsUrl(url) || localPath(url).has_value()) {
        return;
    }
    if (!state_->inflightUrls.insert(url).second) {
        return;
    }

    const std::uint64_t requestId = nextRequestId_++;
    state_->urlByRequestId.emplace(requestId, url);
    if (!transport_.submit({requestId, RaHttpChannel::Image, std::move(url), {}})) {
        state_->inflightUrls.erase(state_->urlByRequestId.at(requestId));
        state_->urlByRequestId.erase(requestId);
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
            continue;
        }
        writeImageAtomically(cacheDirectory_, url, response.body, response.id);
    }
}

std::optional<std::string> RetroAchievementsImageCache::localPath(std::string_view url) const {
    const auto path = cachedPathFor(cacheDirectory_, url);
    if (!path.has_value()) {
        return std::nullopt;
    }
    return path->string();
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

} // namespace gb::frontend
