#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gb::frontend {

struct RaStoredProgress {
    std::string romHash;
    std::string stateFingerprint;
    std::vector<std::uint8_t> payload;
};

struct RaStateFileImage {
    std::vector<std::uint8_t> bytes;
    std::string fingerprint;
};

std::string retroAchievementsProgressPathForState(const std::string& statePath);
std::optional<RaStateFileImage> readRetroAchievementsStateFile(
    const std::string& path
);
bool saveRetroAchievementsProgress(
    const std::string& path,
    std::string_view romHash,
    const std::vector<std::uint8_t>& payload
);
std::optional<RaStoredProgress> loadRetroAchievementsProgress(
    const std::string& path,
    std::string_view expectedRomHash
);
bool saveRetroAchievementsProgressV2(
    const std::string& path,
    std::string_view romHash,
    std::string_view stateFingerprint,
    const std::vector<std::uint8_t>& payload
);
std::optional<RaStoredProgress> loadRetroAchievementsProgressV2(
    const std::string& path,
    std::string_view expectedRomHash,
    std::string_view expectedStateFingerprint
);
bool invalidateRetroAchievementsProgress(const std::string& path);

} // namespace gb::frontend
