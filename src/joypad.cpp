#include "gb/joypad.hpp"

namespace gb {

void Joypad::setButton(Button button, bool pressed) {
    const auto idx = static_cast<std::size_t>(button);
    const bool wasPressed = pressed_[idx];
    pressed_[idx] = pressed;

    if (!wasPressed && pressed) {
        interruptRequested_ = true;
    }
}

u8 Joypad::read() const {
    u8 value = 0xCF;
    value |= static_cast<u8>(select_ & 0x30);

    if ((select_ & 0x10) == 0) {
        if (pressed_[static_cast<std::size_t>(Button::Right)]) value &= static_cast<u8>(~0x01);
        if (pressed_[static_cast<std::size_t>(Button::Left)]) value &= static_cast<u8>(~0x02);
        if (pressed_[static_cast<std::size_t>(Button::Up)]) value &= static_cast<u8>(~0x04);
        if (pressed_[static_cast<std::size_t>(Button::Down)]) value &= static_cast<u8>(~0x08);
    }

    if ((select_ & 0x20) == 0) {
        if (pressed_[static_cast<std::size_t>(Button::A)]) value &= static_cast<u8>(~0x01);
        if (pressed_[static_cast<std::size_t>(Button::B)]) value &= static_cast<u8>(~0x02);
        if (pressed_[static_cast<std::size_t>(Button::Select)]) value &= static_cast<u8>(~0x04);
        if (pressed_[static_cast<std::size_t>(Button::Start)]) value &= static_cast<u8>(~0x08);
    }

    return value;
}

void Joypad::write(u8 value) {
    select_ = static_cast<u8>(value & 0x30);
}

bool Joypad::consumeInterrupt() {
    if (!interruptRequested_) {
        return false;
    }
    interruptRequested_ = false;
    return true;
}

Joypad::State Joypad::state() const {
    return State{pressed_, select_, interruptRequested_};
}

void Joypad::loadState(const State& state) {
    pressed_ = state.pressed;
    select_ = state.select;
    interruptRequested_ = state.interruptRequested;
}

} // namespace gb
