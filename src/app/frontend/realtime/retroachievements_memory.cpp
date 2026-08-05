#include "gb/app/frontend/realtime/retroachievements_memory.hpp"

#include <algorithm>
#include <vector>

namespace gb::frontend {

namespace {

constexpr std::uint32_t kGbaRaIwramEnd = 0x007FFFU;
constexpr std::uint32_t kGbaRaEwramStart = 0x008000U;
constexpr std::uint32_t kGbaRaEwramEnd = 0x047FFFU;
constexpr std::uint32_t kGbaRaSaveStart = 0x048000U;
constexpr std::uint32_t kGbaRaSaveEnd = 0x057FFFU;

std::uint32_t gbaPhysicalAddress(std::uint32_t address) {
    if (address <= kGbaRaIwramEnd) {
        return 0x03000000U + address;
    }
    if (address <= kGbaRaEwramEnd) {
        return 0x02000000U + (address - kGbaRaEwramStart);
    }
    return 0x0E000000U + (address - kGbaRaSaveStart);
}

} // namespace

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

std::uint32_t readRetroAchievementsGbaMemory(
    const RaGbaByteReader& readByte,
    std::uint32_t address,
    std::uint8_t* buffer,
    std::uint32_t numBytes
) {
    if (!readByte || !buffer || numBytes == 0 || address > kGbaRaSaveEnd
        || numBytes - 1U > kGbaRaSaveEnd - address) {
        return 0;
    }

    std::vector<std::uint8_t> bytes(numBytes);
    for (std::uint32_t index = 0; index < numBytes; ++index) {
        const auto value = readByte(gbaPhysicalAddress(address + index));
        if (!value.has_value()) {
            return 0;
        }
        bytes[index] = value.value();
    }
    std::copy(bytes.begin(), bytes.end(), buffer);
    return numBytes;
}

} // namespace gb::frontend
