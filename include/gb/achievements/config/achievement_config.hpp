#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "gb/achievements/security/secret_string.hpp"
#include "gb/achievements/storage/private_file_io.hpp"

namespace gb::achievements {

using AchievementConfigWipeObserver = SecretWipeObserver;

struct AchievementConfig {
    AchievementConfig(
        int versionValue = 1,
        std::string_view usernameValue = {},
        std::string_view tokenValue = {},
        bool autoLoginValue = true,
        bool showNotificationsValue = true,
        AchievementConfigWipeObserver wipeObserver = {}
    );
    ~AchievementConfig();

    AchievementConfig(const AchievementConfig& other);
    AchievementConfig& operator=(const AchievementConfig& other);
    AchievementConfig(AchievementConfig&& other);
    AchievementConfig& operator=(AchievementConfig&& other);

    void assignToken(std::string_view value);
    void assignTokenAndErase(std::string& source);
    void transferTokenTo(SecretString& destination);
    void clearToken();

    int version = 1;
    std::string username;
    std::string token;
    bool autoLogin = true;
    bool showNotifications = true;

private:
    AchievementConfigWipeObserver wipeObserver_{};
};

AchievementConfig loadAchievementConfig(
    const std::string& path,
    AchievementConfigWipeObserver wipeObserver = {}
);
bool saveAchievementConfig(
    const std::string& path,
    const AchievementConfig& config
);
bool invalidateAchievementConfig(
    const std::string& path,
    bool* quarantinedSensitiveData = nullptr,
    const storage::PrivateFileIoHooks* hooks = nullptr
);

} // namespace gb::achievements
