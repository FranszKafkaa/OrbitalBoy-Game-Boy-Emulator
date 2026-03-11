#pragma once

#include <string>

namespace gb {

struct AppOptions {
    std::string romPath;
    std::string romSuiteManifest;
    bool headless = false;
    bool chooseRom = false;
    int frames = 120;
    int scale = 4;
    int audioBuffer = 1024;
};

bool parseAppOptions(int argc, char** argv, AppOptions& outOptions, std::string& errorMessage);

} // namespace gb
