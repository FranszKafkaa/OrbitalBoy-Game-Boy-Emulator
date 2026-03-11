#include "gb/timer.hpp"

namespace gb {

void Timer::tick(u32 cycles) {
    divCounter_ += static_cast<u16>(cycles);
    while (divCounter_ >= 256) {
        divCounter_ -= 256;
        ++div_;
    }

    if ((tac_ & 0x04) == 0) {
        return;
    }

    u16 threshold = 1024;
    switch (tac_ & 0x03) {
    case 0: threshold = 1024; break;
    case 1: threshold = 16; break;
    case 2: threshold = 64; break;
    case 3: threshold = 256; break;
    default: break;
    }

    timaCounter_ += static_cast<u16>(cycles);
    while (timaCounter_ >= threshold) {
        timaCounter_ -= threshold;
        incrementTima();
    }
}

u8 Timer::read(u16 address) const {
    switch (address) {
    case 0xFF04: return div_;
    case 0xFF05: return tima_;
    case 0xFF06: return tma_;
    case 0xFF07: return static_cast<u8>(tac_ | 0xF8);
    default: return 0xFF;
    }
}

void Timer::write(u16 address, u8 value) {
    switch (address) {
    case 0xFF04:
        div_ = 0;
        divCounter_ = 0;
        break;
    case 0xFF05:
        tima_ = value;
        break;
    case 0xFF06:
        tma_ = value;
        break;
    case 0xFF07:
        tac_ = static_cast<u8>(value & 0x07);
        break;
    default:
        break;
    }
}

bool Timer::consumeInterrupt() {
    if (!timerInterruptRequested_) {
        return false;
    }
    timerInterruptRequested_ = false;
    return true;
}

Timer::State Timer::state() const {
    return State{
        divCounter_,
        timaCounter_,
        div_,
        tima_,
        tma_,
        tac_,
        timerInterruptRequested_,
    };
}

void Timer::loadState(const State& state) {
    divCounter_ = state.divCounter;
    timaCounter_ = state.timaCounter;
    div_ = state.div;
    tima_ = state.tima;
    tma_ = state.tma;
    tac_ = state.tac;
    timerInterruptRequested_ = state.timerInterruptRequested;
}

void Timer::incrementTima() {
    if (tima_ == 0xFF) {
        tima_ = tma_;
        timerInterruptRequested_ = true;
        return;
    }
    ++tima_;
}

} // namespace gb
