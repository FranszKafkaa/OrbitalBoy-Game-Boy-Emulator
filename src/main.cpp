#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "gb/gameboy.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>
#ifdef GBEMU_USE_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif
#endif

namespace {

void writeFrameAsPPM(const std::string& path, const gb::PPU& ppu) {
    static constexpr int width = gb::PPU::ScreenWidth;
    static constexpr int height = gb::PPU::ScreenHeight;
    static constexpr int palette[4] = {255, 192, 96, 0};

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }

    out << "P6\n" << width << " " << height << "\n255\n";

    const auto& frame = ppu.framebuffer();
    for (auto shade : frame) {
        const auto v = static_cast<unsigned char>(palette[shade & 0x03]);
        out.write(reinterpret_cast<const char*>(&v), 1);
        out.write(reinterpret_cast<const char*>(&v), 1);
        out.write(reinterpret_cast<const char*>(&v), 1);
    }
}

#ifdef GBEMU_USE_SDL2
std::array<gb::u8, 7> hexGlyph(char c);
void drawHexText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color color, int scale);

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

bool hasRomExtension(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    const std::string ext = path.extension().string();
    return ext == ".gb" || ext == ".gbc" || ext == ".GB" || ext == ".GBC";
}

bool hasImageExtension(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    const std::string ext = path.extension().string();
    return ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG";
}

struct RomEntry {
    std::string label;
    std::string romPath;
    std::string imagePath;
};

std::string normalizeLabel(std::string text, std::size_t maxLen) {
    for (char& ch : text) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::toupper(uch));
        const bool ok =
            (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '.' || ch == '_' || ch == '-' || ch == '/' || ch == ' ';
        if (!ok) {
            ch = '-';
        }
    }
    if (text.size() > maxLen) {
        text = text.substr(0, maxLen);
    }
    return text;
}

std::vector<RomEntry> discoverRoms() {
    std::vector<RomEntry> roms;
    const std::array<std::filesystem::path, 2> dirs = {
        std::filesystem::path("./rom"),
        std::filesystem::path("./roms"),
    };

    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_directory()) {
                continue;
            }
            std::optional<std::filesystem::path> foundRom;
            std::optional<std::filesystem::path> foundImage;
            for (const auto& gameFile : std::filesystem::directory_iterator(entry.path())) {
                if (!gameFile.is_regular_file()) {
                    continue;
                }
                const auto path = gameFile.path();
                if (!foundRom && hasRomExtension(path)) {
                    foundRom = path;
                }
                if (!foundImage && hasImageExtension(path)) {
                    foundImage = path;
                }
            }
            if (!foundRom) {
                continue;
            }
            RomEntry r;
            r.label = normalizeLabel(entry.path().filename().string(), 18);
            r.romPath = foundRom->string();
            r.imagePath = foundImage ? foundImage->string() : std::string();
            roms.push_back(std::move(r));
        }
    }

    std::sort(roms.begin(), roms.end(), [](const RomEntry& a, const RomEntry& b) {
        return a.label < b.label;
    });
    roms.erase(std::unique(roms.begin(), roms.end(), [](const RomEntry& a, const RomEntry& b) {
        return a.romPath == b.romPath;
    }), roms.end());
    return roms;
}

std::string chooseRomWithSdlDialog() {
    const auto roms = discoverRoms();
    if (roms.empty()) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_WARNING,
            "Nenhuma ROM encontrada",
            "Use estrutura ./rom/NOME_DO_JOGO/{arquivo.gb, capa.jpg}.",
            nullptr
        );
        return {};
    }

    SDL_Window* window = SDL_CreateWindow(
        "Selecionar ROM",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        920,
        560,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        return {};
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return {};
    }

    auto cardAt = [&](int mx, int my, int w, int h, int scrollRows) -> int {
        const int margin = 20;
        const int top = 78;
        const int bottomPad = 54;
        const int areaW = w - margin * 2;
        const int areaH = h - top - bottomPad;
        const int cardW = 170;
        const int cardH = 120;
        const int gapX = 12;
        const int gapY = 12;
        const int cols = std::max(1, (areaW + gapX) / (cardW + gapX));
        const int gridW = cols * cardW + (cols - 1) * gapX;
        const int gridX = margin + (areaW - gridW) / 2;

        if (mx < margin || mx >= margin + areaW || my < top || my >= top + areaH) {
            return -1;
        }

        const int rowVisible = (my - top) / (cardH + gapY);
        const int yInCell = (my - top) % (cardH + gapY);
        if (yInCell >= cardH) {
            return -1;
        }
        const int rowGlobal = scrollRows + rowVisible;
        const int col = (mx - gridX) / (cardW + gapX);
        const int xInCell = (mx - gridX) % (cardW + gapX);
        if (col < 0 || col >= cols || xInCell >= cardW) {
            return -1;
        }
        const int idx = rowGlobal * cols + col;
        if (idx < 0 || idx >= static_cast<int>(roms.size())) {
            return -1;
        }
        return idx;
    };

    int selected = 0;
    int scrollRows = 0;
    int hover = -1;
    int lastClickIdx = -1;
    Uint32 lastClickTicks = 0;
    bool running = true;
    std::string chosen;
    std::vector<SDL_Texture*> previewTextures(roms.size(), nullptr);

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (ev.type == SDL_KEYDOWN) {
                const int maxIdx = static_cast<int>(roms.size()) - 1;
                int w = 0;
                int h = 0;
                SDL_GetRendererOutputSize(renderer, &w, &h);
                const int margin = 20;
                const int areaW = w - margin * 2;
                const int cardW = 170;
                const int gapX = 12;
                const int cols = std::max(1, (areaW + gapX) / (cardW + gapX));
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_UP:
                    selected = std::max(0, selected - cols);
                    break;
                case SDLK_DOWN:
                    selected = std::min(maxIdx, selected + cols);
                    break;
                case SDLK_LEFT:
                    selected = std::max(0, selected - 1);
                    break;
                case SDLK_RIGHT:
                    selected = std::min(maxIdx, selected + 1);
                    break;
                case SDLK_PAGEUP:
                    selected = std::max(0, selected - cols * 3);
                    break;
                case SDLK_PAGEDOWN:
                    selected = std::min(maxIdx, selected + cols * 3);
                    break;
                case SDLK_HOME:
                    selected = 0;
                    break;
                case SDLK_END:
                    selected = maxIdx;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    chosen = roms[static_cast<std::size_t>(selected)].romPath;
                    running = false;
                    break;
                default:
                    break;
                }
            }
            if (ev.type == SDL_MOUSEWHEEL) {
                int w = 0;
                int h = 0;
                SDL_GetRendererOutputSize(renderer, &w, &h);
                const int margin = 16;
                const int areaW = w - margin * 2;
                const int cardW = 170;
                const int gapX = 12;
                const int cols = std::max(1, (areaW + gapX) / (cardW + gapX));
                selected = std::clamp(selected - ev.wheel.y * cols, 0, static_cast<int>(roms.size()) - 1);
            }
            if (ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEBUTTONDOWN) {
                int w = 0;
                int h = 0;
                SDL_GetRendererOutputSize(renderer, &w, &h);
                const int mx = (ev.type == SDL_MOUSEMOTION) ? ev.motion.x : ev.button.x;
                const int my = (ev.type == SDL_MOUSEMOTION) ? ev.motion.y : ev.button.y;
                hover = cardAt(mx, my, w, h, scrollRows);

                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && hover >= 0) {
                    selected = hover;
                    const Uint32 now = SDL_GetTicks();
                    if (ev.button.clicks >= 2 || (hover == lastClickIdx && (now - lastClickTicks) < 300)) {
                        chosen = roms[static_cast<std::size_t>(selected)].romPath;
                        running = false;
                    }
                    lastClickIdx = hover;
                    lastClickTicks = now;
                }
            }
        }

        int w = 0;
        int h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        const int margin = 20;
        const int top = 78;
        const int bottomPad = 54;
        const int areaW = w - margin * 2;
        const int areaH = h - top - bottomPad;
        const int cardW = 170;
        const int cardH = 120;
        const int gapX = 12;
        const int gapY = 12;
        const int cols = std::max(1, (areaW + gapX) / (cardW + gapX));
        const int gridW = cols * cardW + (cols - 1) * gapX;
        const int gridX = margin + (areaW - gridW) / 2;
        const int visibleRows = std::max(1, (areaH + gapY) / (cardH + gapY));
        const int totalRows = std::max(1, (static_cast<int>(roms.size()) + cols - 1) / cols);
        const int startY = top;

        selected = std::clamp(selected, 0, static_cast<int>(roms.size()) - 1);
        const int selectedRow = selected / cols;
        if (selectedRow < scrollRows) {
            scrollRows = selectedRow;
        }
        if (selectedRow >= scrollRows + visibleRows) {
            scrollRows = selectedRow - visibleRows + 1;
        }
        scrollRows = std::clamp(scrollRows, 0, std::max(0, totalRows - visibleRows));

        SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
        SDL_RenderClear(renderer);

        drawHexText(renderer, 18, 16, "SELECT ROM", SDL_Color{240, 244, 255, 255}, 2);
        drawHexText(renderer, 18, 40, "DOUBLE CLICK OR ENTER", SDL_Color{150, 160, 185, 255}, 1);

        SDL_SetRenderDrawColor(renderer, 30, 35, 48, 255);
        SDL_Rect listBg{margin, top, areaW, areaH};
        SDL_RenderFillRect(renderer, &listBg);
        SDL_SetRenderDrawColor(renderer, 64, 72, 92, 255);
        SDL_RenderDrawRect(renderer, &listBg);

        for (int row = 0; row < visibleRows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const int idx = (row + scrollRows) * cols + col;
                if (idx >= static_cast<int>(roms.size())) {
                    continue;
                }
                const int x = gridX + col * (cardW + gapX);
                const int y = startY + row * (cardH + gapY);
                const bool isSel = idx == selected;
                const bool isHover = idx == hover;

                SDL_SetRenderDrawColor(renderer, 38, 44, 62, 255);
                SDL_Rect card{x, y, cardW, cardH};
                SDL_RenderFillRect(renderer, &card);
                SDL_SetRenderDrawColor(renderer, isSel ? 118 : (isHover ? 94 : 70), isSel ? 142 : (isHover ? 112 : 84), isSel ? 190 : (isHover ? 150 : 110), 255);
                SDL_RenderDrawRect(renderer, &card);

                SDL_SetRenderDrawColor(renderer, 24, 28, 40, 255);
                SDL_Rect ph{x + 10, y + 10, cardW - 20, 64};
                SDL_RenderFillRect(renderer, &ph);
                SDL_SetRenderDrawColor(renderer, 86, 96, 126, 255);
                SDL_RenderDrawRect(renderer, &ph);
#ifdef GBEMU_USE_SDL2_IMAGE
                if (previewTextures[static_cast<std::size_t>(idx)] == nullptr && !roms[static_cast<std::size_t>(idx)].imagePath.empty()) {
                    SDL_Surface* surface = IMG_Load(roms[static_cast<std::size_t>(idx)].imagePath.c_str());
                    if (surface) {
                        previewTextures[static_cast<std::size_t>(idx)] = SDL_CreateTextureFromSurface(renderer, surface);
                        SDL_FreeSurface(surface);
                    }
                }
#endif
                SDL_Texture* preview = previewTextures[static_cast<std::size_t>(idx)];
                if (preview) {
                    int tw = 0;
                    int th = 0;
                    SDL_QueryTexture(preview, nullptr, nullptr, &tw, &th);
                    if (tw > 0 && th > 0) {
                        const float scale = std::min(
                            static_cast<float>(ph.w) / static_cast<float>(tw),
                            static_cast<float>(ph.h) / static_cast<float>(th)
                        );
                        const int rw = std::max(1, static_cast<int>(tw * scale));
                        const int rh = std::max(1, static_cast<int>(th * scale));
                        SDL_Rect dst{ph.x + (ph.w - rw) / 2, ph.y + (ph.h - rh) / 2, rw, rh};
                        SDL_RenderCopy(renderer, preview, nullptr, &dst);
                    }
                } else {
                    drawHexText(renderer, x + 16, y + 34, "NO IMG", SDL_Color{140, 150, 176, 255}, 1);
                }

                char prefix[16];
                std::snprintf(prefix, sizeof(prefix), "%03d", idx + 1);
                drawHexText(renderer, x + 10, y + 82, prefix, SDL_Color{220, 226, 236, 255}, 1);
                drawHexText(renderer, x + 50, y + 82, roms[static_cast<std::size_t>(idx)].label, SDL_Color{220, 226, 236, 255}, 1);
            }
        }

        drawHexText(renderer, 18, h - 30, "ENTER OPEN  ESC CANCEL", SDL_Color{170, 176, 196, 255}, 1);
        SDL_RenderPresent(renderer);
    }

    for (SDL_Texture* texture : previewTextures) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return chosen;
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

struct SpriteDebugRow {
    gb::u16 addr = 0;
    gb::u8 y = 0;
    gb::u8 x = 0;
    gb::u8 tile = 0;
    gb::u8 attr = 0;
};

constexpr int kReadStartY = 84;
constexpr int kReadLineHeight = 14;
constexpr int kReadLines = 8;
constexpr int kSelectedSectionTopGap = 6;
constexpr int kSelectedSectionHeight = 96;
constexpr int kSectionGap = 6;
constexpr int kSpriteHeaderOffset = 6;
constexpr int kSpriteSectionTopPad = 18;
constexpr int kSpriteLineHeight = 12;

int selectedSectionY() {
    return kReadStartY + kReadLines * kReadLineHeight + kSelectedSectionTopGap;
}

int spriteHeaderYFromLayout() {
    return selectedSectionY() + kSelectedSectionHeight + kSectionGap;
}

int spriteListYFromLayout() {
    return spriteHeaderYFromLayout() + kSpriteSectionTopPad;
}

std::vector<SpriteDebugRow> snapshotSprites(const gb::Bus& bus, std::size_t maxItems) {
    std::vector<SpriteDebugRow> out;
    out.reserve(maxItems);
    for (int i = 0; i < 40 && out.size() < maxItems; ++i) {
        const gb::u16 base = static_cast<gb::u16>(0xFE00 + i * 4);
        const gb::u8 y = bus.peek(base);
        const gb::u8 x = bus.peek(static_cast<gb::u16>(base + 1));
        const gb::u8 tile = bus.peek(static_cast<gb::u16>(base + 2));
        const gb::u8 attr = bus.peek(static_cast<gb::u16>(base + 3));

        // Keep sprites that look active on OAM, but always include slot 0.
        if (i != 0 && y == 0 && x == 0 && tile == 0 && attr == 0) {
            continue;
        }
        out.push_back(SpriteDebugRow{base, y, x, tile, attr});
    }
    return out;
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

void drawMemoryPanel(
    SDL_Renderer* renderer,
    int panelX,
    int panelWidth,
    int panelHeight,
    const std::vector<gb::Bus::MemoryReadEvent>& reads,
    const std::vector<SpriteDebugRow>& sprites,
    std::optional<gb::u16> selectedSpriteAddr,
    const gb::Bus& bus,
    gb::u16 execPc,
    gb::u8 execOp,
    gb::u16 nextPc,
    gb::u8 nextOp,
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

    drawHexText(renderer, panelX + 12, 12, paused ? "PAUSED" : "RUNNING", active, 2);
    drawHexText(renderer, panelX + 12, 34, muted ? "MUTED" : "AUDIO-ON", paused ? dim : active, 2);
    char execLine[24];
    std::snprintf(execLine, sizeof(execLine), "PC:%04X OP:%02X", execPc, execOp);
    drawHexText(renderer, panelX + 12, 50, execLine, active, 1);
    char nextLine[24];
    std::snprintf(nextLine, sizeof(nextLine), "NP:%04X OP:%02X", nextPc, nextOp);
    drawHexText(renderer, panelX + 12, 62, nextLine, dim, 1);

    SDL_SetRenderDrawColor(renderer, 54, 60, 80, 255);
    SDL_RenderDrawLine(renderer, panelX + 8, 76, panelX + panelWidth - 8, 76);

    int y = kReadStartY;
    const int lineHeight = kReadLineHeight;
    const int readLines = kReadLines;
    const int count = static_cast<int>(std::min<std::size_t>(reads.size(), static_cast<std::size_t>(readLines)));

    for (int i = 0; i < count; ++i) {
        const auto& evt = reads[static_cast<std::size_t>(i)];
        const auto color = memoryRegionColor(evt.address);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_Rect marker{panelX + 10, y + i * lineHeight + 2, 4, 8};
        SDL_RenderFillRect(renderer, &marker);

        char line[16];
        std::snprintf(line, sizeof(line), "%04X:%02X", evt.address, evt.value);
        drawHexText(renderer, panelX + 20, y + i * lineHeight, line, active, 1);
    }

    const auto selectedSprite = findSelectedSprite(sprites, selectedSpriteAddr);
    const int detailY = selectedSectionY();
    SDL_SetRenderDrawColor(renderer, 54, 60, 80, 255);
    SDL_RenderDrawLine(renderer, panelX + 8, detailY, panelX + panelWidth - 8, detailY);
    drawHexText(renderer, panelX + 12, detailY + 4, "SPR SEL", active, 1);
    if (selectedSprite.has_value()) {
        const auto sp = selectedSprite.value();
        char line1[40];
        std::snprintf(line1, sizeof(line1), "ADR:%04X Y:%02X X:%02X", sp.addr, sp.y, sp.x);
        drawHexText(renderer, panelX + 12, detailY + 18, line1, active, 1);

        const int sy = static_cast<int>(sp.y) - 16;
        const int sx = static_cast<int>(sp.x) - 8;
        const bool priLow = (sp.attr & 0x80) != 0;
        const bool yFlip = (sp.attr & 0x40) != 0;
        const bool xFlip = (sp.attr & 0x20) != 0;
        const bool pal1 = (sp.attr & 0x10) != 0;

        char line2[48];
        std::snprintf(line2, sizeof(line2), "SY:%02X SX:%02X T:%02X A:%02X", sy & 0xFF, sx & 0xFF, sp.tile, sp.attr);
        drawHexText(renderer, panelX + 12, detailY + 30, line2, active, 1);

        char line3[48];
        std::snprintf(line3, sizeof(line3), "P:%d YF:%d XF:%d PL:%d", priLow ? 1 : 0, yFlip ? 1 : 0, xFlip ? 1 : 0, pal1 ? 1 : 0);
        drawHexText(renderer, panelX + 12, detailY + 42, line3, active, 1);

        drawHexText(renderer, panelX + 12, detailY + 54, "SPRITE", active, 1);
        drawSpritePreview(renderer, bus, sp, panelX + 12, detailY + 66, 2);
    } else {
        drawHexText(renderer, panelX + 12, detailY + 20, "NONE", dim, 1);
    }

    const int spriteHeaderY = spriteHeaderYFromLayout();
    drawHexText(renderer, panelX + 12, spriteHeaderY, "SPR OAM", active, 1);
    SDL_SetRenderDrawColor(renderer, 54, 60, 80, 255);
    SDL_RenderDrawLine(renderer, panelX + 8, spriteHeaderY + 12, panelX + panelWidth - 8, spriteHeaderY + 12);

    const int spriteY = spriteListYFromLayout();
    const int spriteLineHeight = kSpriteLineHeight;
    const int spriteMaxLines = std::max(1, (panelHeight - spriteY - 8) / spriteLineHeight);
    const int spriteCount = static_cast<int>(std::min<std::size_t>(sprites.size(), static_cast<std::size_t>(spriteMaxLines)));
    for (int i = 0; i < spriteCount; ++i) {
        const auto& sp = sprites[static_cast<std::size_t>(i)];
        const bool selected = selectedSpriteAddr.has_value() && selectedSpriteAddr.value() == sp.addr;
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 70, 78, 110, 255);
            SDL_Rect hl{panelX + 8, spriteY + i * spriteLineHeight - 1, panelWidth - 16, spriteLineHeight};
            SDL_RenderFillRect(renderer, &hl);
        }
        char line[40];
        std::snprintf(
            line,
            sizeof(line),
            "%04X Y:%02X X:%02X T:%02X A:%02X",
            sp.addr,
            sp.y,
            sp.x,
            sp.tile,
            sp.attr
        );
        drawHexText(renderer, panelX + 12, spriteY + i * spriteLineHeight, line, selected ? SDL_Color{255, 230, 120, 255} : active, 1);
    }
}

void updateWindowTitle(SDL_Window* window, const std::string& title, bool paused, bool muted) {
    std::string full = "GB Emulator - " + title + " - ";
    full += paused ? "PAUSED" : "RUNNING";
    full += muted ? " - MUTED" : " - AUDIO ON";
    SDL_SetWindowTitle(window, full.c_str());
}
#endif

int runHeadless(gb::GameBoy& gb, int frames) {
    for (int i = 0; i < frames; ++i) {
        gb.runFrame();
    }

    writeFrameAsPPM("frame.ppm", gb.ppu());

    const auto& regs = gb.cpu().regs();
    std::cout << "Emulacao finalizada (" << frames << " frames)\n";
    std::cout << "PC=" << std::hex << regs.pc << " SP=" << regs.sp << " A=" << static_cast<int>(regs.a) << "\n";
    std::cout << "Framebuffer salvo em frame.ppm\n";
    return 0;
}

#ifdef GBEMU_USE_SDL2
std::string statePathForRom(const std::string& romPath) {
    const std::filesystem::path rom(romPath);
    std::error_code ec;
    std::filesystem::create_directories("states", ec);

    std::string stem = rom.stem().string();
    if (stem.empty()) {
        stem = "default";
    }
    std::filesystem::path p = std::filesystem::path("states") / (stem + ".state");
    return p.string();
}

std::string legacyStatePathForRom(const std::string& romPath) {
    std::filesystem::path p(romPath);
    p.replace_extension(".state");
    return p.string();
}

int runRealtime(
    gb::GameBoy& gb,
    int scale,
    int audioBuffer,
    const std::string& statePath,
    const std::string& legacyStatePath
) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
        std::cerr << "erro SDL_Init: " << SDL_GetError() << "\n";
        return 1;
    }

    const int width = gb::PPU::ScreenWidth;
    const int height = gb::PPU::ScreenHeight;
    const int panelWidth = 260;
    const int gameWidth = width * scale;
    const int gameHeight = height * scale;
    bool showPanel = false;
    bool fullscreen = false;

    SDL_Window* window = SDL_CreateWindow(
        "GB Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        gameWidth + panelWidth,
        gameHeight,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        std::cerr << "erro SDL_CreateWindow: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "erro SDL_CreateRenderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        std::cerr << "erro SDL_CreateTexture: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_AudioSpec want{};
    want.freq = gb::APU::SampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = static_cast<Uint16>(audioBuffer);
    want.callback = nullptr;

    SDL_AudioSpec have{};
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    const bool audioEnabled = audioDev != 0;
    if (!audioEnabled) {
        std::cerr << "aviso: audio SDL indisponivel: " << SDL_GetError() << "\n";
    } else {
        SDL_PauseAudioDevice(audioDev, 0);
    }

    bool running = true;
    bool paused = false;
    bool muted = false;
    bool backToMenu = false;
    std::optional<gb::u16> selectedSpriteAddr;
    std::string uiMessage;
    int uiMessageFrames = 0;
    std::array<unsigned char, gb::PPU::ScreenWidth * gb::PPU::ScreenHeight * 3> pixels{};
    updateWindowTitle(window, gb.cartridge().title(), paused, muted);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
#endif

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (ev.key.keysym.sym == SDLK_SPACE) {
                    paused = !paused;
                    if (audioEnabled) {
                        SDL_ClearQueuedAudio(audioDev);
                    }
                    updateWindowTitle(window, gb.cartridge().title(), paused, muted);
                } else if (ev.key.keysym.sym == SDLK_p) {
                    muted = !muted;
                    if (audioEnabled && muted) {
                        SDL_ClearQueuedAudio(audioDev);
                    }
                    updateWindowTitle(window, gb.cartridge().title(), paused, muted);
                } else if (ev.key.keysym.sym == SDLK_i) {
                    showPanel = !showPanel;
                    if (!showPanel) {
                        selectedSpriteAddr.reset();
                    }
                } else if (ev.key.keysym.sym == SDLK_f) {
                    fullscreen = !fullscreen;
                    const Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
                    if (SDL_SetWindowFullscreen(window, flags) != 0) {
                        std::cerr << "falha ao alternar fullscreen: " << SDL_GetError() << "\n";
                        fullscreen = !fullscreen;
                    } else {
                        uiMessage = fullscreen ? "FULLSCREEN ON" : "FULLSCREEN OFF";
                        uiMessageFrames = 120;
                    }
                } else if (ev.key.keysym.sym == SDLK_l && (ev.key.keysym.mod & KMOD_CTRL) == 0) {
                    uiMessage = "BACK TO MENU";
                    uiMessageFrames = 30;
                    backToMenu = true;
                    running = false;
                } else if (ev.key.keysym.sym == SDLK_F3
                           || (ev.key.keysym.sym == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL))) {
                    if (gb.saveStateToFile(statePath)) {
                        uiMessage = "STATE SAVED";
                        std::cout << "state salvo: " << statePath << "\n";
                    } else {
                        uiMessage = "SAVE FAIL";
                        std::cerr << "falha ao salvar state: " << statePath << "\n";
                    }
                    uiMessageFrames = 180;
                } else if (ev.key.keysym.sym == SDLK_F5
                           || (ev.key.keysym.sym == SDLK_l && (ev.key.keysym.mod & KMOD_CTRL))) {
                    if (gb.loadStateFromFile(statePath)) {
                        if (audioEnabled) {
                            gb.apu().takeSamples();
                            SDL_ClearQueuedAudio(audioDev);
                        }
                        uiMessage = "STATE LOADED";
                        std::cout << "state carregado: " << statePath << "\n";
                    } else if (gb.loadStateFromFile(legacyStatePath)) {
                        if (audioEnabled) {
                            gb.apu().takeSamples();
                            SDL_ClearQueuedAudio(audioDev);
                        }
                        uiMessage = "STATE LOADED";
                    } else {
                        uiMessage = "NO STATE";
                        std::cerr << "state nao encontrado: " << statePath << "\n";
                    }
                    uiMessageFrames = 180;
                } else {
                    setButtonFromKey(gb, ev.key.keysym.sym, true);
                }
            }
            if (ev.type == SDL_KEYUP) {
                setButtonFromKey(gb, ev.key.keysym.sym, false);
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && showPanel) {
                const int mx = ev.button.x;
                const int my = ev.button.y;
                int outputW = 0;
                int outputH = 0;
                SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                const int panelX = outputW - panelWidth;
                if (mx >= panelX) {
                    const auto sprites = snapshotSprites(gb.bus(), 12);
                    const int spriteY = spriteListYFromLayout();
                    const int spriteMaxLines = std::max(1, (outputH - spriteY - 8) / kSpriteLineHeight);
                    const int spriteCount = static_cast<int>(std::min<std::size_t>(sprites.size(), static_cast<std::size_t>(spriteMaxLines)));

                    if (my >= spriteY && my < spriteY + spriteCount * kSpriteLineHeight) {
                        const int idx = (my - spriteY) / kSpriteLineHeight;
                        if (idx >= 0 && idx < spriteCount) {
                            selectedSpriteAddr = sprites[static_cast<std::size_t>(idx)].addr;
                        }
                    }
                }
            }
        }

        if (!paused) {
            gb.runFrame();
        }
        if (audioEnabled && !muted && !paused) {
            auto samples = gb.apu().takeSamples();
            if (!samples.empty()) {
                if (SDL_GetQueuedAudioSize(audioDev) > static_cast<Uint32>(have.freq * have.channels * sizeof(int16_t))) {
                    SDL_ClearQueuedAudio(audioDev);
                }
                SDL_QueueAudio(audioDev, samples.data(), static_cast<Uint32>(samples.size() * sizeof(int16_t)));
            }
        } else if (audioEnabled) {
            gb.apu().takeSamples();
            SDL_ClearQueuedAudio(audioDev);
        }

        const auto& frame = gb.ppu().framebuffer();
        static constexpr unsigned char palette[4] = {255, 192, 96, 0};
        for (std::size_t i = 0; i < frame.size(); ++i) {
            const auto shade = palette[frame[i] & 0x03];
            pixels[i * 3 + 0] = shade;
            pixels[i * 3 + 1] = shade;
            pixels[i * 3 + 2] = shade;
        }

        SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 3);
        SDL_RenderClear(renderer);
        int outputW = 0;
        int outputH = 0;
        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
        const int debugWidth = showPanel ? panelWidth : 0;
        const int contentW = std::max(1, outputW - debugWidth);
        const int contentX = 0;
        const int sx = std::max(1, contentW / width);
        const int sy = std::max(1, outputH / height);
        const int scaleInt = std::max(1, std::min(sx, sy));
        const int drawW = width * scaleInt;
        const int drawH = height * scaleInt;
        const int gameX = contentX + (contentW - drawW) / 2;
        const int gameY = (outputH - drawH) / 2;
        SDL_Rect gameDst{gameX, gameY, drawW, drawH};
        SDL_RenderCopy(renderer, texture, nullptr, &gameDst);
        if (uiMessageFrames > 0) {
            SDL_SetRenderDrawColor(renderer, 20, 20, 20, 180);
            SDL_Rect msgBg{gameX + 8, gameY + 8, 160, 20};
            SDL_RenderFillRect(renderer, &msgBg);
            drawHexText(renderer, gameX + 12, gameY + 12, uiMessage, SDL_Color{255, 230, 120, 255}, 1);
            --uiMessageFrames;
        }
        if (showPanel) {
            const auto reads = gb.bus().snapshotRecentReads(128);
            const auto sprites = snapshotSprites(gb.bus(), 12);
            const auto selectedSprite = findSelectedSprite(sprites, selectedSpriteAddr);
            const int overlayScale = std::max(1, drawW / width);
            drawSelectedSpriteOverlay(renderer, gb.bus(), selectedSprite, overlayScale, gameX, gameY);
            const auto& regs = gb.cpu().regs();
            const gb::u16 execPc = gb.cpu().lastExecutedPc();
            const gb::u8 execOp = gb.cpu().lastExecutedOpcode();
            const gb::u16 nextPc = regs.pc;
            const gb::u8 nextOp = gb.bus().peek(nextPc);
            drawMemoryPanel(renderer, outputW - panelWidth, panelWidth, outputH, reads, sprites, selectedSpriteAddr, gb.bus(), execPc, execOp, nextPc, nextOp, paused, muted);
        }
        SDL_RenderPresent(renderer);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    SDL_DestroyTexture(texture);
    if (audioEnabled) {
        SDL_ClearQueuedAudio(audioDev);
        SDL_CloseAudioDevice(audioDev);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return backToMenu ? 2 : 0;
}
#endif

} // namespace

int main(int argc, char** argv) {
    std::string romPath;
    bool headless = false;
    bool chooseRom = false;
    int frames = 120;
    int scale = 4;
    int audioBuffer = 1024;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rom" && i + 1 < argc) {
            romPath = argv[++i];
        } else if (arg == "--choose-rom") {
            chooseRom = true;
        } else if (arg == "--headless") {
            headless = true;
            if (i + 1 < argc) {
                frames = std::stoi(argv[++i]);
                if (frames < 1) {
                    frames = 1;
                }
            }
        } else if (arg == "--scale" && i + 1 < argc) {
            scale = std::stoi(argv[++i]);
            if (scale < 1) {
                scale = 1;
            }
        } else if (arg == "--audio-buffer" && i + 1 < argc) {
            audioBuffer = std::stoi(argv[++i]);
            if (audioBuffer < 256) {
                audioBuffer = 256;
            }
            if (audioBuffer > 8192) {
                audioBuffer = 8192;
            }
        } else if (!arg.empty() && arg[0] != '-' && romPath.empty()) {
            romPath = arg;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "opcao invalida: " << arg << "\n";
            return 1;
        } else {
            frames = std::stoi(arg);
            if (frames < 1) {
                frames = 1;
            }
            headless = true;
        }
    }

    if (romPath.empty()) {
#ifdef GBEMU_USE_SDL2
        chooseRom = true;
        headless = false;
#endif
    }

#ifdef GBEMU_USE_SDL2
    auto openRomSelector = [&romPath]() -> bool {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "erro SDL_Init para seletor de ROM: " << SDL_GetError() << "\n";
            return false;
        }
#ifdef GBEMU_USE_SDL2_IMAGE
        const int imgFlags = IMG_INIT_JPG | IMG_INIT_PNG;
        IMG_Init(imgFlags);
#endif
        romPath = chooseRomWithSdlDialog();
#ifdef GBEMU_USE_SDL2_IMAGE
        IMG_Quit();
#endif
        SDL_Quit();
        if (romPath.empty()) {
            std::cerr << "nenhuma ROM selecionada.\n";
            return false;
        }
        return true;
    };

    if (chooseRom && !openRomSelector()) {
        return 1;
    }
#endif

    if (romPath.empty()) {
        std::cerr << "nenhuma ROM selecionada.\n";
#ifndef GBEMU_USE_SDL2
        std::cerr << "use: gbemu --rom <rom.gb>\n";
        std::cerr << "ou compile com SDL2 para usar seletor grafico.\n";
#endif
        std::cerr << "audio: --audio-buffer 1024 (256..8192)\n";
        return 1;
    }

#ifdef GBEMU_USE_SDL2
    if (!headless) {
        while (true) {
            gb::GameBoy gb;
            if (!gb.loadRom(romPath)) {
                std::cerr << "falha ao carregar ROM: " << romPath << "\n";
                return 1;
            }
            std::cout << "ROM carregada: " << gb.cartridge().title() << "\n";

            const int rc = runRealtime(
                gb,
                scale,
                audioBuffer,
                statePathForRom(romPath),
                legacyStatePathForRom(romPath)
            );
            if (rc == 2) {
                if (!openRomSelector()) {
                    return 0;
                }
                continue;
            }
            return rc;
        }
    }
#else
    if (!headless) {
        std::cout << "SDL2 nao detectado no build. Executando em modo headless.\n";
    }
#endif

    gb::GameBoy gb;
    if (!gb.loadRom(romPath)) {
        std::cerr << "falha ao carregar ROM: " << romPath << "\n";
        return 1;
    }

    std::cout << "ROM carregada: " << gb.cartridge().title() << "\n";
    return runHeadless(gb, frames);
}
