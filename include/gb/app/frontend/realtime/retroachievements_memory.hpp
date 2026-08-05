#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "gb/core/bus.hpp"

namespace gb::frontend {

using RaGbaByteReader = std::function<std::optional<std::uint8_t>(std::uint32_t)>;

[[nodiscard]] std::uint32_t readRetroAchievementsMemory(
    const gb::Bus& bus,
    std::uint32_t address,
    std::uint8_t* buffer,
    std::uint32_t numBytes
);

[[nodiscard]] std::uint32_t readRetroAchievementsGbaMemory(
    const RaGbaByteReader& readByte,
    std::uint32_t address,
    std::uint8_t* buffer,
    std::uint32_t numBytes
);

} // namespace gb::frontend
