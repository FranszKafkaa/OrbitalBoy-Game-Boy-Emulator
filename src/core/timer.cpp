#include "gb/core/timer.hpp"

namespace gb {

namespace {

u16 timaThresholdFromTac(u8 tac) {
    switch (tac & 0x03) {
    case 0: return 1024;
    case 1: return 16;
    case 2: return 64;
    case 3: return 256;
    default: return 1024;
    }
}

} // namespace

void Timer::tick(u32 cycles) {
    const u16 threshold = timaThresholdFromTac(tac_);
    for (u32 i = 0; i < cycles; ++i) {
        ++divCounter_;
        if (divCounter_ >= 256) {
            divCounter_ = 0;
            ++div_;
        }

        if (overflowPending_) {
            if (overflowDelay_ > 0) {
                --overflowDelay_;
            }
            if (overflowDelay_ == 0) {
                overflowPending_ = false;
                tima_ = tma_;
                timerInterruptRequested_ = true;
            }
        }

        if ((tac_ & 0x04) == 0) {
            continue;
        }

        ++timaCounter_;
        if (timaCounter_ >= threshold) {
            timaCounter_ = 0;
            incrementTima();
        }
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
        overflowPending_ = false;
        overflowDelay_ = 0;
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
        overflowPending_,
        overflowDelay_,
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
    overflowPending_ = state.overflowPending;
    overflowDelay_ = state.overflowDelay;
    timerInterruptRequested_ = state.timerInterruptRequested;
}

void Timer::incrementTima() {
    if (tima_ == 0xFF) {
        tima_ = 0x00;
        overflowPending_ = true;
        overflowDelay_ = 4;
        return;
    }
    ++tima_;
}

} // namespace gb
