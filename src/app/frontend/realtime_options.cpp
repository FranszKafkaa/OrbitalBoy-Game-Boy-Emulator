#include "gb/app/frontend/realtime_options.hpp"

#include "gb/app/runtime_paths.hpp"

namespace gb::frontend {

RealtimeOptions makeRealtimeOptions(const AppOptions& appOptions) {
    RealtimeOptions options{};
    options.scale = appOptions.scale;
    options.audioBufferSamples = appOptions.audioBuffer;
    options.paths.state = statePathForRom(appOptions.romPath);
    options.paths.legacyState = legacyStatePathForRom(appOptions.romPath);
    options.paths.batteryRam = batteryRamPathForRom(appOptions.romPath);
    options.paths.controls = controlsPathForRom(appOptions.romPath);
    options.paths.cheats = cheatsPathForRom(appOptions.romPath);
    options.paths.palette = palettePathForRom(appOptions.romPath);
    options.paths.rtc = rtcPathForRom(appOptions.romPath);
    options.paths.filters = filtersPathForRom(appOptions.romPath);
    options.paths.captureDirectory = captureDirForRom(appOptions.romPath);
    options.network.linkConnect = appOptions.linkConnect;
    options.network.linkHostPort = appOptions.linkHostPort;
    options.network.netplayConnect = appOptions.netplayConnect;
    options.network.netplayHostPort = appOptions.netplayHostPort;
    options.network.netplayDelayFrames = appOptions.netplayDelayFrames;
    options.runLab.enabled = appOptions.runLabControl;
    options.runLab.statePath = appOptions.runLabStatePath;
    options.runLab.commandQueuePath = appOptions.runLabCommandQueuePath;
    return options;
}

} // namespace gb::frontend
