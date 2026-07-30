#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gb::frontend {

struct RaStoredProgress {
    std::string romHash;
    std::vector<std::uint8_t> payload;
};

std::string retroAchievementsProgressPathForState(const std::string& statePath);
bool saveRetroAchievementsProgress(
    const std::string& path,
    std::string_view romHash,
    const std::vector<std::uint8_t>& payload
);
std::optional<RaStoredProgress> loadRetroAchievementsProgress(
    const std::string& path,
    std::string_view expectedRomHash
);

} // namespace gb::frontend
