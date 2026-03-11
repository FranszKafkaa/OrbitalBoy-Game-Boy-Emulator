#pragma once

#include <string>

namespace gb {

std::string statePathForRom(const std::string& romPath);
std::string stateSlotPathForRom(const std::string& romPath, int slot);
std::string stateSlotMetaPathForRom(const std::string& romPath, int slot);
std::string stateSlotThumbnailPathForRom(const std::string& romPath, int slot);
std::string legacyStatePathForRom(const std::string& romPath);
std::string batteryRamPathForRom(const std::string& romPath);
std::string palettePathForRom(const std::string& romPath);
std::string rtcPathForRom(const std::string& romPath);
std::string controlsPathForRom(const std::string& romPath);
std::string filtersPathForRom(const std::string& romPath);
std::string cheatsPathForRom(const std::string& romPath);
std::string replayPathForRom(const std::string& romPath);
std::string captureDirForRom(const std::string& romPath);

} // namespace gb
