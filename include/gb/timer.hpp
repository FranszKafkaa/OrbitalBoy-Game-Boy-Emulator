#pragma once

#include "gb/types.hpp"

namespace gb {

class Timer {
public:
    struct State {
        u16 divCounter = 0;
        u16 timaCounter = 0;
        u8 div = 0;
        u8 tima = 0;
        u8 tma = 0;
        u8 tac = 0;
        bool timerInterruptRequested = false;
    };

    void tick(u32 cycles);

    u8 read(u16 address) const;
    void write(u16 address, u8 value);

    bool consumeInterrupt();
    [[nodiscard]] State state() const;
    void loadState(const State& state);

private:
    void incrementTima();

    u16 divCounter_ = 0;
    u16 timaCounter_ = 0;

    u8 div_ = 0;
    u8 tima_ = 0;
    u8 tma_ = 0;
    u8 tac_ = 0;

    bool timerInterruptRequested_ = false;
};

} // namespace gb
