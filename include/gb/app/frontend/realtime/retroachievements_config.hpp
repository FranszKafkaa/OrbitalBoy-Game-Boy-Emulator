#pragma once

#include <string>

namespace gb::frontend {

struct RaConfig {
    int version = 1;
    std::string username;
    std::string token;
    bool autoLogin = true;
    bool showNotifications = true;
};

RaConfig loadRetroAchievementsConfig(const std::string& path);
bool saveRetroAchievementsConfig(const std::string& path, const RaConfig& config);
bool invalidateRetroAchievementsConfig(const std::string& path);

} // namespace gb::frontend
