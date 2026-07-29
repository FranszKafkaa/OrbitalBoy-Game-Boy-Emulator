#include "gb/app/frontend/realtime/retroachievements_config.hpp"

#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

namespace gb::frontend {

namespace {

constexpr std::size_t kMaxStoredValueBytes = 4U * 1024U;

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

    const std::filesystem::path temporary = destination.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << "version=1\n";
        out << "username=" << escapeValue(config.username) << '\n';
        out << "token=" << escapeValue(config.token) << '\n';
        out << "auto_login=" << (config.autoLogin ? "true" : "false") << '\n';
        out << "show_notifications=" << (config.showNotifications ? "true" : "false") << '\n';
        out.flush();
        if (!out) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    std::filesystem::permissions(
        temporary,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        error
    );
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }

    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace gb::frontend
