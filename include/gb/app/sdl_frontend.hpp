#pragma once

#include <string>

#include "gb/core/gba/libretro_core.hpp"
#include "gb/core/gba/mgba_core.hpp"
#include "gb/core/gameboy.hpp"

namespace gb {

#ifdef GBEMU_USE_SDL2
std::string chooseRomWithSdlDialog();

int runRealtime(
    GameBoy& gb,
    int scale,
    int audioBuffer,
    const std::string& statePath,
    const std::string& legacyStatePath,
    const std::string& batteryRamPath,
    const std::string& controlsPath,
    const std::string& cheatsPath,
    const std::string& palettePath,
    const std::string& rtcPath,
    const std::string& replayPath,
    const std::string& filtersPath,
    const std::string& captureDir,
    const std::string& linkConnect,
    int linkHostPort,
    const std::string& netplayConnect,
    int netplayHostPort,
    int netplayDelayFrames,
    bool runLabControl,
    const std::string& runLabStatePath,
    const std::string& runLabCommandQueuePath
);

int runGbaLibretroRealtime(
    gba::LibretroCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
);
int runGbaMgbaRealtime(
    gba::MgbaCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
);
#endif

} // namespace gb
