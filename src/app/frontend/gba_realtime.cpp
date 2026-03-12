#include "gb/app/frontend/gba_realtime.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>

#include <algorithm>
#include <string>

namespace gb::frontend {

namespace {

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

} // namespace

int runGbaRealtime(gba::System& system, int scale) {
    if (!system.loaded()) {
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
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

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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

    bool running = true;
    gba::InputState eventInput{};
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
        system.setInputState(input);

        system.runFrame();
        const auto& frame = system.framebuffer();
        SDL_UpdateTexture(
            texture,
            nullptr,
            frame.data(),
            gba::System::ScreenWidth * static_cast<int>(sizeof(u16))
        );

        int outputW = windowW;
        int outputH = windowH;
        SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
        const SDL_Rect dst = computeDestinationRect(outputW, outputH, gba::System::ScreenWidth, gba::System::ScreenHeight);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_RenderPresent(renderer);
    }

    if (gamepad != nullptr) {
        SDL_GameControllerClose(gamepad);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace gb::frontend
#endif
