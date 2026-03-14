#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "gb/app/frontend/realtime/cheat_engine.hpp"
#include "gb/app/frontend/realtime/control_bindings.hpp"
#include "gb/app/frontend/debug_ui.hpp"
#include "gb/app/frontend/realtime/audio_ring_buffer.hpp"
#include "gb/app/frontend/realtime/dropping_queue.hpp"
#include "gb/app/frontend/realtime/frame_timeline.hpp"
#include "gb/app/frontend/realtime/link_transport.hpp"
#include "gb/app/frontend/realtime/network_config.hpp"
#include "gb/app/frontend/realtime/replay_io.hpp"
#include "gb/app/frontend/realtime/session_models.hpp"
#include "gb/app/frontend/realtime/save_slots.hpp"
#include "gb/app/frontend/realtime/timing_policy.hpp"
#include "gb/app/frontend/realtime/top_menu.hpp"
#include "gb/app/frontend/realtime.hpp"
#include "gb/app/frontend/realtime_support.hpp"

#ifdef GBEMU_USE_SDL2
#include "gb/app/sdl_compat.hpp"
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
    const std::string& controlsPath,
    const std::string& cheatsPath,
    const std::string& palettePath,
    const std::string& rtcPath,
    const std::string& replayPath,
    const std::string& filtersPath,
    const std::string& captureDir,
    const std::string& linkConnect,
    int linkHostPort,
    const std::string& netplayConnect,
    int netplayHostPort,
    int netplayDelayFrames
) {
    (void)replayPath;
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
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
        "Orion Boy",
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

    SDL_GameController* gamepad = nullptr;
    const int joysticks = SDL_NumJoysticks();
    for (int i = 0; i < joysticks; ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        gamepad = SDL_GameControllerOpen(i);
        if (gamepad) {
            break;
        }
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
    bool showControlsMenu = false;
    bool showTopMenuBar = true;
    bool requestCapture = false;
    FullscreenScaleMode fullscreenMode = FullscreenScaleMode::CrispFit;
    LinkCableMode linkCableMode = LinkCableMode::Off;
    VideoFilterMode filterMode = VideoFilterMode::None;
    int scaleMenuIndex = 0;
    int controlsMenuIndex = 0;
    bool controlsAwaitKey = false;
    bool controlsEditPad = false;
    int activeSaveSlot = 0;
    bool cheatsEnabled = true;
    bool replayRecording = false;
    bool replayPlaying = false;
    std::size_t replayCursor = 0;
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
    ControlBindings controls = defaultControlBindings();
    const std::string globalControlsPath = (std::filesystem::path("states") / "global.controls").string();
    (void)loadControlBindingsWithFallback(controlsPath, globalControlsPath, controls);
    auto cheatsLoad = loadCheatsFromFile(cheatsPath);
    std::vector<CheatCode> cheats = std::move(cheatsLoad.cheats);
    if (!cheatsLoad.errors.empty()) {
        std::cerr << "aviso: erros ao carregar cheats (" << cheatsPath << ")\n";
        for (const auto& err : cheatsLoad.errors) {
            std::cerr << "  - " << err << "\n";
        }
    }
    ReplayData replayData{};
    replayData.version = 1;
    replayData.seed = 0;
    UdpLinkTransport linkTransport{};
    bool socketLinkAvailable = false;
    if (linkHostPort > 0) {
        socketLinkAvailable = linkTransport.openHost(static_cast<std::uint16_t>(linkHostPort));
    } else if (!linkConnect.empty()) {
        if (const auto ep = parseLinkEndpoint(linkConnect); ep.has_value()) {
            socketLinkAvailable = linkTransport.openClient(ep->host, ep->port);
        }
    }
    UdpLinkTransport netplayTransport{};
    bool netplayEnabled = false;
    if (netplayHostPort > 0) {
        netplayEnabled = netplayTransport.openHost(static_cast<std::uint16_t>(netplayHostPort));
    } else if (!netplayConnect.empty()) {
        if (const auto ep = parseLinkEndpoint(netplayConnect); ep.has_value()) {
            netplayEnabled = netplayTransport.openClient(ep->host, ep->port);
        }
    }
    struct NetplayFrameRecord {
        std::uint64_t frame = 0;
        gb::GameBoy::SaveState preState{};
        std::uint8_t localInput = 0;
        std::uint8_t remoteInput = 0;
        bool predicted = false;
    };
    constexpr std::size_t kNetplayHistoryLimit = 180;
    const std::string networkConfigPath = (std::filesystem::path("states") / "global.network").string();
    int configuredNetplayDelay = std::clamp(netplayDelayFrames, 0, 10);
    const auto savedNetwork = loadNetworkFrontendConfig(networkConfigPath);
    if (configuredNetplayDelay == 0 && savedNetwork.has_value()) {
        configuredNetplayDelay = std::clamp(savedNetwork->netplayDelayFrames, 0, 10);
    }
    std::atomic<int> netplayDelayAtomic{configuredNetplayDelay};
    std::deque<std::uint8_t> localInputDelayQueue{};
    for (int i = 0; i < configuredNetplayDelay; ++i) {
        localInputDelayQueue.push_back(0);
    }
    std::deque<NetplayFrameRecord> netplayHistory{};
    std::unordered_map<std::uint64_t, std::uint8_t> netplayAuthoritativeInputs{};
    std::unordered_map<std::uint64_t, std::uint32_t> localFrameChecksums{};
    std::uint64_t netplayRollbackCount = 0;
    std::uint64_t netplayDesyncCount = 0;
    std::uint64_t netplayPredictedCount = 0;
    if (socketLinkAvailable) {
        linkCableMode = LinkCableMode::Socket;
    }
    if (savedNetwork.has_value()) {
        const int savedMode = std::clamp(savedNetwork->linkMode, 0, 3);
        if (savedMode == 0) {
            linkCableMode = LinkCableMode::Off;
        } else if (savedMode == 1) {
            linkCableMode = LinkCableMode::Loopback;
        } else if (savedMode == 2) {
            linkCableMode = LinkCableMode::Noise;
        } else if (savedMode == 3 && socketLinkAvailable) {
            linkCableMode = LinkCableMode::Socket;
        }
    }
    int paletteMenuIndex = static_cast<int>(paletteMode);
    FrameTimeline timeline(gb);
    std::vector<gb::u16> breakpoints{};
    breakpoints.reserve(16);
    BreakpointEditState breakpointEdit{};
    auto memorySearchStorage = std::make_unique<MemorySearchState>();
    MemorySearchState& memorySearch = *memorySearchStorage;
    std::optional<gb::u16> selectedSpriteAddr;
    int spriteScrollRows = 0;
    std::optional<TopMenuSection> openTopMenuSection;
    std::optional<TopMenuSection> hoveredTopMenuSection;
    int hoveredTopMenuItem = -1;
    MemoryWatch memoryWatch{};
    MemoryEditState memoryEdit{};
    MemoryWriteUiState memoryWriteUi{};
    QueuedMemoryWrite queuedWrite{};
    std::atomic<std::uint64_t> emulatedFrameCounter{0};
    std::string uiMessage;
    int uiMessageFrames = 0;
    auto pixelsStorage = std::make_unique<RgbFrame>();
    auto sharpPixelsStorage = std::make_unique<RgbFrame>();
    RgbFrame& pixels = *pixelsStorage;
    RgbFrame& sharpPixels = *sharpPixelsStorage;
    std::mutex gbMutex{};
    std::mutex breakpointsMutex{};
    std::mutex queuedWriteMutex{};
    std::mutex replayMutex{};
    std::atomic<bool> mtRunning{true};
    std::atomic<bool> pausedAtomic{paused};
    std::atomic<bool> fastForwardAtomic{fastForward};
    std::atomic<bool> cheatsEnabledAtomic{cheatsEnabled};
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
        cheatsEnabledAtomic.store(cheatsEnabled, std::memory_order_relaxed);
        watchAddressAtomic.store(memoryWatch.address, std::memory_order_relaxed);
        watchFreezeAtomic.store(memoryWatch.freeze, std::memory_order_relaxed);
        watchFreezeValueAtomic.store(memoryWatch.freezeValue, std::memory_order_relaxed);
        linkCableModeAtomic.store(static_cast<int>(linkCableMode), std::memory_order_relaxed);
        paletteModeAtomic.store(static_cast<int>(paletteMode), std::memory_order_relaxed);
        filterModeAtomic.store(static_cast<int>(filterMode), std::memory_order_relaxed);
        audioGateAtomic.store(audioEnabled && !muted && !paused && !fastForward, std::memory_order_relaxed);
    };

    const auto enqueueRawFrameLocked = [&]() {
        auto packet = std::make_unique<RawFramePacket>();
        packet->sequence = frameSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        packet->mono = gb.ppu().framebuffer();
        packet->color = gb.ppu().colorFramebuffer();
        rawFrameQueue.push(std::move(*packet));
    };

    const auto joypadMaskFromState = [](const gb::Joypad::State& state) -> std::uint8_t {
        return packButtons(
            state.pressed[0],
            state.pressed[1],
            state.pressed[2],
            state.pressed[3],
            state.pressed[4],
            state.pressed[5],
            state.pressed[6],
            state.pressed[7]
        );
    };

    const auto applyJoypadMask = [](gb::Joypad& joypad, std::uint8_t mask) {
        joypad.setButton(gb::Button::Right, (mask & (1u << 0)) != 0);
        joypad.setButton(gb::Button::Left, (mask & (1u << 1)) != 0);
        joypad.setButton(gb::Button::Up, (mask & (1u << 2)) != 0);
        joypad.setButton(gb::Button::Down, (mask & (1u << 3)) != 0);
        joypad.setButton(gb::Button::A, (mask & (1u << 4)) != 0);
        joypad.setButton(gb::Button::B, (mask & (1u << 5)) != 0);
        joypad.setButton(gb::Button::Select, (mask & (1u << 6)) != 0);
        joypad.setButton(gb::Button::Start, (mask & (1u << 7)) != 0);
    };
    const auto frameChecksum = [](const gb::GameBoy& gameBoy) -> std::uint32_t {
        std::uint32_t hash = 2166136261u; // FNV-1a seed
        const auto& fb = gameBoy.ppu().framebuffer();
        for (const auto px : fb) {
            hash ^= static_cast<std::uint32_t>(px);
            hash *= 16777619u;
        }
        const auto& regs = gameBoy.cpu().regs();
        const std::uint16_t pc = regs.pc;
        const std::uint16_t sp = regs.sp;
        hash ^= static_cast<std::uint32_t>(pc);
        hash *= 16777619u;
        hash ^= static_cast<std::uint32_t>(sp);
        hash *= 16777619u;
        return hash;
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

    const auto saveStateToActiveSlot = [&]() {
        const std::string slotStatePath = saveSlotStatePath(statePath, activeSaveSlot);
        const std::string slotMetaPath = saveSlotMetaPath(statePath, activeSaveSlot);
        const std::string slotThumbPath = saveSlotThumbnailPath(statePath, activeSaveSlot);
        bool saved = false;
        {
            std::lock_guard<std::mutex> gbLock(gbMutex);
            saved = gb.saveStateToFile(slotStatePath);
        }
        if (saved) {
            SaveSlotMeta meta{};
            meta.slot = activeSaveSlot;
            meta.title = gb.cartridge().title();
            meta.timestamp = nowIso8601Local();
            meta.frame = emulatedFrameCounter.load(std::memory_order_relaxed);
            writeSaveSlotMeta(slotMetaPath, meta);
            saveRgb24Ppm(slotThumbPath, pixels);

            char msg[40];
            std::snprintf(msg, sizeof(msg), "STATE SAVED S%d", activeSaveSlot);
            uiMessage = msg;
            std::cout << "state salvo: " << slotStatePath << "\n";
        } else {
            uiMessage = "SAVE FAIL";
            std::cerr << "falha ao salvar state slot: " << slotStatePath << "\n";
        }
        uiMessageFrames = 180;
    };

    const auto loadStateFromActiveSlot = [&]() {
        const std::string slotStatePath = saveSlotStatePath(statePath, activeSaveSlot);
        bool loaded = false;
        bool loadedLegacy = false;
        {
            std::lock_guard<std::mutex> gbLock(gbMutex);
            loaded = gb.loadStateFromFile(slotStatePath);
            if (!loaded && activeSaveSlot == 0) {
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
            char msg[40];
            std::snprintf(msg, sizeof(msg), "STATE LOADED S%d", activeSaveSlot);
            uiMessage = msg;
            if (loaded) {
                std::cout << "state carregado: " << slotStatePath << "\n";
            }
        } else {
            uiMessage = "NO STATE";
            std::cerr << "state nao encontrado: " << slotStatePath << "\n";
        }
        uiMessageFrames = 180;
    };

    const auto togglePauseState = [&]() {
        paused = !paused;
        if (audioEnabled) {
            SDL_ClearQueuedAudio(audioDev);
            audioRing.clear();
        }
        pausedAtomic.store(paused, std::memory_order_relaxed);
        updateWindowTitle(window, gb.cartridge().title(), paused, muted);
    };

    const auto toggleMutedState = [&]() {
        muted = !muted;
        if (audioEnabled && muted) {
            SDL_ClearQueuedAudio(audioDev);
            audioRing.clear();
        }
        updateWindowTitle(window, gb.cartridge().title(), paused, muted);
    };

    const auto toggleDebugPanelState = [&]() {
        showPanel = !showPanel;
        if (!showPanel) {
            selectedSpriteAddr.reset();
            memoryEdit.active = false;
            breakpointEdit.active = false;
            memorySearch.ui.visible = false;
            memorySearch.ui.editingValue = false;
            SDL_StopTextInput();
        }
    };

    const auto toggleBreakpointPanelState = [&]() {
        showBreakpointMenu = !showBreakpointMenu;
        if (!showBreakpointMenu && breakpointEdit.active) {
            breakpointEdit.active = false;
            stopTextInputIfUnused();
        }
        uiMessage = showBreakpointMenu ? "BP MENU ON" : "BP MENU OFF";
        uiMessageFrames = 90;
    };

    const auto toggleFullscreenState = [&]() {
        fullscreen = !fullscreen;
        showScaleMenu = false;
        showPaletteMenu = false;
        showControlsMenu = false;
        controlsAwaitKey = false;
        openTopMenuSection.reset();
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
        auto raw = std::make_unique<RawFramePacket>();
        auto latest = std::make_unique<RawFramePacket>();
        auto pending = std::make_unique<RawFramePacket>();
        while (rawFrameQueue.waitPop(*raw)) {
            *latest = std::move(*raw);
            while (rawFrameQueue.tryPopLatest(*pending)) {
                *latest = std::move(*pending);
            }

            auto out = std::make_unique<RgbFramePacket>();
            out->sequence = latest->sequence;
            const auto paletteModeLocal = static_cast<DisplayPaletteMode>(paletteModeAtomic.load(std::memory_order_relaxed));
            const auto filterModeLocal = static_cast<VideoFilterMode>(filterModeAtomic.load(std::memory_order_relaxed));
            const bool useCgbPalette = cgbPaletteAvailable && paletteModeLocal == DisplayPaletteMode::GameBoyColor;

            if (useCgbPalette) {
                for (std::size_t i = 0; i < latest->color.size(); ++i) {
                    const gb::u16 c = latest->color[i];
                    const gb::u8 r5 = static_cast<gb::u8>((c >> 0) & 0x1F);
                    const gb::u8 g5 = static_cast<gb::u8>((c >> 5) & 0x1F);
                    const gb::u8 b5 = static_cast<gb::u8>((c >> 10) & 0x1F);
                    out->pixels[i * 3 + 0] = static_cast<unsigned char>((r5 * 255) / 31);
                    out->pixels[i * 3 + 1] = static_cast<unsigned char>((g5 * 255) / 31);
                    out->pixels[i * 3 + 2] = static_cast<unsigned char>((b5 * 255) / 31);
                }
            } else {
                const auto& palette = monoPalette(paletteModeLocal);
                for (std::size_t i = 0; i < latest->mono.size(); ++i) {
                    const std::size_t shade = static_cast<std::size_t>(latest->mono[i] & 0x03);
                    out->pixels[i * 3 + 0] = palette[shade][0];
                    out->pixels[i * 3 + 1] = palette[shade][1];
                    out->pixels[i * 3 + 2] = palette[shade][2];
                }
            }

            applyVideoFilterRgb24(filterModeLocal, out->pixels);
            rgbFrameQueue.push(std::move(*out));
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
        auto nextFrame = std::chrono::steady_clock::now();
        auto lastLog = std::chrono::steady_clock::now();
        bool lastFastForward = fastForwardAtomic.load(std::memory_order_relaxed);
        std::uint64_t logFrames = 0;
        std::size_t audioDropLogs = 0;

        while (mtRunning.load(std::memory_order_relaxed)) {
            if (pausedAtomic.load(std::memory_order_relaxed)) {
                nextFrame = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const bool ff = fastForwardAtomic.load(std::memory_order_relaxed);
            if (ff != lastFastForward) {
                nextFrame = std::chrono::steady_clock::now();
                lastFastForward = ff;
            }

            const auto frameBudget = emulationFrameBudget(ff);
            const int framesToRun = emulationFramesPerTick(ff);
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
                    const auto processSerialTransferLocked = [&]() {
                        gb::u8 outData = 0;
                        while (gb.bus().consumeSerialTransfer(outData)) {
                            gb::u8 inData = 0xFF;
                            const auto mode = static_cast<LinkCableMode>(linkCableModeAtomic.load(std::memory_order_relaxed));
                            if (mode == LinkCableMode::Loopback) {
                                inData = outData;
                            } else if (mode == LinkCableMode::Noise) {
                                inData = static_cast<gb::u8>(emuRandByte(emuRng));
                            } else if (mode == LinkCableMode::Socket) {
                                std::uint8_t remote = 0xFF;
                                if (linkTransport.exchangeSerialByte(outData, remote)) {
                                    inData = static_cast<gb::u8>(remote);
                                }
                            }
                            gb.bus().completeSerialTransfer(inData);
                        }
                    };
                    const auto runOneFrameLocked = [&]() {
                        if (gb.preciseTiming()) {
                            bool seenVblank = false;
                            gb::u32 elapsed = 0;
                            constexpr gb::u32 kMaxGuardCycles = 70224u * 3u;
                            while (elapsed < kMaxGuardCycles) {
                                elapsed += gb.step();
                                processSerialTransferLocked();
                                const gb::u8 ly = gb.bus().peek(0xFF44);
                                if (!seenVblank && ly >= 144) {
                                    seenVblank = true;
                                } else if (seenVblank && ly < 144) {
                                    break;
                                }
                            }
                            return;
                        }
                        gb::u32 elapsed = 0;
                        constexpr gb::u32 kFrameCycles = 70224u;
                        while (elapsed < kFrameCycles) {
                            elapsed += gb.step();
                            processSerialTransferLocked();
                        }
                    };

                    if (watchEnabled) {
                        watchBefore = gb.bus().peek(watchAddr);
                    }

                    const std::uint64_t frameId = emulatedFrameCounter.load(std::memory_order_relaxed);
                    std::uint8_t localInputMask = joypadMaskFromState(gb.joypad().state());
                    std::uint8_t localAppliedMask = localInputMask;
                    bool replayPlayingNow = false;
                    std::unique_ptr<NetplayFrameRecord> netplayFrameRecord;
                    {
                        std::lock_guard<std::mutex> replayLock(replayMutex);
                        replayPlayingNow = replayPlaying;
                        if (replayPlaying) {
                            if (replayCursor < replayData.frameInputs.size()) {
                                localInputMask = replayData.frameInputs[replayCursor++];
                                applyJoypadMask(gb.joypad(), localInputMask);
                            } else {
                                replayPlaying = false;
                            }
                        }
                    }
                    if (netplayEnabled && !replayPlayingNow) {
                        const int desiredDelay = std::clamp(
                            netplayDelayAtomic.load(std::memory_order_relaxed),
                            0,
                            10
                        );
                        while (static_cast<int>(localInputDelayQueue.size()) < desiredDelay) {
                            localInputDelayQueue.push_back(0);
                        }
                        while (static_cast<int>(localInputDelayQueue.size()) > desiredDelay) {
                            localInputDelayQueue.pop_front();
                        }

                        if (desiredDelay > 0) {
                            localInputDelayQueue.push_back(localInputMask);
                            localAppliedMask = localInputDelayQueue.front();
                            localInputDelayQueue.pop_front();
                        } else {
                            localAppliedMask = localInputMask;
                        }

                        std::uint8_t remoteMask = 0;
                        bool predicted = false;
                        const auto authIt = netplayAuthoritativeInputs.find(frameId);
                        if (authIt != netplayAuthoritativeInputs.end()) {
                            remoteMask = authIt->second;
                            predicted = false;
                            netplayAuthoritativeInputs.erase(authIt);
                            std::uint8_t ignoredRemote = 0;
                            bool ignoredPredicted = false;
                            (void)netplayTransport.exchangeNetplayInput(
                                frameId,
                                localAppliedMask,
                                ignoredRemote,
                                ignoredPredicted
                            );
                        } else {
                            (void)netplayTransport.exchangeNetplayInput(frameId, localAppliedMask, remoteMask, predicted);
                        }
                        if (predicted) {
                            ++netplayPredictedCount;
                        }
                        netplayFrameRecord = std::make_unique<NetplayFrameRecord>(NetplayFrameRecord{
                            frameId,
                            gb.saveState(),
                            localAppliedMask,
                            remoteMask,
                            predicted,
                        });
                        applyJoypadMask(gb.joypad(), static_cast<std::uint8_t>(localAppliedMask | remoteMask));
                    } else {
                        applyJoypadMask(gb.joypad(), localInputMask);
                    }

                    runOneFrameLocked();
                    const std::uint64_t frameCount = emulatedFrameCounter.fetch_add(1, std::memory_order_relaxed) + 1;

                    if (cheatsEnabledAtomic.load(std::memory_order_relaxed) && !cheats.empty()) {
                        applyCheats(cheats, gb.bus());
                    }

                    if (netplayEnabled && !replayPlayingNow) {
                        const std::uint32_t checksum = frameChecksum(gb);
                        localFrameChecksums[frameId] = checksum;
                        while (localFrameChecksums.size() > kNetplayHistoryLimit) {
                            std::uint64_t minFrame = std::numeric_limits<std::uint64_t>::max();
                            for (const auto& entry : localFrameChecksums) {
                                if (entry.first < minFrame) {
                                    minFrame = entry.first;
                                }
                            }
                            if (minFrame == std::numeric_limits<std::uint64_t>::max()) {
                                break;
                            }
                            localFrameChecksums.erase(minFrame);
                        }
                        (void)netplayTransport.sendNetplayChecksum(frameId, checksum);
                    }

                    if (netplayEnabled && !replayPlayingNow) {
                        if (netplayFrameRecord) {
                            netplayHistory.push_back(std::move(*netplayFrameRecord));
                            while (netplayHistory.size() > kNetplayHistoryLimit) {
                                const std::uint64_t oldFrame = netplayHistory.front().frame;
                                netplayHistory.pop_front();
                                netplayAuthoritativeInputs.erase(oldFrame);
                            }
                        }

                        netplayTransport.pump();
                        const auto lateInputs = netplayTransport.takeAllNetplayInputs();
                        for (const auto& local : localFrameChecksums) {
                            std::uint32_t remoteChecksum = 0;
                            if (netplayTransport.takeNetplayChecksum(local.first, remoteChecksum)) {
                                if (remoteChecksum != local.second) {
                                    ++netplayDesyncCount;
                                    pausedAtomic.store(true, std::memory_order_relaxed);
                                    fastForwardAtomic.store(false, std::memory_order_relaxed);
                                    pendingPauseAddr.store(gb.cpu().regs().pc, std::memory_order_relaxed);
                                    pendingPauseReason.store(3, std::memory_order_relaxed);
                                    forceTitleRefresh.store(true, std::memory_order_relaxed);
                                    uiMessage = "NETPLAY DESYNC";
                                    uiMessageFrames = 180;
                                }
                            }
                        }
                        std::optional<std::uint64_t> rollbackFrom{};
                        for (const auto& late : lateInputs) {
                            netplayAuthoritativeInputs[late.first] = late.second;
                            for (const auto& rec : netplayHistory) {
                                if (rec.frame != late.first) {
                                    continue;
                                }
                                if (rec.predicted && rec.remoteInput != late.second) {
                                    if (!rollbackFrom.has_value() || late.first < rollbackFrom.value()) {
                                        rollbackFrom = late.first;
                                    }
                                }
                                break;
                            }
                        }

                        if (rollbackFrom.has_value()) {
                            std::size_t startIndex = 0;
                            bool foundStart = false;
                            for (std::size_t idx = 0; idx < netplayHistory.size(); ++idx) {
                                if (netplayHistory[idx].frame == rollbackFrom.value()) {
                                    startIndex = idx;
                                    foundStart = true;
                                    break;
                                }
                            }
                            if (foundStart) {
                                gb.loadState(netplayHistory[startIndex].preState);
                                for (std::size_t idx = startIndex; idx < netplayHistory.size(); ++idx) {
                                    auto& rec = netplayHistory[idx];
                                    const auto it = netplayAuthoritativeInputs.find(rec.frame);
                                    if (it != netplayAuthoritativeInputs.end()) {
                                        rec.remoteInput = it->second;
                                        rec.predicted = false;
                                    }
                                    applyJoypadMask(gb.joypad(), static_cast<std::uint8_t>(rec.localInput | rec.remoteInput));
                                    runOneFrameLocked();
                                    if (cheatsEnabledAtomic.load(std::memory_order_relaxed) && !cheats.empty()) {
                                        applyCheats(cheats, gb.bus());
                                    }
                                    const std::uint32_t replayChecksum = frameChecksum(gb);
                                    localFrameChecksums[rec.frame] = replayChecksum;
                                    (void)netplayTransport.sendNetplayChecksum(rec.frame, replayChecksum);
                                }
                                (void)gb.apu().takeSamples();
                                audioRing.clear();
                                if (audioEnabled) {
                                    SDL_ClearQueuedAudio(audioDev);
                                }
                                ++netplayRollbackCount;
                                uiMessage = "NETPLAY ROLLBACK";
                                uiMessageFrames = 90;
                            }
                        }
                    }

                    {
                        std::lock_guard<std::mutex> replayLock(replayMutex);
                        if (replayRecording) {
                            replayData.frameInputs.push_back(joypadMaskFromState(gb.joypad().state()));
                        }
                    }

                    if (freezeEnabled && likelyWritableAddress(watchAddr)) {
                        if (gb.bus().peek(watchAddr) != freezeValue) {
                            gb.bus().write(watchAddr, freezeValue);
                        }
                    }

                    const bool captureRewindFrame = !ff || ((frameCount & 1ULL) == 0ULL);
                    if (captureRewindFrame) {
                        timeline.captureCurrent(gb);
                    }
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

            nextFrame += frameBudget;
            std::this_thread::sleep_until(nextFrame);
            const auto now = std::chrono::steady_clock::now();
            if (now - nextFrame > std::chrono::milliseconds(ff ? 40 : 100)) {
                nextFrame = now;
            }

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
    constexpr int kBindActionCount = static_cast<int>(BindAction::Count);

    const auto sanitizeUiText = [](const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (unsigned char raw : text) {
            char ch = static_cast<char>(std::toupper(raw));
            const bool ok = (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9')
                || ch == ' '
                || ch == '-'
                || ch == '_'
                || ch == ':'
                || ch == '.'
                || ch == '/';
            out.push_back(ok ? ch : ' ');
        }
        while (out.find("  ") != std::string::npos) {
            out.replace(out.find("  "), 2, " ");
        }
        if (!out.empty() && out.front() == ' ') {
            out.erase(out.begin());
        }
        if (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
        if (out.empty()) {
            out = "NONE";
        }
        return out;
    };
    const auto clipUiText = [&](const std::string& text, std::size_t maxChars) {
        if (text.size() <= maxChars) {
            return text;
        }
        return text.substr(0, maxChars);
    };
    const auto keyBindingLabel = [&](int key) {
        if (key == SDLK_UNKNOWN || key == 0) {
            return std::string("NONE");
        }
        const char* raw = SDL_GetKeyName(static_cast<SDL_Keycode>(key));
        if (!raw || raw[0] == '\0') {
            return std::string("KEY");
        }
        return sanitizeUiText(raw);
    };
    const auto padBindingLabel = [&](int button) {
        if (button < 0) {
            return std::string("NONE");
        }
        if (button >= static_cast<int>(SDL_CONTROLLER_BUTTON_MAX)) {
            return std::string("BTN ") + std::to_string(button);
        }
        const char* raw = SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(button));
        if (!raw || raw[0] == '\0') {
            return std::string("BTN ") + std::to_string(button);
        }
        return sanitizeUiText(raw);
    };
    const auto persistControlsWithMessage = [&](const std::string& okMessage) {
        if (saveControlBindingsWithMirror(controlsPath, globalControlsPath, controls)) {
            uiMessage = okMessage;
        } else {
            uiMessage = "CTRL SAVE FAIL";
        }
        uiMessageFrames = 120;
    };
    const auto persistNetworkConfig = [&]() {
        NetworkFrontendConfig cfg{};
        const int delay = std::clamp(netplayDelayAtomic.load(std::memory_order_relaxed), 0, 10);
        cfg.netplayDelayFrames = delay;
        cfg.linkMode = 0;
        if (linkCableMode == LinkCableMode::Loopback) {
            cfg.linkMode = 1;
        } else if (linkCableMode == LinkCableMode::Noise) {
            cfg.linkMode = 2;
        } else if (linkCableMode == LinkCableMode::Socket) {
            cfg.linkMode = 3;
        }
        return saveNetworkFrontendConfig(networkConfigPath, cfg);
    };
    const auto openControlsMenuState = [&]() {
        showControlsMenu = true;
        controlsAwaitKey = false;
        controlsEditPad = false;
        controlsMenuIndex = std::clamp(controlsMenuIndex, 0, kBindActionCount - 1);
        showScaleMenu = false;
        showPaletteMenu = false;
        openTopMenuSection.reset();
        if (memoryEdit.active) {
            memoryEdit.active = false;
        }
        if (breakpointEdit.active) {
            breakpointEdit.active = false;
        }
        memorySearch.ui.editingValue = false;
        stopTextInputIfUnused();
        fastForward = false;
        fastForwardAtomic.store(false, std::memory_order_relaxed);
        uiMessage = "CONTROLS MENU ON";
        uiMessageFrames = 90;
    };
    const auto cycleLinkModeState = [&]() {
        if (linkCableMode == LinkCableMode::Off) {
            linkCableMode = LinkCableMode::Loopback;
        } else if (linkCableMode == LinkCableMode::Loopback) {
            linkCableMode = LinkCableMode::Noise;
        } else if (linkCableMode == LinkCableMode::Noise) {
            linkCableMode = socketLinkAvailable ? LinkCableMode::Socket : LinkCableMode::Off;
        } else {
            linkCableMode = LinkCableMode::Off;
        }
        uiMessage = linkCableUiName(linkCableMode);
        uiMessageFrames = 120;
        (void)persistNetworkConfig();
    };
    const auto dispatchTopMenuAction = [&](TopMenuAction action) {
        switch (action) {
        case TopMenuAction::TogglePause:
            togglePauseState();
            break;
        case TopMenuAction::ToggleMute:
            toggleMutedState();
            break;
        case TopMenuAction::ToggleFastForward:
            fastForward = !fastForward;
            fastForwardAtomic.store(fastForward, std::memory_order_relaxed);
            uiMessage = fastForward ? "FAST FORWARD" : "NORMAL SPEED";
            uiMessageFrames = 60;
            break;
        case TopMenuAction::SaveState:
            saveStateToActiveSlot();
            break;
        case TopMenuAction::LoadState:
            loadStateFromActiveSlot();
            break;
        case TopMenuAction::BackToMenu:
            uiMessage = "BACK TO MENU";
            uiMessageFrames = 30;
            backToMenu = true;
            running = false;
            break;
        case TopMenuAction::ExitApp:
            running = false;
            break;
        case TopMenuAction::ToggleFullscreen:
            toggleFullscreenState();
            break;
        case TopMenuAction::ToggleScaleMenu:
            if (!fullscreen) {
                uiMessage = "SCALE ONLY FULLSCREEN";
                uiMessageFrames = 120;
                break;
            }
            showScaleMenu = !showScaleMenu;
            showPaletteMenu = false;
            showControlsMenu = false;
            scaleMenuIndex = static_cast<int>(fullscreenMode);
            break;
        case TopMenuAction::TogglePaletteMenu:
            showPaletteMenu = !showPaletteMenu;
            showScaleMenu = false;
            showControlsMenu = false;
            fastForward = false;
            paletteMenuIndex = std::clamp(static_cast<int>(paletteMode), 0, cgbPaletteAvailable ? 2 : 1);
            break;
        case TopMenuAction::CycleFilter:
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
            break;
        case TopMenuAction::CaptureFrame:
            requestCapture = true;
            break;
        case TopMenuAction::ToggleDebugPanel:
            toggleDebugPanelState();
            break;
        case TopMenuAction::ToggleBreakpointMenu:
            toggleBreakpointPanelState();
            break;
        case TopMenuAction::ToggleSearchPanel:
            if (!showPanel) {
                showPanel = true;
            }
            memorySearch.ui.visible = !memorySearch.ui.visible;
            memorySearch.ui.editingValue = false;
            stopTextInputIfUnused();
            uiMessage = memorySearch.ui.visible ? "SEARCH ON" : "SEARCH OFF";
            uiMessageFrames = 90;
            break;
        case TopMenuAction::OpenControlsMenu:
            openControlsMenuState();
            break;
        case TopMenuAction::CycleLinkMode:
            cycleLinkModeState();
            break;
        case TopMenuAction::NetplayDelayDown: {
            if (!netplayEnabled) {
                uiMessage = "NETPLAY OFF";
                uiMessageFrames = 90;
                break;
            }
            const int current = netplayDelayAtomic.load(std::memory_order_relaxed);
            const int next = std::max(0, current - 1);
            netplayDelayAtomic.store(next, std::memory_order_relaxed);
            (void)persistNetworkConfig();
            uiMessage = "NETPLAY DELAY " + std::to_string(next);
            uiMessageFrames = 120;
            break;
        }
        case TopMenuAction::NetplayDelayUp: {
            if (!netplayEnabled) {
                uiMessage = "NETPLAY OFF";
                uiMessageFrames = 90;
                break;
            }
            const int current = netplayDelayAtomic.load(std::memory_order_relaxed);
            const int next = std::min(10, current + 1);
            netplayDelayAtomic.store(next, std::memory_order_relaxed);
            (void)persistNetworkConfig();
            uiMessage = "NETPLAY DELAY " + std::to_string(next);
            uiMessageFrames = 120;
            break;
        }
        default:
            break;
        }
    };
    struct TopMenuUiItem {
        TopMenuAction action = TopMenuAction::None;
        std::string label{};
    };
    const auto hasLoadableState = [&]() {
        const std::string slotStatePath = saveSlotStatePath(statePath, activeSaveSlot);
        if (std::filesystem::exists(slotStatePath)) {
            return true;
        }
        return activeSaveSlot == 0 && std::filesystem::exists(legacyStatePath);
    };
    const auto buildTopMenuUiItems = [&](TopMenuSection section) {
        std::vector<TopMenuUiItem> out;
        switch (section) {
        case TopMenuSection::Session:
            out.push_back({TopMenuAction::TogglePause, paused ? "CONTINUAR" : "PAUSAR"});
            if (audioEnabled) {
                out.push_back({TopMenuAction::ToggleMute, muted ? "ATIVAR AUDIO" : "MUTAR AUDIO"});
            }
            out.push_back({TopMenuAction::ToggleFastForward, fastForward ? "FF NORMAL" : "FF LIGAR"});
            out.push_back({TopMenuAction::SaveState, "SALVAR STATE"});
            if (hasLoadableState()) {
                out.push_back({TopMenuAction::LoadState, "CARREGAR STATE"});
            }
            out.push_back({TopMenuAction::BackToMenu, "VOLTAR MENU ROM"});
            out.push_back({TopMenuAction::ExitApp, "SAIR"});
            break;
        case TopMenuSection::Image:
            out.push_back({TopMenuAction::ToggleFullscreen, fullscreen ? "SAIR FULLSCREEN" : "ENTRAR FULLSCREEN"});
            if (fullscreen) {
                out.push_back({TopMenuAction::ToggleScaleMenu, "MENU ESCALA"});
            }
            out.push_back({TopMenuAction::TogglePaletteMenu, "MENU PALETA"});
            out.push_back({TopMenuAction::CycleFilter, "CICLAR FILTRO"});
            out.push_back({TopMenuAction::CaptureFrame, "CAPTURA TELA"});
            break;
        case TopMenuSection::Debug:
            out.push_back({TopMenuAction::ToggleDebugPanel, showPanel ? "OCULTAR DEBUG" : "MOSTRAR DEBUG"});
            if (showPanel) {
                out.push_back({TopMenuAction::ToggleBreakpointMenu, showBreakpointMenu ? "OCULTAR BP WP" : "MOSTRAR BP WP"});
                out.push_back({TopMenuAction::ToggleSearchPanel, memorySearch.ui.visible ? "OCULTAR BUSCA" : "MOSTRAR BUSCA"});
            }
            break;
        case TopMenuSection::Controls:
            out.push_back({TopMenuAction::OpenControlsMenu, "ABRIR MENU CONTROLES"});
            break;
        case TopMenuSection::Network:
            out.push_back({TopMenuAction::CycleLinkMode, std::string("LINK ") + linkCableUiName(linkCableMode)});
            if (netplayEnabled) {
                const int delay = netplayDelayAtomic.load(std::memory_order_relaxed);
                out.push_back({TopMenuAction::NetplayDelayDown, "DELAY NETPLAY -"});
                out.push_back({TopMenuAction::NetplayDelayUp, "DELAY NETPLAY +"});
                out.push_back({TopMenuAction::None, "ATUAL " + std::to_string(delay) + " FRAMES"});
            } else {
                out.push_back({TopMenuAction::None, "NETPLAY OFF"});
            }
            break;
        default:
            break;
        }
        return out;
    };
    const auto topMenuDropdownRectForItems = [&](int outputW, TopMenuSection section, const std::vector<TopMenuUiItem>& items) {
        const auto sec = topMenuSectionRect(outputW, section);
        int maxChars = 0;
        for (const auto& item : items) {
            const int chars = static_cast<int>(sanitizeUiText(item.label).size());
            if (chars > maxChars) {
                maxChars = chars;
            }
        }
        const int width = std::max(sec.w + 30, maxChars * 6 + 16);
        const int height = std::max(12, static_cast<int>(items.size()) * topMenuItemHeight() + 8);
        return TopMenuRect{sec.x, topMenuBarHeight(), width, height};
    };
    const auto hitTestTopMenuUiAction = [&](int outputW, TopMenuSection section, int px, int py) -> std::optional<TopMenuAction> {
        const auto items = buildTopMenuUiItems(section);
        if (items.empty()) {
            return std::nullopt;
        }
        const auto drop = topMenuDropdownRectForItems(outputW, section, items);
        if (!topMenuRectContains(drop, px, py)) {
            return std::nullopt;
        }
        const int localY = py - drop.y - 4;
        if (localY < 0) {
            return std::nullopt;
        }
        const int row = localY / topMenuItemHeight();
        if (row < 0 || row >= static_cast<int>(items.size())) {
            return std::nullopt;
        }
        return items[static_cast<std::size_t>(row)].action;
    };
    const auto drawTopTaskbar = [&](int outputW) {
        if (!showTopMenuBar) {
            return;
        }
        const int barH = topMenuBarHeight();
        SDL_SetRenderDrawColor(renderer, 6, 10, 18, 228);
        SDL_Rect bar{0, 0, outputW, barH};
        SDL_RenderFillRect(renderer, &bar);
        SDL_SetRenderDrawColor(renderer, 60, 80, 122, 255);
        SDL_RenderDrawLine(renderer, 0, barH - 1, outputW, barH - 1);

        for (int i = 0; i < static_cast<int>(TopMenuSection::Count); ++i) {
            const auto section = static_cast<TopMenuSection>(i);
            const auto r = topMenuSectionRect(outputW, section);
            const bool active = openTopMenuSection.has_value() && openTopMenuSection.value() == section;
            const bool hovered = hoveredTopMenuSection.has_value() && hoveredTopMenuSection.value() == section;
            SDL_SetRenderDrawColor(
                renderer,
                active ? 40 : (hovered ? 30 : 20),
                active ? 56 : (hovered ? 44 : 34),
                active ? 92 : (hovered ? 78 : 62),
                255
            );
            SDL_Rect secRect{r.x, r.y, r.w, r.h};
            SDL_RenderFillRect(renderer, &secRect);
            SDL_SetRenderDrawColor(renderer, 90, 112, 156, 255);
            SDL_RenderDrawRect(renderer, &secRect);
            drawHexText(
                renderer,
                r.x + 6,
                r.y + 6,
                sanitizeUiText(topMenuSectionLabel(section)),
                active ? SDL_Color{255, 230, 120, 255} : SDL_Color{205, 220, 246, 255},
                1
            );
        }

        if (openTopMenuSection.has_value()) {
            const auto section = openTopMenuSection.value();
            const auto items = buildTopMenuUiItems(section);
            const auto drop = topMenuDropdownRectForItems(outputW, section, items);
            SDL_SetRenderDrawColor(renderer, 8, 12, 24, 236);
            SDL_Rect dropRect{drop.x, drop.y, drop.w, drop.h};
            SDL_RenderFillRect(renderer, &dropRect);
            SDL_SetRenderDrawColor(renderer, 90, 110, 150, 255);
            SDL_RenderDrawRect(renderer, &dropRect);

            const int rowH = topMenuItemHeight();
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                const int rowY = drop.y + 4 + i * rowH;
                if (i == hoveredTopMenuItem) {
                    SDL_SetRenderDrawColor(renderer, 30, 42, 70, 255);
                    SDL_Rect rowBg{drop.x + 2, rowY, drop.w - 4, rowH};
                    SDL_RenderFillRect(renderer, &rowBg);
                }
                drawHexText(
                    renderer,
                    drop.x + 6,
                    rowY + 4,
                    sanitizeUiText(items[static_cast<std::size_t>(i)].label),
                    i == hoveredTopMenuItem ? SDL_Color{255, 230, 120, 255} : SDL_Color{205, 220, 246, 255},
                    1
                );
            }
        }

        char status[80];
        std::snprintf(
            status,
            sizeof(status),
            "%s %s %s",
            paused ? "PAUSADO" : "RODANDO",
            muted ? "MUTE" : "AUDIO",
            fastForward ? "FF" : "NORMAL"
        );
        const std::string statusText = sanitizeUiText(status);
        const int statusX = std::max(8, outputW - static_cast<int>(statusText.size()) * 6 - 12);
        drawHexText(renderer, statusX, 8, statusText, SDL_Color{150, 188, 236, 255}, 1);
    };
    const auto controlsMenuLayout = [&](int outputW, int outputH) {
        const int boxW = std::max(420, std::min(620, outputW - 24));
        const int boxH = std::max(250, std::min(340, outputH - 24));
        const int x = (outputW - boxW) / 2;
        const int y = (outputH - boxH) / 2;
        return PopupWindowLayout{
            SDL_Rect{x, y, boxW, boxH},
            SDL_Rect{x + boxW - 20, y + 4, 14, 12},
        };
    };
    const auto drawControlsMenuOverlay = [&](int outputW, int outputH) {
        const auto layout = controlsMenuLayout(outputW, outputH);
        const int x = layout.box.x;
        const int y = layout.box.y;
        const int boxW = layout.box.w;
        const int boxH = layout.box.h;
        const int rowH = 20;
        const int tableY = y + 54;

        SDL_SetRenderDrawColor(renderer, 8, 12, 24, 236);
        SDL_RenderFillRect(renderer, &layout.box);
        SDL_SetRenderDrawColor(renderer, 90, 110, 150, 255);
        SDL_RenderDrawRect(renderer, &layout.box);

        SDL_SetRenderDrawColor(renderer, 28, 38, 62, 255);
        SDL_RenderFillRect(renderer, &layout.closeButton);
        SDL_SetRenderDrawColor(renderer, 96, 122, 170, 255);
        SDL_RenderDrawRect(renderer, &layout.closeButton);
        drawHexText(renderer, layout.closeButton.x + 4, layout.closeButton.y + 2, "X", SDL_Color{255, 228, 140, 255}, 1);

        drawHexText(renderer, x + 12, y + 10, "CONTROLS MENU", SDL_Color{236, 242, 255, 255}, 1);
        drawHexText(renderer, x + 12, y + 24, "F11 TOGGLE  ENTER CHANGE  DEL CLEAR  X CLOSE", SDL_Color{170, 180, 204, 255}, 1);

        const int colAction = x + 14;
        const int colKey = x + boxW * 34 / 100;
        const int colPad = x + boxW * 64 / 100;
        drawHexText(renderer, colAction, tableY - 12, "ACTION", SDL_Color{158, 190, 236, 255}, 1);
        drawHexText(renderer, colKey, tableY - 12, "KEYBOARD", SDL_Color{158, 190, 236, 255}, 1);
        drawHexText(renderer, colPad, tableY - 12, "CONTROLLER", SDL_Color{158, 190, 236, 255}, 1);

        for (int i = 0; i < kBindActionCount; ++i) {
            const int rowY = tableY + i * rowH;
            if (rowY + rowH > y + boxH - 44) {
                break;
            }
            const bool selectedRow = i == controlsMenuIndex;
            if (selectedRow) {
                SDL_SetRenderDrawColor(renderer, 28, 38, 62, 255);
                SDL_Rect rowBg{x + 8, rowY - 1, boxW - 16, rowH - 2};
                SDL_RenderFillRect(renderer, &rowBg);
            }
            const auto action = static_cast<BindAction>(i);
            const std::string keyText = clipUiText(keyBindingLabel(controls.keys[static_cast<std::size_t>(i)]), 14);
            const std::string padText = clipUiText(padBindingLabel(controls.padButtons[static_cast<std::size_t>(i)]), 14);

            drawHexText(
                renderer,
                colAction,
                rowY + 4,
                bindActionName(action),
                selectedRow ? SDL_Color{255, 230, 120, 255} : SDL_Color{214, 224, 242, 255},
                1
            );
            drawHexText(
                renderer,
                colKey,
                rowY + 4,
                keyText,
                selectedRow && !controlsEditPad ? SDL_Color{255, 230, 120, 255} : SDL_Color{202, 212, 230, 255},
                1
            );
            drawHexText(
                renderer,
                colPad,
                rowY + 4,
                padText,
                selectedRow && controlsEditPad ? SDL_Color{255, 230, 120, 255} : SDL_Color{202, 212, 230, 255},
                1
            );
        }

        const int footerY = y + boxH - 28;
        if (controlsAwaitKey) {
            const std::string wait = controlsEditPad ? "PRESS CONTROLLER BUTTON  ESC CANCEL" : "PRESS KEYBOARD KEY  ESC CANCEL";
            drawHexText(renderer, x + 12, footerY, wait, SDL_Color{255, 214, 120, 255}, 1);
        } else {
            drawHexText(renderer, x + 12, footerY, "UP DOWN ROW  LEFT RIGHT FIELD  R RESET  S SAVE", SDL_Color{160, 172, 198, 255}, 1);
        }
    };

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
            } else if (pendingPause == 3) {
                std::snprintf(msg, sizeof(msg), "NETPLAY DESYNC");
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
            if (ev.type == SDL_CONTROLLERDEVICEADDED && !gamepad) {
                if (SDL_IsGameController(ev.cdevice.which)) {
                    gamepad = SDL_GameControllerOpen(ev.cdevice.which);
                }
            }
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && gamepad) {
                const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
                if (id == ev.cdevice.which) {
                    SDL_GameControllerClose(gamepad);
                    gamepad = nullptr;
                }
            }
            if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
                if (showControlsMenu) {
                    if (controlsAwaitKey && controlsEditPad && ev.type == SDL_CONTROLLERBUTTONDOWN) {
                        const int clamped = std::clamp(controlsMenuIndex, 0, kBindActionCount - 1);
                        controls.padButtons[static_cast<std::size_t>(clamped)] = static_cast<int>(ev.cbutton.button);
                        controlsAwaitKey = false;
                        const std::string msg = std::string(bindActionName(static_cast<BindAction>(clamped))) + " PAD SET";
                        persistControlsWithMessage(msg);
                    }
                    continue;
                }
                if (showScaleMenu || showPaletteMenu || memoryEdit.active || breakpointEdit.active || memorySearch.ui.editingValue) {
                    continue;
                }
                std::lock_guard<std::mutex> gbLock(gbMutex);
                applyGamepadBinding(gb, controls, ev.cbutton.button, ev.type == SDL_CONTROLLERBUTTONDOWN);
                continue;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0) {
                if (ev.key.keysym.sym == SDLK_F11) {
                    if (showControlsMenu) {
                        showControlsMenu = false;
                        controlsAwaitKey = false;
                        uiMessage = "CONTROLS MENU OFF";
                        uiMessageFrames = 90;
                    } else {
                        openControlsMenuState();
                    }
                    continue;
                }
                if (ev.key.keysym.sym == SDLK_ESCAPE && openTopMenuSection.has_value()) {
                    openTopMenuSection.reset();
                    continue;
                }
                if (openTopMenuSection.has_value()) {
                    openTopMenuSection.reset();
                }
                if (showControlsMenu) {
                    controlsMenuIndex = std::clamp(controlsMenuIndex, 0, kBindActionCount - 1);
                    if (controlsAwaitKey) {
                        if (ev.key.keysym.sym == SDLK_ESCAPE) {
                            controlsAwaitKey = false;
                            uiMessage = "CTRL MAP CANCEL";
                            uiMessageFrames = 90;
                        } else {
                            controls.keys[static_cast<std::size_t>(controlsMenuIndex)] = static_cast<int>(ev.key.keysym.sym);
                            controlsAwaitKey = false;
                            const std::string msg = std::string(bindActionName(static_cast<BindAction>(controlsMenuIndex))) + " KEY SET";
                            persistControlsWithMessage(msg);
                        }
                        continue;
                    }
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        showControlsMenu = false;
                        uiMessage = "CONTROLS MENU OFF";
                        uiMessageFrames = 90;
                    } else if (ev.key.keysym.sym == SDLK_UP) {
                        controlsMenuIndex = (controlsMenuIndex + kBindActionCount - 1) % kBindActionCount;
                    } else if (ev.key.keysym.sym == SDLK_DOWN) {
                        controlsMenuIndex = (controlsMenuIndex + 1) % kBindActionCount;
                    } else if (ev.key.keysym.sym == SDLK_LEFT) {
                        controlsEditPad = false;
                    } else if (ev.key.keysym.sym == SDLK_RIGHT) {
                        controlsEditPad = true;
                    } else if (ev.key.keysym.sym == SDLK_BACKSPACE || ev.key.keysym.sym == SDLK_DELETE) {
                        if (controlsEditPad) {
                            controls.padButtons[static_cast<std::size_t>(controlsMenuIndex)] = -1;
                            persistControlsWithMessage("PAD CLEARED");
                        } else {
                            controls.keys[static_cast<std::size_t>(controlsMenuIndex)] = SDLK_UNKNOWN;
                            persistControlsWithMessage("KEY CLEARED");
                        }
                    } else if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
                        controlsAwaitKey = true;
                        uiMessage = controlsEditPad ? "PRESS PAD BUTTON" : "PRESS KEYBOARD KEY";
                        uiMessageFrames = 120;
                    } else if (ev.key.keysym.sym == SDLK_r) {
                        controls = defaultControlBindings();
                        persistControlsWithMessage("CONTROLS RESET");
                    } else if (ev.key.keysym.sym == SDLK_s) {
                        persistControlsWithMessage("CONTROLS SAVED");
                    }
                    continue;
                }
                if (ev.key.keysym.sym == SDLK_n && fullscreen) {
                    showScaleMenu = !showScaleMenu;
                    showPaletteMenu = false;
                    showControlsMenu = false;
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
                    showControlsMenu = false;
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
                    togglePauseState();
                } else if (ev.key.keysym.sym == SDLK_p) {
                    toggleMutedState();
                } else if (ev.key.keysym.sym == SDLK_i) {
                    toggleDebugPanelState();
                } else if (ev.key.keysym.sym == SDLK_d) {
                    toggleBreakpointPanelState();
                } else if (ev.key.keysym.sym == SDLK_j) {
                    cycleLinkModeState();
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
                    toggleFullscreenState();
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
                } else if (ev.key.keysym.sym == SDLK_F3 && (ev.key.keysym.mod & KMOD_CTRL) == 0) {
                    showTopMenuBar = !showTopMenuBar;
                    if (!showTopMenuBar) {
                        openTopMenuSection.reset();
                    }
                    hoveredTopMenuSection.reset();
                    hoveredTopMenuItem = -1;
                    uiMessage = showTopMenuBar ? "TOP BAR ON" : "TOP BAR OFF";
                    uiMessageFrames = 90;
                } else if (ev.key.keysym.sym == SDLK_F1) {
                    activeSaveSlot = normalizeSaveSlot(activeSaveSlot - 1);
                    char msg[32];
                    std::snprintf(msg, sizeof(msg), "SLOT %d", activeSaveSlot);
                    uiMessage = msg;
                    uiMessageFrames = 90;
                } else if (ev.key.keysym.sym == SDLK_F2) {
                    activeSaveSlot = normalizeSaveSlot(activeSaveSlot + 1);
                    char msg[32];
                    std::snprintf(msg, sizeof(msg), "SLOT %d", activeSaveSlot);
                    uiMessage = msg;
                    uiMessageFrames = 90;
                } else if (ev.key.keysym.sym == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL)) {
                    saveStateToActiveSlot();
                } else if (ev.key.keysym.sym == SDLK_F5
                           || (ev.key.keysym.sym == SDLK_l && (ev.key.keysym.mod & KMOD_CTRL))) {
                    loadStateFromActiveSlot();
                } else {
                    std::lock_guard<std::mutex> gbLock(gbMutex);
                    applyKeyboardBinding(gb, controls, ev.key.keysym.sym, true);
                }
            }
            if (ev.type == SDL_KEYUP) {
                if (showControlsMenu || showScaleMenu || showPaletteMenu || memoryEdit.active || breakpointEdit.active || memorySearch.ui.editingValue) {
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
                applyKeyboardBinding(gb, controls, ev.key.keysym.sym, false);
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
            if (ev.type == SDL_MOUSEMOTION) {
                hoveredTopMenuSection.reset();
                hoveredTopMenuItem = -1;
                if (showTopMenuBar) {
                    int outputW = 0;
                    int outputH = 0;
                    SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                    const int mx = ev.motion.x;
                    const int my = ev.motion.y;
                    hoveredTopMenuSection = hitTestTopMenuSection(outputW, mx, my);
                    if (openTopMenuSection.has_value()) {
                        const auto items = buildTopMenuUiItems(openTopMenuSection.value());
                        const auto drop = topMenuDropdownRectForItems(outputW, openTopMenuSection.value(), items);
                        if (topMenuRectContains(drop, mx, my)) {
                            const int localY = my - drop.y - 4;
                            if (localY >= 0) {
                                const int row = localY / topMenuItemHeight();
                                if (row >= 0 && row < static_cast<int>(items.size())) {
                                    hoveredTopMenuItem = row;
                                }
                            }
                        }
                    }
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int outputW = 0;
                int outputH = 0;
                SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                const int mx = ev.button.x;
                const int my = ev.button.y;

                if (showControlsMenu) {
                    const auto layout = controlsMenuLayout(outputW, outputH);
                    if (popupLayoutHitClose(layout, mx, my)) {
                        showControlsMenu = false;
                        controlsAwaitKey = false;
                        uiMessage = "CONTROLS MENU OFF";
                        uiMessageFrames = 90;
                        continue;
                    }
                }
                if (showScaleMenu) {
                    const auto layout = fullscreenScaleMenuLayout(outputW, outputH);
                    if (popupLayoutHitClose(layout, mx, my)) {
                        showScaleMenu = false;
                        uiMessage = "SCALE MENU OFF";
                        uiMessageFrames = 90;
                        continue;
                    }
                }
                if (showPaletteMenu) {
                    const auto layout = paletteModeMenuLayout(outputW, outputH, cgbPaletteAvailable);
                    if (popupLayoutHitClose(layout, mx, my)) {
                        showPaletteMenu = false;
                        uiMessage = "PALETA MENU OFF";
                        uiMessageFrames = 90;
                        continue;
                    }
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && showTopMenuBar) {
                int outputW = 0;
                int outputH = 0;
                SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
                const int mx = ev.button.x;
                const int my = ev.button.y;
                bool consumed = false;

                if (const auto section = hitTestTopMenuSection(outputW, mx, my); section.has_value()) {
                    if (openTopMenuSection.has_value() && openTopMenuSection.value() == section.value()) {
                        openTopMenuSection.reset();
                    } else {
                        openTopMenuSection = section.value();
                    }
                    consumed = true;
                } else if (openTopMenuSection.has_value()) {
                    const auto action = hitTestTopMenuUiAction(outputW, openTopMenuSection.value(), mx, my);
                    if (action.has_value()) {
                        dispatchTopMenuAction(action.value());
                    }
                    openTopMenuSection.reset();
                    hoveredTopMenuItem = -1;
                    consumed = true;
                } else if (my >= 0 && my < topMenuBarHeight()) {
                    consumed = true;
                }

                if (consumed) {
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

        auto latestFrame = std::make_unique<RgbFramePacket>();
        if (rgbFrameQueue.tryPopLatest(*latestFrame)) {
            pixels = latestFrame->pixels;
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
        drawTopTaskbar(outputW);
        if (uiMessageFrames > 0) {
            const int minMsgY = showTopMenuBar ? (topMenuBarHeight() + 4) : 8;
            const int msgY = std::max(blit.gameDst.y + 8, minMsgY);
            SDL_SetRenderDrawColor(renderer, 20, 20, 20, 180);
            SDL_Rect msgBg{blit.gameDst.x + 8, msgY, 200, 20};
            SDL_RenderFillRect(renderer, &msgBg);
            drawHexText(renderer, blit.gameDst.x + 12, msgY + 4, uiMessage, SDL_Color{255, 230, 120, 255}, 1);
            --uiMessageFrames;
        }
        char statusLine[84];
        if (netplayEnabled) {
            std::snprintf(
                statusLine,
                sizeof(statusLine),
                "%s %s NP:D%d P%llu R%llu X%llu%s%s",
                linkCableUiName(linkCableMode),
                filterUiName(filterMode),
                std::clamp(netplayDelayAtomic.load(std::memory_order_relaxed), 0, 10),
                static_cast<unsigned long long>(netplayPredictedCount),
                static_cast<unsigned long long>(netplayRollbackCount),
                static_cast<unsigned long long>(netplayDesyncCount),
                watchpointEnabled ? " WP" : "",
                fastForward ? " FF" : ""
            );
        } else {
            std::snprintf(
                statusLine,
                sizeof(statusLine),
                "%s %s%s%s",
                linkCableUiName(linkCableMode),
                filterUiName(filterMode),
                watchpointEnabled ? " WP" : "",
                fastForward ? " FF" : ""
            );
        }
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
        if (showControlsMenu) {
            drawControlsMenuOverlay(outputW, outputH);
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
    (void)persistNetworkConfig();

    return backToMenu ? 2 : 0;
}
#endif

} // namespace gb::frontend
