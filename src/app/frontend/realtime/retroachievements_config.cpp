#include "gb/app/frontend/realtime/retroachievements_config.hpp"

#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gb::frontend {

namespace {

constexpr std::size_t kMaxStoredValueBytes = 4U * 1024U;
constexpr int kTemporaryFileAttempts = 32;

bool isSafeStoredValue(std::string_view value) {
    if (value.size() > kMaxStoredValueBytes) {
        return false;
    }
    for (const unsigned char character : value) {
        if (std::iscntrl(character) != 0) {
            return false;
        }
    }
    return true;
}

std::string escapeValue(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '=') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

std::optional<std::string> unescapeValue(std::string_view value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    bool escaped = false;
    for (const char character : value) {
        if (escaped) {
            if (character != '\\' && character != '=') {
                return std::nullopt;
            }
            unescaped.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            unescaped.push_back(character);
        }
    }
    if (escaped || !isSafeStoredValue(unescaped)) {
        return std::nullopt;
    }
    return unescaped;
}

std::optional<int> parseVersion(std::string_view value) {
    int parsed = 0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto [next, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || next != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> parseBoolean(std::string_view value) {
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    return std::nullopt;
}

std::string serializeConfig(const RaConfig& config) {
    std::string serialized;
    serialized.reserve(config.username.size() + config.token.size() + 96U);
    serialized += "version=1\nusername=";
    serialized += escapeValue(config.username);
    serialized += "\ntoken=";
    serialized += escapeValue(config.token);
    serialized += "\nauto_login=";
    serialized += config.autoLogin ? "true" : "false";
    serialized += "\nshow_notifications=";
    serialized += config.showNotifications ? "true" : "false";
    serialized += '\n';
    return serialized;
}

std::filesystem::path temporarySiblingPath(const std::filesystem::path& destination, int attempt) {
    static std::atomic<std::uint64_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::uint64_t serial = counter.fetch_add(1, std::memory_order_relaxed);
    const std::string name = destination.filename().string()
        + ".tmp."
        + std::to_string(now)
        + "."
        + std::to_string(serial)
        + "."
        + std::to_string(attempt);
    return destination.parent_path() / name;
}

void removeTemporaryFile(const std::filesystem::path& temporary) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
}

#if defined(_WIN32)

bool writePrivateTemporaryFile(const std::filesystem::path& destination,
                               std::string_view contents,
                               std::filesystem::path& temporary) {
    for (int attempt = 0; attempt < kTemporaryFileAttempts; ++attempt) {
        temporary = temporarySiblingPath(destination, attempt);
        const std::wstring temporaryPath = temporary.wstring();
        HANDLE handle = CreateFileW(
            temporaryPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
                continue;
            }
            return false;
        }

        std::size_t offset = 0;
        bool wrote = true;
        while (offset < contents.size()) {
            const std::size_t remaining = contents.size() - offset;
            const DWORD chunk = remaining > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (!WriteFile(handle, contents.data() + offset, chunk, &written, nullptr) || written == 0U) {
                wrote = false;
                break;
            }
            offset += written;
        }
        const bool flushed = wrote && FlushFileBuffers(handle) != 0;
        const bool closed = CloseHandle(handle) != 0;
        if (flushed && closed) {
            return true;
        }
        removeTemporaryFile(temporary);
        return false;
    }
    return false;
}

bool replaceWithTemporaryFile(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
    return MoveFileExW(
               temporary.wstring().c_str(),
               destination.wstring().c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
           ) != 0;
}

#else

bool writeAll(int descriptor, std::string_view contents) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t written = ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool writePrivateTemporaryFile(const std::filesystem::path& destination,
                               std::string_view contents,
                               std::filesystem::path& temporary) {
    for (int attempt = 0; attempt < kTemporaryFileAttempts; ++attempt) {
        temporary = temporarySiblingPath(destination, attempt);
        const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (descriptor < 0) {
            if (errno == EEXIST) {
                continue;
            }
            return false;
        }

        const bool wrote = writeAll(descriptor, contents);
        const bool flushed = wrote && ::fsync(descriptor) == 0;
        const bool closed = ::close(descriptor) == 0;
        if (flushed && closed) {
            return true;
        }
        removeTemporaryFile(temporary);
        return false;
    }
    return false;
}

bool replaceWithTemporaryFile(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
    return ::rename(temporary.c_str(), destination.c_str()) == 0;
}

#endif

} // namespace

RaConfig loadRetroAchievementsConfig(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }

    RaConfig config{};
    bool hasValidVersion = false;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string_view key(line.data(), separator);
        const auto value = unescapeValue(std::string_view(line.data() + separator + 1U, line.size() - separator - 1U));
        if (!value.has_value()) {
            continue;
        }

        if (key == "version") {
            const auto version = parseVersion(*value);
            if (!version.has_value() || *version != config.version) {
                return {};
            }
            hasValidVersion = true;
        } else if (key == "username") {
            config.username = *value;
        } else if (key == "token") {
            config.token = *value;
        } else if (key == "auto_login") {
            if (const auto parsed = parseBoolean(*value)) {
                config.autoLogin = *parsed;
            }
        } else if (key == "show_notifications") {
            if (const auto parsed = parseBoolean(*value)) {
                config.showNotifications = *parsed;
            }
        }
    }

    return hasValidVersion ? config : RaConfig{};
}

bool saveRetroAchievementsConfig(const std::string& path, const RaConfig& config) {
    if (path.empty() || config.version != 1 || !isSafeStoredValue(config.username) || !isSafeStoredValue(config.token)) {
        return false;
    }

    const std::filesystem::path destination(path);
    const std::filesystem::path parent = destination.parent_path();
    std::error_code error;
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    std::filesystem::path temporary;
    if (!writePrivateTemporaryFile(destination, serializeConfig(config), temporary)) {
        return false;
    }

    if (!replaceWithTemporaryFile(temporary, destination)) {
        removeTemporaryFile(temporary);
        return false;
    }
    return true;
}

} // namespace gb::frontend
