#pragma once

#include "gb/app/frontend/realtime_options.hpp"
#include "gb/core/gameboy.hpp"

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

int runRealtime(
    GameBoy& gb,
    const RealtimeOptions& options
);

} // namespace gb::frontend
#endif
