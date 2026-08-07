#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "gb/app/frontend/realtime/secure_string.hpp"

namespace gb::frontend {

namespace detail {
struct PrivateFileIoHooks;
}

using RaConfigWipeObserver = SecureStringWipeObserver;

struct RaConfig {
    RaConfig(
        int versionValue = 1,
        std::string_view usernameValue = {},
        std::string_view tokenValue = {},
        bool autoLoginValue = true,
        bool showNotificationsValue = true,
        RaConfigWipeObserver wipeObserver = {}
    );
    ~RaConfig();

    RaConfig(const RaConfig& other);
    RaConfig& operator=(const RaConfig& other);
    RaConfig(RaConfig&& other);
    RaConfig& operator=(RaConfig&& other);

    void assignToken(std::string_view value);
    void assignTokenAndErase(std::string& source);
    void transferTokenTo(RaSecretString& destination);
    void clearToken();

    int version = 1;
    std::string username;
    std::string token;
    bool autoLogin = true;
    bool showNotifications = true;

private:
    RaConfigWipeObserver wipeObserver_{};
};

RaConfig loadRetroAchievementsConfig(
    const std::string& path,
    RaConfigWipeObserver wipeObserver = {}
);
bool saveRetroAchievementsConfig(const std::string& path, const RaConfig& config);
bool invalidateRetroAchievementsConfig(
    const std::string& path,
    bool* quarantinedSensitiveData = nullptr,
    const detail::PrivateFileIoHooks* hooks = nullptr
);

} // namespace gb::frontend
