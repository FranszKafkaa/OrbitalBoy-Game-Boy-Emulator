#include "gb/app/frontend/realtime/retroachievements_memory.hpp"

namespace gb::frontend {

std::uint32_t readRetroAchievementsMemory(
    const gb::Bus& bus,
    std::uint32_t address,
    std::uint8_t* buffer,
    std::uint32_t numBytes
) {
    if (!buffer || numBytes == 0 || address > 0xFFFFU || numBytes - 1U > 0xFFFFU - address) {
        return 0;
    }

    for (std::uint32_t i = 0; i < numBytes; ++i) {
        buffer[i] = bus.peek(static_cast<gb::u16>(address + i));
    }
    return numBytes;
}

} // namespace gb::frontend
