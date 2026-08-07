#pragma once

#include <string>
#include <utility>

#include "gb/achievements/config/achievement_config.hpp"
#include "gb/app/frontend/realtime/private_file_io.hpp"

namespace gb::frontend {

using RaConfigWipeObserver = gb::achievements::AchievementConfigWipeObserver;
using RaConfig = gb::achievements::AchievementConfig;

inline RaConfig loadRetroAchievementsConfig(
    const std::string& path,
    RaConfigWipeObserver wipeObserver = {}
) {
    return gb::achievements::loadAchievementConfig(path, std::move(wipeObserver));
}

inline bool saveRetroAchievementsConfig(
    const std::string& path,
    const RaConfig& config
) {
    return gb::achievements::saveAchievementConfig(path, config);
}

inline bool invalidateRetroAchievementsConfig(
    const std::string& path,
    bool* quarantinedSensitiveData = nullptr,
    const detail::PrivateFileIoHooks* hooks = nullptr
) {
    return gb::achievements::invalidateAchievementConfig(
        path,
        quarantinedSensitiveData,
        hooks
    );
}

} // namespace gb::frontend
