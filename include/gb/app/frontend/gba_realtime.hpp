#pragma once

#include "gb/core/gba/system.hpp"

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

int runGbaRealtime(gba::System& system, int scale);

} // namespace gb::frontend
#endif
