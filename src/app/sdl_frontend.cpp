#include "gb/app/sdl_frontend.hpp"

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
    const std::string& palettePath,
    const std::string& rtcPath,
    const std::string& filtersPath,
    const std::string& captureDir
) {
    return frontend::runRealtime(
        gb,
        scale,
        audioBuffer,
        statePath,
        legacyStatePath,
        batteryRamPath,
        palettePath,
        rtcPath,
        filtersPath,
        captureDir
    );
}
#endif

} // namespace gb
