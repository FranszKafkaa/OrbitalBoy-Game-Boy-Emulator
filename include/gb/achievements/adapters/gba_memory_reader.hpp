#pragma once

#include "gb/achievements/memory/memory_reader.hpp"

namespace gb::gba {
class System;
class MgbaCore;
}

namespace gb::achievements::adapters {

memory::MemoryReader makeGbaMemoryReader(gb::gba::System& system);
memory::MemoryReader makeGbaMemoryReader(gb::gba::MgbaCore& core);

} // namespace gb::achievements::adapters
