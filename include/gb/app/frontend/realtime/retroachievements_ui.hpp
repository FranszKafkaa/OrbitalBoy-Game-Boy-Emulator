#pragma once

#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <optional>
#include <string>
#include <string_view>

#include "gb/app/frontend/realtime/retroachievements_models.hpp"

#ifdef GBEMU_USE_SDL2
#include "gb/app/sdl_compat.hpp"
#endif

namespace gb::frontend {

inline constexpr std::size_t kRaToastQueueLimit = 5;
inline constexpr std::uint64_t kRaToastDurationMs = 4000;
inline constexpr std::uint64_t kRaToastFadeMs = 600;

struct RaUiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct RaUiSize {
    int w = 0;
    int h = 0;
};

bool raUiRectContains(const RaUiRect& rect, int px, int py);
RaUiSize raWindowedSize(int requestedW, int requestedH);

enum class RaOverlayGameplayEvent {
    KeyboardDown,
    KeyboardUp,
    ControllerDown,
    ControllerUp,
    Other,
};

struct RaGameplayInputState {
    std::uint8_t joypadMask = 0;
    bool fastForward = false;
};

bool raOverlayConsumesGameplayEvent(RaOverlayGameplayEvent event);
RaGameplayInputState neutralizeRaGameplayInput(
    std::uint8_t joypadMask,
    bool fastForward
);
bool raShouldStopTextInput(bool loginOpen, bool anotherEditorActive);

enum class RaUiKey {
    Escape,
    Enter,
    Tab,
    Left,
    Right,
    Up,
    Down,
    PageUp,
    PageDown,
    Home,
    End,
    Backspace,
};

enum class RaLoginField {
    Username,
    Password,
};

enum class RaLoginModalAction {
    None,
    Submit,
    Close,
};

struct RaLoginModalState {
    bool open = false;
    RaLoginField focusedField = RaLoginField::Username;
    std::string username;
    std::string password;
    bool requesting = false;
    std::string errorText;
};

struct RaLoginModalLayout {
    RaUiRect overlay;
    RaUiRect panel;
    RaUiRect closeButton;
    RaUiRect usernameField;
    RaUiRect passwordField;
    RaUiRect submitButton;
    RaUiRect cancelButton;
};

RaLoginModalLayout raLoginModalLayout(int outputW, int outputH);
void openRaLoginModal(RaLoginModalState& state, std::string username = {});
void closeRaLoginModal(RaLoginModalState& state);
void appendRaLoginText(RaLoginModalState& state, std::string_view text);
void backspaceRaLoginText(RaLoginModalState& state);
std::string maskedRaPassword(const RaLoginModalState& state);
bool canSubmitRaLogin(const RaLoginModalState& state);
RaLoginModalAction handleRaLoginKey(RaLoginModalState& state, RaUiKey key);
RaLoginModalAction applyRaLoginSnapshot(
    RaLoginModalState& state,
    const RaSessionSnapshot& snapshot
);

enum class RaProfileTab {
    Summary = 0,
    CurrentGame = 1,
    Library = 2,
};

struct RaProfilePanelState {
    bool open = false;
    RaProfileTab tab = RaProfileTab::Summary;
    int scroll = 0;
};

struct RaProfilePanelLayout {
    RaUiRect overlay;
    RaUiRect panel;
    RaUiRect closeButton;
    std::array<RaUiRect, 3> tabs{};
    RaUiRect content;
};

RaProfilePanelLayout raProfilePanelLayout(int outputW, int outputH);
std::optional<RaProfileTab> hitTestRaProfileTab(
    const RaProfilePanelLayout& layout,
    int px,
    int py
);
void openRaProfilePanel(RaProfilePanelState& state, RaProfileTab tab);
void closeRaProfilePanel(RaProfilePanelState& state);
void cycleRaProfileTab(RaProfilePanelState& state, int direction);
int raProfileMaxScroll(int contentHeight, int viewportHeight);
void clampRaProfileScroll(
    RaProfilePanelState& state,
    int contentHeight,
    int viewportHeight
);
void scrollRaProfilePanel(
    RaProfilePanelState& state,
    int delta,
    int contentHeight,
    int viewportHeight
);
void handleRaProfileNavigation(
    RaProfilePanelState& state,
    RaUiKey key,
    int contentHeight,
    int viewportHeight
);
int raProfileContentHeight(
    const RaSessionSnapshot& snapshot,
    RaProfileTab tab,
    int contentWidth
);

struct RaVisibleRowRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

RaVisibleRowRange raVisibleProfileRows(
    RaProfileTab tab,
    std::size_t rowCount,
    int scroll,
    int viewportHeight
);

struct RaToastEntry {
    RaUiEvent event;
};

struct RaToastState {
    std::deque<RaToastEntry> queue;
    std::uint64_t activeSinceMs = 0;
};

struct RaToastLayout {
    RaUiRect card;
    RaUiRect badge;
};

RaToastLayout raToastLayout(int outputW, int outputH);
void enqueueRaToast(RaToastState& state, const RaUiEvent& event, std::uint64_t nowMs);
void advanceRaToast(RaToastState& state, std::uint64_t nowMs);
std::size_t raToastQueueSize(const RaToastState& state);
const RaUiEvent* currentRaToast(const RaToastState& state);
std::uint8_t raToastOpacity(const RaToastState& state, std::uint64_t nowMs);

#ifdef GBEMU_USE_SDL2

class RaImageTextureCache {
public:
    static constexpr std::size_t kDefaultCapacity = 64;

    explicit RaImageTextureCache(std::size_t capacity = kDefaultCapacity);
    ~RaImageTextureCache();

    RaImageTextureCache(const RaImageTextureCache&) = delete;
    RaImageTextureCache& operator=(const RaImageTextureCache&) = delete;

    void beginFrame();
    SDL_Texture* texture(SDL_Renderer* renderer, const std::string& localPath);
    [[nodiscard]] std::size_t entryCount() const;
    [[nodiscard]] std::size_t retiredCount() const;
    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] bool contains(const std::string& localPath) const;
    void clear();
    void shutdown();

private:
    struct Entry {
        std::string path;
        SDL_Texture* texture = nullptr;
        bool attempted = false;
    };
    std::list<Entry> entries_;
    std::deque<SDL_Texture*> retiredTextures_;
    std::size_t capacity_ = kDefaultCapacity;
#ifdef GBEMU_USE_SDL2_IMAGE
    int imageFormats_ = 0;
    bool imageSubsystemStarted_ = false;
#endif
};

RaLoginModalAction handleRaLoginModalEvent(
    RaLoginModalState& state,
    const SDL_Event& event,
    int outputW,
    int outputH
);
bool handleRaProfilePanelEvent(
    RaProfilePanelState& state,
    const RaSessionSnapshot& snapshot,
    const SDL_Event& event,
    int outputW,
    int outputH
);

void renderRaLoginModal(
    SDL_Renderer* renderer,
    const RaLoginModalState& state,
    const RaSessionSnapshot& snapshot,
    int outputW,
    int outputH
);
void renderRaProfilePanel(
    SDL_Renderer* renderer,
    RaProfilePanelState& state,
    const RaSessionSnapshot& snapshot,
    RaImageTextureCache& imageCache,
    int outputW,
    int outputH
);
void renderRaToast(
    SDL_Renderer* renderer,
    const RaToastState& state,
    RaImageTextureCache& imageCache,
    std::uint64_t nowMs,
    int outputW,
    int outputH
);

#endif

} // namespace gb::frontend

#endif
