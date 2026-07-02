#include "gb/app/sdl_frontend.hpp"

#include "gb/app/frontend/gba_realtime.hpp"
#include "gb/app/frontend/realtime.hpp"
#include "gb/app/frontend/rom_selector.hpp"

namespace gb {

#ifdef GBEMU_USE_SDL2
std::string chooseRomWithSdlDialog() {
    return frontend::chooseRomWithSdlDialog();
}

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
) {
    return frontend::runRealtime(
        gb,
        scale,
        audioBuffer,
        statePath,
        legacyStatePath,
        batteryRamPath,
        controlsPath,
        cheatsPath,
        palettePath,
        rtcPath,
        replayPath,
        filtersPath,
        captureDir,
        linkConnect,
        linkHostPort,
        netplayConnect,
        netplayHostPort,
        netplayDelayFrames,
        runLabControl,
        runLabStatePath,
        runLabCommandQueuePath
    );
}

int runGbaLibretroRealtime(
    gba::LibretroCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
) {
    return frontend::runGbaLibretroRealtime(core, scale, statePath, batteryRamPath, captureDir);
}

int runGbaMgbaRealtime(
    gba::MgbaCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
) {
    return frontend::runGbaMgbaRealtime(core, scale, statePath, batteryRamPath, captureDir);
}
#endif

} // namespace gb
