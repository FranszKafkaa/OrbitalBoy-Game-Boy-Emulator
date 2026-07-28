#pragma once

#include <string>

#include "gb/core/gba/mgba_core.hpp"

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

int runGbaRealtime(
    gba::MgbaCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
);

} // namespace gb::frontend
#endif
