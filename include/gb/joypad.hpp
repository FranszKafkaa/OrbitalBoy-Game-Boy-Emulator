#pragma once

#include <array>

#include "gb/types.hpp"

namespace gb {

enum class Button {
    Right,
    Left,
    Up,
    Down,
    A,
    B,
    Select,
    Start,
};

class Joypad {
public:
    struct State {
        std::array<bool, 8> pressed{};
        u8 select = 0x30;
        bool interruptRequested = false;
    };

    void setButton(Button button, bool pressed);

    u8 read() const;
    void write(u8 value);

    bool consumeInterrupt();
    [[nodiscard]] State state() const;
    void loadState(const State& state);

private:
    std::array<bool, 8> pressed_{};
    u8 select_ = 0x30;
    bool interruptRequested_ = false;
};

} // namespace gb
