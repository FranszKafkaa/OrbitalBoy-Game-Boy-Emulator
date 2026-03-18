#include "gb/app/frontend/gba_realtime.hpp"

#ifdef GBEMU_USE_SDL2
#include "gb/app/sdl_compat.hpp"
#include "gb/core/environment.hpp"
#include "gb/core/gba/apu.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>   // timeBeginPeriod / timeEndPeriod
#pragma comment(lib, "winmm.lib")
#endif

namespace gb::frontend {

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
SDL_Rect computeDestinationRect(int outputW, int outputH, int srcW, int srcH) {
    if (outputW <= 0 || outputH <= 0 || srcW <= 0 || srcH <= 0) {
        return SDL_Rect{0, 0, 0, 0};
    }

    const int scaleX = outputW / srcW;
    const int scaleY = outputH / srcH;
    const int scale = std::max(1, std::min(scaleX, scaleY));

    const int dstW = srcW * scale;
    const int dstH = srcH * scale;
    return SDL_Rect{
        (outputW - dstW) / 2,
        (outputH - dstH) / 2,
        dstW,
        dstH,
    };
}

bool profileTitleEnabledByDefault() {
    return gb::environmentVariableEnabled("GBEMU_GBA_SHOW_PROFILE_TITLE");
}

int readIntEnvironmentOrDefault(const char* name, int fallback) {
    const auto value = gb::readEnvironmentVariable(name);
    if (!value.has_value() || value->empty()) {
        return fallback;
    }
    try {
        return std::stoi(*value);
    } catch (...) {
        return fallback;
    }
}

bool frameTimingLoggingEnabled() {
    return gb::environmentVariableEnabled("GBEMU_GBA_LOG_FRAME_TIMING");
}

int frameTimingLogEvery() {
    return std::max(1, readIntEnvironmentOrDefault("GBEMU_GBA_LOG_FRAME_TIMING_EVERY", 1));
}

bool audioDisabledByDebug() {
    return gb::environmentVariableEnabled("GBEMU_GBA_DISABLE_AUDIO");
}

bool videoDisabledByDebug() {
    return gb::environmentVariableEnabled("GBEMU_GBA_DISABLE_VIDEO");
}

int videoFrameSkipCount() {
    return std::max(0, readIntEnvironmentOrDefault("GBEMU_GBA_FRAME_SKIP", 0));
}

int exitAfterFrameCount() {
    return std::max(0, readIntEnvironmentOrDefault("GBEMU_GBA_EXIT_AFTER_FRAMES", 0));
}

bool adaptiveAudioFrameSkipEnabled(const gba::System& system) {
    if (gb::hasEnvironmentVariable("GBEMU_GBA_AUDIO_PRIORITY_FRAME_SKIP")) {
        return gb::environmentVariableEnabled("GBEMU_GBA_AUDIO_PRIORITY_FRAME_SKIP");
    }
    return system.compatibilityProfile().name == "advance-wars";
}

std::string buildWindowTitle(
    const std::string& baseTitle,
    const gba::System::FrameProfile& profile,
    bool showProfile,
    std::uint32_t visibleFrameCounter
) {
    if (!showProfile) {
        return baseTitle;
    }

    char buffer[320]{};
    const double totalMs = static_cast<double>(profile.totalNs) / 1000000.0;
    const double cpuMs = static_cast<double>(profile.cpuNs) / 1000000.0;
    const double renderMs = static_cast<double>(profile.renderNs) / 1000000.0;
    const double bgMs = static_cast<double>(profile.ppu.bgNs) / 1000000.0;
    const double objMs = static_cast<double>(profile.ppu.objNs) / 1000000.0;
    const double objWinMs = static_cast<double>(profile.ppu.objWindowNs) / 1000000.0;
    const double composeMs = static_cast<double>(profile.ppu.composeNs) / 1000000.0;
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s | frame %.2f cpu %.2f render %.2f bg %.2f obj %.2f win %.2f comp %.2f | obj/line %u vis %u",
        baseTitle.c_str(),
        totalMs,
        cpuMs,
        renderMs,
        bgMs,
        objMs,
        objWinMs,
        composeMs,
        static_cast<unsigned>(profile.ppu.maxVisibleObjectsOnScanline),
        static_cast<unsigned>(visibleFrameCounter)
    );
    return std::string(buffer);
}

} // namespace

// ===========================================================================
// runGbaRealtime — single-threaded, VSync-paced.
// ===========================================================================
int runGbaRealtime(gba::System& system, int scale) {
    if (!system.loaded()) {
        return 1;
    }

    const bool debugDisableAudio = audioDisabledByDebug();
    const bool debugDisableVideo = videoDisabledByDebug();
    const int debugFrameSkip = videoFrameSkipCount();
    const int debugExitAfterFrames = exitAfterFrameCount();
    const bool logFrameTiming = frameTimingLoggingEnabled();
    const int logFrameTimingEvery = frameTimingLogEvery();
    const bool adaptiveFrameSkip = adaptiveAudioFrameSkipEnabled(system);
    const Uint32 sdlInitFlags = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER
        | (debugDisableAudio ? 0U : static_cast<Uint32>(SDL_INIT_AUDIO));
    if (SDL_Init(sdlInitFlags) != 0) {
        return 1;
    }

    const int baseScale = std::max(1, scale);
    const int windowW = gba::System::ScreenWidth * baseScale;
    const int windowH = gba::System::ScreenHeight * baseScale;

    std::string title = "GBA Fase 3";
    if (!system.metadata().title.empty()) {
        title += " - ";
        title += system.metadata().title;
    }
    const std::string baseTitle = title;

    SDL_Window* window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowW,
        windowH,
        SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        gba::System::ScreenWidth,
        gba::System::ScreenHeight
    );
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
#endif

    SDL_GameController* gamepad = nullptr;
    const int joystickCount = SDL_NumJoysticks();
    for (int i = 0; i < joystickCount; ++i) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        gamepad = SDL_GameControllerOpen(i);
        if (gamepad != nullptr) {
            break;
        }
    }

    SDL_AudioSpec have{};
    SDL_AudioDeviceID audioDev = 0;
    SDL_AudioStream* audioStream = nullptr;
    bool audioStarted = false;
    if (!debugDisableAudio) {
        SDL_AudioSpec want{};
        want.freq = gba::Apu::SampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        want.callback = nullptr;

        audioDev = SDL_OpenAudioDevice(
            nullptr,
            0,
            &want,
            &have,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE
        );
        if (audioDev != 0) {
            audioStream = SDL_NewAudioStream(
                AUDIO_S16SYS,
                2,
                gba::Apu::SampleRate,
                have.format,
                have.channels,
                have.freq
            );
        }
        if (audioDev != 0 && audioStream == nullptr) {
            std::cerr << "aviso: falha ao criar resampler SDL para GBA: " << SDL_GetError() << "\n";
            SDL_CloseAudioDevice(audioDev);
            audioDev = 0;
        } else {
            if (audioDev != 0) {
                std::cerr << "[GBA][AUD] device freq=" << have.freq
                          << " channels=" << static_cast<int>(have.channels)
                          << " samples=" << have.samples << "\n";
                SDL_PauseAudioDevice(audioDev, 1);
            } else {
                std::cerr << "aviso: audio SDL indisponivel no frontend GBA: " << SDL_GetError() << "\n";
            }
        }
    }
    const bool audioReady = audioDev != 0 && audioStream != nullptr;
    const Uint32 bytesPerSecond = audioReady
        ? static_cast<Uint32>(have.freq * have.channels * static_cast<int>(sizeof(int16_t)))
        : 0U;
    const Uint32 minQueuedAudioBytes = audioReady
        ? bytesPerSecond / 12U
        : 0U;
    const Uint32 targetQueuedAudioBytes = audioReady
        ? bytesPerSecond / 8U
        : 0U;
    const Uint32 highQueuedAudioBytes = audioReady
        ? bytesPerSecond / 6U
        : 0U;
    const Uint32 maxQueuedAudioBytes = audioReady
        ? bytesPerSecond / 4U
        : 0U;

    // GBA frame period: 280896 CPU cycles / 16 777 216 Hz ≈ 16.7427 ms (~59.73 Hz).
    constexpr auto kGbaFrameBudget = std::chrono::duration_cast<
        std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(280896.0 / 16777216.0));
    constexpr double kGbaFrameBudgetMs = 1000.0 * (280896.0 / 16777216.0);

#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    // -------------------------------------------------------------------
    // Main loop — single-threaded, timer-paced.
    //
    // sleep_until at the GBA frame rate (~59.73 Hz) with 1 ms timer
    // resolution. Audio is produced at the APU native rate and converted
    // to the actual SDL device format/rate with SDL_AudioStream. Pacing is
    // relaxed whenever the queued audio drops too low, so short render spikes
    // do not immediately become audible underruns.
    // -------------------------------------------------------------------
    bool running = true;
    bool backToMenu = false;
    bool showProfileTitle = profileTitleEnabledByDefault();
    gba::InputState eventInput{};
    std::array<std::uint32_t, 30> recentVisibleObjects{};
    std::size_t recentVisibleIndex = 0;
    std::uint64_t recentVisibleSum = 0;
    std::uint32_t recentVisibleAverage = 0;
    std::uint32_t frameCounter = 0;

    // Diagnostic counters — printed to console every ~2 seconds.
    auto diagStart = std::chrono::steady_clock::now();
    std::uint32_t diagFrames = 0;
    double diagTotalEmuMs = 0.0;
    double previousFrameWallMs = kGbaFrameBudgetMs;
    Uint32 previousQueuedAudioBytes = targetQueuedAudioBytes;
    int consecutiveAdaptiveRenderSkips = 0;

    auto nextFrame = std::chrono::steady_clock::now();

    SDL_SetWindowTitle(window, baseTitle.c_str());

    const auto applyKeyToInput = [](gba::InputState& input, SDL_Keycode key, bool pressed) {
        switch (key) {
        case SDLK_RIGHT:
        case SDLK_d:
            input.right = pressed;
            break;
        case SDLK_LEFT:
        case SDLK_a:
            input.left = pressed;
            break;
        case SDLK_UP:
        case SDLK_w:
            input.up = pressed;
            break;
        case SDLK_DOWN:
        case SDLK_s:
            input.down = pressed;
            break;
        case SDLK_z:
        case SDLK_j:
        case SDLK_k:
        case SDLK_c:
            input.a = pressed;
            break;
        case SDLK_x:
        case SDLK_u:
        case SDLK_i:
        case SDLK_v:
            input.b = pressed;
            break;
        case SDLK_BACKSPACE:
        case SDLK_RSHIFT:
        case SDLK_TAB:
            input.select = pressed;
            break;
        case SDLK_RETURN:
        case SDLK_SPACE:
            input.start = pressed;
            break;
        case SDLK_q:
            input.l = pressed;
            break;
        case SDLK_e:
            input.r = pressed;
            break;
        default:
            break;
        }
    };

    while (running) {
        const auto frameLoopStart = std::chrono::steady_clock::now();
        // --- Event handling ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                    continue;
                }
                if (event.key.keysym.sym == SDLK_F3 && event.key.repeat == 0) {
                    showProfileTitle = !showProfileTitle;
                    SDL_SetWindowTitle(window, baseTitle.c_str());
                    continue;
                }
                if (event.key.keysym.sym == SDLK_l && (event.key.keysym.mod & KMOD_CTRL) == 0) {
                    backToMenu = true;
                    running = false;
                    continue;
                }
                if (event.key.repeat == 0) {
                    applyKeyToInput(eventInput, event.key.keysym.sym, true);
                }
                continue;
            }
            if (event.type == SDL_KEYUP) {
                applyKeyToInput(eventInput, event.key.keysym.sym, false);
                continue;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                eventInput = gba::InputState{};
            }
        }

        // --- Composite keyboard + gamepad input ---
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        gba::InputState polled{};
        polled.right = keys[SDL_SCANCODE_RIGHT] != 0 || keys[SDL_SCANCODE_D] != 0;
        polled.left = keys[SDL_SCANCODE_LEFT] != 0 || keys[SDL_SCANCODE_A] != 0;
        polled.up = keys[SDL_SCANCODE_UP] != 0 || keys[SDL_SCANCODE_W] != 0;
        polled.down = keys[SDL_SCANCODE_DOWN] != 0 || keys[SDL_SCANCODE_S] != 0;
        polled.a = keys[SDL_SCANCODE_Z] != 0 || keys[SDL_SCANCODE_J] != 0 || keys[SDL_SCANCODE_K] != 0 || keys[SDL_SCANCODE_C] != 0;
        polled.b = keys[SDL_SCANCODE_X] != 0 || keys[SDL_SCANCODE_U] != 0 || keys[SDL_SCANCODE_I] != 0 || keys[SDL_SCANCODE_V] != 0;
        polled.select = keys[SDL_SCANCODE_BACKSPACE] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0 || keys[SDL_SCANCODE_TAB] != 0;
        polled.start = keys[SDL_SCANCODE_RETURN] != 0 || keys[SDL_SCANCODE_SPACE] != 0;
        polled.l = keys[SDL_SCANCODE_Q] != 0;
        polled.r = keys[SDL_SCANCODE_E] != 0;

        gba::InputState input{};
        input.right = eventInput.right || polled.right;
        input.left = eventInput.left || polled.left;
        input.up = eventInput.up || polled.up;
        input.down = eventInput.down || polled.down;
        input.a = eventInput.a || polled.a;
        input.b = eventInput.b || polled.b;
        input.select = eventInput.select || polled.select;
        input.start = eventInput.start || polled.start;
        input.l = eventInput.l || polled.l;
        input.r = eventInput.r || polled.r;

        if (gamepad != nullptr) {
            input.a = input.a || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A) != 0;
            input.b = input.b || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_B) != 0;
            input.select = input.select || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_BACK) != 0;
            input.start = input.start || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_START) != 0;
            input.up = input.up || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
            input.down = input.down || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
            input.left = input.left || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
            input.right = input.right || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;
            input.l = input.l || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
            input.r = input.r || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
        }

        const bool debugSkipRenderThisFrame = debugDisableVideo
            || (debugFrameSkip > 0 && (frameCounter % static_cast<std::uint32_t>(debugFrameSkip + 1)) != 0U);
        const bool audioStarving = audioReady && audioStarted && previousQueuedAudioBytes < minQueuedAudioBytes;
        const bool frameOverBudget = previousFrameWallMs > (kGbaFrameBudgetMs * 1.05);
        const bool adaptiveSkipRenderThisFrame = adaptiveFrameSkip
            && !debugSkipRenderThisFrame
            && audioStarving
            && frameOverBudget
            && consecutiveAdaptiveRenderSkips < 2;
        const bool renderThisFrame = !(debugSkipRenderThisFrame || adaptiveSkipRenderThisFrame);

        // --- Run one emulation frame ---
        const auto emuStart = std::chrono::steady_clock::now();
        system.setInputState(input);
        system.runFrame(renderThisFrame);
        const auto emuEnd = std::chrono::steady_clock::now();

        const double emuMs = std::chrono::duration<double, std::milli>(emuEnd - emuStart).count();
        diagTotalEmuMs += emuMs;

        // --- Queue audio ---
        Uint32 queuedAudioBytes = 0;
        std::size_t samplesGenerated = 0;
        Uint32 bytesQueuedToAudio = 0;
        auto samples = system.apu().takeSamples();
        samplesGenerated = samples.size() / 2U;
        if (audioReady) {
            if (!samples.empty()) {
                const int inputBytes = static_cast<int>(samples.size() * sizeof(int16_t));
                if (SDL_AudioStreamPut(audioStream, samples.data(), inputBytes) < 0) {
                    std::cerr << "aviso: SDL_AudioStreamPut falhou: " << SDL_GetError() << "\n";
                } else {
                    const int availableBytes = SDL_AudioStreamAvailable(audioStream);
                    if (availableBytes > 0) {
                        std::vector<std::uint8_t> converted(static_cast<std::size_t>(availableBytes));
                        const int gotBytes = SDL_AudioStreamGet(audioStream, converted.data(), availableBytes);
                        if (gotBytes > 0) {
                            const Uint32 currentlyQueued = SDL_GetQueuedAudioSize(audioDev);
                            // Keep latency bounded and predictable under sustained load.
                            if (currentlyQueued > maxQueuedAudioBytes) {
                                SDL_ClearQueuedAudio(audioDev);
                            }
                            SDL_QueueAudio(audioDev, converted.data(), static_cast<Uint32>(gotBytes));
                            bytesQueuedToAudio += static_cast<Uint32>(gotBytes);
                        }
                    }
                }
            }
            queuedAudioBytes = SDL_GetQueuedAudioSize(audioDev);
            if (!audioStarted && queuedAudioBytes >= minQueuedAudioBytes) {
                SDL_PauseAudioDevice(audioDev, 0);
                audioStarted = true;
            }
        }

        // --- Profile stats ---
        const auto& frameProfile = system.lastFrameProfile();
        recentVisibleSum -= recentVisibleObjects[recentVisibleIndex];
        recentVisibleObjects[recentVisibleIndex] = frameProfile.ppu.visibleObjectsFrame;
        recentVisibleSum += recentVisibleObjects[recentVisibleIndex];
        recentVisibleIndex = (recentVisibleIndex + 1U) % recentVisibleObjects.size();
        ++frameCounter;
        ++diagFrames;
        const std::size_t sampleCount = std::min<std::size_t>(frameCounter, recentVisibleObjects.size());
        recentVisibleAverage = sampleCount == 0U
            ? 0U
            : static_cast<std::uint32_t>(recentVisibleSum / sampleCount);

        if (showProfileTitle && (frameCounter % 15U) == 0U) {
            const std::string profileTitle = buildWindowTitle(baseTitle, frameProfile, true, recentVisibleAverage);
            SDL_SetWindowTitle(window, profileTitle.c_str());
        } else if (!showProfileTitle && (frameCounter % 15U) == 0U) {
            SDL_SetWindowTitle(window, baseTitle.c_str());
        }

        // --- Diagnostics (every ~2 seconds) ---
        {
            const auto diagNow = std::chrono::steady_clock::now();
            const double diagElapsed = std::chrono::duration<double>(diagNow - diagStart).count();
            if (diagElapsed >= 2.0 && diagFrames > 0) {
                const double fps = static_cast<double>(diagFrames) / diagElapsed;
                const double avgEmu = diagTotalEmuMs / static_cast<double>(diagFrames);
                const Uint32 audioQueued = audioReady ? queuedAudioBytes : 0;
                std::printf("[GBA] fps=%.1f  avg_emu=%.2fms  audio_queue=%u bytes\n",
                            fps, avgEmu, static_cast<unsigned>(audioQueued));
                std::fflush(stdout);
                diagStart = diagNow;
                diagFrames = 0;
                diagTotalEmuMs = 0.0;
            }
        }

        // --- Render ---
        double presentMs = 0.0;
        if (renderThisFrame) {
            SDL_UpdateTexture(
                texture,
                nullptr,
                system.framebuffer().data(),
                gba::System::ScreenWidth * static_cast<int>(sizeof(u16))
            );

            int outputW = windowW;
            int outputH = windowH;
            SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
            const SDL_Rect dst = computeDestinationRect(outputW, outputH, gba::System::ScreenWidth, gba::System::ScreenHeight);

            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            const auto presentStart = std::chrono::steady_clock::now();
            SDL_RenderPresent(renderer);
            const auto presentEnd = std::chrono::steady_clock::now();
            presentMs = std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
        }

        // --- Frame pacing: sleep_until next frame boundary ---
        nextFrame += kGbaFrameBudget;
        const auto now = std::chrono::steady_clock::now();
        const bool audioNeedsCatchUp = audioReady && audioStarted && queuedAudioBytes < targetQueuedAudioBytes;
        const bool audioTooFarAhead = audioReady && audioStarted && queuedAudioBytes > highQueuedAudioBytes;
        double sleepMs = 0.0;
        if (nextFrame < now - kGbaFrameBudget * 2) {
            // Fell too far behind — resync.
            nextFrame = now;
        } else if (!audioNeedsCatchUp && nextFrame > now) {
            const auto sleepStart = std::chrono::steady_clock::now();
            std::this_thread::sleep_until(nextFrame);
            const auto sleepEnd = std::chrono::steady_clock::now();
            sleepMs = std::chrono::duration<double, std::milli>(sleepEnd - sleepStart).count();
            if (audioTooFarAhead) {
                const auto extraSleepStart = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                const auto extraSleepEnd = std::chrono::steady_clock::now();
                sleepMs += std::chrono::duration<double, std::milli>(extraSleepEnd - extraSleepStart).count();
            }
        }

        const double totalFrameMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frameLoopStart).count();
        previousFrameWallMs = totalFrameMs;
        previousQueuedAudioBytes = queuedAudioBytes;
        if (adaptiveSkipRenderThisFrame) {
            ++consecutiveAdaptiveRenderSkips;
        } else {
            consecutiveAdaptiveRenderSkips = 0;
        }

        if (logFrameTiming && (frameCounter % static_cast<std::uint32_t>(logFrameTimingEvery)) == 0U) {
            const auto& profile = system.lastFrameProfile();
            const double cpuMs = static_cast<double>(profile.cpuStepNs) / 1000000.0;
            const double memMs = static_cast<double>(profile.memoryNs) / 1000000.0;
            const double ppuStepMs = static_cast<double>(profile.stepPpuNs) / 1000000.0;
            const double ppuRenderMs = static_cast<double>(profile.renderNs) / 1000000.0;
            const double ppuMs = ppuStepMs + ppuRenderMs;
            const double apuMs = static_cast<double>(profile.apuNs) / 1000000.0;
            std::printf(
                "[GBA][FRAME] frame=%u cpu_ms=%.3f mem_ms=%.3f ppu_ms=%.3f ppu_step_ms=%.3f ppu_render_ms=%.3f apu_ms=%.3f present_ms=%.3f sleep_ms=%.3f emu_ms=%.3f frame_ms=%.3f samples_generated=%u bytes_sent=%u audio_queue=%u audio_enabled=%u video_enabled=%u render_skipped=%u adaptive_skip=%u stretch=%.3f frameskip=%d\n",
                static_cast<unsigned>(frameCounter),
                cpuMs,
                memMs,
                ppuMs,
                ppuStepMs,
                ppuRenderMs,
                apuMs,
                presentMs,
                sleepMs,
                emuMs,
                totalFrameMs,
                static_cast<unsigned>(samplesGenerated),
                static_cast<unsigned>(bytesQueuedToAudio),
                static_cast<unsigned>(queuedAudioBytes),
                audioReady ? 1U : 0U,
                renderThisFrame ? 1U : 0U,
                renderThisFrame ? 0U : 1U,
                adaptiveSkipRenderThisFrame ? 1U : 0U,
                1.0,
                debugFrameSkip);
            std::fflush(stdout);
        }

        if (debugExitAfterFrames > 0 && frameCounter >= static_cast<std::uint32_t>(debugExitAfterFrames)) {
            running = false;
        }
    }

    // -------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    if (gamepad != nullptr) {
        SDL_GameControllerClose(gamepad);
    }
    if (audioReady) {
        SDL_ClearQueuedAudio(audioDev);
        SDL_AudioStreamClear(audioStream);
        SDL_FreeAudioStream(audioStream);
        SDL_CloseAudioDevice(audioDev);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return backToMenu ? 2 : 0;
}

} // namespace gb::frontend
#endif
