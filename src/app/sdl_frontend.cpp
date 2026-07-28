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
    const frontend::RealtimeOptions& options
) {
    return frontend::runRealtime(gb, options);
}

int runGbaRealtime(
    gba::MgbaCore& core,
    int scale,
    const std::string& statePath,
    const std::string& batteryRamPath,
    const std::string& captureDir
) {
    return frontend::runGbaRealtime(core, scale, statePath, batteryRamPath, captureDir);
}
#endif

} // namespace gb
