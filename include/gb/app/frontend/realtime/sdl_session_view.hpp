#pragma once

#ifdef GBEMU_USE_SDL2
#include "gb/app/sdl_compat.hpp"

namespace gb::frontend {

class SdlSessionView {
public:
    SdlSessionView() = default;
    ~SdlSessionView();

    SdlSessionView(const SdlSessionView&) = delete;
    SdlSessionView& operator=(const SdlSessionView&) = delete;

    void markSdlInitialized();
    void ownWindow(SDL_Window* window);
    void ownRenderer(SDL_Renderer* renderer);
    void ownTexture(SDL_Texture* texture);
    void ownSharpTexture(SDL_Texture* texture);
    void ownAudioDevice(SDL_AudioDeviceID device);
    void ownGameController(SDL_GameController* controller);
    void closeGameController();
    void reset();

private:
    bool sdlInitialized_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_Texture* sharpTexture_ = nullptr;
    SDL_AudioDeviceID audioDevice_ = 0;
    SDL_GameController* gameController_ = nullptr;
};

} // namespace gb::frontend
#endif
