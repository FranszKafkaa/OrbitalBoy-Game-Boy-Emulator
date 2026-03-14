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

bool hasImageExtension(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
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
#else
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
            r.label = normalizeLabel(entry.path().filename().string(), 18);
            r.romPath = gb::resolveRomPathForRuntime(foundRom->string());
            r.imagePath = foundImage ? gb::resolveRomPathForRuntime(foundImage->string()) : std::string();
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
    constexpr int kMargin = 20;
    constexpr int kTop = 78;
    constexpr int kBottomPad = 54;
    constexpr int kCardW = 170;
    constexpr int kCardH = 120;
    constexpr int kMinGapX = 20;
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

    auto colsFromWidth = [&](int w) -> int {
        const int areaW = std::max(1, w - kMargin * 2);
        return std::max(1, (areaW + kMinGapX) / (kCardW + kMinGapX));
    };

    struct RowLayout {
        int startIdx = 0;
        int items = 0;
        int gapX = 0;
        int startX = 0;
    };

    auto calcRowLayout = [&](int areaW, int rowGlobal, int cols) -> RowLayout {
        RowLayout out;
        out.startIdx = rowGlobal * cols;
        if (out.startIdx < 0 || out.startIdx >= static_cast<int>(roms.size())) {
            return out;
        }
        out.items = std::min(cols, static_cast<int>(roms.size()) - out.startIdx);
        out.gapX = std::max(kMinGapX, (areaW - out.items * kCardW) / (out.items + 1));
        const int rowWidth = out.items * kCardW + (out.items - 1) * out.gapX;
        out.startX = kMargin + std::max(0, (areaW - rowWidth) / 2);
        return out;
    };

    auto cardAt = [&](int mx, int my, int w, int h, int scrollRows) -> int {
        const int areaW = w - kMargin * 2;
        const int areaH = h - kTop - kBottomPad;
        const int cols = colsFromWidth(w);

        if (mx < kMargin || mx >= kMargin + areaW || my < kTop || my >= kTop + areaH) {
            return -1;
        }

        const int rowVisible = (my - kTop) / (kCardH + kGapY);
        const int yInCell = (my - kTop) % (kCardH + kGapY);
        if (yInCell >= kCardH) {
            return -1;
        }

        const int rowGlobal = scrollRows + rowVisible;
        const auto layout = calcRowLayout(areaW, rowGlobal, cols);
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
        const int cols = colsFromWidth(w);
        const int visibleRows = std::max(1, (areaH + kGapY) / (kCardH + kGapY));
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

        SDL_SetRenderDrawColor(renderer, 18, 20, 28, 255);
        SDL_RenderClear(renderer);

        drawHexText(renderer, 18, 16, "SELECIONE A ROM", SDL_Color{240, 244, 255, 255}, 2);
        drawHexText(renderer, 18, 40, "CLIQUE DUPLO OU PRESSIONE ENTER", SDL_Color{150, 160, 185, 255}, 1);

        SDL_SetRenderDrawColor(renderer, 30, 35, 48, 255);
        SDL_Rect listBg{kMargin, kTop, areaW, areaH};
        SDL_RenderFillRect(renderer, &listBg);
        SDL_SetRenderDrawColor(renderer, 64, 72, 92, 255);
        SDL_RenderDrawRect(renderer, &listBg);

        for (int row = 0; row < visibleRows; ++row) {
            const int rowGlobal = row + scrollRows;
            const auto layout = calcRowLayout(areaW, rowGlobal, cols);
            if (layout.items == 0) {
                continue;
            }
            for (int col = 0; col < layout.items; ++col) {
                const int idx = layout.startIdx + col;
                const int x = layout.startX + col * (kCardW + layout.gapX);
                const int y = kTop + row * (kCardH + kGapY);
                const bool isSel = idx == selected;
                const bool isHover = idx == hover;

                SDL_SetRenderDrawColor(renderer, 38, 44, 62, 255);
                SDL_Rect card{x, y, kCardW, kCardH};
                SDL_RenderFillRect(renderer, &card);
                SDL_SetRenderDrawColor(renderer, isSel ? 118 : (isHover ? 94 : 70), isSel ? 142 : (isHover ? 112 : 84), isSel ? 190 : (isHover ? 150 : 110), 255);
                SDL_RenderDrawRect(renderer, &card);

                SDL_SetRenderDrawColor(renderer, 24, 28, 40, 255);
                SDL_Rect ph{x + 10, y + 10, kCardW - 20, 64};
                SDL_RenderFillRect(renderer, &ph);
                SDL_SetRenderDrawColor(renderer, 86, 96, 126, 255);
                SDL_RenderDrawRect(renderer, &ph);
#ifdef GBEMU_USE_SDL2_IMAGE
                if (previewTextures[static_cast<std::size_t>(idx)] == nullptr && !roms[static_cast<std::size_t>(idx)].imagePath.empty()) {
                    SDL_Surface* surface = loadPreviewSurface(roms[static_cast<std::size_t>(idx)].imagePath);
                    if (surface) {
                        previewTextures[static_cast<std::size_t>(idx)] = SDL_CreateTextureFromSurface(renderer, surface);
                        SDL_FreeSurface(surface);
                    }
                }
#else
                if (previewTextures[static_cast<std::size_t>(idx)] == nullptr && !roms[static_cast<std::size_t>(idx)].imagePath.empty()) {
                    SDL_Surface* surface = loadPreviewSurface(roms[static_cast<std::size_t>(idx)].imagePath);
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
#endif

} // namespace gb::frontend
