#pragma once

#include "gb/achievements/memory/memory_reader.hpp"

namespace gb {
class GameBoy;
namespace achievements::adapters {

memory::MemoryReader makeGameBoyMemoryReader(GameBoy& gameBoy);

} // namespace achievements::adapters
} // namespace gb
