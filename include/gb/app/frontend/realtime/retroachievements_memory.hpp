#pragma once

#include <cstdint>

#include "gb/core/bus.hpp"

namespace gb::frontend {

[[nodiscard]] std::uint32_t readRetroAchievementsMemory(
    const gb::Bus& bus,
    std::uint32_t address,
    std::uint8_t* buffer,
    std::uint32_t numBytes
);

} // namespace gb::frontend
