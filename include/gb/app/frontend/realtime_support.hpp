#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "gb/core/bus.hpp"
#include "gb/core/ppu.hpp"
#include "gb/core/types.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>

namespace gb::frontend {

enum class FullscreenScaleMode {
    CrispFit = 0,
    FullStretch = 1,
    FullStretchSharp = 2,
};

enum class LinkCableMode {
    Off = 0,
    Loopback = 1,
    Noise = 2,
    Socket = 3,
};

enum class DisplayPaletteMode {
    GameBoyClassic = 0,
    GameBoyPocket = 1,
    GameBoyColor = 2,
};

enum class VideoFilterMode {
    None = 0,
    Scanline = 1,
    Lcd = 2,
};

using RgbFrame = std::array<unsigned char, gb::PPU::ScreenWidth * gb::PPU::ScreenHeight * 3>;

struct BlitLayout {
    SDL_Rect contentDst{};
    SDL_Rect gameDst{};
};

void updateWindowTitle(SDL_Window* window, const std::string& title, bool paused, bool muted);

const char* displayPaletteUiName(DisplayPaletteMode mode);
const char* scaleModeUiName(FullscreenScaleMode mode);
const char* linkCableUiName(LinkCableMode mode);
const char* filterUiName(VideoFilterMode mode);

void applySharpenRgb24(const RgbFrame& in, RgbFrame& out);
void applyVideoFilterRgb24(VideoFilterMode mode, RgbFrame& pixels);

void drawFullscreenScaleMenu(SDL_Renderer* renderer, int outputW, int outputH, int selectedIndex);
void drawPaletteModeMenu(SDL_Renderer* renderer, int outputW, int outputH, int selectedIndex, bool cgbSupported);

const std::array<std::array<unsigned char, 3>, 4>& monoPalette(DisplayPaletteMode mode);
std::optional<DisplayPaletteMode> loadPalettePreference(const std::string& path);
void savePalettePreference(const std::string& path, DisplayPaletteMode mode);
std::optional<VideoFilterMode> loadFilterPreference(const std::string& path);
void saveFilterPreference(const std::string& path, VideoFilterMode mode);

int opcodeLength(gb::u8 op);
std::string disasmBytesLine(const gb::Bus& bus, gb::u16 pc);
std::vector<std::string> buildDisasmWindow(const gb::Bus& bus, gb::u16 startPc, int maxLines);

bool saveRgb24Ppm(const std::string& path, const RgbFrame& pixels);
std::string nextCapturePath(const std::string& captureDir);

BlitLayout computeGameBlitLayout(
    int outputW,
    int outputH,
    int screenW,
    int screenH,
    int panelWidth,
    bool showPanel,
    bool fullscreen,
    FullscreenScaleMode fullscreenMode
);

} // namespace gb::frontend
#endif
