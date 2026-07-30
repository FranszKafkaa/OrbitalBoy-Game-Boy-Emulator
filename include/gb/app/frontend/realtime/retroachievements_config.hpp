#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace gb::frontend {

struct RaConfig {
    int version = 1;
    std::string username;
    std::string token;
    bool autoLogin = true;
    bool showNotifications = true;
};

using RaConfigWipeObserver =
    std::function<void(const char* bytes, std::size_t size)>;

RaConfig loadRetroAchievementsConfig(
    const std::string& path,
    RaConfigWipeObserver wipeObserver = {}
);
bool saveRetroAchievementsConfig(const std::string& path, const RaConfig& config);
bool invalidateRetroAchievementsConfig(
    const std::string& path,
    bool* quarantinedSensitiveData = nullptr
);

} // namespace gb::frontend
