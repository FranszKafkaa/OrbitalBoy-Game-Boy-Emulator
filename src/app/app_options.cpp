#include "gb/app/app_options.hpp"

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
