#include "gb/app/frontend/realtime_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "gb/app/frontend/debug_ui.hpp"

namespace gb::frontend {

#ifdef GBEMU_USE_SDL2
void updateWindowTitle(SDL_Window* window, const std::string& title, bool paused, bool muted) {
    std::string full = "GB Emulator - " + title + " - ";
    full += paused ? "PAUSED" : "RUNNING";
    full += muted ? " - MUTED" : " - AUDIO ON";
    SDL_SetWindowTitle(window, full.c_str());
}

const char* displayPaletteUiName(DisplayPaletteMode mode) {
    switch (mode) {
    case DisplayPaletteMode::GameBoyClassic: return "CLASSIC";
    case DisplayPaletteMode::GameBoyPocket: return "POCKET";
    default: return "COLOR";
    }
}

const char* scaleModeUiName(FullscreenScaleMode mode) {
    switch (mode) {
    case FullscreenScaleMode::CrispFit: return "CRISP FIT";
    case FullscreenScaleMode::FullStretch: return "FULL STRETCH";
    default: return "FULL STRETCH SHARP";
    }
}

const char* linkCableUiName(LinkCableMode mode) {
    switch (mode) {
    case LinkCableMode::Off: return "LINK OFF";
    case LinkCableMode::Loopback: return "LINK LOOP";
    default: return "LINK NOISE";
    }
}

const char* filterUiName(VideoFilterMode mode) {
    switch (mode) {
    case VideoFilterMode::None: return "FILTER NONE";
    case VideoFilterMode::Scanline: return "FILTER SCANLINE";
    default: return "FILTER LCD";
    }
}

void applySharpenRgb24(const RgbFrame& in, RgbFrame& out) {
    constexpr int w = gb::PPU::ScreenWidth;
    constexpr int h = gb::PPU::ScreenHeight;
    out = in;
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const int i = (y * w + x) * 3;
            const int l = (y * w + (x - 1)) * 3;
            const int r = (y * w + (x + 1)) * 3;
            const int u = ((y - 1) * w + x) * 3;
            const int d = ((y + 1) * w + x) * 3;
            for (int c = 0; c < 3; ++c) {
                int v = 5 * static_cast<int>(in[static_cast<std::size_t>(i + c)])
                      - static_cast<int>(in[static_cast<std::size_t>(l + c)])
                      - static_cast<int>(in[static_cast<std::size_t>(r + c)])
                      - static_cast<int>(in[static_cast<std::size_t>(u + c)])
                      - static_cast<int>(in[static_cast<std::size_t>(d + c)]);
                v = std::clamp(v, 0, 255);
                out[static_cast<std::size_t>(i + c)] = static_cast<unsigned char>(v);
            }
        }
    }
}

void applyVideoFilterRgb24(VideoFilterMode mode, RgbFrame& pixels) {
    if (mode == VideoFilterMode::None) {
        return;
    }

    constexpr int w = gb::PPU::ScreenWidth;
    constexpr int h = gb::PPU::ScreenHeight;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>((y * w + x) * 3);
            int r = pixels[i + 0];
            int g = pixels[i + 1];
            int b = pixels[i + 2];

            if (mode == VideoFilterMode::Scanline) {
                if ((y & 1) != 0) {
                    r = (r * 72) / 100;
                    g = (g * 72) / 100;
                    b = (b * 72) / 100;
                }
            } else if (mode == VideoFilterMode::Lcd) {
                r = std::min(255, (r * 96) / 100);
                g = std::min(255, ((g * 108) / 100) + 4);
                b = std::min(255, (b * 92) / 100);
                if ((x % 3) == 0) {
                    r = std::min(255, r + 8);
                } else if ((x % 3) == 1) {
                    g = std::min(255, g + 8);
                } else {
                    b = std::min(255, b + 8);
                }
                if ((y & 1) != 0) {
                    r = (r * 90) / 100;
                    g = (g * 90) / 100;
                    b = (b * 90) / 100;
                }
            }

            pixels[i + 0] = static_cast<unsigned char>(std::clamp(r, 0, 255));
            pixels[i + 1] = static_cast<unsigned char>(std::clamp(g, 0, 255));
            pixels[i + 2] = static_cast<unsigned char>(std::clamp(b, 0, 255));
        }
    }
}

void drawFullscreenScaleMenu(SDL_Renderer* renderer, int outputW, int outputH, int selectedIndex) {
    const int boxW = 460;
    const int boxH = 120;
    const int x = (outputW - boxW) / 2;
    const int y = (outputH - boxH) / 2;

    SDL_SetRenderDrawColor(renderer, 10, 14, 24, 220);
    SDL_Rect bg{x, y, boxW, boxH};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 90, 110, 150, 255);
    SDL_RenderDrawRect(renderer, &bg);

    drawHexText(renderer, x + 12, y + 8, "N MENU SCALE MODE", SDL_Color{235, 240, 255, 255}, 1);

    const std::array<const char*, 3> items{
        "1 CRISP FIT BARRAS",
        "2 FULL STRETCH",
        "3 FULL STRETCH SHARP",
    };
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const bool selected = i == selectedIndex;
        SDL_Color color = selected ? SDL_Color{255, 230, 120, 255} : SDL_Color{190, 198, 218, 255};
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 45, 55, 80, 255);
            SDL_Rect row{x + 8, y + 26 + i * 24, boxW - 16, 18};
            SDL_RenderFillRect(renderer, &row);
        }
        drawHexText(renderer, x + 14, y + 30 + i * 24, items[static_cast<std::size_t>(i)], color, 1);
    }

    drawHexText(renderer, x + 12, y + boxH - 16, "UP DOWN ENTER N", SDL_Color{145, 156, 182, 255}, 1);
}

void drawPaletteModeMenu(SDL_Renderer* renderer, int outputW, int outputH, int selectedIndex, bool cgbSupported) {
    const int itemCount = cgbSupported ? 3 : 2;
    const int boxW = 460;
    const int boxH = 78 + itemCount * 24;
    const int x = (outputW - boxW) / 2;
    const int y = (outputH - boxH) / 2;

    SDL_SetRenderDrawColor(renderer, 10, 14, 24, 220);
    SDL_Rect bg{x, y, boxW, boxH};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 90, 110, 150, 255);
    SDL_RenderDrawRect(renderer, &bg);

    drawHexText(renderer, x + 12, y + 8, "V MENU PALETA", SDL_Color{235, 240, 255, 255}, 1);

    const std::array<const char*, 3> items{
        "1 GAME BOY CLASSICO",
        "2 GAME BOY POCKET",
        "3 GAME BOY COLOR",
    };
    const int clampedSelected = std::clamp(selectedIndex, 0, itemCount - 1);
    for (int i = 0; i < itemCount; ++i) {
        const bool selected = i == clampedSelected;
        SDL_Color color = selected ? SDL_Color{255, 230, 120, 255} : SDL_Color{190, 198, 218, 255};
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 45, 55, 80, 255);
            SDL_Rect row{x + 8, y + 26 + i * 24, boxW - 16, 18};
            SDL_RenderFillRect(renderer, &row);
        }
        drawHexText(renderer, x + 14, y + 30 + i * 24, items[static_cast<std::size_t>(i)], color, 1);
    }

    drawHexText(renderer, x + 12, y + boxH - 16, "UP DOWN ENTER V ESC", SDL_Color{145, 156, 182, 255}, 1);
}

const std::array<std::array<unsigned char, 3>, 4>& monoPalette(DisplayPaletteMode mode) {
    static constexpr std::array<std::array<unsigned char, 3>, 4> kClassicPalette = {{
        {{155, 188, 15}},
        {{139, 172, 15}},
        {{48, 98, 48}},
        {{15, 56, 15}},
    }};
    static constexpr std::array<std::array<unsigned char, 3>, 4> kPocketPalette = {{
        {{255, 255, 255}},
        {{176, 176, 176}},
        {{88, 88, 88}},
        {{0, 0, 0}},
    }};
    return mode == DisplayPaletteMode::GameBoyPocket ? kPocketPalette : kClassicPalette;
}

std::optional<DisplayPaletteMode> loadPalettePreference(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    int value = -1;
    in >> value;
    if (!in || value < 0 || value > 2) {
        return std::nullopt;
    }
    return static_cast<DisplayPaletteMode>(value);
}

void savePalettePreference(const std::string& path, DisplayPaletteMode mode) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }
    out << static_cast<int>(mode) << "\n";
}

std::optional<VideoFilterMode> loadFilterPreference(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    int value = -1;
    in >> value;
    if (!in || value < 0 || value > 2) {
        return std::nullopt;
    }
    return static_cast<VideoFilterMode>(value);
}

void saveFilterPreference(const std::string& path, VideoFilterMode mode) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }
    out << static_cast<int>(mode) << "\n";
}

int opcodeLength(gb::u8 op) {
    if (op == 0xCB) {
        return 2;
    }
    switch (op) {
    case 0x01: case 0x08: case 0x11: case 0x21: case 0x31:
    case 0xC2: case 0xC3: case 0xC4: case 0xCA: case 0xCC:
    case 0xCD: case 0xD2: case 0xD4: case 0xDA: case 0xDC:
    case 0xEA: case 0xFA:
        return 3;
    case 0x06: case 0x0E: case 0x10: case 0x16: case 0x18:
    case 0x1E: case 0x20: case 0x26: case 0x28: case 0x2E:
    case 0x30: case 0x36: case 0x38: case 0x3E: case 0xC6:
    case 0xCE: case 0xD6: case 0xDE: case 0xE0: case 0xE6:
    case 0xE8: case 0xEE: case 0xF0: case 0xF6: case 0xF8:
    case 0xFE:
        return 2;
    default:
        return 1;
    }
}

std::string disasmBytesLine(const gb::Bus& bus, gb::u16 pc) {
    const gb::u8 op = bus.peek(pc);
    const int len = opcodeLength(op);
    char line[48];
    if (len == 1) {
        std::snprintf(line, sizeof(line), "%04X:%02X", pc, op);
    } else if (len == 2) {
        const gb::u8 p1 = bus.peek(static_cast<gb::u16>(pc + 1));
        std::snprintf(line, sizeof(line), "%04X:%02X %02X", pc, op, p1);
    } else {
        const gb::u8 p1 = bus.peek(static_cast<gb::u16>(pc + 1));
        const gb::u8 p2 = bus.peek(static_cast<gb::u16>(pc + 2));
        std::snprintf(line, sizeof(line), "%04X:%02X %02X %02X", pc, op, p1, p2);
    }
    return line;
}

std::vector<std::string> buildDisasmWindow(const gb::Bus& bus, gb::u16 startPc, int maxLines) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(std::max(0, maxLines)));
    gb::u16 pc = startPc;
    for (int i = 0; i < maxLines; ++i) {
        out.push_back(disasmBytesLine(bus, pc));
        pc = static_cast<gb::u16>(pc + opcodeLength(bus.peek(pc)));
    }
    return out;
}

bool saveRgb24Ppm(const std::string& path, const RgbFrame& pixels) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "P6\n" << gb::PPU::ScreenWidth << " " << gb::PPU::ScreenHeight << "\n255\n";
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return static_cast<bool>(out);
}

std::string nextCapturePath(const std::string& captureDir) {
    std::error_code ec;
    std::filesystem::create_directories(captureDir, ec);

    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char name[64];
    std::snprintf(
        name,
        sizeof(name),
        "frame_%04d%02d%02d_%02d%02d%02d_%03d.ppm",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        static_cast<int>(ms.count())
    );
    return (std::filesystem::path(captureDir) / name).string();
}

BlitLayout computeGameBlitLayout(
    int outputW,
    int outputH,
    int screenW,
    int screenH,
    int panelWidth,
    bool showPanel,
    bool fullscreen,
    FullscreenScaleMode fullscreenMode
) {
    const int debugWidth = showPanel ? panelWidth : 0;
    const int contentW = std::max(1, outputW - debugWidth);
    const int contentX = 0;
    const SDL_Rect contentDst{contentX, 0, contentW, outputH};

    int drawW = 0;
    int drawH = 0;
    if (fullscreen && !showPanel) {
        if (fullscreenMode == FullscreenScaleMode::CrispFit) {
            drawH = outputH;
            drawW = std::max(1, static_cast<int>(
                (static_cast<float>(outputH) * static_cast<float>(screenW)) / static_cast<float>(screenH) + 0.5f
            ));
            if (drawW > contentW) {
                drawW = contentW;
                drawH = std::max(1, static_cast<int>(
                    (static_cast<float>(contentW) * static_cast<float>(screenH)) / static_cast<float>(screenW) + 0.5f
                ));
            }
        } else {
            drawW = contentW;
            drawH = outputH;
        }
    } else {
        const float sx = static_cast<float>(contentW) / static_cast<float>(screenW);
        const float sy = static_cast<float>(outputH) / static_cast<float>(screenH);
        const float s = std::max(0.1f, std::min(sx, sy));
        drawW = std::max(1, static_cast<int>(screenW * s + 0.5f));
        drawH = std::max(1, static_cast<int>(screenH * s + 0.5f));
    }

    const int gameX = contentX + (contentW - drawW) / 2;
    const int gameY = (outputH - drawH) / 2;
    const SDL_Rect gameDst{gameX, gameY, drawW, drawH};
    return BlitLayout{contentDst, gameDst};
}
#endif

} // namespace gb::frontend
