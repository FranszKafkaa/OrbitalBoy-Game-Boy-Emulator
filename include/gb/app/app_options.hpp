#pragma once

#include <string>

namespace gb {

enum class HardwareModePreference {
    Auto,
    Dmg,
    Cgb,
};

struct AppOptions {
    std::string romPath;
    std::string romSuiteManifest;
    std::string bootRomPath;
    std::string linkConnect;
    std::string netplayConnect;
    std::string runLabStatePath = ".runlab/current-state.json";
    std::string runLabCommandQueuePath = ".runlab/commands.jsonl";
    bool headless = false;
    bool chooseRom = false;
    bool preciseTiming = false;
    bool runLabControl = false;
    HardwareModePreference hardwareMode = HardwareModePreference::Auto;
    int linkHostPort = 0;
    int netplayHostPort = 0;
    int netplayDelayFrames = 0;
    int frames = 120;
    int scale = 4;
    int audioBuffer = 1024;
};

bool parseAppOptions(int argc, char** argv, AppOptions& outOptions, std::string& errorMessage);

} // namespace gb
