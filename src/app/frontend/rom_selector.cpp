#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gb/app/frontend/debug_ui.hpp"
#include "gb/app/frontend/rom_selector.hpp"
#include "gb/app/runtime_paths.hpp"

#ifdef GBEMU_USE_SDL2
#include "gb/app/sdl_compat.hpp"
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <wincodec.h>
#endif

namespace gb::frontend {

#ifdef GBEMU_USE_SDL2
bool hasRomExtension(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".gb" || ext == ".gbc" || ext == ".gba";
}

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool hasImageExtension(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    std::string ext = toLowerAscii(path.extension().string());
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
}

struct RomEntry {
    std::string label;
    std::string romPath;
    std::string imagePath;
    std::string system;
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

std::string systemLabelForRom(const std::filesystem::path& path) {
    const std::string ext = toLowerAscii(path.extension().string());
    if (ext == ".gba") {
        return "GBA";
    }
    if (ext == ".gbc") {
        return "GBC";
    }
    return "GB";
}

SDL_Color systemAccentColor(const std::string& system) {
    if (system == "GBA") {
        return SDL_Color{255, 190, 92, 255};
    }
    if (system == "GBC") {
        return SDL_Color{126, 214, 168, 255};
    }
    return SDL_Color{132, 174, 255, 255};
}

void drawPanel(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border) {
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void drawBadge(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color color) {
    const int w = 10 + static_cast<int>(text.size()) * 9;
    SDL_Rect outer{x, y, w, 18};
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(std::max(0, static_cast<int>(color.r) - 120)),
        static_cast<Uint8>(std::max(0, static_cast<int>(color.g) - 120)),
        static_cast<Uint8>(std::max(0, static_cast<int>(color.b) - 120)),
        255
    );
    SDL_RenderFillRect(renderer, &outer);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
    SDL_RenderDrawRect(renderer, &outer);
    drawHexText(renderer, x + 5, y + 5, text, color, 1);
}

#if defined(_WIN32)
template <typename T>
void safeRelease(T*& ptr) {
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

SDL_Surface* loadPreviewSurfaceWithWic(const std::filesystem::path& imagePath) {
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initHr);

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    SDL_Surface* surface = nullptr;

    const auto cleanup = [&]() {
        safeRelease(converter);
        safeRelease(frame);
        safeRelease(decoder);
        safeRelease(factory);
        if (shouldUninitialize) {
            CoUninitialize();
        }
    };

    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) {
        cleanup();
        return nullptr;
    }

    const std::wstring widePath = imagePath.wstring();
    if (FAILED(factory->CreateDecoderFromFilename(
            widePath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &decoder))) {
        cleanup();
        return nullptr;
    }

    if (FAILED(decoder->GetFrame(0, &frame))) {
        cleanup();
        return nullptr;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0U || height == 0U) {
        cleanup();
        return nullptr;
    }

    if (FAILED(factory->CreateFormatConverter(&converter))) {
        cleanup();
        return nullptr;
    }

    if (FAILED(converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))) {
        cleanup();
        return nullptr;
    }

    surface = SDL_CreateRGBSurfaceWithFormat(
        0,
        static_cast<int>(width),
        static_cast<int>(height),
        32,
        SDL_PIXELFORMAT_BGRA32
    );
    if (surface == nullptr) {
        cleanup();
        return nullptr;
    }

    const UINT pitch = static_cast<UINT>(surface->pitch);
    const UINT imageSize = pitch * height;
    if (FAILED(converter->CopyPixels(nullptr, pitch, imageSize, static_cast<BYTE*>(surface->pixels)))) {
        SDL_FreeSurface(surface);
        surface = nullptr;
        cleanup();
        return nullptr;
    }

    cleanup();
    return surface;
}
#endif

#if defined(__APPLE__)
SDL_Surface* loadPreviewSurfaceWithImageIo(const std::filesystem::path& imagePath) {
    const std::string pathText = imagePath.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(pathText.c_str()),
        static_cast<CFIndex>(pathText.size()),
        false
    );
    if (url == nullptr) {
        return nullptr;
    }

    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (source == nullptr) {
        return nullptr;
    }

    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (image == nullptr) {
        return nullptr;
    }

    const auto width = static_cast<int>(CGImageGetWidth(image));
    const auto height = static_cast<int>(CGImageGetHeight(image));
    if (width <= 0 || height <= 0) {
        CGImageRelease(image);
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0,
        width,
        height,
        32,
        SDL_PIXELFORMAT_RGBA32
    );
    if (surface == nullptr) {
        CGImageRelease(image);
        return nullptr;
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        surface->pixels,
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        8,
        static_cast<std::size_t>(surface->pitch),
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );
    CGColorSpaceRelease(colorSpace);
    if (context == nullptr) {
        SDL_FreeSurface(surface);
        CGImageRelease(image);
        return nullptr;
    }

    CGContextClearRect(context, CGRectMake(0, 0, width, height));
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);
    CGImageRelease(image);
    return surface;
}
#endif

SDL_Surface* loadPreviewSurface(const std::string& imagePath) {
    if (imagePath.empty()) {
        return nullptr;
    }

#ifdef GBEMU_USE_SDL2_IMAGE
    if (SDL_Surface* surface = IMG_Load(imagePath.c_str())) {
        return surface;
    }
#endif

#if defined(_WIN32)
    return loadPreviewSurfaceWithWic(std::filesystem::path(imagePath));
#elif defined(__APPLE__)
    return loadPreviewSurfaceWithImageIo(std::filesystem::path(imagePath));
#else
    const std::filesystem::path path(imagePath);
    if (toLowerAscii(path.extension().string()) == ".bmp") {
        return SDL_LoadBMP(imagePath.c_str());
    }
    return nullptr;
#endif
}

std::vector<RomEntry> discoverRoms() {
    std::vector<RomEntry> roms;
    std::vector<std::filesystem::path> dirs;
    for (const auto& dirText : gb::romSearchDirectoriesForRuntime()) {
        dirs.emplace_back(dirText);
    }

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
            r.label = normalizeLabel(entry.path().filename().string(), 24);
            r.romPath = gb::resolveRomPathForRuntime(foundRom->string());
            r.imagePath = foundImage ? gb::resolveRomPathForRuntime(foundImage->string()) : std::string();
            r.system = systemLabelForRom(*foundRom);
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
    constexpr int kMargin = 18;
    constexpr int kTop = 88;
    constexpr int kBottomPad = 44;
    constexpr int kGridPad = 12;
    constexpr int kCardW = 158;
    constexpr int kCardH = 172;
    constexpr int kMinGapX = 12;
    constexpr int kGapY = 12;

    const auto roms = discoverRoms();
    if (roms.empty()) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_WARNING,
            "Nenhuma ROM encontrada",
            "Use estrutura ./rom/NOME_DO_JOGO/{arquivo.gb|arquivo.gbc|arquivo.gba, capa.jpg}.",
            nullptr
        );
        return {};
    }

    SDL_Window* window = SDL_CreateWindow(
        "Selecionar ROM",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1000,
        640,
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

    auto colsFromWidth = [&](int w) -> int {
        const int contentW = std::max(1, w - kMargin * 2 - kGridPad * 2 - 18);
        return std::max(1, (contentW + kMinGapX) / (kCardW + kMinGapX));
    };

    struct RowLayout {
        int startIdx = 0;
        int items = 0;
        int gapX = 0;
        int startX = 0;
    };

    auto calcRowLayout = [&](int rowGlobal, int cols) -> RowLayout {
        RowLayout out;
        out.startIdx = rowGlobal * cols;
        if (out.startIdx < 0 || out.startIdx >= static_cast<int>(roms.size())) {
            return out;
        }
        out.items = std::min(cols, static_cast<int>(roms.size()) - out.startIdx);
        out.gapX = kMinGapX;
        out.startX = kMargin + kGridPad;
        return out;
    };

    auto cardAt = [&](int mx, int my, int w, int h, int scrollRows) -> int {
        const int areaW = w - kMargin * 2;
        const int areaH = h - kTop - kBottomPad - kGridPad * 2;
        const int cols = colsFromWidth(w);

        if (mx < kMargin + kGridPad || mx >= kMargin + areaW || my < kTop + kGridPad || my >= kTop + kGridPad + areaH) {
            return -1;
        }

        const int rowVisible = (my - kTop - kGridPad) / (kCardH + kGapY);
        const int yInCell = (my - kTop - kGridPad) % (kCardH + kGapY);
        if (yInCell >= kCardH) {
            return -1;
        }

        const int rowGlobal = scrollRows + rowVisible;
        const auto layout = calcRowLayout(rowGlobal, cols);
        if (layout.items == 0) {
            return -1;
        }

        for (int col = 0; col < layout.items; ++col) {
            const int x = layout.startX + col * (kCardW + layout.gapX);
            if (mx >= x && mx < x + kCardW) {
                return layout.startIdx + col;
            }
        }
        return -1;
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
                const int cols = colsFromWidth(w);
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
                const int cols = colsFromWidth(w);
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
        const int areaW = w - kMargin * 2;
        const int areaH = h - kTop - kBottomPad;
        const int gridH = std::max(1, areaH - kGridPad * 2);
        const int cols = colsFromWidth(w);
        const int visibleRows = std::max(1, (gridH + kGapY) / (kCardH + kGapY));
        const int totalRows = std::max(1, (static_cast<int>(roms.size()) + cols - 1) / cols);

        selected = std::clamp(selected, 0, static_cast<int>(roms.size()) - 1);
        const int selectedRow = selected / cols;
        if (selectedRow < scrollRows) {
            scrollRows = selectedRow;
        }
        if (selectedRow >= scrollRows + visibleRows) {
            scrollRows = selectedRow - visibleRows + 1;
        }
        scrollRows = std::clamp(scrollRows, 0, std::max(0, totalRows - visibleRows));

        SDL_SetRenderDrawColor(renderer, 9, 12, 19, 255);
        SDL_RenderClear(renderer);

        SDL_Rect topBand{0, 0, w, 74};
        SDL_SetRenderDrawColor(renderer, 14, 19, 30, 255);
        SDL_RenderFillRect(renderer, &topBand);
        SDL_SetRenderDrawColor(renderer, 255, 190, 92, 255);
        SDL_RenderDrawLine(renderer, 18, 73, w - 18, 73);
        SDL_SetRenderDrawColor(renderer, 38, 50, 76, 255);
        SDL_RenderDrawLine(renderer, 0, 74, w, 74);

        drawHexText(renderer, 22, 16, "ORBITALBOY", SDL_Color{248, 250, 255, 255}, 2);
        drawHexText(renderer, 22, 43, "BIBLIOTECA", SDL_Color{156, 169, 198, 255}, 1);

        char countText[32];
        std::snprintf(countText, sizeof(countText), "%d ROMS", static_cast<int>(roms.size()));
        drawHexText(renderer, std::max(22, w - 154), 16, countText, SDL_Color{255, 190, 92, 255}, 2);
        drawHexText(renderer, std::max(22, w - 308), 46, "ENTER ABRE  ESC SAI  SETAS NAVEGAM", SDL_Color{154, 166, 196, 255}, 1);

        SDL_Rect listBg{kMargin, kTop, areaW, areaH};
        drawPanel(renderer, listBg, SDL_Color{13, 17, 27, 255}, SDL_Color{38, 50, 76, 255});

        for (int row = 0; row < visibleRows; ++row) {
            const int rowGlobal = row + scrollRows;
            const auto layout = calcRowLayout(rowGlobal, cols);
            if (layout.items == 0) {
                continue;
            }
            for (int col = 0; col < layout.items; ++col) {
                const int idx = layout.startIdx + col;
                const int x = layout.startX + col * (kCardW + layout.gapX);
                const int y = kTop + kGridPad + row * (kCardH + kGapY);
                const bool isSel = idx == selected;
                const bool isHover = idx == hover;
                const auto& rom = roms[static_cast<std::size_t>(idx)];
                const SDL_Color accent = systemAccentColor(rom.system);

                SDL_Rect card{x, y, kCardW, kCardH};
                if (isSel) {
                    SDL_Rect glow{x - 2, y - 2, kCardW + 4, kCardH + 4};
                    SDL_SetRenderDrawColor(renderer, accent.r / 4, accent.g / 4, accent.b / 4, 255);
                    SDL_RenderFillRect(renderer, &glow);
                }
                const SDL_Color fill = isSel
                    ? SDL_Color{34, 42, 62, 255}
                    : (isHover ? SDL_Color{27, 34, 52, 255} : SDL_Color{20, 25, 39, 255});
                const SDL_Color border = isSel
                    ? accent
                    : (isHover ? SDL_Color{86, 104, 140, 255} : SDL_Color{44, 56, 82, 255});
                drawPanel(renderer, card, fill, border);

                SDL_Rect ph{x + 10, y + 10, kCardW - 20, 112};
                drawPanel(renderer, ph, SDL_Color{8, 11, 18, 255}, SDL_Color{48, 60, 86, 255});
                if (previewTextures[static_cast<std::size_t>(idx)] == nullptr && !rom.imagePath.empty()) {
                    SDL_Surface* surface = loadPreviewSurface(rom.imagePath);
                    if (surface) {
                        previewTextures[static_cast<std::size_t>(idx)] = SDL_CreateTextureFromSurface(renderer, surface);
                        SDL_FreeSurface(surface);
                    }
                }

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
                    drawHexText(renderer, x + 46, y + 60, "NO IMG", SDL_Color{104, 118, 150, 255}, 1);
                }

                drawBadge(renderer, x + 10, y + 130, rom.system, accent);
                const std::string label = (isSel ? "> " : "") + rom.label.substr(0, 18);
                drawHexText(
                    renderer,
                    x + 10,
                    y + 153,
                    label,
                    isSel ? SDL_Color{248, 250, 255, 255} : SDL_Color{205, 214, 232, 255},
                    1
                );
            }
        }

        SDL_Rect footer{0, h - 34, w, 34};
        SDL_SetRenderDrawColor(renderer, 14, 19, 30, 255);
        SDL_RenderFillRect(renderer, &footer);
        SDL_SetRenderDrawColor(renderer, 38, 50, 76, 255);
        SDL_RenderDrawLine(renderer, 0, h - 34, w, h - 34);
        drawHexText(renderer, 22, h - 23, "ENTER OPEN  DOUBLE CLICK OPEN  ESC CANCEL", SDL_Color{170, 181, 207, 255}, 1);

        if (totalRows > visibleRows) {
            const int trackH = areaH - 16;
            const int trackX = kMargin + areaW - 13;
            const int trackY = kTop + 8;
            SDL_Rect track{trackX, trackY, 7, trackH};
            SDL_SetRenderDrawColor(renderer, 31, 41, 62, 255);
            SDL_RenderFillRect(renderer, &track);
            const int thumbH = std::max(24, (trackH * visibleRows) / totalRows);
            const int maxScroll = std::max(1, totalRows - visibleRows);
            const int thumbY = trackY + ((trackH - thumbH) * scrollRows) / maxScroll;
            SDL_Rect thumb{trackX, thumbY, 7, thumbH};
            SDL_SetRenderDrawColor(renderer, 132, 154, 210, 255);
            SDL_RenderFillRect(renderer, &thumb);
        }

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
#endif

} // namespace gb::frontend
