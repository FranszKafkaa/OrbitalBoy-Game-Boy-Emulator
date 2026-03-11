#pragma once

#include <string>

#include "gb/core/gameboy.hpp"

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

int runRealtime(
    GameBoy& gb,
    int scale,
    int audioBuffer,
    const std::string& statePath,
    const std::string& legacyStatePath,
    const std::string& batteryRamPath,
    const std::string& palettePath,
    const std::string& rtcPath,
    const std::string& filtersPath,
    const std::string& captureDir
);

} // namespace gb::frontend
#endif
