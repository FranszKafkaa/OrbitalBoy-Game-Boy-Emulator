#include "gb/app/app_options.hpp"

#include <algorithm>
#include <cctype>
#include <exception>

namespace gb {

namespace {

bool parseIntValue(const std::string& text, int& out) {
    try {
        std::size_t idx = 0;
        const int parsed = std::stoi(text, &idx, 10);
        if (idx != text.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool readIntArg(
    int argc,
    char** argv,
    int& i,
    const std::string& optionName,
    int& outValue,
    std::string& errorMessage
) {
    if (i + 1 >= argc) {
        errorMessage = "faltou valor para " + optionName;
        return false;
    }
    const std::string valueText = argv[++i];
    if (!parseIntValue(valueText, outValue)) {
        errorMessage = "valor invalido para " + optionName + ": " + valueText;
        return false;
    }
    return true;
}

bool readStringArg(
    int argc,
    char** argv,
    int& i,
    const std::string& optionName,
    std::string& outValue,
    std::string& errorMessage
) {
    if (i + 1 >= argc) {
        errorMessage = "faltou valor para " + optionName;
        return false;
    }
    outValue = argv[++i];
    return true;
}

} // namespace

bool parseAppOptions(int argc, char** argv, AppOptions& outOptions, std::string& errorMessage) {
    AppOptions options{};

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--rom") {
            if (!readStringArg(argc, argv, i, arg, options.romPath, errorMessage)) {
                return false;
            }
            continue;
        }
        if (arg == "--rom-suite") {
            if (!readStringArg(argc, argv, i, arg, options.romSuiteManifest, errorMessage)) {
                return false;
            }
            options.headless = true;
            continue;
        }
        if (arg == "--boot-rom") {
            if (!readStringArg(argc, argv, i, arg, options.bootRomPath, errorMessage)) {
                return false;
            }
            continue;
        }
        if (arg == "--choose-rom") {
            options.chooseRom = true;
            continue;
        }
        if (arg == "--precise-timing") {
            options.preciseTiming = true;
            continue;
        }
        if (arg == "--hardware") {
            std::string mode;
            if (!readStringArg(argc, argv, i, arg, mode, errorMessage)) {
                return false;
            }
            mode = toLowerAscii(mode);
            if (mode == "auto") {
                options.hardwareMode = HardwareModePreference::Auto;
            } else if (mode == "dmg") {
                options.hardwareMode = HardwareModePreference::Dmg;
            } else if (mode == "cgb") {
                options.hardwareMode = HardwareModePreference::Cgb;
            } else {
                errorMessage = "valor invalido para --hardware: " + mode + " (use auto|dmg|cgb)";
                return false;
            }
            continue;
        }
        if (arg == "--system") {
            std::string mode;
            if (!readStringArg(argc, argv, i, arg, mode, errorMessage)) {
                return false;
            }
            mode = toLowerAscii(mode);
            if (mode == "auto") {
                options.targetSystem = TargetSystemPreference::Auto;
            } else if (mode == "gb") {
                options.targetSystem = TargetSystemPreference::Gb;
            } else if (mode == "gba") {
                options.targetSystem = TargetSystemPreference::Gba;
            } else {
                errorMessage = "valor invalido para --system: " + mode + " (use auto|gb|gba)";
                return false;
            }
            continue;
        }
        if (arg == "--headless") {
            options.headless = true;
            if (i + 1 < argc) {
                const std::string next = argv[i + 1];
                int parsedFrames = 0;
                if (parseIntValue(next, parsedFrames)) {
                    ++i;
                    options.frames = parsedFrames;
                    if (options.frames < 1) {
                        options.frames = 1;
                    }
                }
            }
            continue;
        }
        if (arg == "--scale") {
            if (!readIntArg(argc, argv, i, arg, options.scale, errorMessage)) {
                return false;
            }
            if (options.scale < 1) {
                options.scale = 1;
            }
            continue;
        }
        if (arg == "--audio-buffer") {
            if (!readIntArg(argc, argv, i, arg, options.audioBuffer, errorMessage)) {
                return false;
            }
            if (options.audioBuffer < 256) {
                options.audioBuffer = 256;
            }
            if (options.audioBuffer > 8192) {
                options.audioBuffer = 8192;
            }
            continue;
        }
        if (arg == "--link-host") {
            if (!readIntArg(argc, argv, i, arg, options.linkHostPort, errorMessage)) {
                return false;
            }
            if (options.linkHostPort < 1 || options.linkHostPort > 65535) {
                errorMessage = "porta invalida para --link-host";
                return false;
            }
            continue;
        }
        if (arg == "--link-connect") {
            if (!readStringArg(argc, argv, i, arg, options.linkConnect, errorMessage)) {
                return false;
            }
            continue;
        }
        if (arg == "--netplay-host") {
            if (!readIntArg(argc, argv, i, arg, options.netplayHostPort, errorMessage)) {
                return false;
            }
            if (options.netplayHostPort < 1 || options.netplayHostPort > 65535) {
                errorMessage = "porta invalida para --netplay-host";
                return false;
            }
            continue;
        }
        if (arg == "--netplay-connect") {
            if (!readStringArg(argc, argv, i, arg, options.netplayConnect, errorMessage)) {
                return false;
            }
            continue;
        }
        if (arg == "--netplay-delay") {
            if (!readIntArg(argc, argv, i, arg, options.netplayDelayFrames, errorMessage)) {
                return false;
            }
            if (options.netplayDelayFrames < 0) {
                options.netplayDelayFrames = 0;
            }
            if (options.netplayDelayFrames > 10) {
                options.netplayDelayFrames = 10;
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

        if (!parseIntValue(arg, options.frames)) {
            errorMessage = "argumento invalido: " + arg;
            return false;
        }
        if (options.frames < 1) {
            options.frames = 1;
        }
        options.headless = true;
    }

    outOptions = options;
    return true;
}

} // namespace gb
