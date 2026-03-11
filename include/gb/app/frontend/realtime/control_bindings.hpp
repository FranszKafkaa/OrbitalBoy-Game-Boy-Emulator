#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "gb/core/gameboy.hpp"

namespace gb::frontend {

enum class BindAction : std::size_t {
    Right = 0,
    Left,
    Up,
    Down,
    A,
    B,
    Select,
    Start,
    Count,
};

struct ControlBindings {
    std::array<int, static_cast<std::size_t>(BindAction::Count)> keys{};
    std::array<int, static_cast<std::size_t>(BindAction::Count)> padButtons{};
};

const char* bindActionName(BindAction action);
ControlBindings defaultControlBindings();

bool loadControlBindings(const std::string& path, ControlBindings& out);
bool saveControlBindings(const std::string& path, const ControlBindings& bindings);

bool applyKeyboardBinding(gb::GameBoy& gb, const ControlBindings& bindings, int key, bool pressed);
bool applyGamepadBinding(gb::GameBoy& gb, const ControlBindings& bindings, int button, bool pressed);

} // namespace gb::frontend
