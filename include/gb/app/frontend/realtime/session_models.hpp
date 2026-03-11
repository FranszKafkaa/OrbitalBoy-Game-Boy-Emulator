#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "gb/app/frontend/debug_ui.hpp"
#include "gb/app/frontend/realtime_support.hpp"
#include "gb/core/ppu.hpp"
#include "gb/core/types.hpp"

namespace gb::frontend {

struct QueuedMemoryWrite {
    bool active = false;
    gb::u16 address = 0;
    gb::u8 value = 0;
    const char* source = "";
};

struct BreakpointEditState {
    bool active = false;
    std::string addressHex{};
};

struct MemorySearchState {
    static constexpr std::size_t MaxStoredMatches = 4096;

    MemorySearchUiState ui{};
    std::array<gb::u8, 0x10000> snapshot{};
};

struct RawFramePacket {
    std::uint64_t sequence = 0;
    std::array<gb::u8, gb::PPU::ScreenWidth * gb::PPU::ScreenHeight> mono{};
    std::array<gb::u16, gb::PPU::ScreenWidth * gb::PPU::ScreenHeight> color{};
};

struct RgbFramePacket {
    std::uint64_t sequence = 0;
    RgbFrame pixels{};
};

} // namespace gb::frontend
