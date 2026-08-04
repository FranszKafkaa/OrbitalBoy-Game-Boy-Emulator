#include "gb/app/frontend/realtime/retroachievements_ui.hpp"

#include "gb/app/frontend/realtime/secure_string.hpp"

#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

#ifdef GBEMU_USE_SDL2
#include "gb/app/frontend/debug_ui.hpp"
#include "gb/app/sdl_compat.hpp"
#endif

namespace gb::frontend {

namespace {

constexpr int kLoginPanelWidth = 460;
constexpr int kLoginPanelHeight = 300;
constexpr int kProfilePanelWidth = 860;
constexpr int kProfilePanelHeight = 620;
constexpr int kProfileFooterPad = 18;
constexpr int kAchievementRowHeight = 82;
constexpr int kLibraryRowHeight = 70;
constexpr int kProfileScrollStep = 32;

void eraseLastUtf8CodePoint(std::string& text) {
    if (text.empty()) {
        return;
    }
    std::size_t index = text.size() - 1;
    while (index > 0
        && (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U) {
        --index;
    }
    text.erase(index);
}

std::size_t utf8CodePointCount(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

std::size_t validUtf8SequenceLength(std::string_view text, std::size_t index) {
    if (index >= text.size()) {
        return 0;
    }
    const auto byte = [&](std::size_t offset) {
        return static_cast<unsigned char>(text[index + offset]);
    };
    const unsigned char lead = byte(0);
    if (lead <= 0x7FU) {
        return 1;
    }
    std::size_t length = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
    } else {
        return 0;
    }
    if (index + length > text.size()) {
        return 0;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
        if ((byte(offset) & 0xC0U) != 0x80U) {
            return 0;
        }
    }
    if (length == 3) {
        if (lead == 0xE0U && byte(1) < 0xA0U) {
            return 0;
        }
        if (lead == 0xEDU && byte(1) >= 0xA0U) {
            return 0;
        }
    }
    if (length == 4) {
        if (lead == 0xF0U && byte(1) < 0x90U) {
            return 0;
        }
        if (lead == 0xF4U && byte(1) > 0x8FU) {
            return 0;
        }
    }
    return length;
}

void appendRaLoginTextImpl(
    RaLoginModalState& state,
    std::string_view text,
    bool rejectControls
) {
    if (!state.open || state.requesting || text.empty()) {
        return;
    }
    std::string& field = state.focusedField == RaLoginField::Username
        ? state.username
        : state.password;
    const std::size_t limit = state.focusedField == RaLoginField::Username ? 256U : 1024U;
    for (std::size_t index = 0; index < text.size();) {
        const std::size_t length = validUtf8SequenceLength(text, index);
        if (length == 0) {
            ++index;
            continue;
        }
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (rejectControls && length == 1 && (byte < 0x20U || byte == 0x7FU)) {
            index += length;
            continue;
        }
        if (length > limit - std::min(field.size(), limit)) {
            break;
        }
        field.append(text.substr(index, length));
        index += length;
    }
    state.errorText.clear();
}

bool isScrollableTab(RaProfileTab tab) {
    return tab == RaProfileTab::CurrentGame || tab == RaProfileTab::Library;
}

int profileTabIndex(RaProfileTab tab) {
    return static_cast<int>(tab);
}

#ifdef GBEMU_USE_SDL2

std::string asciiUiText(std::string_view text, std::size_t maxChars = 80) {
    std::string out;
    out.reserve(std::min(text.size(), maxChars));
    for (std::size_t index = 0; index < text.size() && out.size() < maxChars; ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte >= 32U && byte <= 126U) {
            out.push_back(static_cast<char>(byte));
            continue;
        }
        if (byte == 0xC3U && index + 1 < text.size()) {
            const unsigned char suffix = static_cast<unsigned char>(text[++index]);
            char replacement = '\0';
            if ((suffix >= 0x80U && suffix <= 0x85U) || (suffix >= 0xA0U && suffix <= 0xA5U)) {
                replacement = suffix < 0xA0U ? 'A' : 'a';
            } else if (suffix == 0x87U || suffix == 0xA7U) {
                replacement = suffix == 0x87U ? 'C' : 'c';
            } else if ((suffix >= 0x88U && suffix <= 0x8BU) || (suffix >= 0xA8U && suffix <= 0xABU)) {
                replacement = suffix < 0xA0U ? 'E' : 'e';
            } else if ((suffix >= 0x8CU && suffix <= 0x8FU) || (suffix >= 0xACU && suffix <= 0xAFU)) {
                replacement = suffix < 0xA0U ? 'I' : 'i';
            } else if ((suffix >= 0x92U && suffix <= 0x96U) || (suffix >= 0xB2U && suffix <= 0xB6U)) {
                replacement = suffix < 0xA0U ? 'O' : 'o';
            } else if ((suffix >= 0x99U && suffix <= 0x9CU) || (suffix >= 0xB9U && suffix <= 0xBCU)) {
                replacement = suffix < 0xA0U ? 'U' : 'u';
            }
            if (replacement != '\0') {
                out.push_back(replacement);
            }
        }
    }
    return out;
}

SDL_Rect sdlRect(const RaUiRect& rect) {
    return SDL_Rect{rect.x, rect.y, rect.w, rect.h};
}

SDL_Color withAlpha(SDL_Color color, std::uint8_t alpha) {
    color.a = static_cast<Uint8>(
        static_cast<unsigned>(color.a) * static_cast<unsigned>(alpha) / 255U
    );
    return color;
}

void setColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void fillRect(SDL_Renderer* renderer, const RaUiRect& rect, SDL_Color color) {
    setColor(renderer, color);
    SDL_Rect target = sdlRect(rect);
    SDL_RenderFillRect(renderer, &target);
}

void outlineRect(SDL_Renderer* renderer, const RaUiRect& rect, SDL_Color color) {
    setColor(renderer, color);
    SDL_Rect target = sdlRect(rect);
    SDL_RenderDrawRect(renderer, &target);
}

void fillRoundedStyleRect(
    SDL_Renderer* renderer,
    const RaUiRect& rect,
    SDL_Color fill,
    SDL_Color border
) {
    if (rect.w < 8 || rect.h < 8) {
        fillRect(renderer, rect, fill);
        outlineRect(renderer, rect, border);
        return;
    }
    fillRect(renderer, RaUiRect{rect.x + 3, rect.y, rect.w - 6, rect.h}, fill);
    fillRect(renderer, RaUiRect{rect.x, rect.y + 3, rect.w, rect.h - 6}, fill);
    setColor(renderer, border);
    SDL_RenderDrawLine(renderer, rect.x + 3, rect.y, rect.x + rect.w - 4, rect.y);
    SDL_RenderDrawLine(
        renderer,
        rect.x + 3,
        rect.y + rect.h - 1,
        rect.x + rect.w - 4,
        rect.y + rect.h - 1
    );
    SDL_RenderDrawLine(renderer, rect.x, rect.y + 3, rect.x, rect.y + rect.h - 4);
    SDL_RenderDrawLine(
        renderer,
        rect.x + rect.w - 1,
        rect.y + 3,
        rect.x + rect.w - 1,
        rect.y + rect.h - 4
    );
    SDL_RenderDrawPoint(renderer, rect.x + 1, rect.y + 2);
    SDL_RenderDrawPoint(renderer, rect.x + 2, rect.y + 1);
    SDL_RenderDrawPoint(renderer, rect.x + rect.w - 2, rect.y + 2);
    SDL_RenderDrawPoint(renderer, rect.x + rect.w - 3, rect.y + 1);
    SDL_RenderDrawPoint(renderer, rect.x + 1, rect.y + rect.h - 3);
    SDL_RenderDrawPoint(renderer, rect.x + 2, rect.y + rect.h - 2);
    SDL_RenderDrawPoint(renderer, rect.x + rect.w - 2, rect.y + rect.h - 3);
    SDL_RenderDrawPoint(renderer, rect.x + rect.w - 3, rect.y + rect.h - 2);
}

void drawPlus(SDL_Renderer* renderer, int x, int y, SDL_Color color) {
    setColor(renderer, color);
    SDL_RenderDrawLine(renderer, x + 3, y, x + 3, y + 6);
    SDL_RenderDrawLine(renderer, x, y + 3, x + 6, y + 3);
}

void drawTrophy(SDL_Renderer* renderer, const RaUiRect& rect, SDL_Color color) {
    const int cx = rect.x + rect.w / 2;
    const int top = rect.y + std::max(4, rect.h / 6);
    const int cupW = std::max(12, rect.w / 2);
    const int cupH = std::max(8, rect.h / 4);
    fillRect(renderer, RaUiRect{cx - cupW / 2, top, cupW, cupH}, color);
    setColor(renderer, color);
    SDL_Rect leftHandle{
        cx - cupW / 2 - 4,
        top + 2,
        4,
        std::max(3, cupH - 4),
    };
    SDL_Rect rightHandle{
        cx + cupW / 2,
        top + 2,
        4,
        std::max(3, cupH - 4),
    };
    SDL_RenderDrawRect(renderer, &leftHandle);
    SDL_RenderDrawRect(renderer, &rightHandle);
    fillRect(renderer, RaUiRect{cx - 2, top + cupH, 4, 7}, color);
    fillRect(renderer, RaUiRect{cx - 8, top + cupH + 7, 16, 3}, color);
}

void drawImageOrPlaceholder(
    SDL_Renderer* renderer,
    RaImageTextureCache& cache,
    const std::string& path,
    const RaUiRect& rect,
    const char* placeholder,
    std::uint8_t alpha = 255
) {
    SDL_Texture* texture = cache.texture(renderer, path);
    if (texture != nullptr) {
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_Rect target = sdlRect(rect);
        SDL_RenderCopy(renderer, texture, nullptr, &target);
        SDL_SetTextureAlphaMod(texture, 255);
        return;
    }
    fillRoundedStyleRect(
        renderer,
        rect,
        withAlpha(SDL_Color{18, 24, 36, 255}, alpha),
        withAlpha(SDL_Color{88, 104, 132, 255}, alpha)
    );
    if (placeholder != nullptr) {
        const std::string label = asciiUiText(placeholder, 8);
        drawHexText(
            renderer,
            rect.x + std::max(3, (rect.w - static_cast<int>(label.size()) * 6) / 2),
            rect.y + std::max(3, rect.h / 2 - 4),
            label,
            withAlpha(SDL_Color{170, 182, 206, 255}, alpha),
            1
        );
    }
}

RaUiKey raUiKeyFromSdl(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE: return RaUiKey::Escape;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return RaUiKey::Enter;
    case SDLK_TAB: return RaUiKey::Tab;
    case SDLK_LEFT: return RaUiKey::Left;
    case SDLK_RIGHT: return RaUiKey::Right;
    case SDLK_UP: return RaUiKey::Up;
    case SDLK_DOWN: return RaUiKey::Down;
    case SDLK_PAGEUP: return RaUiKey::PageUp;
    case SDLK_PAGEDOWN: return RaUiKey::PageDown;
    case SDLK_HOME: return RaUiKey::Home;
    case SDLK_END: return RaUiKey::End;
    default: return RaUiKey::Backspace;
    }
}

bool isRaNavigationKey(SDL_Keycode key) {
    return key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER
        || key == SDLK_TAB || key == SDLK_LEFT || key == SDLK_RIGHT
        || key == SDLK_UP || key == SDLK_DOWN || key == SDLK_PAGEUP
        || key == SDLK_PAGEDOWN || key == SDLK_HOME || key == SDLK_END
        || key == SDLK_BACKSPACE;
}

void drawProgressBar(
    SDL_Renderer* renderer,
    const RaUiRect& rect,
    std::uint32_t unlocked,
    std::uint32_t total
) {
    fillRect(renderer, rect, SDL_Color{22, 29, 43, 255});
    outlineRect(renderer, rect, SDL_Color{67, 82, 112, 255});
    const std::uint32_t safeUnlocked = std::min(unlocked, total);
    const int innerW = total == 0
        ? 0
        : static_cast<int>(
            static_cast<std::uint64_t>(std::max(0, rect.w - 2)) * safeUnlocked / total
        );
    if (innerW > 0) {
        fillRect(
            renderer,
            RaUiRect{rect.x + 1, rect.y + 1, innerW, std::max(0, rect.h - 2)},
            SDL_Color{230, 184, 72, 255}
        );
    }
}

#endif

} // namespace

bool raUiRectContains(const RaUiRect& rect, int px, int py) {
    return px >= rect.x && px < rect.x + rect.w
        && py >= rect.y && py < rect.y + rect.h;
}

RaUiSize raWindowedSize(int requestedW, int requestedH) {
    return RaUiSize{
        std::max(640, requestedW),
        std::max(480, requestedH),
    };
}

bool raOverlayConsumesGameplayEvent(RaOverlayGameplayEvent event) {
    return event != RaOverlayGameplayEvent::KeyboardUp
        && event != RaOverlayGameplayEvent::ControllerUp;
}

RaGameplayInputState neutralizeRaGameplayInput(
    std::uint8_t joypadMask,
    bool fastForward
) {
    (void)joypadMask;
    (void)fastForward;
    return RaGameplayInputState{};
}

bool raShouldStopTextInput(bool loginOpen, bool anotherEditorActive) {
    return !loginOpen && !anotherEditorActive;
}

RaLoginModalLayout raLoginModalLayout(int outputW, int outputH) {
    const int safeW = std::max(1, outputW);
    const int safeH = std::max(1, outputH);
    const int margin = safeW < 360 || safeH < 280 ? 4 : 12;
    const int panelW = std::max(1, std::min(kLoginPanelWidth, safeW - margin * 2));
    const int panelH = std::max(1, std::min(kLoginPanelHeight, safeH - margin * 2));
    const int x = std::max(0, (safeW - panelW) / 2);
    const int y = std::max(0, (safeH - panelH) / 2);
    const bool compact = panelW < 360 || panelH < 250;
    const int pad = compact ? std::min(8, panelW / 6) : 24;
    const int fieldW = std::max(1, panelW - pad * 2);
    const int fieldH = compact ? std::min(24, std::max(1, panelH / 5)) : 34;
    const int usernameY = compact ? y + std::min(34, panelH / 4) : y + 82;
    const int passwordY = compact
        ? std::min(y + panelH - fieldH - 38, usernameY + fieldH + 8)
        : y + 146;
    const int buttonGap = compact ? 6 : 12;
    const int buttonH = compact ? std::min(22, std::max(1, panelH / 6)) : 30;
    const int buttonW = std::max(1, (fieldW - buttonGap) / 2);
    const int buttonY = y + panelH - pad - buttonH;
    const int closeSize = std::max(1, std::min(compact ? 14 : 18, panelW - 2));
    return RaLoginModalLayout{
        RaUiRect{0, 0, safeW, safeH},
        RaUiRect{x, y, panelW, panelH},
        RaUiRect{x + panelW - pad - closeSize, y + pad, closeSize, closeSize},
        RaUiRect{x + pad, usernameY, fieldW, fieldH},
        RaUiRect{x + pad, passwordY, fieldW, fieldH},
        RaUiRect{x + pad, buttonY, buttonW, buttonH},
        RaUiRect{x + pad + buttonW + buttonGap, buttonY, buttonW, buttonH},
    };
}

void openRaLoginModal(RaLoginModalState& state, std::string username) {
    (void)secureEraseStringStorage(state.password);
    state.open = true;
    state.focusedField = RaLoginField::Username;
    state.username = std::move(username);
    state.requesting = false;
    state.errorText.clear();
}

void closeRaLoginModal(RaLoginModalState& state) {
    state.open = false;
    state.requesting = false;
    state.errorText.clear();
    (void)secureEraseStringStorage(state.password);
}

void appendRaLoginText(RaLoginModalState& state, std::string_view text) {
    appendRaLoginTextImpl(state, text, false);
}

void pasteRaLoginText(RaLoginModalState& state, std::string_view text) {
    appendRaLoginTextImpl(state, text, true);
}

void backspaceRaLoginText(RaLoginModalState& state) {
    if (!state.open || state.requesting) {
        return;
    }
    std::string& field = state.focusedField == RaLoginField::Username
        ? state.username
        : state.password;
    eraseLastUtf8CodePoint(field);
    state.errorText.clear();
}

std::string maskedRaPassword(const RaLoginModalState& state) {
    return std::string(utf8CodePointCount(state.password), '*');
}

bool canSubmitRaLogin(const RaLoginModalState& state) {
    return state.open && !state.requesting
        && !state.username.empty() && !state.password.empty();
}

RaLoginModalAction handleRaLoginKey(RaLoginModalState& state, RaUiKey key) {
    if (!state.open) {
        return RaLoginModalAction::None;
    }
    switch (key) {
    case RaUiKey::Escape:
        closeRaLoginModal(state);
        return RaLoginModalAction::Close;
    case RaUiKey::Enter:
        if (canSubmitRaLogin(state)) {
            state.requesting = true;
            state.errorText.clear();
            return RaLoginModalAction::Submit;
        }
        return RaLoginModalAction::None;
    case RaUiKey::Tab:
    case RaUiKey::Up:
    case RaUiKey::Down:
        state.focusedField = state.focusedField == RaLoginField::Username
            ? RaLoginField::Password
            : RaLoginField::Username;
        return RaLoginModalAction::None;
    case RaUiKey::Backspace:
        backspaceRaLoginText(state);
        return RaLoginModalAction::None;
    default:
        return RaLoginModalAction::None;
    }
}

RaLoginModalAction applyRaLoginSnapshot(
    RaLoginModalState& state,
    const RaSessionSnapshot& snapshot
) {
    if (!state.open) {
        return RaLoginModalAction::None;
    }
    if (snapshot.connectionState == RaConnectionState::LoggingIn) {
        state.requesting = true;
        state.errorText.clear();
    } else if (snapshot.connectionState == RaConnectionState::Online) {
        closeRaLoginModal(state);
        return RaLoginModalAction::Close;
    } else {
        state.requesting = false;
        if (!snapshot.errorText.empty()) {
            state.errorText = snapshot.errorText;
        } else if (snapshot.connectionState == RaConnectionState::Error
            || snapshot.connectionState == RaConnectionState::Offline) {
            state.errorText = "Nao foi possivel entrar. Verifique a conexao e tente novamente.";
        }
    }
    return RaLoginModalAction::None;
}

RaProfilePanelLayout raProfilePanelLayout(int outputW, int outputH) {
    const int safeW = std::max(1, outputW);
    const int safeH = std::max(1, outputH);
    const int margin = safeW < 480 || safeH < 360 ? 4 : 12;
    const int panelW = std::max(1, std::min(kProfilePanelWidth, safeW - margin * 2));
    const int panelH = std::max(1, std::min(kProfilePanelHeight, safeH - margin * 2));
    const int x = std::max(0, (safeW - panelW) / 2);
    const int y = std::max(0, (safeH - panelH) / 2);
    const bool compact = panelW < 480 || panelH < 360;
    const int pad = compact ? std::min(4, panelW / 8) : 20;
    const int tabX = x + pad;
    const int tabY = y + (compact ? std::min(24, panelH / 5) : 50);
    const int tabH = compact ? std::min(20, std::max(1, panelH / 6)) : 28;
    const int tabAreaW = std::max(1, panelW - pad * 2);
    const int tabW = std::max(1, tabAreaW / 3);
    std::array<RaUiRect, 3> tabs{};
    for (int index = 0; index < 3; ++index) {
        const int currentX = tabX + index * tabW;
        const int currentW = index == 2
            ? std::max(1, tabX + tabAreaW - currentX)
            : tabW;
        tabs[static_cast<std::size_t>(index)] = RaUiRect{
            currentX,
            tabY,
            currentW,
            tabH,
        };
    }
    const int contentY = tabY + tabH + (compact ? 2 : 8);
    const int contentBottom = y + panelH - (compact ? pad : kProfileFooterPad);
    const int closeSize = std::max(1, std::min(compact ? 14 : 18, panelW - 2));
    return RaProfilePanelLayout{
        RaUiRect{0, 0, safeW, safeH},
        RaUiRect{x, y, panelW, panelH},
        RaUiRect{x + panelW - pad - closeSize, y + pad, closeSize, closeSize},
        tabs,
        RaUiRect{
            x + pad,
            contentY,
            tabAreaW,
            std::max(1, contentBottom - contentY),
        },
    };
}

std::optional<RaProfileTab> hitTestRaProfileTab(
    const RaProfilePanelLayout& layout,
    int px,
    int py
) {
    for (std::size_t index = 0; index < layout.tabs.size(); ++index) {
        if (raUiRectContains(layout.tabs[index], px, py)) {
            return static_cast<RaProfileTab>(index);
        }
    }
    return std::nullopt;
}

void openRaProfilePanel(RaProfilePanelState& state, RaProfileTab tab) {
    state.open = true;
    state.tab = tab;
    state.scroll = 0;
}

void closeRaProfilePanel(RaProfilePanelState& state) {
    state.open = false;
    state.scroll = 0;
}

void cycleRaProfileTab(RaProfilePanelState& state, int direction) {
    const int count = 3;
    const int step = direction < 0 ? -1 : 1;
    const int next = (profileTabIndex(state.tab) + count + step) % count;
    state.tab = static_cast<RaProfileTab>(next);
    state.scroll = 0;
}

int raProfileMaxScroll(int contentHeight, int viewportHeight) {
    return std::max(0, contentHeight - std::max(0, viewportHeight));
}

void clampRaProfileScroll(
    RaProfilePanelState& state,
    int contentHeight,
    int viewportHeight
) {
    if (!isScrollableTab(state.tab)) {
        state.scroll = 0;
        return;
    }
    state.scroll = std::clamp(
        state.scroll,
        0,
        raProfileMaxScroll(contentHeight, viewportHeight)
    );
}

void scrollRaProfilePanel(
    RaProfilePanelState& state,
    int delta,
    int contentHeight,
    int viewportHeight
) {
    if (!isScrollableTab(state.tab)) {
        state.scroll = 0;
        return;
    }
    state.scroll += delta;
    clampRaProfileScroll(state, contentHeight, viewportHeight);
}

void handleRaProfileNavigation(
    RaProfilePanelState& state,
    RaUiKey key,
    int contentHeight,
    int viewportHeight
) {
    if (!state.open) {
        return;
    }
    switch (key) {
    case RaUiKey::Escape:
        closeRaProfilePanel(state);
        return;
    case RaUiKey::Left:
        cycleRaProfileTab(state, -1);
        return;
    case RaUiKey::Right:
    case RaUiKey::Tab:
        cycleRaProfileTab(state, 1);
        return;
    case RaUiKey::Up:
        scrollRaProfilePanel(state, -kProfileScrollStep, contentHeight, viewportHeight);
        return;
    case RaUiKey::Down:
        scrollRaProfilePanel(state, kProfileScrollStep, contentHeight, viewportHeight);
        return;
    case RaUiKey::PageUp:
        scrollRaProfilePanel(
            state,
            -std::max(1, viewportHeight * 4 / 5),
            contentHeight,
            viewportHeight
        );
        return;
    case RaUiKey::PageDown:
        scrollRaProfilePanel(
            state,
            std::max(1, viewportHeight * 4 / 5),
            contentHeight,
            viewportHeight
        );
        return;
    case RaUiKey::Home:
        state.scroll = 0;
        return;
    case RaUiKey::End:
        state.scroll = raProfileMaxScroll(contentHeight, viewportHeight);
        return;
    default:
        return;
    }
}

int raProfileContentHeight(
    const RaSessionSnapshot& snapshot,
    RaProfileTab tab,
    int contentWidth
) {
    (void)contentWidth;
    switch (tab) {
    case RaProfileTab::Summary:
        return 260;
    case RaProfileTab::CurrentGame:
        return 112
            + static_cast<int>(snapshot.currentAchievements.size()) * kAchievementRowHeight;
    case RaProfileTab::Library:
        return 28 + static_cast<int>(snapshot.profile.library.size()) * kLibraryRowHeight;
    }
    return 0;
}

RaVisibleRowRange raVisibleProfileRows(
    RaProfileTab tab,
    std::size_t rowCount,
    int scroll,
    int viewportHeight
) {
    if (rowCount == 0 || viewportHeight <= 0 || tab == RaProfileTab::Summary) {
        return {};
    }

    const std::int64_t headerHeight =
        tab == RaProfileTab::CurrentGame ? 100 : 28;
    const std::int64_t rowHeight =
        tab == RaProfileTab::CurrentGame ? kAchievementRowHeight : kLibraryRowHeight;
    const std::int64_t viewTop = std::max(0, scroll);
    const std::int64_t viewBottom = viewTop + viewportHeight;
    const std::int64_t first = viewTop <= headerHeight
        ? 0
        : (viewTop - headerHeight) / rowHeight;
    const std::int64_t end = viewBottom <= headerHeight
        ? 0
        : (viewBottom - headerHeight + rowHeight - 1) / rowHeight;
    return RaVisibleRowRange{
        std::min(rowCount, static_cast<std::size_t>(first)),
        std::min(rowCount, static_cast<std::size_t>(end)),
    };
}

std::vector<std::string> raVisibleImageUrls(
    const RaSessionSnapshot& snapshot,
    const RaProfilePanelState& panel,
    int outputW,
    int outputH
) {
    std::vector<std::string> urls;
    const auto append = [&](const std::string& url) {
        if (!url.empty()
            && std::find(urls.begin(), urls.end(), url) == urls.end()) {
            urls.push_back(url);
        }
    };
    append(snapshot.profile.user.avatarUrl);
    append(snapshot.currentGame.badgeUrl);
    if (!panel.open || panel.tab == RaProfileTab::Summary) {
        return urls;
    }
    const auto layout = raProfilePanelLayout(outputW, outputH);
    if (panel.tab == RaProfileTab::CurrentGame) {
        const auto visible = raVisibleProfileRows(
            panel.tab,
            snapshot.currentAchievements.size(),
            panel.scroll,
            layout.content.h
        );
        for (std::size_t index = visible.begin; index < visible.end; ++index) {
            append(snapshot.currentAchievements[index].badgeUrl);
        }
    } else {
        const auto visible = raVisibleProfileRows(
            panel.tab,
            snapshot.profile.library.size(),
            panel.scroll,
            layout.content.h
        );
        for (std::size_t index = visible.begin; index < visible.end; ++index) {
            append(snapshot.profile.library[index].badgeUrl);
        }
    }
    return urls;
}

RaToastLayout raToastLayout(int outputW, int outputH) {
    const int safeW = std::max(1, outputW);
    const int safeH = std::max(1, outputH);
    const int margin = safeW < 360 || safeH < 220 ? 8 : 18;
    const int cardW = std::max(1, std::min(450, safeW - margin * 2));
    const int cardH = std::max(1, std::min(112, safeH - margin * 2));
    const int x = std::max(0, safeW - cardW - margin);
    const int y = cardH + 32 <= safeH ? 32 : std::max(0, safeH - cardH - margin);
    const int badgePad = cardW < 240 ? std::min(8, cardW / 8) : 16;
    const int badgeSize = std::max(
        1,
        std::min({68, std::max(1, cardH - badgePad * 2), std::max(1, cardW / 4)})
    );
    return RaToastLayout{
        RaUiRect{x, y, cardW, cardH},
        RaUiRect{x + badgePad, y + (cardH - badgeSize) / 2, badgeSize, badgeSize},
    };
}

void enqueueRaToast(RaToastState& state, const RaUiEvent& event, std::uint64_t nowMs) {
    if (event.type != RaUiEventType::AchievementUnlocked) {
        return;
    }
    if (state.queue.size() >= kRaToastQueueLimit) {
        state.queue.pop_front();
        state.activeSinceMs = nowMs;
    }
    const bool wasEmpty = state.queue.empty();
    state.queue.push_back(RaToastEntry{event});
    if (wasEmpty) {
        state.activeSinceMs = nowMs;
    }
}

void advanceRaToast(RaToastState& state, std::uint64_t nowMs) {
    if (state.queue.empty()) {
        state.activeSinceMs = nowMs;
        return;
    }
    if (nowMs < state.activeSinceMs) {
        state.activeSinceMs = nowMs;
        return;
    }
    while (!state.queue.empty()
        && nowMs - state.activeSinceMs >= kRaToastDurationMs) {
        state.queue.pop_front();
        state.activeSinceMs += kRaToastDurationMs;
    }
    if (state.queue.empty()) {
        state.activeSinceMs = nowMs;
    }
}

std::size_t raToastQueueSize(const RaToastState& state) {
    return state.queue.size();
}

const RaUiEvent* currentRaToast(const RaToastState& state) {
    return state.queue.empty() ? nullptr : &state.queue.front().event;
}

std::uint8_t raToastOpacity(const RaToastState& state, std::uint64_t nowMs) {
    if (state.queue.empty() || nowMs < state.activeSinceMs) {
        return 0;
    }
    const std::uint64_t elapsed = nowMs - state.activeSinceMs;
    if (elapsed >= kRaToastDurationMs) {
        return 0;
    }
    const std::uint64_t fadeStart = kRaToastDurationMs - kRaToastFadeMs;
    if (elapsed <= fadeStart) {
        return 255;
    }
    const std::uint64_t remaining = kRaToastDurationMs - elapsed;
    return static_cast<std::uint8_t>(remaining * 255U / kRaToastFadeMs);
}

#ifdef GBEMU_USE_SDL2

RaImageTextureCache::RaImageTextureCache(std::size_t capacity)
    : capacity_(std::max<std::size_t>(1, capacity)) {
#ifdef GBEMU_USE_SDL2_IMAGE
    imageSubsystemStarted_ = true;
    const int requested = IMG_INIT_JPG | IMG_INIT_PNG;
    imageFormats_ = IMG_Init(requested) & requested;
#endif
}

RaImageTextureCache::~RaImageTextureCache() {
    shutdown();
}

void RaImageTextureCache::beginFrame() {
    for (SDL_Texture* texture : retiredTextures_) {
        SDL_DestroyTexture(texture);
    }
    retiredTextures_.clear();
}

SDL_Texture* RaImageTextureCache::texture(
    SDL_Renderer* renderer,
    const std::string& localPath
) {
    if (renderer == nullptr || localPath.empty()) {
        return nullptr;
    }
    auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.path == localPath;
    });
    if (found != entries_.end()) {
        entries_.splice(entries_.begin(), entries_, found);
        found = entries_.begin();
    } else {
        while (entries_.size() >= capacity_) {
            Entry& evicted = entries_.back();
            if (evicted.texture != nullptr) {
                retiredTextures_.push_back(evicted.texture);
                evicted.texture = nullptr;
            }
            entries_.pop_back();
        }
        entries_.push_front(Entry{localPath, nullptr, false});
        found = entries_.begin();
    }
    if (found->attempted) {
        return found->texture;
    }
    found->attempted = true;
    SDL_Surface* surface = nullptr;
#ifdef GBEMU_USE_SDL2_IMAGE
    if (imageFormats_ != 0) {
        surface = IMG_Load(localPath.c_str());
    }
#else
    surface = SDL_LoadBMP(localPath.c_str());
#endif
    if (surface != nullptr) {
        found->texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (found->texture != nullptr) {
            SDL_SetTextureBlendMode(found->texture, SDL_BLENDMODE_BLEND);
        }
    }
    return found->texture;
}

std::size_t RaImageTextureCache::entryCount() const {
    return entries_.size();
}

std::size_t RaImageTextureCache::retiredCount() const {
    return retiredTextures_.size();
}

std::size_t RaImageTextureCache::capacity() const {
    return capacity_;
}

bool RaImageTextureCache::contains(const std::string& localPath) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        return entry.path == localPath;
    });
}

void RaImageTextureCache::clear() {
    for (auto& entry : entries_) {
        if (entry.texture != nullptr) {
            SDL_DestroyTexture(entry.texture);
            entry.texture = nullptr;
        }
    }
    entries_.clear();
    beginFrame();
}

void RaImageTextureCache::shutdown() {
    clear();
#ifdef GBEMU_USE_SDL2_IMAGE
    if (imageSubsystemStarted_) {
        IMG_Quit();
        imageSubsystemStarted_ = false;
        imageFormats_ = 0;
    }
#endif
}

bool isRaLoginPasteShortcut(SDL_Keycode key, SDL_Keymod modifiers) {
#if defined(__APPLE__)
    constexpr SDL_Keymod kPasteModifier = KMOD_GUI;
#else
    constexpr SDL_Keymod kPasteModifier = KMOD_CTRL;
#endif
    return key == SDLK_v && (modifiers & kPasteModifier) != 0;
}

RaLoginModalAction handleRaLoginModalEvent(
    RaLoginModalState& state,
    const SDL_Event& event,
    int outputW,
    int outputH
) {
    if (!state.open) {
        return RaLoginModalAction::None;
    }
    if (event.type == SDL_TEXTINPUT) {
        appendRaLoginText(state, event.text.text);
        return RaLoginModalAction::None;
    }
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0
        && isRaLoginPasteShortcut(
            event.key.keysym.sym,
            static_cast<SDL_Keymod>(event.key.keysym.mod)
        )) {
        char* clipboard = SDL_GetClipboardText();
        if (clipboard != nullptr) {
            const bool wipingPasswordClipboard = state.focusedField == RaLoginField::Password;
            pasteRaLoginText(state, clipboard);
            if (wipingPasswordClipboard) {
                const std::size_t clipboardSize = std::strlen(clipboard);
                volatile char* clipboardBytes = clipboard;
                for (std::size_t index = 0; index < clipboardSize; ++index) {
                    clipboardBytes[index] = '\0';
                }
            }
            SDL_free(clipboard);
        }
        return RaLoginModalAction::None;
    }
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0
        && isRaNavigationKey(event.key.keysym.sym)) {
        const RaLoginModalAction action = handleRaLoginKey(
            state,
            raUiKeyFromSdl(event.key.keysym.sym)
        );
        return action;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const auto layout = raLoginModalLayout(outputW, outputH);
        const int px = event.button.x;
        const int py = event.button.y;
        if (raUiRectContains(layout.usernameField, px, py)) {
            state.focusedField = RaLoginField::Username;
        } else if (raUiRectContains(layout.passwordField, px, py)) {
            state.focusedField = RaLoginField::Password;
        } else if (raUiRectContains(layout.submitButton, px, py)) {
            return handleRaLoginKey(state, RaUiKey::Enter);
        } else if (raUiRectContains(layout.closeButton, px, py)
            || raUiRectContains(layout.cancelButton, px, py)) {
            closeRaLoginModal(state);
            return RaLoginModalAction::Close;
        }
    }
    return RaLoginModalAction::None;
}

bool handleRaProfilePanelEvent(
    RaProfilePanelState& state,
    const RaSessionSnapshot& snapshot,
    const SDL_Event& event,
    int outputW,
    int outputH
) {
    if (!state.open) {
        return false;
    }
    const auto layout = raProfilePanelLayout(outputW, outputH);
    const int contentHeight = raProfileContentHeight(
        snapshot,
        state.tab,
        layout.content.w
    );
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0
        && isRaNavigationKey(event.key.keysym.sym)) {
        handleRaProfileNavigation(
            state,
            raUiKeyFromSdl(event.key.keysym.sym),
            contentHeight,
            layout.content.h
        );
        return true;
    }
    if (event.type == SDL_MOUSEWHEEL) {
        scrollRaProfilePanel(
            state,
            -event.wheel.y * kProfileScrollStep,
            contentHeight,
            layout.content.h
        );
        return true;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const int px = event.button.x;
        const int py = event.button.y;
        if (raUiRectContains(layout.closeButton, px, py)) {
            closeRaProfilePanel(state);
            return true;
        }
        if (const auto tab = hitTestRaProfileTab(layout, px, py); tab.has_value()) {
            state.tab = tab.value();
            state.scroll = 0;
            return true;
        }
        return raUiRectContains(layout.panel, px, py);
    }
    return event.type == SDL_TEXTINPUT;
}

void renderRaLoginModal(
    SDL_Renderer* renderer,
    const RaLoginModalState& state,
    const RaSessionSnapshot& snapshot,
    int outputW,
    int outputH
) {
    if (renderer == nullptr || !state.open) {
        return;
    }
    SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &oldBlend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto layout = raLoginModalLayout(outputW, outputH);
    const bool compact = layout.panel.w < 360 || layout.panel.h < 250;
    fillRect(renderer, layout.overlay, SDL_Color{3, 6, 12, 188});
    fillRoundedStyleRect(
        renderer,
        layout.panel,
        SDL_Color{10, 15, 25, 248},
        SDL_Color{220, 177, 66, 255}
    );
    outlineRect(
        renderer,
        RaUiRect{
            layout.panel.x + 3,
            layout.panel.y + 3,
            layout.panel.w - 6,
            layout.panel.h - 6,
        },
        SDL_Color{76, 63, 35, 255}
    );
    drawHexText(
        renderer,
        layout.panel.x + 24,
        layout.panel.y + (compact ? 10 : 22),
        "RETROACHIEVEMENTS",
        SDL_Color{245, 204, 85, 255},
        compact ? 1 : 2
    );
    drawHexText(
        renderer,
        layout.closeButton.x + 5,
        layout.closeButton.y + 5,
        "X",
        SDL_Color{244, 216, 142, 255},
        1
    );
    drawHexText(
        renderer,
        layout.usernameField.x,
        layout.usernameField.y - (compact ? 10 : 14),
        "USUARIO",
        SDL_Color{176, 190, 216, 255},
        1
    );
    drawHexText(
        renderer,
        layout.passwordField.x,
        layout.passwordField.y - (compact ? 10 : 14),
        "SENHA",
        SDL_Color{176, 190, 216, 255},
        1
    );
    fillRoundedStyleRect(
        renderer,
        layout.usernameField,
        SDL_Color{17, 23, 36, 255},
        state.focusedField == RaLoginField::Username
            ? SDL_Color{238, 192, 76, 255}
            : SDL_Color{72, 88, 118, 255}
    );
    fillRoundedStyleRect(
        renderer,
        layout.passwordField,
        SDL_Color{17, 23, 36, 255},
        state.focusedField == RaLoginField::Password
            ? SDL_Color{238, 192, 76, 255}
            : SDL_Color{72, 88, 118, 255}
    );
    drawHexText(
        renderer,
        layout.usernameField.x + 10,
        layout.usernameField.y + 13,
        asciiUiText(
            state.username,
            static_cast<std::size_t>(
                std::max(1, (layout.usernameField.w - 20) / 6)
            )
        ),
        SDL_Color{232, 238, 250, 255},
        1
    );
    const int maskCount = static_cast<int>(maskedRaPassword(state).size());
    const int visibleMaskCount = std::min(
        maskCount,
        std::max(0, (layout.passwordField.w - 20) / 7)
    );
    setColor(renderer, SDL_Color{232, 238, 250, 255});
    for (int index = 0; index < visibleMaskCount; ++index) {
        const int cx = layout.passwordField.x + 12 + index * 7;
        const int cy = layout.passwordField.y + 17;
        SDL_RenderDrawLine(renderer, cx - 2, cy, cx + 2, cy);
        SDL_RenderDrawLine(renderer, cx, cy - 2, cx, cy + 2);
    }
    const bool submitEnabled = canSubmitRaLogin(state);
    fillRoundedStyleRect(
        renderer,
        layout.submitButton,
        submitEnabled ? SDL_Color{71, 55, 19, 255} : SDL_Color{34, 36, 40, 255},
        submitEnabled ? SDL_Color{238, 192, 76, 255} : SDL_Color{82, 84, 90, 255}
    );
    fillRoundedStyleRect(
        renderer,
        layout.cancelButton,
        SDL_Color{28, 33, 45, 255},
        SDL_Color{92, 106, 134, 255}
    );
    drawHexText(
        renderer,
        layout.submitButton.x + 22,
        layout.submitButton.y + 11,
        state.requesting ? "AGUARDE" : "ENTRAR",
        submitEnabled
            ? SDL_Color{255, 226, 137, 255}
            : SDL_Color{132, 136, 146, 255},
        1
    );
    drawHexText(
        renderer,
        layout.cancelButton.x + 18,
        layout.cancelButton.y + 11,
        "CANCELAR",
        SDL_Color{205, 215, 234, 255},
        1
    );
    const std::string error = !state.errorText.empty()
        ? state.errorText
        : snapshot.errorText;
    const int feedbackY = compact
        ? layout.passwordField.y + layout.passwordField.h + 5
        : layout.panel.y + layout.panel.h - 76;
    if (!error.empty()) {
        drawHexText(
            renderer,
            layout.panel.x + (compact ? 8 : 24),
            feedbackY,
            asciiUiText(
                error,
                static_cast<std::size_t>(
                    std::max(1, (layout.panel.w - (compact ? 16 : 48)) / 6)
                )
            ),
            SDL_Color{255, 142, 132, 255},
            1
        );
    } else if (state.requesting) {
        drawHexText(
            renderer,
            layout.panel.x + (compact ? 8 : 24),
            feedbackY,
            "ENTRANDO NO RETROACHIEVEMENTS...",
            SDL_Color{245, 204, 85, 255},
            1
        );
    }
    SDL_SetRenderDrawBlendMode(renderer, oldBlend);
}

void renderRaProfilePanel(
    SDL_Renderer* renderer,
    RaProfilePanelState& state,
    const RaSessionSnapshot& snapshot,
    RaImageTextureCache& imageCache,
    int outputW,
    int outputH
) {
    if (renderer == nullptr || !state.open) {
        return;
    }
    SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &oldBlend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto layout = raProfilePanelLayout(outputW, outputH);
    const int contentHeight = raProfileContentHeight(
        snapshot,
        state.tab,
        layout.content.w
    );
    clampRaProfileScroll(state, contentHeight, layout.content.h);
    fillRect(renderer, layout.overlay, SDL_Color{3, 6, 12, 190});
    fillRoundedStyleRect(
        renderer,
        layout.panel,
        SDL_Color{9, 14, 24, 250},
        SDL_Color{211, 169, 61, 255}
    );
    drawHexText(
        renderer,
        layout.panel.x + 20,
        layout.panel.y + 19,
        asciiUiText(
            "MEU PERFIL RETROACHIEVEMENTS",
            static_cast<std::size_t>(
                std::max(1, (layout.closeButton.x - layout.panel.x - 24) / 6)
            )
        ),
        SDL_Color{244, 204, 89, 255},
        1
    );
    drawHexText(
        renderer,
        layout.closeButton.x + 5,
        layout.closeButton.y + 5,
        "X",
        SDL_Color{244, 216, 142, 255},
        1
    );
    static constexpr std::array<const char*, 3> kTabLabels{
        "RESUMO",
        "JOGO ATUAL",
        "BIBLIOTECA",
    };
    for (std::size_t index = 0; index < layout.tabs.size(); ++index) {
        const bool selected = profileTabIndex(state.tab) == static_cast<int>(index);
        fillRect(
            renderer,
            layout.tabs[index],
            selected ? SDL_Color{45, 39, 24, 255} : SDL_Color{17, 23, 36, 255}
        );
        outlineRect(
            renderer,
            layout.tabs[index],
            selected ? SDL_Color{226, 183, 70, 255} : SDL_Color{64, 78, 104, 255}
        );
        drawHexText(
            renderer,
            layout.tabs[index].x + 10,
            layout.tabs[index].y + 10,
            asciiUiText(
                kTabLabels[index],
                static_cast<std::size_t>(
                    std::max(1, (layout.tabs[index].w - 12) / 6)
                )
            ),
            selected
                ? SDL_Color{255, 222, 124, 255}
                : SDL_Color{168, 184, 211, 255},
            1
        );
    }

    SDL_Rect oldClip{};
    const SDL_bool hadClip = SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetClipRect(renderer, &oldClip);
    SDL_Rect contentClip = sdlRect(layout.content);
    SDL_RenderSetClipRect(renderer, &contentClip);
    const int x = layout.content.x;
    int y = layout.content.y - state.scroll;
    const int w = layout.content.w;

    if (state.tab == RaProfileTab::Summary) {
        const auto& user = snapshot.profile.user;
        drawImageOrPlaceholder(
            renderer,
            imageCache,
            user.avatarPath,
            RaUiRect{x + 4, y + 10, 92, 92},
            "AVATAR"
        );
        const std::string displayName = user.displayName.empty()
            ? user.username
            : user.displayName;
        drawHexText(
            renderer,
            x + 116,
            y + 17,
            asciiUiText(displayName, 46),
            SDL_Color{242, 246, 255, 255},
            2
        );
        drawHexText(
            renderer,
            x + 116,
            y + 45,
            asciiUiText("@" + user.username, 54),
            SDL_Color{149, 170, 207, 255},
            1
        );
        drawHexText(
            renderer,
            x + 116,
            y + 67,
            "HARDCORE " + std::to_string(user.scoreHardcore) + " PONTOS",
            SDL_Color{246, 197, 80, 255},
            1
        );
        drawHexText(
            renderer,
            x + 116,
            y + 85,
            "CASUAL " + std::to_string(user.scoreCasual) + " PONTOS",
            SDL_Color{132, 206, 247, 255},
            1
        );
        std::uint64_t total = 0;
        std::uint64_t casual = 0;
        std::uint64_t hardcore = 0;
        for (const auto& game : snapshot.profile.library) {
            total += game.total;
            casual += game.unlockedCasual;
            hardcore += game.unlockedHardcore;
        }
        fillRoundedStyleRect(
            renderer,
            RaUiRect{x + 4, y + 122, w - 8, 112},
            SDL_Color{15, 21, 33, 255},
            SDL_Color{62, 77, 104, 255}
        );
        drawHexText(
            renderer,
            x + 18,
            y + 141,
            "PROGRESSO GB/GBC",
            SDL_Color{224, 231, 246, 255},
            1
        );
        drawHexText(
            renderer,
            x + 18,
            y + 164,
            "CASUAL " + std::to_string(casual) + "/" + std::to_string(total),
            SDL_Color{132, 206, 247, 255},
            1
        );
        drawHexText(
            renderer,
            x + 18,
            y + 184,
            "HARDCORE " + std::to_string(hardcore) + "/" + std::to_string(total),
            SDL_Color{246, 197, 80, 255},
            1
        );
        drawHexText(
            renderer,
            x + 18,
            y + 207,
            "MENSAGENS NAO LIDAS " + std::to_string(user.unreadMessages),
            SDL_Color{190, 202, 224, 255},
            1
        );
    } else if (state.tab == RaProfileTab::CurrentGame) {
        const auto& game = snapshot.currentGame;
        drawImageOrPlaceholder(
            renderer,
            imageCache,
            game.badgePath,
            RaUiRect{x + 4, y + 8, 76, 76},
            "JOGO"
        );
        drawHexText(
            renderer,
            x + 96,
            y + 16,
            asciiUiText(
                game.title.empty() ? "JOGO NAO RECONHECIDO" : game.title,
                static_cast<std::size_t>(std::max(1, (w - 108) / 6))
            ),
            SDL_Color{242, 246, 255, 255},
            1
        );
        drawHexText(
            renderer,
            x + 96,
            y + 39,
            "CASUAL " + std::to_string(game.unlockedCasual)
                + "/" + std::to_string(game.total),
            SDL_Color{132, 206, 247, 255},
            1
        );
        drawProgressBar(
            renderer,
            RaUiRect{x + 96, y + 60, std::max(80, w - 112), 12},
            game.unlockedCasual,
            game.total
        );
        y += 100;
        if (snapshot.currentAchievements.empty()) {
            drawHexText(
                renderer,
                x + 8,
                y + 16,
                snapshot.gameLoaded
                    ? "ESTE JOGO NAO POSSUI CONQUISTAS."
                    : "CARREGUE UM JOGO RECONHECIDO.",
                SDL_Color{164, 178, 202, 255},
                1
            );
        }
        const RaVisibleRowRange visibleRows = raVisibleProfileRows(
            state.tab,
            snapshot.currentAchievements.size(),
            state.scroll,
            layout.content.h
        );
        y += static_cast<int>(visibleRows.begin) * kAchievementRowHeight;
        for (std::size_t index = visibleRows.begin; index < visibleRows.end; ++index) {
            const auto& achievement = snapshot.currentAchievements[index];
            const RaUiRect row{x + 2, y + 2, w - 4, kAchievementRowHeight - 6};
            fillRoundedStyleRect(
                renderer,
                row,
                achievement.unlocked
                    ? SDL_Color{29, 31, 26, 255}
                    : SDL_Color{14, 20, 31, 255},
                achievement.unlocked
                    ? SDL_Color{166, 132, 50, 255}
                    : SDL_Color{55, 70, 96, 255}
            );
            drawImageOrPlaceholder(
                renderer,
                imageCache,
                achievement.badgePath,
                RaUiRect{row.x + 10, row.y + 10, 54, 54},
                achievement.unlocked ? "OK" : "LOCK"
            );
            drawHexText(
                renderer,
                row.x + 76,
                row.y + 11,
                asciiUiText(achievement.title, static_cast<std::size_t>(std::max(1, (row.w - 180) / 6))),
                achievement.unlocked
                    ? SDL_Color{255, 221, 119, 255}
                    : SDL_Color{214, 224, 241, 255},
                1
            );
            drawHexText(
                renderer,
                row.x + 76,
                row.y + 30,
                asciiUiText(achievement.description, static_cast<std::size_t>(std::max(1, (row.w - 90) / 6))),
                SDL_Color{151, 166, 192, 255},
                1
            );
            std::string detail = std::to_string(achievement.points) + " PONTOS";
            if (!achievement.measuredProgress.empty()) {
                detail += "  " + achievement.measuredProgress;
            }
            drawHexText(
                renderer,
                row.x + 76,
                row.y + 51,
                asciiUiText(detail, static_cast<std::size_t>(std::max(1, (row.w - 90) / 6))),
                achievement.unlocked
                    ? SDL_Color{236, 190, 72, 255}
                    : SDL_Color{125, 144, 175, 255},
                1
            );
            y += kAchievementRowHeight;
        }
    } else {
        drawHexText(
            renderer,
            x + 4,
            y + 8,
            "PROGRESSO DA BIBLIOTECA GB/GBC",
            SDL_Color{221, 229, 244, 255},
            1
        );
        y += 28;
        if (snapshot.profile.library.empty()) {
            drawHexText(
                renderer,
                x + 4,
                y + 12,
                "NENHUM PROGRESSO ENCONTRADO.",
                SDL_Color{164, 178, 202, 255},
                1
            );
        }
        const RaVisibleRowRange visibleRows = raVisibleProfileRows(
            state.tab,
            snapshot.profile.library.size(),
            state.scroll,
            layout.content.h
        );
        y += static_cast<int>(visibleRows.begin) * kLibraryRowHeight;
        for (std::size_t index = visibleRows.begin; index < visibleRows.end; ++index) {
            const auto& game = snapshot.profile.library[index];
            const RaUiRect row{x + 2, y + 2, w - 4, kLibraryRowHeight - 6};
            fillRoundedStyleRect(
                renderer,
                row,
                SDL_Color{14, 20, 31, 255},
                SDL_Color{55, 70, 96, 255}
            );
            drawImageOrPlaceholder(
                renderer,
                imageCache,
                game.badgePath,
                RaUiRect{row.x + 9, row.y + 8, 48, 48},
                "GB"
            );
            drawHexText(
                renderer,
                row.x + 68,
                row.y + 10,
                asciiUiText(game.title.empty() ? "JOGO SEM TITULO" : game.title, 54),
                SDL_Color{226, 233, 247, 255},
                1
            );
            drawHexText(
                renderer,
                row.x + 68,
                row.y + 30,
                "CASUAL " + std::to_string(game.unlockedCasual)
                    + "/" + std::to_string(game.total),
                SDL_Color{132, 206, 247, 255},
                1
            );
            drawHexText(
                renderer,
                row.x + std::max(260, row.w / 2),
                row.y + 30,
                "HARDCORE " + std::to_string(game.unlockedHardcore)
                    + "/" + std::to_string(game.total),
                SDL_Color{246, 197, 80, 255},
                1
            );
            y += kLibraryRowHeight;
        }
    }

    if (hadClip == SDL_TRUE) {
        SDL_RenderSetClipRect(renderer, &oldClip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
    const int maxScroll = raProfileMaxScroll(contentHeight, layout.content.h);
    if (maxScroll > 0) {
        const int thumbH = std::max(24, layout.content.h * layout.content.h / contentHeight);
        const int thumbTravel = layout.content.h - thumbH;
        const int thumbY = layout.content.y
            + (maxScroll == 0 ? 0 : thumbTravel * std::min(state.scroll, maxScroll) / maxScroll);
        fillRect(
            renderer,
            RaUiRect{layout.content.x + layout.content.w - 5, thumbY, 4, thumbH},
            SDL_Color{219, 174, 65, 220}
        );
    }
    SDL_SetRenderDrawBlendMode(renderer, oldBlend);
}

void renderRaToast(
    SDL_Renderer* renderer,
    const RaToastState& state,
    RaImageTextureCache& imageCache,
    std::uint64_t nowMs,
    int outputW,
    int outputH
) {
    const RaUiEvent* event = currentRaToast(state);
    const std::uint8_t alpha = raToastOpacity(state, nowMs);
    if (renderer == nullptr || event == nullptr || alpha == 0) {
        return;
    }
    SDL_BlendMode oldBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &oldBlend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto layout = raToastLayout(outputW, outputH);
    fillRoundedStyleRect(
        renderer,
        layout.card,
        withAlpha(SDL_Color{9, 13, 21, 248}, alpha),
        withAlpha(SDL_Color{242, 192, 61, 255}, alpha)
    );
    outlineRect(
        renderer,
        RaUiRect{
            layout.card.x + 3,
            layout.card.y + 3,
            layout.card.w - 6,
            layout.card.h - 6,
        },
        withAlpha(SDL_Color{101, 77, 28, 255}, alpha)
    );
    if (!event->imagePath.empty()
        && imageCache.texture(renderer, event->imagePath) != nullptr) {
        drawImageOrPlaceholder(
            renderer,
            imageCache,
            event->imagePath,
            layout.badge,
            nullptr,
            alpha
        );
    } else {
        fillRoundedStyleRect(
            renderer,
            layout.badge,
            withAlpha(SDL_Color{25, 26, 25, 255}, alpha),
            withAlpha(SDL_Color{235, 188, 68, 255}, alpha)
        );
        drawTrophy(
            renderer,
            RaUiRect{
                layout.badge.x + 8,
                layout.badge.y + 7,
                layout.badge.w - 16,
                layout.badge.h - 14,
            },
            withAlpha(SDL_Color{247, 204, 86, 255}, alpha)
        );
    }
    const int textX = layout.badge.x + layout.badge.w + 16;
    const int textRight = layout.card.x + layout.card.w - 10;
    const std::size_t textCapacity = static_cast<std::size_t>(
        std::max(1, (textRight - textX) / 6)
    );
    drawHexText(
        renderer,
        textX,
        layout.card.y + 20,
        asciiUiText("CONQUISTA DESBLOQUEADA", textCapacity),
        withAlpha(SDL_Color{249, 205, 78, 255}, alpha),
        1
    );
    drawHexText(
        renderer,
        textX,
        layout.card.y + 44,
        asciiUiText(
            event->title,
            static_cast<std::size_t>(std::max(1, (textRight - textX) / 12))
        ),
        withAlpha(SDL_Color{247, 249, 255, 255}, alpha),
        2
    );
    if (!event->detail.empty()) {
        drawHexText(
            renderer,
            textX,
            layout.card.y + 69,
            asciiUiText(event->detail, textCapacity),
            withAlpha(SDL_Color{174, 181, 195, 255}, alpha),
            1
        );
    }
    const std::string points = std::to_string(event->points) + " PONTOS";
    const int pointsX = layout.card.x + layout.card.w
        - static_cast<int>(points.size()) * 6 - 22;
    drawPlus(
        renderer,
        pointsX,
        layout.card.y + layout.card.h - 20,
        withAlpha(SDL_Color{249, 205, 78, 255}, alpha)
    );
    drawHexText(
        renderer,
        pointsX + 10,
        layout.card.y + layout.card.h - 20,
        points,
        withAlpha(SDL_Color{249, 205, 78, 255}, alpha),
        1
    );
    SDL_SetRenderDrawBlendMode(renderer, oldBlend);
}

#endif

} // namespace gb::frontend

#endif
