#pragma once

#include <string>

#include "gb/app/frontend/realtime_options.hpp"
#include "gb/core/gba/mgba_core.hpp"
#include "gb/core/gameboy.hpp"

namespace gb {

#ifdef GBEMU_USE_SDL2
std::string chooseRomWithSdlDialog();

int runRealtime(
    GameBoy& gb,
    const frontend::RealtimeOptions& options
);

int runGbaRealtime(
    gba::MgbaCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
);
#endif

} // namespace gb
