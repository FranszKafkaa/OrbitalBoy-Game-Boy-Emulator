#include "gb/app/app_options.hpp"

namespace gb {

bool parseAppOptions(int argc, char** argv, AppOptions& outOptions, std::string& errorMessage) {
    AppOptions options{};

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--rom" && i + 1 < argc) {
            options.romPath = argv[++i];
            continue;
        }
        if (arg == "--rom-suite" && i + 1 < argc) {
            options.romSuiteManifest = argv[++i];
            options.headless = true;
            continue;
        }
        if (arg == "--choose-rom") {
            options.chooseRom = true;
            continue;
        }
        if (arg == "--headless") {
            options.headless = true;
            if (i + 1 < argc) {
                options.frames = std::stoi(argv[++i]);
                if (options.frames < 1) {
                    options.frames = 1;
                }
            }
            continue;
        }
        if (arg == "--scale" && i + 1 < argc) {
            options.scale = std::stoi(argv[++i]);
            if (options.scale < 1) {
                options.scale = 1;
            }
            continue;
        }
        if (arg == "--audio-buffer" && i + 1 < argc) {
            options.audioBuffer = std::stoi(argv[++i]);
            if (options.audioBuffer < 256) {
                options.audioBuffer = 256;
            }
            if (options.audioBuffer > 8192) {
                options.audioBuffer = 8192;
            }
            continue;
        }

        if (!arg.empty() && arg[0] != '-' && options.romPath.empty()) {
            options.romPath = arg;
            continue;
        }

        if (!arg.empty() && arg[0] == '-') {
            errorMessage = "opcao invalida: " + arg;
            return false;
        }

        options.frames = std::stoi(arg);
        if (options.frames < 1) {
            options.frames = 1;
        }
        options.headless = true;
    }

    outOptions = options;
    return true;
}

} // namespace gb
