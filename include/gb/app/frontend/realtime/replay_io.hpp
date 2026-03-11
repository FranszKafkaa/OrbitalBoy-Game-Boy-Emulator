#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gb::frontend {

struct ReplayData {
    std::uint32_t version = 1;
    std::uint32_t seed = 0;
    std::vector<std::uint8_t> frameInputs{};
};

bool saveReplayFile(const std::string& path, const ReplayData& replay);
std::optional<ReplayData> loadReplayFile(const std::string& path);

std::uint8_t packButtons(
    bool right,
    bool left,
    bool up,
    bool down,
    bool a,
    bool b,
    bool select,
    bool start
);

} // namespace gb::frontend
