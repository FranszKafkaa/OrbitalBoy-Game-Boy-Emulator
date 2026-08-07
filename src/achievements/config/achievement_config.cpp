#include "gb/achievements/config/achievement_config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

namespace gb::achievements {
namespace {

constexpr std::size_t kMaxStoredValueBytes = 4U * 1024U;
constexpr std::size_t kMaxConfigFileBytes = 4U * 1024U;
constexpr int kQuarantineAttempts = 32;

void wipeString(
    std::string& value,
    const AchievementConfigWipeObserver& observer = {}
) {
    (void)secureEraseStringStorage(value, observer);
}

bool isSafeStoredValue(std::string_view value) {
    if (value.size() > kMaxStoredValueBytes) {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char character) {
            return std::iscntrl(character) == 0;
        }
    );
}

void appendEscapedValue(std::string& destination, std::string_view value) {
    for (const char character : value) {
        if (character == '\\' || character == '=') {
            destination.push_back('\\');
        }
        destination.push_back(character);
    }
}

bool unescapeValueInto(
    std::string_view value,
    std::string& destination,
    const AchievementConfigWipeObserver& observer
) {
    wipeString(destination, observer);
    destination.reserve(value.size());
    bool escaped = false;
    for (const char character : value) {
        if (escaped) {
            if (character != '\\' && character != '=') {
                wipeString(destination, observer);
                return false;
            }
            destination.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            destination.push_back(character);
        }
    }
    if (escaped || !isSafeStoredValue(destination)) {
        wipeString(destination, observer);
        return false;
    }
    return true;
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

std::string serializeConfig(const AchievementConfig& config) {
    std::string serialized;
    serialized.reserve(config.username.size() + config.token.size() + 96U);
    serialized += "version=1\nusername=";
    appendEscapedValue(serialized, config.username);
    serialized += "\ntoken=";
    appendEscapedValue(serialized, config.token);
    serialized += "\nauto_login=";
    serialized += config.autoLogin ? "true" : "false";
    serialized += "\nshow_notifications=";
    serialized += config.showNotifications ? "true" : "false";
    serialized += '\n';
    return serialized;
}

std::filesystem::path quarantinePath(
    const std::filesystem::path& source
) {
    thread_local std::mt19937_64 generator(std::random_device{}());
    return source.parent_path()
        / (source.filename().string()
           + ".invalid."
           + std::to_string(generator()));
}

bool quarantineConfig(
    const std::filesystem::path& source,
    bool sensitive
) {
    if (sensitive && !storage::makeFileOwnerPrivate(source)) {
        return false;
    }
    for (int attempt = 0; attempt < kQuarantineAttempts; ++attempt) {
        const auto quarantine = quarantinePath(source);
        std::error_code existsError;
        if (std::filesystem::exists(quarantine, existsError) || existsError) {
            continue;
        }
        bool entryChanged = false;
        if (storage::renameFileDurably(
                source,
                quarantine,
                &entryChanged
            )) {
            return true;
        }
        if (entryChanged) {
            return false;
        }
    }
    return false;
}

} // namespace

AchievementConfig::AchievementConfig(
    int versionValue,
    std::string_view usernameValue,
    std::string_view tokenValue,
    bool autoLoginValue,
    bool showNotificationsValue,
    AchievementConfigWipeObserver wipeObserver
)
    : version(versionValue),
      username(usernameValue),
      autoLogin(autoLoginValue),
      showNotifications(showNotificationsValue),
      wipeObserver_(std::move(wipeObserver)) {
    if (!tokenValue.empty()) {
        token.assign(tokenValue.data(), tokenValue.size());
    }
}

AchievementConfig::~AchievementConfig() {
    clearToken();
}

AchievementConfig::AchievementConfig(const AchievementConfig& other)
    : version(other.version),
      username(other.username),
      autoLogin(other.autoLogin),
      showNotifications(other.showNotifications),
      wipeObserver_(other.wipeObserver_) {
    if (!other.token.empty()) {
        token.assign(other.token.data(), other.token.size());
    }
}

AchievementConfig& AchievementConfig::operator=(const AchievementConfig& other) {
    if (this != &other) {
        clearToken();
        version = other.version;
        username = other.username;
        autoLogin = other.autoLogin;
        showNotifications = other.showNotifications;
        wipeObserver_ = other.wipeObserver_;
        if (!other.token.empty()) {
            token.assign(other.token.data(), other.token.size());
        }
    }
    return *this;
}

AchievementConfig::AchievementConfig(AchievementConfig&& other)
    : version(other.version),
      username(std::move(other.username)),
      autoLogin(other.autoLogin),
      showNotifications(other.showNotifications),
      wipeObserver_(other.wipeObserver_) {
    if (!other.token.empty()) {
        token.assign(other.token.data(), other.token.size());
    }
    other.clearToken();
}

AchievementConfig& AchievementConfig::operator=(AchievementConfig&& other) {
    if (this != &other) {
        clearToken();
        version = other.version;
        username = std::move(other.username);
        autoLogin = other.autoLogin;
        showNotifications = other.showNotifications;
        wipeObserver_ = other.wipeObserver_;
        if (!other.token.empty()) {
            token.assign(other.token.data(), other.token.size());
        }
        other.clearToken();
    }
    return *this;
}

void AchievementConfig::assignToken(std::string_view value) {
    clearToken();
    if (!value.empty()) {
        token.assign(value.data(), value.size());
    }
}

void AchievementConfig::assignTokenAndErase(std::string& source) {
    assignToken(std::string_view(source.data(), source.size()));
    wipeString(source, wipeObserver_);
}

void AchievementConfig::transferTokenTo(SecretString& destination) {
    destination.assign(std::string_view(token.data(), token.size()));
    clearToken();
}

void AchievementConfig::clearToken() {
    wipeString(token, wipeObserver_);
}

AchievementConfig loadAchievementConfig(
    const std::string& path,
    AchievementConfigWipeObserver wipeObserver
) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return {};
    }
    const std::streampos end = in.tellg();
    if (end < 0
        || static_cast<std::uint64_t>(end) > kMaxConfigFileBytes) {
        return {};
    }
    in.seekg(0, std::ios::beg);
    if (!in) {
        return {};
    }

    AchievementConfig config{1, {}, {}, true, true, wipeObserver};
    bool hasValidVersion = false;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            wipeString(line, wipeObserver);
            continue;
        }
        const std::string_view key(line.data(), separator);
        const std::string_view encodedValue(
            line.data() + separator + 1U,
            line.size() - separator - 1U
        );
        if (key == "token") {
            if (!unescapeValueInto(
                    encodedValue,
                    config.token,
                    wipeObserver
                )) {
                wipeString(line, wipeObserver);
                continue;
            }
            wipeString(line, wipeObserver);
            continue;
        }
        std::string value;
        if (!unescapeValueInto(encodedValue, value, wipeObserver)) {
            wipeString(line, wipeObserver);
            continue;
        }

        if (key == "version") {
            const auto version = parseVersion(value);
            if (!version.has_value() || *version != config.version) {
                wipeString(value, wipeObserver);
                wipeString(line, wipeObserver);
                wipeString(config.token, wipeObserver);
                config.username.clear();
                return {};
            }
            hasValidVersion = true;
        } else if (key == "username") {
            config.username = value;
        } else if (key == "auto_login") {
            if (const auto parsed = parseBoolean(value)) {
                config.autoLogin = *parsed;
            }
        } else if (key == "show_notifications") {
            if (const auto parsed = parseBoolean(value)) {
                config.showNotifications = *parsed;
            }
        }
        wipeString(value, wipeObserver);
        wipeString(line, wipeObserver);
    }
    if (!hasValidVersion) {
        wipeString(config.token, wipeObserver);
        config.username.clear();
        return {};
    }
    return config;
}

bool saveAchievementConfigWithHooks(
    const std::string& path,
    const AchievementConfig& config,
    const storage::PrivateFileIoHooks* hooks
) {
    if (path.empty() || config.version != 1
        || !isSafeStoredValue(config.username)
        || !isSafeStoredValue(config.token)) {
        return false;
    }
    std::string serialized = serializeConfig(config);
    if (serialized.size() > kMaxConfigFileBytes) {
        wipeString(serialized);
        return false;
    }
    const bool saved = storage::writePrivateFileAtomically(
        std::filesystem::path(path),
        std::string_view(serialized),
        hooks
    );
    wipeString(serialized);
    return saved;
}

bool saveAchievementConfig(
    const std::string& path,
    const AchievementConfig& config
) {
    return saveAchievementConfigWithHooks(path, config, nullptr);
}

bool invalidateAchievementConfig(
    const std::string& path,
    bool* quarantinedSensitiveData,
    const storage::PrivateFileIoHooks* hooks
) {
    if (quarantinedSensitiveData) {
        *quarantinedSensitiveData = false;
    }
    if (path.empty()) {
        return false;
    }
    const std::filesystem::path source(path);
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(source, statusError);
    if (statusError) {
        return false;
    }
    if (!std::filesystem::exists(status)) {
        return true;
    }
    if (!std::filesystem::is_regular_file(status)) {
        return false;
    }

    const AchievementConfig emptyConfig{1, {}, {}, false, true};
    if (saveAchievementConfigWithHooks(path, emptyConfig, hooks)) {
        bool entryChanged = false;
        if (storage::removeFileDurably(source, &entryChanged, hooks)) {
            return true;
        }
        if (entryChanged) {
            return false;
        }
        return quarantineConfig(source, false);
    }

    if (!quarantineConfig(source, true)) {
        return false;
    }
    if (quarantinedSensitiveData) {
        *quarantinedSensitiveData = true;
    }
    return true;
}

} // namespace gb::achievements
