#include "gb/app/frontend/realtime/control_bindings.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>
#endif

namespace gb::frontend {

namespace {

constexpr std::size_t kActionCount = static_cast<std::size_t>(BindAction::Count);

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::size_t actionIndex(BindAction action) {
    return static_cast<std::size_t>(action);
}

gb::Button buttonFromAction(BindAction action) {
    switch (action) {
    case BindAction::Right: return gb::Button::Right;
    case BindAction::Left: return gb::Button::Left;
    case BindAction::Up: return gb::Button::Up;
    case BindAction::Down: return gb::Button::Down;
    case BindAction::A: return gb::Button::A;
    case BindAction::B: return gb::Button::B;
    case BindAction::Select: return gb::Button::Select;
    case BindAction::Start: return gb::Button::Start;
    default: return gb::Button::Start;
    }
}

int defaultKey(BindAction action) {
#ifdef GBEMU_USE_SDL2
    switch (action) {
    case BindAction::Right: return SDLK_RIGHT;
    case BindAction::Left: return SDLK_LEFT;
    case BindAction::Up: return SDLK_UP;
    case BindAction::Down: return SDLK_DOWN;
    case BindAction::A: return SDLK_z;
    case BindAction::B: return SDLK_x;
    case BindAction::Select: return SDLK_BACKSPACE;
    case BindAction::Start: return SDLK_RETURN;
    default: return SDLK_UNKNOWN;
    }
#else
    (void)action;
    return 0;
#endif
}

int defaultPadButton(BindAction action) {
#ifdef GBEMU_USE_SDL2
    switch (action) {
    case BindAction::Right: return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    case BindAction::Left: return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    case BindAction::Up: return SDL_CONTROLLER_BUTTON_DPAD_UP;
    case BindAction::Down: return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    case BindAction::A: return SDL_CONTROLLER_BUTTON_A;
    case BindAction::B: return SDL_CONTROLLER_BUTTON_B;
    case BindAction::Select: return SDL_CONTROLLER_BUTTON_BACK;
    case BindAction::Start: return SDL_CONTROLLER_BUTTON_START;
    default: return -1;
    }
#else
    (void)action;
    return -1;
#endif
}

bool parseActionName(const std::string& text, BindAction& out) {
    if (text == "RIGHT") { out = BindAction::Right; return true; }
    if (text == "LEFT") { out = BindAction::Left; return true; }
    if (text == "UP") { out = BindAction::Up; return true; }
    if (text == "DOWN") { out = BindAction::Down; return true; }
    if (text == "A") { out = BindAction::A; return true; }
    if (text == "B") { out = BindAction::B; return true; }
    if (text == "SELECT") { out = BindAction::Select; return true; }
    if (text == "START") { out = BindAction::Start; return true; }
    return false;
}

} // namespace

const char* bindActionName(BindAction action) {
    switch (action) {
    case BindAction::Right: return "RIGHT";
    case BindAction::Left: return "LEFT";
    case BindAction::Up: return "UP";
    case BindAction::Down: return "DOWN";
    case BindAction::A: return "A";
    case BindAction::B: return "B";
    case BindAction::Select: return "SELECT";
    case BindAction::Start: return "START";
    default: return "UNKNOWN";
    }
}

ControlBindings defaultControlBindings() {
    ControlBindings out{};
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const auto action = static_cast<BindAction>(i);
        out.keys[i] = defaultKey(action);
        out.padButtons[i] = defaultPadButton(action);
    }
    return out;
}

bool loadControlBindings(const std::string& path, ControlBindings& out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    out = defaultControlBindings();

    std::string line;
    while (std::getline(in, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, pos));
        const std::string value = trim(line.substr(pos + 1));
        const auto sep = key.rfind('_');
        if (sep == std::string::npos) {
            continue;
        }
        const std::string actionName = key.substr(0, sep);
        const std::string kind = key.substr(sep + 1);

        BindAction action{};
        if (!parseActionName(actionName, action)) {
            continue;
        }

        const int parsed = std::stoi(value);
        if (kind == "KEY") {
            out.keys[actionIndex(action)] = parsed;
        } else if (kind == "PAD") {
            out.padButtons[actionIndex(action)] = parsed;
        }
    }
    return true;
}

bool saveControlBindings(const std::string& path, const ControlBindings& bindings) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }

    for (std::size_t i = 0; i < kActionCount; ++i) {
        const auto action = static_cast<BindAction>(i);
        out << bindActionName(action) << "_KEY=" << bindings.keys[i] << "\n";
        out << bindActionName(action) << "_PAD=" << bindings.padButtons[i] << "\n";
    }
    return static_cast<bool>(out);
}

bool applyKeyboardBinding(gb::GameBoy& gb, const ControlBindings& bindings, int key, bool pressed) {
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (bindings.keys[i] != key) {
            continue;
        }
        gb.joypad().setButton(buttonFromAction(static_cast<BindAction>(i)), pressed);
        return true;
    }
    return false;
}

bool applyGamepadBinding(gb::GameBoy& gb, const ControlBindings& bindings, int button, bool pressed) {
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (bindings.padButtons[i] != button) {
            continue;
        }
        gb.joypad().setButton(buttonFromAction(static_cast<BindAction>(i)), pressed);
        return true;
    }
    return false;
}

} // namespace gb::frontend
