#include "gb/achievements/adapters/gameboy_memory_reader.hpp"
#include "gb/achievements/adapters/gba_memory_reader.hpp"

#include <algorithm>
#include <cstdint>

#include "gb/core/gameboy.hpp"
#include "gb/core/gba/memory.hpp"
#include "gb/core/gba/system.hpp"
#include "gb/core/gba/mgba_core.hpp"

namespace gb::achievements::adapters {

memory::MemoryReader makeGameBoyMemoryReader(gb::GameBoy& gameBoy) {
    return [&gameBoy](std::uint32_t address, std::uint8_t* destination, std::size_t count) {
        if (destination == nullptr || count == 0U || address > 0xFFFFU) return std::size_t{0U};
        const auto available = std::min<std::size_t>(count, 0x10000U - static_cast<std::size_t>(address));
        for (std::size_t index = 0U; index < available; ++index) {
            destination[index] = gameBoy.bus().peek(static_cast<gb::u16>(address + index));
        }
        return available;
    };
}

namespace {

bool isMappedByte(const gb::gba::Memory& memory, std::uint32_t address) {
    if (address >= 0x02000000U && address < 0x02000000U + gb::gba::Memory::EwramSize) return true;
    if (address >= 0x03000000U && address < 0x03000000U + gb::gba::Memory::IwramSize) return true;
    if (address >= gb::gba::Memory::IoBase && address < gb::gba::Memory::IoBase + gb::gba::Memory::IoSize) return true;
    if (address >= gb::gba::Memory::PramBase && address < gb::gba::Memory::PramBase + gb::gba::Memory::PramSize) return true;
    if (address >= gb::gba::Memory::VramBase && address < gb::gba::Memory::VramBase + gb::gba::Memory::VramSize) return true;
    if (address >= gb::gba::Memory::OamBase && address < gb::gba::Memory::OamBase + gb::gba::Memory::OamSize) return true;
    if (address >= 0x08000000U && static_cast<std::size_t>(address - 0x08000000U) < memory.rom().size()) return true;
    return false;
}

} // namespace

memory::MemoryReader makeGbaMemoryReader(gb::gba::System& system) {
    return [&system](std::uint32_t address, std::uint8_t* destination, std::size_t count) {
        if (destination == nullptr || count == 0U) return std::size_t{0U};
        std::size_t read = 0U;
        for (; read < count; ++read) {
            const auto current = address + static_cast<std::uint32_t>(read);
            if (current < address || !isMappedByte(system.memory(), current)) break;
            destination[read] = system.memory().read8(current);
        }
        return read;
    };
}

memory::MemoryReader makeGbaMemoryReader(gb::gba::MgbaCore& core) {
    return [&core](std::uint32_t address, std::uint8_t* destination, std::size_t count) {
        if (destination == nullptr || count == 0U) return std::size_t{0U};
        std::size_t read = 0U;
        for (; read < count; ++read) {
            const auto current = address + static_cast<std::uint32_t>(read);
            if (current < address) break;
            const auto value = core.debugRead8(current);
            if (!value.has_value()) break;
            destination[read] = *value;
        }
        return read;
    };
}

} // namespace gb::achievements::adapters
