#pragma once

#include <array>
#include <string>
#include <vector>

#include "gb/core/types.hpp"

namespace gb::gba {

struct GbaCpuDebugSnapshot {
    std::array<u32, 16> regs{};
    u32 cpsr = 0;
    u32 spsr = 0;
    bool thumb = false;
    std::string mode;
};

struct GbaMemoryBlockDebugInfo {
    std::string shortName;
    std::string longName;
    u32 start = 0;
    u32 end = 0;
    u32 size = 0;
    bool readable = false;
    bool writable = false;
};

struct GbaChannelDebugInfo {
    std::string name;
    std::string type;
    bool enabled = true;
};

struct GbaDebugSnapshot {
    bool available = false;
    GbaCpuDebugSnapshot cpu{};
    std::vector<GbaMemoryBlockDebugInfo> memoryBlocks{};
    std::vector<GbaChannelDebugInfo> videoLayers{};
    std::vector<GbaChannelDebugInfo> audioChannels{};
    u32 frameCounter = 0;
};

} // namespace gb::gba
