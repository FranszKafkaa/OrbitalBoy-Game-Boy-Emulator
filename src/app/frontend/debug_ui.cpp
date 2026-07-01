#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "gb/app/frontend/debug_ui.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>
#endif

namespace gb::frontend {

#ifdef GBEMU_USE_SDL2
void setButtonFromKey(gb::GameBoy& gb, int key, bool pressed) {
    switch (key) {
    case SDLK_UP: gb.joypad().setButton(gb::Button::Up, pressed); break;
    case SDLK_DOWN: gb.joypad().setButton(gb::Button::Down, pressed); break;
    case SDLK_LEFT: gb.joypad().setButton(gb::Button::Left, pressed); break;
    case SDLK_RIGHT: gb.joypad().setButton(gb::Button::Right, pressed); break;
    case SDLK_z: gb.joypad().setButton(gb::Button::A, pressed); break;
    case SDLK_x: gb.joypad().setButton(gb::Button::B, pressed); break;
    case SDLK_BACKSPACE: gb.joypad().setButton(gb::Button::Select, pressed); break;
    case SDLK_RETURN: gb.joypad().setButton(gb::Button::Start, pressed); break;
    default: break;
    }
}
std::array<gb::u8, 7> hexGlyph(char c) {
    switch (c) {
    case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7': return {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04};
    case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    case 'D': return {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
    case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    case 'G': return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'I': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    case 'J': return {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    case ':': return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
    case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    case '/': return {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
    case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    default: return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
}

int selectedSectionHeightForPanel(int panelHeight) {
    if (panelHeight < 320) {
        return 52;
    }
    if (panelHeight < 440) {
        return 68;
    }
    if (panelHeight < 560) {
        return 82;
    }
    return kSelectedSectionHeight;
}

int readStartYFromLayout(int panelHeight, bool showBreakpointMenu) {
    const int headerBottom = 122;
    const int menuSpace = showBreakpointMenu ? 86 : 0;
    const int preferred = showBreakpointMenu ? kReadStartYWithBreakpointMenu : kReadStartYWithoutBreakpointMenu;
    const int minStart = headerBottom + menuSpace + 6;
    const int maxStart = std::max(minStart, panelHeight - 220);
    return std::min(std::max(preferred, minStart), maxStart);
}

int selectedSectionY(int panelHeight, int readStartY) {
    (void)panelHeight;
    return readStartY + kReadLines * kReadLineHeight + kSelectedSectionTopGap;
}

int spriteHeaderYFromLayout(int panelHeight, int readStartY) {
    return selectedSectionY(panelHeight, readStartY) + selectedSectionHeightForPanel(panelHeight) + kSectionGap;
}

int readVisibleLinesForPanel(int panelHeight, bool showBreakpointMenu) {
    const int readStartY = readStartYFromLayout(panelHeight, showBreakpointMenu);
    const int detailY = selectedSectionY(panelHeight, readStartY);
    const int available = std::max(0, detailY - readStartY - 2);
    return std::max(1, std::min(kReadLines, available / kReadLineHeight));
}

int spriteListYFromLayout(int panelHeight, bool showBreakpointMenu) {
    const int readStartY = readStartYFromLayout(panelHeight, showBreakpointMenu);
    return spriteHeaderYFromLayout(panelHeight, readStartY) + kSpriteSectionTopPad;
}

void resetMemoryWatch(MemoryWatch& watch, const gb::Bus& bus) {
    watch.count = 1;
    watch.head = 1;
    watch.history[0] = bus.peek(watch.address);
    watch.freezeValue = watch.history[0];
}

void sampleMemoryWatch(MemoryWatch& watch, const gb::Bus& bus) {
    watch.history[watch.head] = bus.peek(watch.address);
    watch.head = (watch.head + 1) % watch.history.size();
    watch.count = std::min(watch.history.size(), watch.count + 1);
}

std::optional<gb::u16> parseHex16(const std::string& hex) {
    if (hex.empty() || hex.size() > 4) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (const char ch : hex) {
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= static_cast<unsigned>(ch - '0');
        } else if (ch >= 'A' && ch <= 'F') {
            value |= static_cast<unsigned>(ch - 'A' + 10);
        } else {
            return std::nullopt;
        }
    }
    return static_cast<gb::u16>(value & 0xFFFF);
}

std::optional<gb::u8> parseHex8(const std::string& hex) {
    if (hex.empty() || hex.size() > 2) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (const char ch : hex) {
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= static_cast<unsigned>(ch - '0');
        } else if (ch >= 'A' && ch <= 'F') {
            value |= static_cast<unsigned>(ch - 'A' + 10);
        } else {
            return std::nullopt;
        }
    }
    return static_cast<gb::u8>(value & 0xFF);
}

bool likelyWritableAddress(gb::u16 addr) {
    if (addr <= 0x7FFF) {
        return false; // ROM/control banks
    }
    if (addr >= 0xA000 && addr <= 0xBFFF) {
        return true; // cart RAM (if enabled by mapper)
    }
    if (addr >= 0xC000 && addr <= 0xFDFF) {
        return true; // WRAM + echo
    }
    if (addr >= 0xFE00 && addr <= 0xFE9F) {
        return true; // OAM
    }
    if (addr >= 0xFF00 && addr <= 0xFFFE) {
        return true; // IO + HRAM
    }
    if (addr == 0xFFFF) {
        return true; // IE
    }
    return false;
}

std::vector<SpriteDebugRow> snapshotSprites(const gb::Bus& bus) {
    std::vector<SpriteDebugRow> out;
    out.reserve(40);
    for (int i = 0; i < 40; ++i) {
        const gb::u16 base = static_cast<gb::u16>(0xFE00 + i * 4);
        const gb::u8 y = bus.peek(base);
        const gb::u8 x = bus.peek(static_cast<gb::u16>(base + 1));
        const gb::u8 tile = bus.peek(static_cast<gb::u16>(base + 2));
        const gb::u8 attr = bus.peek(static_cast<gb::u16>(base + 3));
        out.push_back(SpriteDebugRow{base, y, x, tile, attr});
    }
    return out;
}

int spriteVisibleLinesForPanel(int panelHeight, bool showBreakpointMenu) {
    return std::max(1, (panelHeight - spriteListYFromLayout(panelHeight, showBreakpointMenu) - 148) / kSpriteLineHeight);
}

int searchVisibleLinesForPanel(int panelHeight) {
    return std::max(1, (panelHeight - (kSearchOverlayTop + kSearchListYOffset) - kSearchOverlayBottomPad) / kSearchListLineHeight);
}

std::string spriteRoleText(const SpriteDebugRow& sp, const gb::Bus& bus) {
    const gb::u8 lcdc = bus.peek(0xFF40);
    const bool tallSprites = (lcdc & 0x04) != 0;
    const int spriteHeight = tallSprites ? 16 : 8;
    const int sx = static_cast<int>(sp.x) - 8;
    const int sy = static_cast<int>(sp.y) - 16;
    const bool visible = (sx > -8 && sx < gb::PPU::ScreenWidth && sy > -spriteHeight && sy < gb::PPU::ScreenHeight);
    const bool priLow = (sp.attr & 0x80) != 0;
    const bool yFlip = (sp.attr & 0x40) != 0;
    const bool xFlip = (sp.attr & 0x20) != 0;
    const bool pal1 = (sp.attr & 0x10) != 0;

    char role[48];
    std::snprintf(
        role,
        sizeof(role),
        "%s %s %s%s P%d",
        visible ? "ON" : "OFF",
        priLow ? "BG" : "TOP",
        yFlip ? "Y" : "",
        xFlip ? "X" : "",
        pal1 ? 1 : 0
    );
    return role;
}

std::optional<SpriteDebugRow> findSelectedSprite(
    const std::vector<SpriteDebugRow>& sprites,
    std::optional<gb::u16> selectedAddr
) {
    if (!selectedAddr.has_value()) {
        return std::nullopt;
    }
    for (const auto& sp : sprites) {
        if (sp.addr == selectedAddr.value()) {
            return sp;
        }
    }
    return std::nullopt;
}

SDL_Color shadeToColor(gb::u8 shade) {
    switch (shade & 0x03) {
    case 0: return SDL_Color{255, 255, 255, 255};
    case 1: return SDL_Color{192, 192, 192, 255};
    case 2: return SDL_Color{96, 96, 96, 255};
    default: return SDL_Color{16, 16, 16, 255};
    }
}

void drawSpritePreview(
    SDL_Renderer* renderer,
    const gb::Bus& bus,
    const SpriteDebugRow& sp,
    int x,
    int y,
    int pixelScale
) {
    const gb::u8 lcdc = bus.peek(0xFF40);
    const bool tallSprites = (lcdc & 0x04) != 0;
    const int spriteHeight = tallSprites ? 16 : 8;
    const bool yFlip = (sp.attr & 0x40) != 0;
    const bool xFlip = (sp.attr & 0x20) != 0;
    const gb::u8 paletteReg = (sp.attr & 0x10) ? bus.peek(0xFF49) : bus.peek(0xFF48);

    SDL_SetRenderDrawColor(renderer, 20, 24, 34, 255);
    SDL_Rect bg{x - 2, y - 2, 8 * pixelScale + 4, spriteHeight * pixelScale + 4};
    SDL_RenderFillRect(renderer, &bg);

    for (int py = 0; py < spriteHeight; ++py) {
        int srcY = yFlip ? (spriteHeight - 1 - py) : py;
        gb::u8 tileNum = sp.tile;
        if (tallSprites) {
            tileNum = static_cast<gb::u8>(tileNum & 0xFE);
            if (srcY >= 8) {
                tileNum = static_cast<gb::u8>(tileNum + 1);
                srcY -= 8;
            }
        }

        const gb::u16 tileAddr = static_cast<gb::u16>(0x8000 + tileNum * 16 + srcY * 2);
        const gb::u8 lo = bus.peek(tileAddr);
        const gb::u8 hi = bus.peek(static_cast<gb::u16>(tileAddr + 1));

        for (int px = 0; px < 8; ++px) {
            const int bit = xFlip ? px : (7 - px);
            const gb::u8 colorId = static_cast<gb::u8>((((hi >> bit) & 0x01) << 1) | ((lo >> bit) & 0x01));
            const gb::u8 shade = static_cast<gb::u8>((paletteReg >> (colorId * 2)) & 0x03);
            const auto color = shadeToColor(shade);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            SDL_Rect p{x + px * pixelScale, y + py * pixelScale, pixelScale, pixelScale};
            SDL_RenderFillRect(renderer, &p);
        }
    }

    SDL_SetRenderDrawColor(renderer, 250, 220, 100, 255);
    SDL_Rect border{x - 2, y - 2, 8 * pixelScale + 4, spriteHeight * pixelScale + 4};
    SDL_RenderDrawRect(renderer, &border);
}

void drawSelectedSpriteOverlay(
    SDL_Renderer* renderer,
    const gb::Bus& bus,
    const std::optional<SpriteDebugRow>& selected,
    int scale,
    int gameX,
    int gameY
) {
    if (!selected.has_value()) {
        return;
    }
    const gb::u8 lcdc = bus.peek(0xFF40);
    const bool tallSprites = (lcdc & 0x04) != 0;
    const int spriteHeight = tallSprites ? 16 : 8;

    const int sx = static_cast<int>(selected->x) - 8;
    const int sy = static_cast<int>(selected->y) - 16;

    SDL_SetRenderDrawColor(renderer, 255, 220, 90, 255);
    SDL_Rect rect{gameX + sx * scale, gameY + sy * scale, 8 * scale, spriteHeight * scale};
    SDL_RenderDrawRect(renderer, &rect);

    SDL_SetRenderDrawColor(renderer, 255, 220, 90, 120);
    SDL_RenderDrawLine(renderer, rect.x, rect.y + rect.h / 2, rect.x + rect.w, rect.y + rect.h / 2);
    SDL_RenderDrawLine(renderer, rect.x + rect.w / 2, rect.y, rect.x + rect.w / 2, rect.y + rect.h);
}

void drawRunLabCandidateOverlay(
    SDL_Renderer* renderer,
    const gb::Bus& bus,
    const std::vector<SpriteDebugRow>& sprites,
    const runlab::State& runlabState,
    bool selectedEntityOnly,
    int scale,
    int gameX,
    int gameY
) {
    const gb::u8 lcdc = bus.peek(0xFF40);
    const bool tallSprites = (lcdc & 0x04) != 0;
    const int spriteHeight = tallSprites ? 16 : 8;
    const bool hasSelectedEntity = runlabState.selectedEntity.has_value()
        && runlabState.selectedEntity.value() < runlabState.entities.size();

    std::vector<int> selectedIndices;
    if (hasSelectedEntity) {
        selectedIndices = runlabState.entities[runlabState.selectedEntity.value()].oamIndices;
    }

    const auto isSelectedEntitySprite = [&](int idx) {
        return std::find(selectedIndices.begin(), selectedIndices.end(), idx) != selectedIndices.end();
    };

    for (const auto& sp : sprites) {
        const int idx = runlab::oamIndexFromAddress(sp.addr);
        const bool selected = isSelectedEntitySprite(idx);
        if (selectedEntityOnly && !selected) {
            continue;
        }
        const int sx = static_cast<int>(sp.x) - 8;
        const int sy = static_cast<int>(sp.y) - 16;
        const bool visible = sx > -8 && sx < gb::PPU::ScreenWidth && sy > -spriteHeight && sy < gb::PPU::ScreenHeight;
        if (!visible) {
            continue;
        }

        SDL_Rect rect{gameX + sx * scale, gameY + sy * scale, 8 * scale, spriteHeight * scale};
        SDL_SetRenderDrawColor(renderer, selected ? 255 : 220, selected ? 24 : 48, selected ? 24 : 48, 255);
        SDL_RenderDrawRect(renderer, &rect);
        if (selected) {
            SDL_Rect outer{rect.x - 1, rect.y - 1, rect.w + 2, rect.h + 2};
            SDL_RenderDrawRect(renderer, &outer);
            SDL_SetRenderDrawColor(renderer, 255, 24, 24, 180);
            SDL_RenderDrawLine(renderer, rect.x, rect.y + rect.h / 2, rect.x + rect.w, rect.y + rect.h / 2);
            SDL_RenderDrawLine(renderer, rect.x + rect.w / 2, rect.y, rect.x + rect.w / 2, rect.y + rect.h);
        }
    }
}

void drawHexText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color color, int scale) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int cursor = x;
    for (char ch : text) {
        const auto glyph = hexGlyph(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (1u << (4 - col))) == 0) {
                    continue;
                }
                SDL_Rect px{
                    cursor + col * scale,
                    y + row * scale,
                    scale,
                    scale,
                };
                SDL_RenderFillRect(renderer, &px);
            }
        }
        cursor += 6 * scale;
    }
}

std::string clippedText(const std::string& text, int maxPixels, int scale) {
    if (scale <= 0 || maxPixels <= 0) {
        return {};
    }
    const int charPx = 6 * scale;
    const int maxChars = std::max(0, maxPixels / charPx);
    if (maxChars <= 0 || static_cast<int>(text.size()) <= maxChars) {
        return text;
    }
    if (maxChars <= 2) {
        return text.substr(0, static_cast<std::size_t>(maxChars));
    }
    return text.substr(0, static_cast<std::size_t>(maxChars - 1)) + "~";
}

void drawHexTextFit(SDL_Renderer* renderer, int x, int y, int maxPixels, const std::string& text, SDL_Color color, int scale) {
    drawHexText(renderer, x, y, clippedText(text, maxPixels, scale), color, scale);
}

std::string uiUpper(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

SDL_Color memoryRegionColor(gb::u16 address) {
    if (address <= 0x3FFF) return SDL_Color{96, 155, 255, 255};
    if (address <= 0x7FFF) return SDL_Color{60, 120, 220, 255};
    if (address <= 0x9FFF) return SDL_Color{150, 200, 120, 255};
    if (address <= 0xBFFF) return SDL_Color{180, 150, 90, 255};
    if (address <= 0xDFFF) return SDL_Color{230, 200, 120, 255};
    if (address <= 0xFDFF) return SDL_Color{220, 160, 120, 255};
    if (address <= 0xFE9F) return SDL_Color{255, 130, 130, 255};
    if (address <= 0xFF7F) return SDL_Color{180, 140, 255, 255};
    if (address <= 0xFFFE) return SDL_Color{160, 220, 255, 255};
    return SDL_Color{255, 255, 255, 255};
}

struct RunLabCandidateRow {
    const char* group = "";
    runlab::CorrelationCandidate candidate{};
};

std::vector<RunLabCandidateRow> runLabCandidateRows(const runlab::State& state) {
    std::vector<RunLabCandidateRow> rows;
    rows.reserve(
        state.correlationResult.entityX.size()
        + state.correlationResult.entityY.size()
        + state.correlationResult.cameraX.size()
        + state.correlationResult.state.size()
    );
    for (const auto& c : state.correlationResult.entityX) rows.push_back(RunLabCandidateRow{"X", c});
    for (const auto& c : state.correlationResult.entityY) rows.push_back(RunLabCandidateRow{"Y", c});
    for (const auto& c : state.correlationResult.cameraX) rows.push_back(RunLabCandidateRow{"CAM", c});
    for (const auto& c : state.correlationResult.state) rows.push_back(RunLabCandidateRow{"STATE", c});
    return rows;
}

bool spriteVisibleOnScreen(const SpriteDebugRow& sp, const gb::Bus& bus) {
    const gb::u8 lcdc = bus.peek(0xFF40);
    const bool tallSprites = (lcdc & 0x04) != 0;
    const int spriteHeight = tallSprites ? 16 : 8;
    const int sx = static_cast<int>(sp.x) - 8;
    const int sy = static_cast<int>(sp.y) - 16;
    return sx > -8 && sx < gb::PPU::ScreenWidth && sy > -spriteHeight && sy < gb::PPU::ScreenHeight;
}

runlab::SpriteBounds boundsForSpriteDebugRow(const SpriteDebugRow& sp, const gb::Bus& bus) {
    return runlab::spriteBounds(runlab::OamSpriteRef{sp.addr, sp.y, sp.x, sp.tile, sp.attr}, bus);
}

std::string fieldPrefixForMemoryLabel(const runlab::MemoryLabel& label) {
    const std::string field = uiUpper(label.field.empty() ? label.label : label.field);
    if (field.find(".X") != std::string::npos || field == "X") return "X";
    if (field.find(".Y") != std::string::npos || field == "Y") return "Y";
    if (field.find("STATE") != std::string::npos) return "ST";
    if (field.find("LIVES") != std::string::npos || field.find("HP") != std::string::npos) return "HP";
    if (field.find("LEVEL") != std::string::npos || field.find("ROOM") != std::string::npos) return "LV";
    return field.empty() ? "MEM" : field.substr(0, std::min<std::size_t>(field.size(), 4));
}

std::string memorySummaryForEntity(
    const runlab::State& runlabState,
    const runlab::EntityCandidate& entity,
    const gb::Bus& bus,
    std::optional<gb::u16>* primaryAddress
) {
    std::string out;
    int added = 0;
    for (const auto& label : runlabState.memoryLabels) {
        if (label.entity != entity.label) {
            continue;
        }
        if (added > 0) out += " ";
        const std::int32_t value = runlab::readMemoryLabelValue(bus, label);
        out += fieldPrefixForMemoryLabel(label);
        out += " ";
        out += runlab::formatAddress(label.address);
        out += "=";
        out += std::to_string(value);
        if (primaryAddress && !primaryAddress->has_value()) {
            *primaryAddress = label.address;
        }
        if (++added >= 3) {
            break;
        }
    }
    return out;
}

bool entityUsesOamIndex(const runlab::State& runlabState, int oamIndex) {
    for (const auto& entity : runlabState.entities) {
        if (std::find(entity.oamIndices.begin(), entity.oamIndices.end(), oamIndex) != entity.oamIndices.end()) {
            return true;
        }
    }
    return false;
}

std::vector<RunLabRecognitionRow> buildRunLabRecognitionRows(
    const runlab::State& runlabState,
    const std::vector<SpriteDebugRow>& sprites,
    const gb::Bus& bus,
    std::size_t maxRows
) {
    std::vector<RunLabRecognitionRow> rows;
    rows.reserve(std::min<std::size_t>(maxRows, 16));
    bool hasPlayer = false;

    for (const auto& entity : runlabState.entities) {
        if (rows.size() >= maxRows) break;
        RunLabRecognitionRow row;
        row.role = uiUpper(runlab::entityTypeName(entity.type));
        row.label = entity.label;
        row.hasBounds = entity.lastBounds.w > 0 && entity.lastBounds.h > 0;
        row.bounds = entity.lastBounds;
        row.memorySummary = memorySummaryForEntity(runlabState, entity, bus, &row.primaryAddress);
        if (!entity.oamIndices.empty()) {
            const int oamIndex = entity.oamIndices.front();
            if (oamIndex >= 0 && oamIndex < 40) {
                row.spriteAddress = static_cast<gb::u16>(0xFE00 + oamIndex * 4);
                if (!row.primaryAddress.has_value()) {
                    row.primaryAddress = row.spriteAddress;
                }
                if (row.memorySummary.empty()) {
                    row.memorySummary = "OAM " + runlab::formatAddress(row.spriteAddress.value())
                        + " X " + runlab::formatAddress(static_cast<gb::u16>(row.spriteAddress.value() + 1))
                        + " Y " + runlab::formatAddress(row.spriteAddress.value());
                }
            }
        }
        if (row.memorySummary.empty()) {
            row.memorySummary = "MEM ?";
        }
        char detail[48];
        std::snprintf(detail, sizeof(detail), "BOX %d,%d %dx%d", row.bounds.x, row.bounds.y, row.bounds.w, row.bounds.h);
        row.detail = detail;
        if (entity.type == runlab::EntityType::Player) {
            hasPlayer = true;
        }
        rows.push_back(std::move(row));
    }

    if (!hasPlayer) {
        auto chosen = sprites.end();
        chosen = std::find_if(sprites.begin(), sprites.end(), [&](const SpriteDebugRow& sp) {
            return spriteVisibleOnScreen(sp, bus);
        });
        if (chosen != sprites.end() && rows.size() < maxRows) {
            RunLabRecognitionRow row;
            row.role = "PLAYER?";
            row.label = "visible_sprite";
            row.primaryAddress = chosen->addr;
            row.spriteAddress = chosen->addr;
            row.bounds = boundsForSpriteDebugRow(*chosen, bus);
            row.hasBounds = true;
            row.memorySummary = "OAM " + runlab::formatAddress(chosen->addr)
                + " X " + runlab::formatAddress(static_cast<gb::u16>(chosen->addr + 1))
                + " Y " + runlab::formatAddress(chosen->addr);
            row.detail = "NO PLAYER LABEL";
            rows.push_back(std::move(row));
        }
    }

    for (const auto& sp : sprites) {
        if (rows.size() >= maxRows) break;
        const int oamIndex = runlab::oamIndexFromAddress(sp.addr);
        if (!spriteVisibleOnScreen(sp, bus) || entityUsesOamIndex(runlabState, oamIndex)) {
            continue;
        }
        RunLabRecognitionRow row;
        row.role = "ITEM?";
        row.label = "sprite" + std::to_string(oamIndex);
        row.primaryAddress = sp.addr;
        row.spriteAddress = sp.addr;
        row.bounds = boundsForSpriteDebugRow(sp, bus);
        row.hasBounds = true;
        row.memorySummary = "OAM " + runlab::formatAddress(sp.addr)
            + " X " + runlab::formatAddress(static_cast<gb::u16>(sp.addr + 1))
            + " Y " + runlab::formatAddress(sp.addr);
        char detail[48];
        std::snprintf(detail, sizeof(detail), "TILE %02X ATTR %02X", sp.tile, sp.attr);
        row.detail = detail;
        rows.push_back(std::move(row));
    }

    if (rows.size() < maxRows) {
        RunLabRecognitionRow row;
        row.role = "SCENARIO";
        row.label = "camera_bg";
        row.primaryAddress = 0xFF43;
        char memory[96];
        std::snprintf(
            memory,
            sizeof(memory),
            "SCX FF43=%d SCY FF42=%d WX FF4B=%d WY FF4A=%d",
            bus.peek(0xFF43),
            bus.peek(0xFF42),
            bus.peek(0xFF4B),
            bus.peek(0xFF4A)
        );
        row.memorySummary = memory;
        row.detail = "BG/CAMERA REGS";
        rows.push_back(std::move(row));
    }

    return rows;
}

void drawMemoryPanel(
    SDL_Renderer* renderer,
    int panelX,
    int panelWidth,
    int panelHeight,
    const std::vector<gb::Bus::MemoryReadEvent>& reads,
    const std::vector<SpriteDebugRow>& sprites,
    int spriteScrollRows,
    const MemoryWatch& watch,
    const MemoryWriteUiState& writeUi,
    const MemorySearchUiState& search,
    const std::vector<std::string>& disasmLines,
    bool showBreakpointMenu,
    bool watchpointEnabled,
    const std::vector<gb::u16>& breakpoints,
    const std::string& breakpointAddressHex,
    bool breakpointAddressEditing,
    std::optional<gb::u16> selectedSpriteAddr,
    const runlab::State& runlabState,
    bool showRunLabCandidateList,
    bool showRunLabRecognitionList,
    const RunLabMcpStatus& mcpStatus,
    const gb::Bus& bus,
    gb::u16 execPc,
    gb::u8 execOp,
    gb::u16 nextPc,
    gb::u8 nextOp,
    double fps,
    bool paused,
    bool muted
) {
    SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
    SDL_Rect panel{panelX, 0, panelWidth, panelHeight};
    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 40, 44, 58, 255);
    SDL_RenderDrawLine(renderer, panelX, 0, panelX, panelHeight);

    const SDL_Color active = {220, 225, 235, 255};
    const SDL_Color dim = {120, 125, 140, 255};
    const int readStartY = readStartYFromLayout(panelHeight, showBreakpointMenu);
    const int leftX = panelX + 12;
    const int splitX = panelX + std::clamp(panelWidth / 2 + 4, 122, panelWidth - 96);
    const int leftW = std::max(36, splitX - leftX - 6);
    constexpr int kBigTextH = 14;   // 7px glyph * scale 2
    constexpr int kSmallTextH = 7;  // 7px glyph * scale 1

    int headerY = 12;
    drawHexTextFit(renderer, leftX, headerY, leftW, paused ? "PAUSED" : "RUNNING", active, 2);
    headerY += kBigTextH + 4;
    drawHexTextFit(renderer, leftX, headerY, leftW, muted ? "MUTED" : "AUDIO-ON", paused ? dim : active, 2);
    headerY += kBigTextH + 4;
    char fpsLine[24];
    std::snprintf(fpsLine, sizeof(fpsLine), "FPS:%05.1f", fps);
    drawHexTextFit(renderer, leftX, headerY, leftW, fpsLine, fps >= 55.0 ? SDL_Color{140, 220, 170, 255} : SDL_Color{255, 210, 140, 255}, 1);
    headerY += kSmallTextH + 3;
    char execLine[24];
    std::snprintf(execLine, sizeof(execLine), "PC:%04X OP:%02X", execPc, execOp);
    drawHexTextFit(renderer, leftX, headerY, leftW, execLine, active, 1);
    headerY += kSmallTextH + 3;
    char nextLine[24];
    std::snprintf(nextLine, sizeof(nextLine), "NP:%04X OP:%02X", nextPc, nextOp);
    drawHexTextFit(renderer, leftX, headerY, leftW, nextLine, dim, 1);
    headerY += kSmallTextH + 3;
    drawHexTextFit(renderer, leftX, headerY, leftW, "DISASM", dim, 1);
    headerY += kSmallTextH + 2;
    const int disasmCount = std::min<int>(3, static_cast<int>(disasmLines.size()));
    for (int i = 0; i < disasmCount; ++i) {
        drawHexTextFit(renderer, leftX, headerY + i * 10, leftW, disasmLines[static_cast<std::size_t>(i)], active, 1);
    }

    const gb::u8 watchValue = bus.peek(watch.address);
    const int watchX = splitX;
    const int watchW = std::max(58, (panelX + panelWidth - 8) - watchX);
    const int watchTextW = std::max(16, watchW - 8);
    SDL_SetRenderDrawColor(renderer, 28, 34, 48, 255);
    SDL_Rect watchBg{watchX, 8, watchW, 64};
    SDL_RenderFillRect(renderer, &watchBg);
    SDL_SetRenderDrawColor(renderer, 72, 88, 118, 255);
    SDL_RenderDrawRect(renderer, &watchBg);
    drawHexTextFit(renderer, watchX + 4, 12, watchTextW, "WATCH", active, 1);
    char watchLine[32];
    std::snprintf(watchLine, sizeof(watchLine), "%04X %02X %03d", watch.address, watchValue, watchValue);
    drawHexTextFit(renderer, watchX + 4, 24, watchTextW, watchLine, active, 1);
    drawHexTextFit(renderer, watchX + 4, 32, watchTextW, watch.freeze ? "LOCK ON" : "LOCK OFF", watch.freeze ? SDL_Color{255, 230, 120, 255} : dim, 1);

    if (writeUi.pending) {
        char pendingLine[40];
        std::snprintf(pendingLine, sizeof(pendingLine), "PEND %04X=%02X", writeUi.pendingAddress, writeUi.pendingValue);
        drawHexTextFit(renderer, watchX + 4, 40, watchTextW, pendingLine, SDL_Color{255, 214, 120, 255}, 1);
    } else if (writeUi.hasLast) {
        if (writeUi.lastOk) {
            char okLine[52];
            std::snprintf(
                okLine,
                sizeof(okLine),
                "LAST OK %04X=%02X F%llu",
                writeUi.lastAddress,
                writeUi.lastValue,
                static_cast<unsigned long long>(writeUi.lastFrame)
            );
            drawHexTextFit(renderer, watchX + 4, 40, watchTextW, okLine, SDL_Color{140, 220, 170, 255}, 1);
        } else {
            drawHexTextFit(renderer, watchX + 4, 40, watchTextW, writeUi.lastTag.empty() ? "LAST ERR" : writeUi.lastTag, SDL_Color{255, 150, 150, 255}, 1);
        }
    } else {
        drawHexTextFit(renderer, watchX + 4, 40, watchTextW, "LAST NONE", dim, 1);
    }

    const int graphX = watchX + 4;
    const int graphY = 50;
    const int graphW = std::max(12, watchW - 8);
    const int graphH = 16;
    SDL_SetRenderDrawColor(renderer, 22, 28, 40, 255);
    SDL_Rect graphBg{graphX, graphY, graphW, graphH};
    SDL_RenderFillRect(renderer, &graphBg);
    SDL_SetRenderDrawColor(renderer, 58, 70, 96, 255);
    SDL_RenderDrawRect(renderer, &graphBg);
    if (watch.count > 1) {
        const int points = std::min<int>(graphW - 2, static_cast<int>(watch.count));
        for (int i = 0; i < points; ++i) {
            const std::size_t idx = (watch.head + watch.history.size() - static_cast<std::size_t>(points) + static_cast<std::size_t>(i)) % watch.history.size();
            const int v = static_cast<int>(watch.history[idx]);
            const int py = graphY + graphH - 2 - ((v * (graphH - 3)) / 255);
            SDL_SetRenderDrawColor(renderer, 124, 208, 168, 255);
            SDL_RenderDrawPoint(renderer, graphX + 1 + i, py);
        }
    }

    if (showBreakpointMenu) {
        SDL_SetRenderDrawColor(renderer, 26, 30, 42, 255);
        SDL_Rect bpMenuBg{panelX + 8, kBreakpointMenuTopY, panelWidth - 16, readStartY - kBreakpointMenuTopY - 4};
        SDL_RenderFillRect(renderer, &bpMenuBg);
        SDL_SetRenderDrawColor(renderer, 60, 72, 98, 255);
        SDL_RenderDrawRect(renderer, &bpMenuBg);
        drawHexTextFit(renderer, panelX + 12, kBreakpointMenuTopY + 2, panelWidth - 24, "BP/WP MENU", active, 1);

        char wpLine[40];
        std::snprintf(wpLine, sizeof(wpLine), "WP %04X %s", watch.address, watchpointEnabled ? "ON" : "OFF");
        drawHexText(
            renderer,
            panelX + 12,
            kBreakpointRowYWatch,
            clippedText(wpLine, panelWidth - 24, 1),
            watchpointEnabled ? SDL_Color{255, 230, 120, 255} : dim,
            1
        );

        char bpPcLine[40];
        std::snprintf(bpPcLine, sizeof(bpPcLine), "BP PC %04X CLICK", nextPc);
        drawHexTextFit(renderer, panelX + 12, kBreakpointRowYPc, panelWidth - 24, bpPcLine, active, 1);

        const std::string bpAddr = breakpointAddressHex.empty() ? "____" : breakpointAddressHex;
        std::string bpAddrLine = std::string("BP ADDR ") + bpAddr;
        if (breakpointAddressEditing) {
            bpAddrLine.push_back('_');
        }
        drawHexText(
            renderer,
            panelX + 12,
            kBreakpointRowYAddr,
            clippedText(bpAddrLine, panelWidth - 24, 1),
            breakpointAddressEditing ? SDL_Color{255, 230, 120, 255} : active,
            1
        );

        const int visibleBp = std::min<int>(kBreakpointListMaxVisible, static_cast<int>(breakpoints.size()));
        for (int i = 0; i < visibleBp; ++i) {
            char bpLine[24];
            std::snprintf(bpLine, sizeof(bpLine), "BP%02d %04X", i + 1, breakpoints[static_cast<std::size_t>(i)]);
            drawHexTextFit(renderer, panelX + 12, kBreakpointListStartY + i * kBreakpointListLineHeight, panelWidth - 24, bpLine, active, 1);
        }
        if (static_cast<int>(breakpoints.size()) > kBreakpointListMaxVisible) {
            char extraLine[24];
            std::snprintf(extraLine, sizeof(extraLine), "+%d MORE", static_cast<int>(breakpoints.size()) - kBreakpointListMaxVisible);
            drawHexTextFit(renderer, panelX + 12, kBreakpointListStartY + visibleBp * kBreakpointListLineHeight, panelWidth - 24, extraLine, dim, 1);
        }
    }

    SDL_SetRenderDrawColor(renderer, 54, 60, 80, 255);
    SDL_RenderDrawLine(renderer, panelX + 8, readStartY - 10, panelX + panelWidth - 8, readStartY - 10);

    int y = readStartY;
    const int lineHeight = kReadLineHeight;
    const int readLines = readVisibleLinesForPanel(panelHeight, showBreakpointMenu);
    if (showRunLabRecognitionList) {
        const auto rows = buildRunLabRecognitionRows(runlabState, sprites, bus);
        const int count = static_cast<int>(std::min<std::size_t>(rows.size(), static_cast<std::size_t>(readLines)));
        if (count == 0) {
            drawHexTextFit(renderer, panelX + 20, y, panelWidth - 28, "RECOG NONE", dim, 1);
        }
        for (int i = 0; i < count; ++i) {
            const auto& row = rows[static_cast<std::size_t>(i)];
            if (row.primaryAddress.has_value() && row.primaryAddress.value() == watch.address) {
                SDL_SetRenderDrawColor(renderer, 42, 58, 70, 255);
                SDL_Rect hl{panelX + 8, y + i * lineHeight - 1, panelWidth - 16, lineHeight};
                SDL_RenderFillRect(renderer, &hl);
            }
            SDL_Color roleColor{120, 190, 255, 255};
            const std::string role = uiUpper(row.role);
            if (role.find("PLAYER") != std::string::npos) {
                roleColor = SDL_Color{80, 255, 130, 255};
            } else if (role.find("ENEMY") != std::string::npos || role.find("BOSS") != std::string::npos) {
                roleColor = SDL_Color{255, 80, 72, 255};
            } else if (role.find("ITEM") != std::string::npos || role.find("OBJECT") != std::string::npos) {
                roleColor = SDL_Color{255, 220, 110, 255};
            }
            SDL_SetRenderDrawColor(renderer, roleColor.r, roleColor.g, roleColor.b, roleColor.a);
            SDL_Rect marker{panelX + 10, y + i * lineHeight + 2, 4, 8};
            SDL_RenderFillRect(renderer, &marker);

            std::string line = row.role + " " + row.label + " " + row.memorySummary;
            drawHexTextFit(renderer, panelX + 20, y + i * lineHeight, panelWidth - 28, uiUpper(line), roleColor, 1);
        }
    } else if (showRunLabCandidateList) {
        const auto rows = runLabCandidateRows(runlabState);
        const int count = static_cast<int>(std::min<std::size_t>(rows.size(), static_cast<std::size_t>(readLines)));
        if (count == 0) {
            drawHexTextFit(renderer, panelX + 20, y, panelWidth - 28, "NONE - PRESS C", dim, 1);
        }
        for (int i = 0; i < count; ++i) {
            const auto& row = rows[static_cast<std::size_t>(i)];
            if (row.candidate.address == watch.address) {
                SDL_SetRenderDrawColor(renderer, 70, 42, 42, 255);
                SDL_Rect hl{panelX + 8, y + i * lineHeight - 1, panelWidth - 16, lineHeight};
                SDL_RenderFillRect(renderer, &hl);
            }
            SDL_SetRenderDrawColor(renderer, 255, 48, 48, 255);
            SDL_Rect marker{panelX + 10, y + i * lineHeight + 2, 4, 8};
            SDL_RenderFillRect(renderer, &marker);

            char line[80];
            std::snprintf(
                line,
                sizeof(line),
                "%s %04X %s S%02d V%d",
                row.group,
                row.candidate.address,
                uiUpper(runlab::memoryValueTypeName(row.candidate.type)).c_str(),
                static_cast<int>(std::clamp(row.candidate.score, 0.0, 1.0) * 99.0),
                row.candidate.currentValue
            );
            drawHexTextFit(renderer, panelX + 20, y + i * lineHeight, panelWidth - 28, line, SDL_Color{255, 220, 220, 255}, 1);
        }
    } else {
        const int count = static_cast<int>(std::min<std::size_t>(reads.size(), static_cast<std::size_t>(readLines)));

        for (int i = 0; i < count; ++i) {
            const auto& evt = reads[static_cast<std::size_t>(i)];
            if (evt.address == watch.address) {
                SDL_SetRenderDrawColor(renderer, 52, 64, 46, 255);
                SDL_Rect hl{panelX + 8, y + i * lineHeight - 1, panelWidth - 16, lineHeight};
                SDL_RenderFillRect(renderer, &hl);
            }
            const auto color = memoryRegionColor(evt.address);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_Rect marker{panelX + 10, y + i * lineHeight + 2, 4, 8};
            SDL_RenderFillRect(renderer, &marker);

            char line[16];
            std::snprintf(line, sizeof(line), "%04X:%02X", evt.address, evt.value);
            drawHexText(renderer, panelX + 20, y + i * lineHeight, line, active, 1);
        }
    }

    const auto selectedSprite = findSelectedSprite(sprites, selectedSpriteAddr);
    const int detailY = selectedSectionY(panelHeight, readStartY);
    const int detailHeight = selectedSectionHeightForPanel(panelHeight);
    SDL_SetRenderDrawColor(renderer, 54, 60, 80, 255);
    SDL_RenderDrawLine(renderer, panelX + 8, detailY, panelX + panelWidth - 8, detailY);
    drawHexTextFit(renderer, panelX + 12, detailY + 4, panelWidth - 24, "SPR SEL", active, 1);
    if (selectedSprite.has_value()) {
        const auto sp = selectedSprite.value();
        char line1[40];
        std::snprintf(line1, sizeof(line1), "ADR:%04X Y:%02X X:%02X", sp.addr, sp.y, sp.x);
        drawHexTextFit(renderer, panelX + 12, detailY + 18, panelWidth - 24, line1, active, 1);

        const int sy = static_cast<int>(sp.y) - 16;
        const int sx = static_cast<int>(sp.x) - 8;
        const bool priLow = (sp.attr & 0x80) != 0;
        const bool yFlip = (sp.attr & 0x40) != 0;
        const bool xFlip = (sp.attr & 0x20) != 0;
        const bool pal1 = (sp.attr & 0x10) != 0;

        char line2[48];
        std::snprintf(line2, sizeof(line2), "SY:%02X SX:%02X T:%02X A:%02X", sy & 0xFF, sx & 0xFF, sp.tile, sp.attr);
        drawHexTextFit(renderer, panelX + 12, detailY + 30, panelWidth - 24, line2, active, 1);

        if (detailHeight >= 64) {
            char line3[48];
            std::snprintf(line3, sizeof(line3), "P:%d YF:%d XF:%d PL:%d", priLow ? 1 : 0, yFlip ? 1 : 0, xFlip ? 1 : 0, pal1 ? 1 : 0);
            drawHexTextFit(renderer, panelX + 12, detailY + 42, panelWidth - 24, line3, active, 1);
        }

        if (detailHeight >= 80) {
            drawHexText(renderer, panelX + 12, detailY + 54, "SPRITE", active, 1);
            drawSpritePreview(renderer, bus, sp, panelX + 12, detailY + 66, 2);
        } else if (detailHeight >= 64) {
            drawHexText(renderer, panelX + 12, detailY + 52, "SPRITE", active, 1);
            drawSpritePreview(renderer, bus, sp, panelX + 12, detailY + 62, 1);
        }
    } else {
        drawHexText(renderer, panelX + 12, detailY + 20, "NONE", dim, 1);
    }

    const int spriteHeaderY = spriteHeaderYFromLayout(panelHeight, readStartY);
    drawHexText(renderer, panelX + 12, spriteHeaderY, "SPR OAM", active, 1);
    SDL_SetRenderDrawColor(renderer, 54, 60, 80, 255);
    SDL_RenderDrawLine(renderer, panelX + 8, spriteHeaderY + 12, panelX + panelWidth - 8, spriteHeaderY + 12);

    const int spriteY = spriteListYFromLayout(panelHeight, showBreakpointMenu);
    const int spriteLineHeight = kSpriteLineHeight;
    const int spriteMaxLines = spriteVisibleLinesForPanel(panelHeight, showBreakpointMenu);
    const int totalSprites = static_cast<int>(sprites.size());
    const int maxScrollRows = std::max(0, totalSprites - spriteMaxLines);
    const int scroll = std::clamp(spriteScrollRows, 0, maxScrollRows);
    const int spriteCount = std::max(0, std::min(spriteMaxLines, totalSprites - scroll));
    for (int i = 0; i < spriteCount; ++i) {
        const int rowIdx = scroll + i;
        const auto& sp = sprites[static_cast<std::size_t>(rowIdx)];
        const bool selected = selectedSpriteAddr.has_value() && selectedSpriteAddr.value() == sp.addr;
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 70, 78, 110, 255);
            SDL_Rect hl{panelX + 8, spriteY + i * spriteLineHeight - 1, panelWidth - 16, spriteLineHeight};
            SDL_RenderFillRect(renderer, &hl);
        }
        char line[80];
        const std::string role = spriteRoleText(sp, bus);
        std::snprintf(
            line,
            sizeof(line),
            "%02d %04X Y:%02X X:%02X T:%02X %s",
            rowIdx,
            sp.addr,
            sp.y,
            sp.x,
            sp.tile,
            role.c_str()
        );
        drawHexTextFit(renderer, panelX + 12, spriteY + i * spriteLineHeight, panelWidth - 24, line, selected ? SDL_Color{255, 230, 120, 255} : active, 1);
    }

    if (totalSprites > spriteMaxLines) {
        const int trackX = panelX + panelWidth - 8;
        const int trackY = spriteY;
        const int trackH = spriteMaxLines * spriteLineHeight;
        SDL_SetRenderDrawColor(renderer, 38, 44, 62, 255);
        SDL_Rect track{trackX, trackY, 3, trackH};
        SDL_RenderFillRect(renderer, &track);

        const int thumbH = std::max(8, (trackH * spriteMaxLines) / std::max(1, totalSprites));
        const int travel = std::max(1, trackH - thumbH);
        const int thumbY = trackY + (travel * scroll) / maxScrollRows;
        SDL_SetRenderDrawColor(renderer, 120, 136, 180, 255);
        SDL_Rect thumb{trackX, thumbY, 3, thumbH};
        SDL_RenderFillRect(renderer, &thumb);
    }

    const int runlabY = std::max(spriteY + spriteCount * spriteLineHeight + 8, panelHeight - 152);
    SDL_SetRenderDrawColor(renderer, 24, 30, 44, 255);
    SDL_Rect runlabBg{panelX + 8, runlabY, panelWidth - 16, 136};
    SDL_RenderFillRect(renderer, &runlabBg);
    SDL_SetRenderDrawColor(renderer, 70, 86, 118, 255);
    SDL_RenderDrawRect(renderer, &runlabBg);
    drawHexTextFit(renderer, panelX + 12, runlabY + 4, panelWidth - 24, "RUNLAB", SDL_Color{176, 208, 246, 255}, 1);

    char mcpLine[96];
    if (!mcpStatus.enabled) {
        std::snprintf(mcpLine, sizeof(mcpLine), "MCP OFF");
        drawHexTextFit(renderer, panelX + 12, runlabY + 16, panelWidth - 24, mcpLine, dim, 1);
    } else {
        const bool activeMcp = !mcpStatus.currentCommand.empty()
            || mcpStatus.pendingCount > 0
            || mcpStatus.stepFramesPending > 0;
        const char* status = activeMcp ? "ACTIVE" : (mcpStatus.clientRecentlySeen ? "CLIENT" : "WAIT");
        const std::string cmd = mcpStatus.currentCommand.empty() ? "-" : uiUpper(mcpStatus.currentCommand);
        std::snprintf(
            mcpLine,
            sizeof(mcpLine),
            "MCP %s Q%zu F%d S%d %s",
            status,
            mcpStatus.pendingCount,
            mcpStatus.remainingFrames,
            mcpStatus.stepFramesPending,
            cmd.c_str()
        );
        drawHexTextFit(
            renderer,
            panelX + 12,
            runlabY + 16,
            panelWidth - 24,
            mcpLine,
            activeMcp ? SDL_Color{120, 255, 196, 255} : SDL_Color{150, 190, 220, 255},
            1
        );
    }

    if (runlabState.selectedEntity.has_value() && runlabState.selectedEntity.value() < runlabState.entities.size()) {
        const auto& entity = runlabState.entities[runlabState.selectedEntity.value()];
        const int selectedIndex = selectedSpriteAddr.has_value() ? runlab::oamIndexFromAddress(selectedSpriteAddr.value()) : -1;
        char line[96];
        std::snprintf(
            line,
            sizeof(line),
            "SEL SPR:%02d ENT:%s",
            std::max(0, selectedIndex),
            uiUpper(entity.label).c_str()
        );
        drawHexTextFit(renderer, panelX + 12, runlabY + 28, panelWidth - 24, line, active, 1);
        std::snprintf(
            line,
            sizeof(line),
            "TYPE:%s WATCH:%zu",
            uiUpper(runlab::entityTypeName(entity.type)).c_str(),
            runlabState.memoryLabels.size()
        );
        drawHexTextFit(renderer, panelX + 12, runlabY + 40, panelWidth - 24, line, active, 1);
    } else {
        const int selectedIndex = selectedSpriteAddr.has_value() ? runlab::oamIndexFromAddress(selectedSpriteAddr.value()) : -1;
        char line[64];
        if (selectedIndex >= 0) {
            std::snprintf(line, sizeof(line), "SEL SPR:%02d ENT:NONE", selectedIndex);
        } else {
            std::snprintf(line, sizeof(line), "SEL SPR:NONE ENT:NONE");
        }
        drawHexTextFit(renderer, panelX + 12, runlabY + 28, panelWidth - 24, line, dim, 1);
        char counts[64];
        std::snprintf(counts, sizeof(counts), "ENTS:%zu WATCH:%zu", runlabState.entities.size(), runlabState.memoryLabels.size());
        drawHexTextFit(renderer, panelX + 12, runlabY + 40, panelWidth - 24, counts, dim, 1);
    }

    const auto drawCandidateLine = [&](int yPos, const char* prefix, const std::vector<runlab::CorrelationCandidate>& candidates) {
        if (candidates.empty()) {
            char none[48];
            std::snprintf(none, sizeof(none), "%s NONE", prefix);
            drawHexTextFit(renderer, panelX + 12, yPos, panelWidth - 24, none, dim, 1);
            return;
        }
        const auto& c = candidates.front();
        char line[96];
        std::snprintf(
            line,
            sizeof(line),
            "%s %04X %s S%02d V%d",
            prefix,
            c.address,
            uiUpper(runlab::memoryValueTypeName(c.type)).c_str(),
            static_cast<int>(std::clamp(c.score, 0.0, 1.0) * 99.0),
            c.currentValue
        );
        drawHexTextFit(renderer, panelX + 12, yPos, panelWidth - 24, line, SDL_Color{180, 220, 190, 255}, 1);
    };

    drawHexTextFit(renderer, panelX + 12, runlabY + 52, panelWidth - 24, "CORRELATION SUGGESTIONS", SDL_Color{176, 208, 246, 255}, 1);
    drawCandidateLine(runlabY + 64, "X", runlabState.correlationResult.entityX);
    drawCandidateLine(runlabY + 76, "Y", runlabState.correlationResult.entityY);
    drawCandidateLine(runlabY + 88, "CAM", runlabState.correlationResult.cameraX);
    drawCandidateLine(runlabY + 100, "STATE", runlabState.correlationResult.state);

    const runlab::TimelineEvent* latestImportant = nullptr;
    for (auto it = runlabState.events.rbegin(); it != runlabState.events.rend(); ++it) {
        if (runlab::eventIsImportant(*it)) {
            latestImportant = &(*it);
            break;
        }
    }
    if (latestImportant) {
        const auto& evt = *latestImportant;
        char line[96];
        std::snprintf(
            line,
            sizeof(line),
            "F%llu %s %d>%d",
            static_cast<unsigned long long>(evt.frame),
            uiUpper(evt.semantic.empty() ? evt.label : evt.semantic).c_str(),
            evt.previous,
            evt.current
        );
        drawHexTextFit(renderer, panelX + 12, runlabY + 112, panelWidth - 24, line, SDL_Color{255, 230, 120, 255}, 1);
    } else if (!runlabState.events.empty()) {
        const auto& evt = runlabState.events.back();
        char line[96];
        std::snprintf(line, sizeof(line), "EV %s %d>%d", uiUpper(runlab::eventTypeName(evt.type)).c_str(), evt.previous, evt.current);
        drawHexTextFit(renderer, panelX + 12, runlabY + 112, panelWidth - 24, line, dim, 1);
    } else if (!runlabState.lastDiff.empty()) {
        const auto& diff = runlabState.lastDiff.front();
        char line[64];
        std::snprintf(line, sizeof(line), "DIFF %04X %02X>%02X", diff.address, diff.before, diff.after);
        drawHexTextFit(renderer, panelX + 12, runlabY + 112, panelWidth - 24, line, SDL_Color{255, 230, 120, 255}, 1);
    } else {
        drawHexTextFit(renderer, panelX + 12, runlabY + 112, panelWidth - 24, "EVENTS NONE", dim, 1);
    }

    if (runlabState.activeGoal.has_value() && runlabState.activeGoal.value() < runlabState.goals.size()) {
        const auto& goal = runlabState.goals[runlabState.activeGoal.value()];
        const char* status = goal.doneTriggered ? "DONE" : (goal.failTriggered ? "FAIL" : (goal.active ? "ACTIVE" : "IDLE"));
        char line[96];
        std::snprintf(line, sizeof(line), "GOAL %s F%llu", status, static_cast<unsigned long long>(goal.startFrame));
        drawHexTextFit(renderer, panelX + 12, runlabY + 124, panelWidth - 24, line, active, 1);
    } else {
        drawHexTextFit(renderer, panelX + 12, runlabY + 124, panelWidth - 24, "GOAL NONE", dim, 1);
    }
    drawHexTextFit(renderer, panelX + 12, panelHeight - 12, panelWidth - 24, "G GOAL R BASE Q CLR C SCAN 1/2/3/4", dim, 1);

    char footer[40];
    const int firstSprite = totalSprites > 0 ? (scroll + 1) : 0;
    const int lastSprite = totalSprites > 0 ? (scroll + spriteCount) : 0;
    std::snprintf(footer, sizeof(footer), "SPR %d-%d/%d", firstSprite, lastSprite, totalSprites);
    drawHexText(renderer, panelX + 12, std::max(spriteY, runlabY - 10), footer, dim, 1);

    if (search.visible) {
        const int overlayX = panelX + 10;
        const int overlayY = kSearchOverlayTop;
        const int overlayW = panelWidth - 20;
        const int overlayH = std::max(80, panelHeight - overlayY - kSearchOverlayBottomPad);
        const int innerX = overlayX + 8;
        const int modeY = overlayY + 18;
        const int valueY = overlayY + 30;
        const int infoY = overlayY + 42;
        const int buttonsY = overlayY + 54;
        const int listY = overlayY + kSearchListYOffset;
        const int listLines = searchVisibleLinesForPanel(panelHeight);
        const int maxScroll = std::max(0, static_cast<int>(search.matches.size()) - listLines);
        const int scroll = std::clamp(search.scroll, 0, maxScroll);

        SDL_SetRenderDrawColor(renderer, 6, 10, 18, 240);
        SDL_Rect bg{overlayX, overlayY, overlayW, overlayH};
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawColor(renderer, 96, 118, 160, 255);
        SDL_RenderDrawRect(renderer, &bg);

        drawHexText(renderer, innerX, overlayY + 6, "MEM SEARCH", active, 1);

        const int modeW = (overlayW - 16) / 5;
        const std::array<const char*, 5> modeLabels{"EX", "GT", "LT", "CH", "UN"};
        for (int i = 0; i < 5; ++i) {
            const bool selected = static_cast<int>(search.mode) == i;
            if (selected) {
                SDL_SetRenderDrawColor(renderer, 46, 58, 84, 255);
                SDL_Rect hi{innerX + i * modeW, modeY - 2, modeW - 2, 10};
                SDL_RenderFillRect(renderer, &hi);
            }
            drawHexText(renderer, innerX + 2 + i * modeW, modeY, modeLabels[static_cast<std::size_t>(i)], selected ? SDL_Color{255, 230, 120, 255} : active, 1);
        }

        std::string valueField = search.valueHex.empty() ? "__" : search.valueHex;
        if (search.editingValue) {
            valueField.push_back('_');
        }
        drawHexText(renderer, innerX, valueY, std::string("VAL ") + valueField, search.editingValue ? SDL_Color{255, 230, 120, 255} : active, 1);

        char infoLine[48];
        std::snprintf(
            infoLine,
            sizeof(infoLine),
            "SNAP %s MATCH %zu",
            search.hasSnapshot ? "ON" : "OFF",
            search.totalMatches
        );
        drawHexTextFit(renderer, innerX, infoY, overlayW - 16, infoLine, dim, 1);
        drawHexTextFit(renderer, innerX, buttonsY, overlayW - 16, "RUN SNAP CLR", SDL_Color{176, 208, 246, 255}, 1);

        for (int i = 0; i < listLines; ++i) {
            const int idx = scroll + i;
            if (idx < 0 || idx >= static_cast<int>(search.matches.size())) {
                break;
            }
            const gb::u16 addr = search.matches[static_cast<std::size_t>(idx)];
            const gb::u8 val = bus.peek(addr);
            char line[24];
            std::snprintf(line, sizeof(line), "%04X:%02X", addr, val);
            drawHexTextFit(renderer, innerX, listY + i * kSearchListLineHeight, overlayW - 16, line, active, 1);
        }

        drawHexTextFit(renderer, innerX, overlayY + overlayH - 10, overlayW - 16, "S CLOSE ENTER RUN E EDIT", dim, 1);
    }
}

void drawMemoryEditOverlay(
    SDL_Renderer* renderer,
    int panelX,
    int panelWidth,
    const MemoryEditState& edit
) {
    if (!edit.active) {
        return;
    }
    SDL_SetRenderDrawColor(renderer, 8, 12, 20, 230);
    SDL_Rect box{panelX + 10, 78, panelWidth - 20, 68};
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 108, 128, 170, 255);
    SDL_RenderDrawRect(renderer, &box);

    drawHexText(renderer, panelX + 16, 84, "EDIT MEM", SDL_Color{230, 236, 255, 255}, 1);

    const std::string addr = edit.addressHex.empty() ? "____" : edit.addressHex;
    const std::string val = edit.valueHex.empty() ? "__" : edit.valueHex;
    const SDL_Color active = SDL_Color{255, 230, 120, 255};
    const SDL_Color dim = SDL_Color{180, 190, 216, 255};
    drawHexText(renderer, panelX + 16, 98, "ADDR:", edit.editAddress ? active : dim, 1);
    drawHexText(renderer, panelX + 52, 98, addr, edit.editAddress ? active : dim, 1);
    drawHexText(renderer, panelX + 96, 98, "VAL:", !edit.editAddress ? active : dim, 1);
    drawHexText(renderer, panelX + 122, 98, val, !edit.editAddress ? active : dim, 1);
    drawHexText(renderer, panelX + 16, 116, "TAB SWITCH ENTER APPLY ESC", SDL_Color{130, 142, 170, 255}, 1);
}

#endif

} // namespace gb::frontend
