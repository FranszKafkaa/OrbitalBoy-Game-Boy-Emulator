#pragma once

#include <vector>

#include "gb/core/types.hpp"

namespace gb {
namespace cartridge_mapper {

constexpr u32 kRomBankSize = 0x4000;
constexpr u32 kRamBankSize = 0x2000;

inline u32 safeRomBankCount(const std::vector<u8>& rom) {
    const u32 banks = static_cast<u32>(rom.size() / kRomBankSize);
    return banks == 0 ? 1 : banks;
}

inline u32 safeRamBankCount(const std::vector<u8>& ram) {
    if (ram.empty()) {
        return 0;
    }
    const u32 banks = static_cast<u32>(ram.size() / kRamBankSize);
    return banks == 0 ? 1 : banks;
}

inline u8 readRomByte(const std::vector<u8>& rom, u32 index) {
    return index < rom.size() ? rom[index] : 0xFF;
}

inline u8 readRomBank(const std::vector<u8>& rom, u32 bank, u16 offset) {
    return readRomByte(rom, bank * kRomBankSize + offset);
}

inline u8 readRamBank(const std::vector<u8>& ram, u32 bank, u16 offset) {
    const u32 index = bank * kRamBankSize + offset;
    return index < ram.size() ? ram[index] : 0xFF;
}

inline bool writeRamBank(std::vector<u8>& ram, u32 bank, u16 offset, u8 value) {
    const u32 index = bank * kRamBankSize + offset;
    if (index >= ram.size()) {
        return false;
    }
    ram[index] = value;
    return true;
}

} // namespace cartridge_mapper
} // namespace gb
