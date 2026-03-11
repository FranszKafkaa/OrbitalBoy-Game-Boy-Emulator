#pragma once

#include <string>

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

std::string chooseRomWithSdlDialog();

} // namespace gb::frontend
#endif

