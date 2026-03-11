#pragma once

#include <vector>

#include "gb/core/types.hpp"

namespace gb {
namespace cartridge_mapper {

inline u32 safeRomBankCount(const std::vector<u8>& rom) {
    const u32 banks = static_cast<u32>(rom.size() / 0x4000);
    return banks == 0 ? 1 : banks;
}

inline u32 safeRamBankCount(const std::vector<u8>& ram) {
    if (ram.empty()) {
        return 0;
    }
    const u32 banks = static_cast<u32>(ram.size() / 0x2000);
    return banks == 0 ? 1 : banks;
}

} // namespace cartridge_mapper
} // namespace gb
