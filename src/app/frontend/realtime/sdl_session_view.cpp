#include "gb/app/frontend/realtime/sdl_session_view.hpp"

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

SdlSessionView::~SdlSessionView() {
    reset();
}

void SdlSessionView::reset() {
    if (gameController_) {
        SDL_GameControllerClose(gameController_);
        gameController_ = nullptr;
    }
    if (audioDevice_ != 0) {
        SDL_ClearQueuedAudio(audioDevice_);
        SDL_CloseAudioDevice(audioDevice_);
        audioDevice_ = 0;
    }
    if (sharpTexture_) {
        SDL_DestroyTexture(sharpTexture_);
        sharpTexture_ = nullptr;
    }
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }
}

void SdlSessionView::markSdlInitialized() {
    sdlInitialized_ = true;
}

void SdlSessionView::ownWindow(SDL_Window* window) {
    window_ = window;
}

void SdlSessionView::ownRenderer(SDL_Renderer* renderer) {
    renderer_ = renderer;
}

void SdlSessionView::ownTexture(SDL_Texture* texture) {
    texture_ = texture;
}

void SdlSessionView::ownSharpTexture(SDL_Texture* texture) {
    sharpTexture_ = texture;
}

void SdlSessionView::ownAudioDevice(SDL_AudioDeviceID device) {
    audioDevice_ = device;
}

void SdlSessionView::ownGameController(SDL_GameController* controller) {
    if (gameController_ && gameController_ != controller) {
        SDL_GameControllerClose(gameController_);
    }
    gameController_ = controller;
}

void SdlSessionView::closeGameController() {
    if (gameController_) {
        SDL_GameControllerClose(gameController_);
        gameController_ = nullptr;
    }
}

} // namespace gb::frontend
#endif
