#include "gb/app/runtime_paths.hpp"

#include <filesystem>
#include <system_error>

namespace gb {

namespace {

std::string romStemOrDefault(const std::string& romPath) {
    const std::filesystem::path rom(romPath);
    std::string stem = rom.stem().string();
    if (stem.empty()) {
        stem = "default";
    }
    return stem;
}

std::string buildStatesPath(const std::string& romPath, const std::string& extension) {
    std::error_code ec;
    std::filesystem::create_directories("states", ec);
    std::filesystem::path p = std::filesystem::path("states") / (romStemOrDefault(romPath) + extension);
    return p.string();
}

} // namespace

std::string statePathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".state");
}

std::string legacyStatePathForRom(const std::string& romPath) {
    std::filesystem::path p(romPath);
    p.replace_extension(".state");
    return p.string();
}

std::string batteryRamPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".sav");
}

std::string palettePathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".palette");
}

std::string rtcPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".rtc");
}

std::string controlsPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".controls");
}

std::string filtersPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".filters");
}

std::string cheatsPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".cheats");
}

std::string replayPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".replay");
}

std::string captureDirForRom(const std::string& romPath) {
    std::error_code ec;
    std::filesystem::create_directories("captures", ec);
    const std::filesystem::path p = std::filesystem::path("captures") / romStemOrDefault(romPath);
    std::filesystem::create_directories(p, ec);
    return p.string();
}

} // namespace gb
