#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "gb/apu.hpp"
#include "gb/cartridge.hpp"
#include "gb/joypad.hpp"
#include "gb/ppu.hpp"
#include "gb/timer.hpp"
#include "gb/types.hpp"

namespace gb {

class Bus {
public:
    struct State {
        std::array<u8, 0x2000> vram{};
        std::array<u8, 0x2000> wram{};
        std::array<u8, 0xA0> oam{};
        std::array<u8, 0x7F> hram{};
        Timer::State timer{};
        Joypad::State joypad{};
        PPU::State ppu{};
        APU::State apu{};
        u8 ie = 0;
        u8 iflag = 0xE1;
    };

    struct MemoryReadEvent {
        u16 address = 0;
        u8 value = 0;
    };

    explicit Bus(Cartridge& cartridge);

    u8 read(u16 address);
    u8 peek(u16 address) const;
    void write(u16 address, u8 value);

    void tick(u32 cycles);

    void requestInterrupt(int bit);
    [[nodiscard]] u8 interruptEnable() const;
    [[nodiscard]] u8 interruptFlags() const;
    void setInterruptFlags(u8 value);

    [[nodiscard]] PPU& ppu();
    [[nodiscard]] const PPU& ppu() const;
    [[nodiscard]] Joypad& joypad();
    [[nodiscard]] APU& apu();
    [[nodiscard]] std::vector<MemoryReadEvent> snapshotRecentReads(std::size_t maxItems) const;
    [[nodiscard]] State state() const;
    void loadState(const State& state);

private:
    u8 readInternal(u16 address);
    void logRead(u16 address, u8 value);
    void doOamDma(u8 page);

    Cartridge& cartridge_;

    std::array<u8, 0x2000> vram_{};
    std::array<u8, 0x2000> wram_{};
    std::array<u8, 0xA0> oam_{};
    std::array<u8, 0x7F> hram_{};

    Timer timer_;
    Joypad joypad_;
    PPU ppu_;
    APU apu_;

    u8 ie_ = 0;
    u8 if_ = 0xE1;

    std::array<MemoryReadEvent, 512> readLog_{};
    std::size_t readLogHead_ = 0;
    std::size_t readLogCount_ = 0;
};

} // namespace gb
