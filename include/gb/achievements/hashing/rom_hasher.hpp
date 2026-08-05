#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gb::achievements::hashing {

enum class RomSystem {
    GameBoy,
    GameBoyColor,
    GameBoyAdvance,
};

struct RomHashResult {
    std::string hash;
    std::uint64_t bytesHashed = 0U;
};

RomHashResult hashRomBytes(
    RomSystem system,
    const std::vector<std::uint8_t>& romBytes
);

std::optional<RomHashResult> hashRomFile(
    RomSystem system,
    const std::filesystem::path& path
);

} // namespace gb::achievements::hashing
