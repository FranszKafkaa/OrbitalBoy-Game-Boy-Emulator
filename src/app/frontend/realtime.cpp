#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "gb/app/frontend/debug_ui.hpp"
#include "gb/app/frontend/realtime/audio_ring_buffer.hpp"
#include "gb/app/frontend/realtime/dropping_queue.hpp"
#include "gb/app/frontend/realtime/frame_timeline.hpp"
#include "gb/app/frontend/realtime/session_models.hpp"
#include "gb/app/frontend/realtime.hpp"
#include "gb/app/frontend/realtime_support.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>
#endif

namespace gb::frontend {

#ifdef GBEMU_USE_SDL2
int runRealtime(
    gb::GameBoy& gb,
    int scale,
    int audioBuffer,
    const std::string& statePath,
    const std::string& legacyStatePath,
    const std::string& batteryRamPath,
    const std::string& palettePath,
    const std::string& rtcPath,
    const std::string& filtersPath,
    const std::string& captureDir
) {
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
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
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    }
    if (!renderer) {
        std::cerr << "erro SDL_CreateRenderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (SDL_RenderSetVSync(renderer, 1) != 0) {
        std::cerr << "aviso: nao foi possivel forcar vsync: " << SDL_GetError() << "\n";
    }
#endif
    SDL_RendererInfo rendererInfo{};
    if (SDL_GetRendererInfo(renderer, &rendererInfo) == 0) {
        if ((rendererInfo.flags & SDL_RENDERER_PRESENTVSYNC) == 0) {
            std::cerr << "aviso: renderer sem vsync; screen tearing pode ocorrer.\n";
        }
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        std::cerr << "erro SDL_CreateTexture: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Texture* sharpTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);

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
    bool fastForward = false;
    bool backToMenu = false;
    bool watchpointEnabled = false;
    bool showBreakpointMenu = false;
    bool showScaleMenu = false;
    bool showPaletteMenu = false;
    bool requestCapture = false;
    FullscreenScaleMode fullscreenMode = FullscreenScaleMode::CrispFit;
    LinkCableMode linkCableMode = LinkCableMode::Off;
    VideoFilterMode filterMode = VideoFilterMode::None;
    int scaleMenuIndex = 0;
    DisplayPaletteMode paletteMode = DisplayPaletteMode::GameBoyClassic;
    const bool cgbPaletteAvailable = gb.cartridge().cgbSupported();
    if (const auto saved = loadPalettePreference(palettePath); saved.has_value()) {
        paletteMode = saved.value();
    }
    if (!cgbPaletteAvailable && paletteMode == DisplayPaletteMode::GameBoyColor) {
        paletteMode = DisplayPaletteMode::GameBoyClassic;
    }
    if (const auto savedFilter = loadFilterPreference(filtersPath); savedFilter.has_value()) {
        filterMode = savedFilter.value();
    }
    int paletteMenuIndex = static_cast<int>(paletteMode);
    FrameTimeline timeline(gb);
    std::vector<gb::u16> breakpoints{};
    breakpoints.reserve(16);
    BreakpointEditState breakpointEdit{};
    MemorySearchState memorySearch{};
    std::optional<gb::u16> selectedSpriteAddr;
    int spriteScrollRows = 0;
    MemoryWatch memoryWatch{};
    MemoryEditState memoryEdit{};
    MemoryWriteUiState memoryWriteUi{};
    QueuedMemoryWrite queuedWrite{};
    std::atomic<std::uint64_t> emulatedFrameCounter{0};
    std::string uiMessage;
    int uiMessageFrames = 0;
    std::array<unsigned char, gb::PPU::ScreenWidth * gb::PPU::ScreenHeight * 3> pixels{};
    std::array<unsigned char, gb::PPU::ScreenWidth * gb::PPU::ScreenHeight * 3> sharpPixels{};
    std::mutex gbMutex{};
    std::mutex breakpointsMutex{};
    std::mutex queuedWriteMutex{};
    std::atomic<bool> mtRunning{true};
    std::atomic<bool> pausedAtomic{paused};
    std::atomic<bool> fastForwardAtomic{fastForward};
    std::atomic<bool> watchpointEnabledAtomic{watchpointEnabled};
    std::atomic<gb::u16> watchAddressAtomic{memoryWatch.address};
    std::atomic<bool> watchFreezeAtomic{memoryWatch.freeze};
    std::atomic<gb::u8> watchFreezeValueAtomic{memoryWatch.freezeValue};
    std::atomic<int> linkCableModeAtomic{static_cast<int>(linkCableMode)};
    std::atomic<int> paletteModeAtomic{static_cast<int>(paletteMode)};
    std::atomic<int> filterModeAtomic{static_cast<int>(filterMode)};
    std::atomic<bool> audioGateAtomic{false};
    std::atomic<int> pendingPauseReason{0};
    std::atomic<gb::u16> pendingPauseAddr{0};
    std::atomic<std::uint64_t> frameSequence{0};
    std::atomic<bool> forceTitleRefresh{false};
    DroppingQueue<RawFramePacket, 3> rawFrameQueue{};
    DroppingQueue<RgbFramePacket, 3> rgbFrameQueue{};
    AudioRingBuffer audioRing(static_cast<std::size_t>(gb::APU::SampleRate) * 2 * 2);
    updateWindowTitle(window, gb.cartridge().title(), paused, muted);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
#endif
    {
        std::lock_guard<std::mutex> gbLock(gbMutex);
        resetMemoryWatch(memoryWatch, gb.bus());
    }

    const auto syncThreadState = [&]() {
        pausedAtomic.store(paused, std::memory_order_relaxed);
        fastForwardAtomic.store(fastForward, std::memory_order_relaxed);
        watchpointEnabledAtomic.store(watchpointEnabled, std::memory_order_relaxed);
        watchAddressAtomic.store(memoryWatch.address, std::memory_order_relaxed);
        watchFreezeAtomic.store(memoryWatch.freeze, std::memory_order_relaxed);
        watchFreezeValueAtomic.store(memoryWatch.freezeValue, std::memory_order_relaxed);
        linkCableModeAtomic.store(static_cast<int>(linkCableMode), std::memory_order_relaxed);
        paletteModeAtomic.store(static_cast<int>(paletteMode), std::memory_order_relaxed);
        filterModeAtomic.store(static_cast<int>(filterMode), std::memory_order_relaxed);
        audioGateAtomic.store(audioEnabled && !muted && !paused && !fastForward, std::memory_order_relaxed);
    };

    const auto enqueueRawFrameLocked = [&]() {
        RawFramePacket packet{};
        packet.sequence = frameSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        packet.mono = gb.ppu().framebuffer();
        packet.color = gb.ppu().colorFramebuffer();
        rawFrameQueue.push(std::move(packet));
    };

    const auto queueMemoryWrite = [&](gb::u16 address, gb::u8 value, const char* source) {
        if (!likelyWritableAddress(address)) {
            std::lock_guard<std::mutex> writeLock(queuedWriteMutex);
            if (queuedWrite.active) {
                memoryWriteUi.pending = true;
                memoryWriteUi.pendingAddress = queuedWrite.address;
                memoryWriteUi.pendingValue = queuedWrite.value;
            } else {
                memoryWriteUi.pending = false;
            }
            memoryWriteUi.hasLast = true;
            memoryWriteUi.lastOk = false;
            memoryWriteUi.lastAddress = address;
            memoryWriteUi.lastValue = value;
            memoryWriteUi.lastFrame = emulatedFrameCounter.load(std::memory_order_relaxed);
            memoryWriteUi.lastTag = "ERR READ ONLY";
            uiMessage = "ADDR READ ONLY";
            uiMessageFrames = 90;
            return;
        }
        {
            std::lock_guard<std::mutex> writeLock(queuedWriteMutex);
            queuedWrite.active = true;
            queuedWrite.address = address;
            queuedWrite.value = value;
            queuedWrite.source = source;
        }
        memoryWriteUi.pending = true;
        memoryWriteUi.pendingAddress = address;
        memoryWriteUi.pendingValue = value;
        uiMessage = "WRITE QUEUED";
        uiMessageFrames = 75;
    };

    const auto applyWriteNow = [&](gb::u16 address, gb::u8 value, const char* tag, bool showToast) {
        if (!likelyWritableAddress(address)) {
            memoryWriteUi.pending = false;
            memoryWriteUi.hasLast = true;
            memoryWriteUi.lastOk = false;
            memoryWriteUi.lastAddress = address;
            memoryWriteUi.lastValue = value;
            memoryWriteUi.lastFrame = emulatedFrameCounter.load(std::memory_order_relaxed);
            memoryWriteUi.lastTag = "ERR READ ONLY";
            if (showToast) {
                uiMessage = "ADDR READ ONLY";
                uiMessageFrames = 90;
            }
            return;
        }
        {
            std::lock_guard<std::mutex> gbLock(gbMutex);
            gb.bus().write(address, value);
            if (memoryWatch.address == address) {
                resetMemoryWatch(memoryWatch, gb.bus());
            }
            enqueueRawFrameLocked();
        }
        memoryWriteUi.pending = false;
        memoryWriteUi.hasLast = true;
        memoryWriteUi.lastOk = true;
        memoryWriteUi.lastAddress = address;
        memoryWriteUi.lastValue = value;
        memoryWriteUi.lastFrame = emulatedFrameCounter.load(std::memory_order_relaxed);
        memoryWriteUi.lastTag = tag;
        if (showToast) {
            char msg[56];
            std::snprintf(msg, sizeof(msg), "WRITE %04X=%02X", address, value);
            uiMessage = msg;
            uiMessageFrames = 90;
        }
    };

    const auto applyFrameLockIfNeeded = [&]() {
        if (!memoryWatch.freeze || !likelyWritableAddress(memoryWatch.address)) {
            return;
        }
        {
            std::lock_guard<std::mutex> gbLock(gbMutex);
            const gb::u8 current = gb.bus().peek(memoryWatch.address);
            if (current == memoryWatch.freezeValue) {
                return;
            }
        }
        applyWriteNow(memoryWatch.address, memoryWatch.freezeValue, "LOCK", false);
    };

    const auto stopTextInputIfUnused = [&]() {
        if (!memoryEdit.active && !breakpointEdit.active && !memorySearch.ui.editingValue) {
            SDL_StopTextInput();
        }
    };

    const auto toggleBreakpoint = [&](gb::u16 address) {
        std::lock_guard<std::mutex> bpLock(breakpointsMutex);
        const auto it = std::find(breakpoints.begin(), breakpoints.end(), address);
        if (it != breakpoints.end()) {
            breakpoints.erase(it);
            char msg[40];
            std::snprintf(msg, sizeof(msg), "BP DEL %04X", address);
            uiMessage = msg;
            uiMessageFrames = 120;
            return;
        }
        if (breakpoints.size() >= 16) {
            uiMessage = "BP LIMIT 16";
            uiMessageFrames = 120;
            return;
        }
        breakpoints.push_back(address);
        std::sort(breakpoints.begin(), breakpoints.end());
        char msg[40];
        std::snprintf(msg, sizeof(msg), "BP ADD %04X", address);
        uiMessage = msg;
        uiMessageFrames = 120;
    };

    const auto captureSearchSnapshot = [&]() {
        std::lock_guard<std::mutex> gbLock(gbMutex);
        for (int address = 0; address <= 0xFFFF; ++address) {
            memorySearch.snapshot[static_cast<std::size_t>(address)] = gb.bus().peek(static_cast<gb::u16>(address));
        }
        memorySearch.ui.hasSnapshot = true;
        uiMessage = "SEARCH SNAPSHOT";
        uiMessageFrames = 90;
    };

    const auto clearSearchResults = [&]() {
        memorySearch.ui.matches.clear();
        memorySearch.ui.totalMatches = 0;
        memorySearch.ui.scroll = 0;
    };

    const auto runMemorySearch = [&]() {
        const MemorySearchMode mode = memorySearch.ui.mode;
        std::optional<gb::u8> target;
        if (mode == MemorySearchMode::Exact || mode == MemorySearchMode::Greater || mode == MemorySearchMode::Less) {
            target = parseHex8(memorySearch.ui.valueHex);
            if (!target.has_value()) {
                uiMessage = "SEARCH VAL INVALID";
                uiMessageFrames = 120;
                return;
            }
        }
        if ((mode == MemorySearchMode::Changed || mode == MemorySearchMode::Unchanged) && !memorySearch.ui.hasSnapshot) {
            captureSearchSnapshot();
            uiMessage = "SNAPSHOT FIRST";
            uiMessageFrames = 120;
            return;
        }

        clearSearchResults();
        {
            std::lock_guard<std::mutex> gbLock(gbMutex);
            for (int address = 0; address <= 0xFFFF; ++address) {
                const gb::u16 addr = static_cast<gb::u16>(address);
                if (!likelyWritableAddress(addr)) {
                    continue;
                }
                const gb::u8 now = gb.bus().peek(addr);
                const gb::u8 prev = memorySearch.snapshot[static_cast<std::size_t>(address)];
                bool match = false;
                switch (mode) {
                case MemorySearchMode::Exact:
                    match = now == target.value();
                    break;
                case MemorySearchMode::Greater:
                    match = now > target.value();
                    break;
                case MemorySearchMode::Less:
                    match = now < target.value();
                    break;
                case MemorySearchMode::Changed:
                    match = now != prev;
                    break;
                case MemorySearchMode::Unchanged:
                    match = now == prev;
                    break;
                }
                if (!match) {
                    continue;
                }
                ++memorySearch.ui.totalMatches;
                if (memorySearch.ui.matches.size() < MemorySearchState::MaxStoredMatches) {
                    memorySearch.ui.matches.push_back(addr);
                }
            }
        }
        memorySearch.ui.scroll = 0;
        char msg[56];
        std::snprintf(msg, sizeof(msg), "SEARCH MATCH %zu", memorySearch.ui.totalMatches);
        uiMessage = msg;
        uiMessageFrames = 120;
    };

    syncThreadState();
    {
        std::lock_guard<std::mutex> gbLock(gbMutex);
        enqueueRawFrameLocked();
    }

    std::cout << "[MT] iniciando workers (emu/render/audio)\n";

    std::thread renderThread([&]() {
        std::cout << "[MT][REN] worker iniciado\n";
        std::size_t processed = 0;
        RawFramePacket raw{};
        while (rawFrameQueue.waitPop(raw)) {
            RawFramePacket latest = std::move(raw);
            RawFramePacket pending{};
            while (rawFrameQueue.tryPopLatest(pending)) {
                latest = std::move(pending);
            }

            RgbFramePacket out{};
            out.sequence = latest.sequence;
            const auto paletteModeLocal = static_cast<DisplayPaletteMode>(paletteModeAtomic.load(std::memory_order_relaxed));
            const auto filterModeLocal = static_cast<VideoFilterMode>(filterModeAtomic.load(std::memory_order_relaxed));
            const bool useCgbPalette = cgbPaletteAvailable && paletteModeLocal == DisplayPaletteMode::GameBoyColor;

            if (useCgbPalette) {
                for (std::size_t i = 0; i < latest.color.size(); ++i) {
                    const gb::u16 c = latest.color[i];
                    const gb::u8 r5 = static_cast<gb::u8>((c >> 0) & 0x1F);
                    const gb::u8 g5 = static_cast<gb::u8>((c >> 5) & 0x1F);
                    const gb::u8 b5 = static_cast<gb::u8>((c >> 10) & 0x1F);
                    out.pixels[i * 3 + 0] = static_cast<unsigned char>((r5 * 255) / 31);
                    out.pixels[i * 3 + 1] = static_cast<unsigned char>((g5 * 255) / 31);
                    out.pixels[i * 3 + 2] = static_cast<unsigned char>((b5 * 255) / 31);
                }
            } else {
                const auto& palette = monoPalette(paletteModeLocal);
                for (std::size_t i = 0; i < latest.mono.size(); ++i) {
                    const std::size_t shade = static_cast<std::size_t>(latest.mono[i] & 0x03);
                    out.pixels[i * 3 + 0] = palette[shade][0];
                    out.pixels[i * 3 + 1] = palette[shade][1];
                    out.pixels[i * 3 + 2] = palette[shade][2];
                }
            }

            applyVideoFilterRgb24(filterModeLocal, out.pixels);
            rgbFrameQueue.push(std::move(out));
            ++processed;
            if ((processed % 360) == 0) {
                std::cout << "[MT][REN] frames=" << processed
                          << " dropIn=" << rawFrameQueue.droppedCount()
                          << " dropOut=" << rgbFrameQueue.droppedCount() << "\n";
            }
        }
        std::cout << "[MT][REN] worker finalizado, dropIn=" << rawFrameQueue.droppedCount()
                  << " dropOut=" << rgbFrameQueue.droppedCount() << "\n";
    });

    std::thread audioThread{};
    if (audioEnabled) {
        audioThread = std::thread([&]() {
            std::cout << "[MT][AUD] worker iniciado\n";
            std::array<int16_t, 4096> chunk{};
            std::size_t underruns = 0;
            auto lastLog = std::chrono::steady_clock::now();
            while (mtRunning.load(std::memory_order_relaxed)) {
                if (!audioGateAtomic.load(std::memory_order_relaxed)) {
                    audioRing.clear();
                    SDL_ClearQueuedAudio(audioDev);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                const std::size_t count = audioRing.pop(chunk.data(), chunk.size(), 8);
                if (count == 0) {
                    ++underruns;
                    continue;
                }
                if (SDL_GetQueuedAudioSize(audioDev) > static_cast<Uint32>(have.freq * have.channels * sizeof(int16_t))) {
                    SDL_ClearQueuedAudio(audioDev);
                }
                SDL_QueueAudio(audioDev, chunk.data(), static_cast<Uint32>(count * sizeof(int16_t)));

                const auto now = std::chrono::steady_clock::now();
                if (now - lastLog >= std::chrono::seconds(3)) {
                    std::cout << "[MT][AUD] queued=" << SDL_GetQueuedAudioSize(audioDev)
                              << " underruns=" << underruns
                              << " droppedSamples=" << audioRing.droppedCount() << "\n";
                    lastLog = now;
                }
            }
            SDL_ClearQueuedAudio(audioDev);
            std::cout << "[MT][AUD] worker finalizado, underruns=" << underruns
                      << " droppedSamples=" << audioRing.droppedCount() << "\n";
        });
    }

    std::thread emuThread([&]() {
        std::cout << "[MT][EMU] worker iniciado\n";
        std::mt19937 emuRng(std::random_device{}());
    std::uniform_int_distribution<int> emuRandByte(0, 255);
    const auto frameBudget = std::chrono::microseconds(16742);
    auto nextFrame = std::chrono::steady_clock::now();
    auto lastLog = std::chrono::steady_clock::now();
        std::uint64_t logFrames = 0;
        std::size_t audioDropLogs = 0;

        while (mtRunning.load(std::memory_order_relaxed)) {
            if (pausedAtomic.load(std::memory_order_relaxed)) {
                nextFrame = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const bool ff = fastForwardAtomic.load(std::memory_order_relaxed);
            const int framesToRun = ff ? 6 : 1;
            for (int i = 0; i < framesToRun && mtRunning.load(std::memory_order_relaxed); ++i) {
                const bool watchEnabled = watchpointEnabledAtomic.load(std::memory_order_relaxed);
                const gb::u16 watchAddr = watchAddressAtomic.load(std::memory_order_relaxed);
                const bool freezeEnabled = watchFreezeAtomic.load(std::memory_order_relaxed);
                const gb::u8 freezeValue = watchFreezeValueAtomic.load(std::memory_order_relaxed);
                bool watchTriggered = false;
                gb::u16 pc = 0;
                gb::u8 watchBefore = 0;

                {
                    std::lock_guard<std::mutex> gbLock(gbMutex);
                    if (watchEnabled) {
                        watchBefore = gb.bus().peek(watchAddr);
                    }

                    gb.runFrame();
                    emulatedFrameCounter.fetch_add(1, std::memory_order_relaxed);

                    gb::u8 outData = 0;
                    if (gb.bus().consumeSerialTransfer(outData)) {
                        gb::u8 inData = 0xFF;
                        const auto mode = static_cast<LinkCableMode>(linkCableModeAtomic.load(std::memory_order_relaxed));
                        if (mode == LinkCableMode::Loopback) {
                            inData = outData;
                        } else if (mode == LinkCableMode::Noise) {
                            inData = static_cast<gb::u8>(emuRandByte(emuRng));
                        }
                        gb.bus().completeSerialTransfer(inData);
                    }

                    if (freezeEnabled && likelyWritableAddress(watchAddr)) {
                        if (gb.bus().peek(watchAddr) != freezeValue) {
                            gb.bus().write(watchAddr, freezeValue);
                        }
                    }

                    timeline.captureCurrent(gb);
                    enqueueRawFrameLocked();
                    pc = gb.cpu().regs().pc;

                    auto samples = gb.apu().takeSamples();
                    if (audioEnabled && audioGateAtomic.load(std::memory_order_relaxed) && !samples.empty()) {
                        const std::size_t written = audioRing.push(samples.data(), samples.size());
                        if (written < samples.size()) {
                            ++audioDropLogs;
                            if ((audioDropLogs % 120) == 1) {
                                std::cerr << "[MT][AUD] ring cheio, samples descartados=" << (samples.size() - written) << "\n";
                            }
                        }
                    }

                    if (watchEnabled) {
                        watchTriggered = gb.bus().peek(watchAddr) != watchBefore;
                    }
                }

                if (watchTriggered) {
                    pausedAtomic.store(true, std::memory_order_relaxed);
                    fastForwardAtomic.store(false, std::memory_order_relaxed);
                    pendingPauseAddr.store(watchAddr, std::memory_order_relaxed);
                    pendingPauseReason.store(1, std::memory_order_relaxed);
                    forceTitleRefresh.store(true, std::memory_order_relaxed);
                    break;
                }

                bool breakpointHit = false;
                {
                    std::lock_guard<std::mutex> bpLock(breakpointsMutex);
                    breakpointHit = std::find(breakpoints.begin(), breakpoints.end(), pc) != breakpoints.end();
                }
                if (breakpointHit) {
                    pausedAtomic.store(true, std::memory_order_relaxed);
                    fastForwardAtomic.store(false, std::memory_order_relaxed);
                    pendingPauseAddr.store(pc, std::memory_order_relaxed);
                    pendingPauseReason.store(2, std::memory_order_relaxed);
                    forceTitleRefresh.store(true, std::memory_order_relaxed);
                    break;
                }

                ++logFrames;
            }

            if (!ff) {
                nextFrame += frameBudget;
                std::this_thread::sleep_until(nextFrame);
                const auto now = std::chrono::steady_clock::now();
                if (now - nextFrame > std::chrono::milliseconds(100)) {
                    nextFrame = now;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - lastLog >= std::chrono::seconds(5)) {
                const double seconds = std::chrono::duration<double>(now - lastLog).count();
                const double fps = seconds > 0.0 ? static_cast<double>(logFrames) / seconds : 0.0;
                std::cout << "[MT][EMU] fps=" << fps
                          << " frames=" << emulatedFrameCounter.load(std::memory_order_relaxed)
                          << " ff=" << (fastForwardAtomic.load(std::memory_order_relaxed) ? "on" : "off")
                          << " paused=" << (pausedAtomic.load(std::memory_order_relaxed) ? "on" : "off") << "\n";
                lastLog = now;
                logFrames = 0;
            }
        }
        std::cout << "[MT][EMU] worker finalizado, totalFrames="
                  << emulatedFrameCounter.load(std::memory_order_relaxed) << "\n";
    });

    double debugFps = 0.0;
    auto fpsWindowStart = std::chrono::steady_clock::now();
    std::uint64_t fpsWindowFrames = emulatedFrameCounter.load(std::memory_order_relaxed);

    while (running) {
        if (paused != pausedAtomic.load(std::memory_order_relaxed)) {
            paused = pausedAtomic.load(std::memory_order_relaxed);
            if (paused) {
                fastForward = false;
            }
            updateWindowTitle(window, gb.cartridge().title(), paused, muted);
        }
        fastForward = fastForwardAtomic.load(std::memory_order_relaxed);
        if (forceTitleRefresh.exchange(false, std::memory_order_relaxed)) {
            updateWindowTitle(window, gb.cartridge().title(), paused, muted);
        }
        const int pendingPause = pendingPauseReason.exchange(0, std::memory_order_relaxed);
        if (pendingPause != 0) {
            const gb::u16 addr = pendingPauseAddr.load(std::memory_order_relaxed);
            char msg[48];
            if (pendingPause == 1) {
                std::snprintf(msg, sizeof(msg), "WATCH HIT %04X", addr);
            } else {
                std::snprintf(msg, sizeof(msg), "BP HIT %04X", addr);
            }
            uiMessage = msg;
            uiMessageFrames = 150;
            paused = true;
            fastForward = false;
            syncThreadState();
            updateWindowTitle(window, gb.cartridge().title(), paused, muted);
        }

        syncThreadState();
        {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fpsWindowStart).count();
            if (elapsed >= 0.25) {
                const std::uint64_t framesNow = emulatedFrameCounter.load(std::memory_order_relaxed);
                debugFps = static_cast<double>(framesNow - fpsWindowFrames) / elapsed;
                fpsWindowFrames = framesNow;
                fpsWindowStart = now;
            }
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
                if (ev.key.keysym.sym == SDLK_n && fullscreen) {
                    showScaleMenu = !showScaleMenu;
                    showPaletteMenu = false;
                    scaleMenuIndex = static_cast<int>(fullscreenMode);
                    continue;
                }
                if (showScaleMenu) {
                    if (ev.key.keysym.sym == SDLK_UP) {
                        scaleMenuIndex = (scaleMenuIndex + 2) % 3;
                    } else if (ev.key.keysym.sym == SDLK_DOWN) {
                        scaleMenuIndex = (scaleMenuIndex + 1) % 3;
                    } else if (ev.key.keysym.sym == SDLK_1) {
                        scaleMenuIndex = 0;
                    } else if (ev.key.keysym.sym == SDLK_2) {
                        scaleMenuIndex = 1;
                    } else if (ev.key.keysym.sym == SDLK_3) {
                        scaleMenuIndex = 2;
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
                        fullscreenMode = static_cast<FullscreenScaleMode>(scaleMenuIndex);
                        showScaleMenu = false;
                        uiMessage = std::string("SCALE ") + scaleModeUiName(fullscreenMode);
                        uiMessageFrames = 120;
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_n) {
                        showScaleMenu = false;
                    }
                    continue;
                }
                if (breakpointEdit.active) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        breakpointEdit.active = false;
                        stopTextInputIfUnused();
                    } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        if (!breakpointEdit.addressHex.empty()) {
                            breakpointEdit.addressHex.pop_back();
                        }
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
                        const auto addr = parseHex16(breakpointEdit.addressHex);
                        if (addr.has_value()) {
                            toggleBreakpoint(addr.value());
                            breakpointEdit.active = false;
                            stopTextInputIfUnused();
                        } else {
                            uiMessage = "BP INVALID";
                            uiMessageFrames = 120;
                        }
                    }
                    continue;
                }
                if (memoryEdit.active) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        memoryEdit.active = false;
                        stopTextInputIfUnused();
                    } else if (ev.key.keysym.sym == SDLK_TAB) {
                        memoryEdit.editAddress = !memoryEdit.editAddress;
                    } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        std::string& field = memoryEdit.editAddress ? memoryEdit.addressHex : memoryEdit.valueHex;
                        if (!field.empty()) {
                            field.pop_back();
                        }
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
                        const auto addr = parseHex16(memoryEdit.addressHex);
                        const auto val = parseHex8(memoryEdit.valueHex);
                        if (addr.has_value() && val.has_value()) {
                            memoryWatch.address = addr.value();
                            {
                                std::lock_guard<std::mutex> gbLock(gbMutex);
                                resetMemoryWatch(memoryWatch, gb.bus());
                            }
                            if (memoryWatch.freeze) {
                                memoryWatch.freezeValue = val.value();
                            }
                            queueMemoryWrite(memoryWatch.address, val.value(), "EDIT");
                            memoryEdit.active = false;
                            stopTextInputIfUnused();
                        } else {
                            uiMessage = "EDIT INVALID";
                            uiMessageFrames = 120;
                        }
                    }
                    continue;
                }
                if (memorySearch.ui.editingValue) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        memorySearch.ui.editingValue = false;
                        stopTextInputIfUnused();
                    } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        if (!memorySearch.ui.valueHex.empty()) {
                            memorySearch.ui.valueHex.pop_back();
                        }
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
                        if (parseHex8(memorySearch.ui.valueHex).has_value()) {
                            memorySearch.ui.editingValue = false;
                            stopTextInputIfUnused();
                            runMemorySearch();
                        } else {
                            uiMessage = "SEARCH VAL INVALID";
                            uiMessageFrames = 120;
                        }
                    }
                    continue;
                }
                if (showPanel && memorySearch.ui.visible) {
                    bool handledSearchMenu = false;
                    if (ev.key.keysym.sym == SDLK_1) {
                        memorySearch.ui.mode = MemorySearchMode::Exact;
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_2) {
                        memorySearch.ui.mode = MemorySearchMode::Greater;
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_3) {
                        memorySearch.ui.mode = MemorySearchMode::Less;
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_4) {
                        memorySearch.ui.mode = MemorySearchMode::Changed;
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_5) {
                        memorySearch.ui.mode = MemorySearchMode::Unchanged;
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_r) {
                        captureSearchSnapshot();
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_c) {
                        clearSearchResults();
                        uiMessage = "SEARCH CLEAR";
                        uiMessageFrames = 90;
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_e) {
                        memorySearch.ui.editingValue = true;
                        SDL_StartTextInput();
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
                        runMemorySearch();
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_PAGEUP) {
                        memorySearch.ui.scroll = std::max(0, memorySearch.ui.scroll - 8);
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_PAGEDOWN) {
                        int outputW = 0;
                        int outputH = 0;
                        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                        const int maxScroll = std::max(0, static_cast<int>(memorySearch.ui.matches.size()) - searchVisibleLinesForPanel(outputH));
                        memorySearch.ui.scroll = std::min(maxScroll, memorySearch.ui.scroll + 8);
                        handledSearchMenu = true;
                    } else if (ev.key.keysym.sym == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL) == 0) {
                        memorySearch.ui.visible = false;
                        memorySearch.ui.editingValue = false;
                        stopTextInputIfUnused();
                        uiMessage = "SEARCH OFF";
                        uiMessageFrames = 90;
                        handledSearchMenu = true;
                    }
                    if (handledSearchMenu) {
                        continue;
                    }
                }
                if (showPanel && ev.key.keysym.sym == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL) == 0) {
                    memorySearch.ui.visible = !memorySearch.ui.visible;
                    memorySearch.ui.editingValue = false;
                    stopTextInputIfUnused();
                    uiMessage = memorySearch.ui.visible ? "SEARCH ON" : "SEARCH OFF";
                    uiMessageFrames = 90;
                    continue;
                }
                if (ev.key.keysym.sym == SDLK_v) {
                    showPaletteMenu = !showPaletteMenu;
                    showScaleMenu = false;
                    fastForward = false;
                    const int maxIndex = cgbPaletteAvailable ? 2 : 1;
                    paletteMenuIndex = std::clamp(static_cast<int>(paletteMode), 0, maxIndex);
                    continue;
                }
                if (showPaletteMenu) {
                    const int itemCount = cgbPaletteAvailable ? 3 : 2;
                    if (ev.key.keysym.sym == SDLK_UP) {
                        paletteMenuIndex = (paletteMenuIndex + itemCount - 1) % itemCount;
                    } else if (ev.key.keysym.sym == SDLK_DOWN) {
                        paletteMenuIndex = (paletteMenuIndex + 1) % itemCount;
                    } else if (ev.key.keysym.sym == SDLK_1) {
                        paletteMenuIndex = 0;
                    } else if (ev.key.keysym.sym == SDLK_2) {
                        paletteMenuIndex = std::min(1, itemCount - 1);
                    } else if (ev.key.keysym.sym == SDLK_3 && cgbPaletteAvailable) {
                        paletteMenuIndex = 2;
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
                        paletteMode = static_cast<DisplayPaletteMode>(paletteMenuIndex);
                        savePalettePreference(palettePath, paletteMode);
                        showPaletteMenu = false;
                        uiMessage = std::string("PALETA ") + displayPaletteUiName(paletteMode);
                        uiMessageFrames = 120;
                    } else if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_v) {
                        showPaletteMenu = false;
                    }
                    continue;
                }
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (ev.key.keysym.sym == SDLK_SPACE) {
                    paused = !paused;
                    if (audioEnabled) {
                        SDL_ClearQueuedAudio(audioDev);
                        audioRing.clear();
                    }
                    pausedAtomic.store(paused, std::memory_order_relaxed);
                    updateWindowTitle(window, gb.cartridge().title(), paused, muted);
                } else if (ev.key.keysym.sym == SDLK_p) {
                    muted = !muted;
                    if (audioEnabled && muted) {
                        SDL_ClearQueuedAudio(audioDev);
                        audioRing.clear();
                    }
                    updateWindowTitle(window, gb.cartridge().title(), paused, muted);
                } else if (ev.key.keysym.sym == SDLK_i) {
                    showPanel = !showPanel;
                    if (!showPanel) {
                        selectedSpriteAddr.reset();
                        memoryEdit.active = false;
                        breakpointEdit.active = false;
                        memorySearch.ui.visible = false;
                        memorySearch.ui.editingValue = false;
                        SDL_StopTextInput();
                    }
                } else if (ev.key.keysym.sym == SDLK_d) {
                    showBreakpointMenu = !showBreakpointMenu;
                    if (!showBreakpointMenu && breakpointEdit.active) {
                        breakpointEdit.active = false;
                        stopTextInputIfUnused();
                    }
                    uiMessage = showBreakpointMenu ? "BP MENU ON" : "BP MENU OFF";
                    uiMessageFrames = 90;
                } else if (ev.key.keysym.sym == SDLK_j) {
                    if (linkCableMode == LinkCableMode::Off) {
                        linkCableMode = LinkCableMode::Loopback;
                    } else if (linkCableMode == LinkCableMode::Loopback) {
                        linkCableMode = LinkCableMode::Noise;
                    } else {
                        linkCableMode = LinkCableMode::Off;
                    }
                    uiMessage = linkCableUiName(linkCableMode);
                    uiMessageFrames = 120;
                } else if (ev.key.keysym.sym == SDLK_h) {
                    if (filterMode == VideoFilterMode::None) {
                        filterMode = VideoFilterMode::Scanline;
                    } else if (filterMode == VideoFilterMode::Scanline) {
                        filterMode = VideoFilterMode::Lcd;
                    } else {
                        filterMode = VideoFilterMode::None;
                    }
                    saveFilterPreference(filtersPath, filterMode);
                    uiMessage = filterUiName(filterMode);
                    uiMessageFrames = 120;
                } else if (ev.key.keysym.sym == SDLK_F9) {
                    requestCapture = true;
                } else if (showPanel && ev.key.keysym.sym == SDLK_m) {
                    breakpointEdit.active = false;
                    memoryEdit.active = true;
                    memoryEdit.editAddress = true;
                    memoryEdit.addressHex.clear();
                    memoryEdit.valueHex.clear();
                    SDL_StartTextInput();
                } else if (showPanel && ev.key.keysym.sym == SDLK_LEFTBRACKET) {
                    memoryWatch.address = static_cast<gb::u16>(memoryWatch.address - 1);
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        resetMemoryWatch(memoryWatch, gb.bus());
                    }
                } else if (showPanel && ev.key.keysym.sym == SDLK_RIGHTBRACKET) {
                    memoryWatch.address = static_cast<gb::u16>(memoryWatch.address + 1);
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        resetMemoryWatch(memoryWatch, gb.bus());
                    }
                } else if (showPanel && ev.key.keysym.sym == SDLK_k) {
                    memoryWatch.freeze = !memoryWatch.freeze;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        memoryWatch.freezeValue = gb.bus().peek(memoryWatch.address);
                    }
                    uiMessage = memoryWatch.freeze ? "LOCK ON" : "LOCK OFF";
                    uiMessageFrames = 90;
                } else if (showPanel && ev.key.keysym.sym == SDLK_w) {
                    watchpointEnabled = !watchpointEnabled;
                    uiMessage = watchpointEnabled ? "WATCHPOINT ON" : "WATCHPOINT OFF";
                    uiMessageFrames = 90;
                } else if (showPanel && ev.key.keysym.sym == SDLK_b) {
                    gb::u16 pc = 0;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        pc = gb.cpu().regs().pc;
                    }
                    toggleBreakpoint(pc);
                } else if (showPanel && ev.key.keysym.sym == SDLK_EQUALS) {
                    gb::u8 current = 0;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        current = gb.bus().peek(memoryWatch.address);
                    }
                    const gb::u8 next = static_cast<gb::u8>(current + 1);
                    if (memoryWatch.freeze) {
                        memoryWatch.freezeValue = next;
                    }
                    queueMemoryWrite(memoryWatch.address, next, "INC");
                } else if (showPanel && ev.key.keysym.sym == SDLK_MINUS) {
                    gb::u8 current = 0;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        current = gb.bus().peek(memoryWatch.address);
                    }
                    const gb::u8 next = static_cast<gb::u8>(current - 1);
                    if (memoryWatch.freeze) {
                        memoryWatch.freezeValue = next;
                    }
                    queueMemoryWrite(memoryWatch.address, next, "DEC");
                } else if (showPanel && ev.key.keysym.sym == SDLK_0) {
                    if (memoryWatch.freeze) {
                        memoryWatch.freezeValue = 0x00;
                    }
                    queueMemoryWrite(memoryWatch.address, 0x00, "ZERO");
                } else if (paused && ev.key.keysym.sym == SDLK_LEFT) {
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        timeline.stepBack(gb);
                        enqueueRawFrameLocked();
                        resetMemoryWatch(memoryWatch, gb.bus());
                    }
                    if (audioEnabled) {
                        SDL_ClearQueuedAudio(audioDev);
                        audioRing.clear();
                    }
                    uiMessage = frameTimelineLabel(timeline);
                    uiMessageFrames = 120;
                } else if (paused && ev.key.keysym.sym == SDLK_RIGHT) {
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        if (!timeline.stepForward(gb)) {
                            gb.runFrame();
                            timeline.captureCurrent(gb);
                        }
                        enqueueRawFrameLocked();
                        resetMemoryWatch(memoryWatch, gb.bus());
                    }
                    if (audioEnabled) {
                        SDL_ClearQueuedAudio(audioDev);
                        audioRing.clear();
                    }
                    uiMessage = frameTimelineLabel(timeline);
                    uiMessageFrames = 120;
                } else if (ev.key.keysym.sym == SDLK_f) {
                    fullscreen = !fullscreen;
                    showScaleMenu = false;
                    showPaletteMenu = false;
                    if (memoryEdit.active) {
                        memoryEdit.active = false;
                    }
                    if (breakpointEdit.active) {
                        breakpointEdit.active = false;
                    }
                    memorySearch.ui.editingValue = false;
                    stopTextInputIfUnused();
                    Uint32 flags = 0;
                    if (fullscreen) {
                        showPanel = false;
                        memorySearch.ui.visible = false;
                        selectedSpriteAddr.reset();
                        flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
                    }

                    if (SDL_SetWindowFullscreen(window, flags) != 0) {
                        std::cerr << "falha ao alternar fullscreen: " << SDL_GetError() << "\n";
                        fullscreen = !fullscreen;
                    } else {
                        if (!fullscreen) {
                            SDL_SetWindowSize(window, gameWidth + panelWidth, gameHeight);
                            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                        }
                        uiMessage = fullscreen ? "FULLSCREEN ON" : "FULLSCREEN OFF";
                        uiMessageFrames = 120;
                    }
                } else if (ev.key.keysym.sym == SDLK_l && (ev.key.keysym.mod & KMOD_CTRL) == 0) {
                    uiMessage = "BACK TO MENU";
                    uiMessageFrames = 30;
                    backToMenu = true;
                    running = false;
                } else if (ev.key.keysym.sym == SDLK_TAB) {
                    fastForward = true;
                    fastForwardAtomic.store(true, std::memory_order_relaxed);
                    uiMessage = "FAST FORWARD";
                    uiMessageFrames = 60;
                } else if (ev.key.keysym.sym == SDLK_F3
                           || (ev.key.keysym.sym == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL))) {
                    bool saved = false;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        saved = gb.saveStateToFile(statePath);
                    }
                    if (saved) {
                        uiMessage = "STATE SAVED";
                        std::cout << "state salvo: " << statePath << "\n";
                    } else {
                        uiMessage = "SAVE FAIL";
                        std::cerr << "falha ao salvar state: " << statePath << "\n";
                    }
                    uiMessageFrames = 180;
                } else if (ev.key.keysym.sym == SDLK_F5
                           || (ev.key.keysym.sym == SDLK_l && (ev.key.keysym.mod & KMOD_CTRL))) {
                    bool loaded = false;
                    bool loadedLegacy = false;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        loaded = gb.loadStateFromFile(statePath);
                        if (!loaded) {
                            loadedLegacy = gb.loadStateFromFile(legacyStatePath);
                        }
                        if (loaded || loadedLegacy) {
                            timeline.reset(gb);
                            resetMemoryWatch(memoryWatch, gb.bus());
                            enqueueRawFrameLocked();
                        }
                    }
                    if (audioEnabled) {
                        SDL_ClearQueuedAudio(audioDev);
                        audioRing.clear();
                    }
                    if (loaded || loadedLegacy) {
                        uiMessage = "STATE LOADED";
                        if (loaded) {
                            std::cout << "state carregado: " << statePath << "\n";
                        }
                    } else {
                        uiMessage = "NO STATE";
                        std::cerr << "state nao encontrado: " << statePath << "\n";
                    }
                    uiMessageFrames = 180;
                } else {
                    std::lock_guard<std::mutex> gbLock(gbMutex);
                    setButtonFromKey(gb, ev.key.keysym.sym, true);
                }
            }
            if (ev.type == SDL_KEYUP) {
                if (showScaleMenu || showPaletteMenu || memoryEdit.active || breakpointEdit.active || memorySearch.ui.editingValue) {
                    continue;
                }
                if (ev.key.keysym.sym == SDLK_TAB) {
                    fastForward = false;
                    fastForwardAtomic.store(false, std::memory_order_relaxed);
                    uiMessage = "NORMAL SPEED";
                    uiMessageFrames = 45;
                    continue;
                }
                if (paused && (ev.key.keysym.sym == SDLK_LEFT || ev.key.keysym.sym == SDLK_RIGHT)) {
                    continue;
                }
                std::lock_guard<std::mutex> gbLock(gbMutex);
                setButtonFromKey(gb, ev.key.keysym.sym, false);
            }
            if (ev.type == SDL_TEXTINPUT) {
                if (memoryEdit.active) {
                    for (const char* p = ev.text.text; *p != '\0'; ++p) {
                        char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
                        const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
                        if (!isHex) {
                            continue;
                        }
                        std::string& field = memoryEdit.editAddress ? memoryEdit.addressHex : memoryEdit.valueHex;
                        const std::size_t maxLen = memoryEdit.editAddress ? 4 : 2;
                        if (field.size() < maxLen) {
                            field.push_back(ch);
                        }
                    }
                    continue;
                }
                if (breakpointEdit.active) {
                    for (const char* p = ev.text.text; *p != '\0'; ++p) {
                        char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
                        const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
                        if (!isHex) {
                            continue;
                        }
                        if (breakpointEdit.addressHex.size() < 4) {
                            breakpointEdit.addressHex.push_back(ch);
                        }
                    }
                    continue;
                }
                if (memorySearch.ui.editingValue) {
                    for (const char* p = ev.text.text; *p != '\0'; ++p) {
                        char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
                        const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
                        if (!isHex) {
                            continue;
                        }
                        if (memorySearch.ui.valueHex.size() < 2) {
                            memorySearch.ui.valueHex.push_back(ch);
                        }
                    }
                    continue;
                }
            }
            if (ev.type == SDL_MOUSEWHEEL && showPanel) {
                int mx = 0;
                int my = 0;
                SDL_GetMouseState(&mx, &my);
                int outputW = 0;
                int outputH = 0;
                SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                const int panelX = outputW - panelWidth;
                if (mx >= panelX) {
                    if (memorySearch.ui.visible) {
                        const int overlayX = panelX + 10;
                        const int overlayY = kSearchOverlayTop;
                        const int overlayW = panelWidth - 20;
                        const int listY = overlayY + kSearchListYOffset;
                        const int listLines = searchVisibleLinesForPanel(outputH);
                        const int listH = listLines * kSearchListLineHeight;
                        if (mx >= overlayX && mx < overlayX + overlayW && my >= listY && my < listY + listH) {
                            const int maxScroll = std::max(0, static_cast<int>(memorySearch.ui.matches.size()) - listLines);
                            memorySearch.ui.scroll = std::clamp(memorySearch.ui.scroll - ev.wheel.y, 0, maxScroll);
                            continue;
                        }
                    }
                    const int spriteY = spriteListYFromLayout(outputH, showBreakpointMenu);
                    if (my >= spriteY) {
                        std::vector<SpriteDebugRow> sprites;
                        {
                            std::lock_guard<std::mutex> gbLock(gbMutex);
                            sprites = snapshotSprites(gb.bus());
                        }
                        const int maxRows = spriteVisibleLinesForPanel(outputH, showBreakpointMenu);
                        const int maxScrollRows = std::max(0, static_cast<int>(sprites.size()) - maxRows);
                        spriteScrollRows = std::clamp(spriteScrollRows - ev.wheel.y, 0, maxScrollRows);
                    }
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && showPanel) {
                const int mx = ev.button.x;
                const int my = ev.button.y;
                int outputW = 0;
                int outputH = 0;
                SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                const int panelX = outputW - panelWidth;
                if (mx >= panelX) {
                    bool consumedBySearch = false;
                    if (memorySearch.ui.visible) {
                        const int overlayX = panelX + 10;
                        const int overlayY = kSearchOverlayTop;
                        const int overlayW = panelWidth - 20;
                        const int overlayH = std::max(80, outputH - overlayY - kSearchOverlayBottomPad);
                        const int innerX = overlayX + 8;
                        const int modeY = overlayY + 18;
                        const int valueY = overlayY + 30;
                        const int buttonsY = overlayY + 54;
                        const int listY = overlayY + kSearchListYOffset;
                        const int listLines = searchVisibleLinesForPanel(outputH);

                        if (mx >= overlayX && mx < overlayX + overlayW && my >= overlayY && my < overlayY + overlayH) {
                            if (my >= modeY && my < modeY + 10) {
                                const int modeW = (overlayW - 16) / 5;
                                const int idx = std::clamp((mx - innerX) / std::max(1, modeW), 0, 4);
                                memorySearch.ui.mode = static_cast<MemorySearchMode>(idx);
                                consumedBySearch = true;
                            } else if (my >= valueY && my < valueY + 10) {
                                memoryEdit.active = false;
                                breakpointEdit.active = false;
                                memorySearch.ui.editingValue = true;
                                SDL_StartTextInput();
                                consumedBySearch = true;
                            } else if (my >= buttonsY && my < buttonsY + 10) {
                                const int relX = mx - innerX;
                                if (relX >= 0 && relX < 30) {
                                    runMemorySearch();
                                } else if (relX >= 36 && relX < 72) {
                                    captureSearchSnapshot();
                                } else if (relX >= 78 && relX < 108) {
                                    clearSearchResults();
                                    uiMessage = "SEARCH CLEAR";
                                    uiMessageFrames = 90;
                                }
                                consumedBySearch = true;
                            } else if (my >= listY && my < listY + listLines * kSearchListLineHeight) {
                                const int idx = memorySearch.ui.scroll + (my - listY) / kSearchListLineHeight;
                                if (idx >= 0 && idx < static_cast<int>(memorySearch.ui.matches.size())) {
                                    memoryWatch.address = memorySearch.ui.matches[static_cast<std::size_t>(idx)];
                                    {
                                        std::lock_guard<std::mutex> gbLock(gbMutex);
                                        resetMemoryWatch(memoryWatch, gb.bus());
                                    }
                                    uiMessage = "WATCH FROM SEARCH";
                                    uiMessageFrames = 90;
                                }
                                consumedBySearch = true;
                            }
                        }
                    }
                    if (consumedBySearch) {
                        continue;
                    }

                    if (showBreakpointMenu) {
                        const int menuX = panelX + 12;
                        const int menuW = panelWidth - 24;
                        if (mx >= menuX && mx < menuX + menuW) {
                            if (my >= kBreakpointRowYWatch && my < kBreakpointRowYWatch + kBreakpointRowHeight) {
                                watchpointEnabled = !watchpointEnabled;
                                uiMessage = watchpointEnabled ? "WATCHPOINT ON" : "WATCHPOINT OFF";
                                uiMessageFrames = 90;
                            } else if (my >= kBreakpointRowYPc && my < kBreakpointRowYPc + kBreakpointRowHeight) {
                                gb::u16 pc = 0;
                                {
                                    std::lock_guard<std::mutex> gbLock(gbMutex);
                                    pc = gb.cpu().regs().pc;
                                }
                                toggleBreakpoint(pc);
                            } else if (my >= kBreakpointRowYAddr && my < kBreakpointRowYAddr + kBreakpointRowHeight) {
                                memoryEdit.active = false;
                                breakpointEdit.active = true;
                                breakpointEdit.addressHex.clear();
                                SDL_StartTextInput();
                            } else if (my >= kBreakpointListStartY
                                       && my < kBreakpointListStartY + kBreakpointListMaxVisible * kBreakpointListLineHeight) {
                                const int row = (my - kBreakpointListStartY) / kBreakpointListLineHeight;
                                {
                                    std::lock_guard<std::mutex> bpLock(breakpointsMutex);
                                    if (row >= 0 && row < static_cast<int>(breakpoints.size())) {
                                        const gb::u16 addr = breakpoints[static_cast<std::size_t>(row)];
                                        breakpoints.erase(breakpoints.begin() + row);
                                        char msg[40];
                                        std::snprintf(msg, sizeof(msg), "BP DEL %04X", addr);
                                        uiMessage = msg;
                                        uiMessageFrames = 120;
                                    }
                                }
                            }
                        }
                    }

                    const int readStartY = readStartYFromLayout(outputH, showBreakpointMenu);
                    std::vector<gb::Bus::MemoryReadEvent> readsNow;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        readsNow = gb.bus().snapshotRecentReads(128);
                    }
                    const int readMaxLines = readVisibleLinesForPanel(outputH, showBreakpointMenu);
                    const int readCount = static_cast<int>(std::min<std::size_t>(readsNow.size(), static_cast<std::size_t>(readMaxLines)));
                    if (my >= readStartY && my < readStartY + readCount * kReadLineHeight) {
                        const int readIdx = (my - readStartY) / kReadLineHeight;
                        if (readIdx >= 0 && readIdx < static_cast<int>(readsNow.size())) {
                            memoryWatch.address = readsNow[static_cast<std::size_t>(readIdx)].address;
                            {
                                std::lock_guard<std::mutex> gbLock(gbMutex);
                                resetMemoryWatch(memoryWatch, gb.bus());
                            }
                        }
                    }

                    std::vector<SpriteDebugRow> sprites;
                    {
                        std::lock_guard<std::mutex> gbLock(gbMutex);
                        sprites = snapshotSprites(gb.bus());
                    }
                    const int spriteY = spriteListYFromLayout(outputH, showBreakpointMenu);
                    const int spriteMaxLines = spriteVisibleLinesForPanel(outputH, showBreakpointMenu);
                    const int maxScrollRows = std::max(0, static_cast<int>(sprites.size()) - spriteMaxLines);
                    spriteScrollRows = std::clamp(spriteScrollRows, 0, maxScrollRows);
                    const int spriteCount = std::max(0, std::min(spriteMaxLines, static_cast<int>(sprites.size()) - spriteScrollRows));

                    if (my >= spriteY && my < spriteY + spriteCount * kSpriteLineHeight) {
                        const int idx = spriteScrollRows + (my - spriteY) / kSpriteLineHeight;
                        if (idx >= 0 && idx < static_cast<int>(sprites.size())) {
                            selectedSpriteAddr = sprites[static_cast<std::size_t>(idx)].addr;
                        }
                    }
                }
            }
        }

        syncThreadState();

        QueuedMemoryWrite writeToApply{};
        {
            std::lock_guard<std::mutex> writeLock(queuedWriteMutex);
            if (queuedWrite.active) {
                writeToApply = queuedWrite;
                queuedWrite.active = false;
            }
        }
        if (writeToApply.active) {
            applyWriteNow(writeToApply.address, writeToApply.value, writeToApply.source, true);
        }
        if (paused) {
            applyFrameLockIfNeeded();
        }

        {
            std::lock_guard<std::mutex> gbLock(gbMutex);
            sampleMemoryWatch(memoryWatch, gb.bus());
        }

        RgbFramePacket latestFrame{};
        if (rgbFrameQueue.tryPopLatest(latestFrame)) {
            pixels = latestFrame.pixels;
        }
        if (requestCapture) {
            const std::string capturePath = nextCapturePath(captureDir);
            if (saveRgb24Ppm(capturePath, pixels)) {
                uiMessage = "CAPTURE SAVED";
                std::cout << "capture salva: " << capturePath << "\n";
            } else {
                uiMessage = "CAPTURE FAIL";
            }
            uiMessageFrames = 150;
            requestCapture = false;
        }

        SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 3);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        int outputW = 0;
        int outputH = 0;
        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
        const BlitLayout blit = computeGameBlitLayout(outputW, outputH, width, height, panelWidth, showPanel, fullscreen, fullscreenMode);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
        if (sharpTexture) {
            SDL_SetTextureScaleMode(sharpTexture, SDL_ScaleModeNearest);
        }
#endif
        SDL_Texture* renderTexture = texture;
        if (fullscreen
            && !showPanel
            && fullscreenMode == FullscreenScaleMode::FullStretchSharp
            && sharpTexture) {
            applySharpenRgb24(pixels, sharpPixels);
            SDL_UpdateTexture(sharpTexture, nullptr, sharpPixels.data(), width * 3);
            renderTexture = sharpTexture;
        }
        SDL_RenderCopy(renderer, renderTexture, nullptr, &blit.gameDst);
        if (uiMessageFrames > 0) {
            SDL_SetRenderDrawColor(renderer, 20, 20, 20, 180);
            SDL_Rect msgBg{blit.gameDst.x + 8, blit.gameDst.y + 8, 160, 20};
            SDL_RenderFillRect(renderer, &msgBg);
            drawHexText(renderer, blit.gameDst.x + 12, blit.gameDst.y + 12, uiMessage, SDL_Color{255, 230, 120, 255}, 1);
            --uiMessageFrames;
        }
        char statusLine[84];
        std::snprintf(
            statusLine,
            sizeof(statusLine),
            "%s %s%s%s",
            linkCableUiName(linkCableMode),
            filterUiName(filterMode),
            watchpointEnabled ? " WP" : "",
            fastForward ? " FF" : ""
        );
        drawHexText(
            renderer,
            blit.gameDst.x + 12,
            blit.gameDst.y + blit.gameDst.h - 12,
            statusLine,
            SDL_Color{178, 188, 214, 255},
            1
        );
        if (showPanel) {
            std::vector<gb::u16> breakpointsSnapshot;
            {
                std::lock_guard<std::mutex> bpLock(breakpointsMutex);
                breakpointsSnapshot = breakpoints;
            }
            {
                std::lock_guard<std::mutex> gbLock(gbMutex);
                const auto reads = gb.bus().snapshotRecentReads(128);
                const auto sprites = snapshotSprites(gb.bus());
                const int spriteMaxLines = spriteVisibleLinesForPanel(outputH, showBreakpointMenu);
                const int maxScrollRows = std::max(0, static_cast<int>(sprites.size()) - spriteMaxLines);
                spriteScrollRows = std::clamp(spriteScrollRows, 0, maxScrollRows);
                const auto selectedSprite = findSelectedSprite(sprites, selectedSpriteAddr);
                const int overlayScale = std::max(1, blit.gameDst.w / width);
                drawSelectedSpriteOverlay(renderer, gb.bus(), selectedSprite, overlayScale, blit.gameDst.x, blit.gameDst.y);
                const auto& regs = gb.cpu().regs();
                const gb::u16 execPc = gb.cpu().lastExecutedPc();
                const gb::u8 execOp = gb.cpu().lastExecutedOpcode();
                const gb::u16 nextPc = regs.pc;
                const gb::u8 nextOp = gb.bus().peek(nextPc);
                const auto disasmLines = buildDisasmWindow(gb.bus(), nextPc, 3);
                drawMemoryPanel(
                    renderer,
                    outputW - panelWidth,
                    panelWidth,
                    outputH,
                    reads,
                    sprites,
                    spriteScrollRows,
                    memoryWatch,
                    memoryWriteUi,
                    memorySearch.ui,
                    disasmLines,
                    showBreakpointMenu,
                    watchpointEnabled,
                    breakpointsSnapshot,
                    breakpointEdit.addressHex,
                    breakpointEdit.active,
                    selectedSpriteAddr,
                    gb.bus(),
                    execPc,
                    execOp,
                    nextPc,
                    nextOp,
                    debugFps,
                    paused,
                    muted
                );
                drawMemoryEditOverlay(renderer, outputW - panelWidth, panelWidth, memoryEdit);
            }
        }
        if (showScaleMenu && fullscreen) {
            drawFullscreenScaleMenu(renderer, outputW, outputH, scaleMenuIndex);
        }
        if (showPaletteMenu) {
            drawPaletteModeMenu(renderer, outputW, outputH, paletteMenuIndex, cgbPaletteAvailable);
        }
        SDL_RenderPresent(renderer);

    }

    mtRunning.store(false, std::memory_order_relaxed);
    pausedAtomic.store(true, std::memory_order_relaxed);
    audioGateAtomic.store(false, std::memory_order_relaxed);
    rawFrameQueue.close();
    rgbFrameQueue.close();
    audioRing.close();
    if (emuThread.joinable()) {
        emuThread.join();
    }
    if (renderThread.joinable()) {
        renderThread.join();
    }
    if (audioThread.joinable()) {
        audioThread.join();
    }
    std::cout << "[MT] workers encerrados\n";

    SDL_DestroyTexture(texture);
    if (sharpTexture) {
        SDL_DestroyTexture(sharpTexture);
    }
    SDL_StopTextInput();
    if (audioEnabled) {
        SDL_ClearQueuedAudio(audioDev);
        SDL_CloseAudioDevice(audioDev);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (gb.saveBatteryRamToFile(batteryRamPath)) {
        std::cout << "save interno gravado: " << batteryRamPath << "\n";
    }
    if (gb.saveRtcToFile(rtcPath)) {
        std::cout << "rtc gravado: " << rtcPath << "\n";
    }
    savePalettePreference(palettePath, paletteMode);
    saveFilterPreference(filtersPath, filterMode);

    return backToMenu ? 2 : 0;
}
#endif

} // namespace gb::frontend
