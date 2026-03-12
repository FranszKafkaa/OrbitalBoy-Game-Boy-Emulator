#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "gb/core/apu.hpp"
#include "gb/core/cartridge.hpp"
#include "gb/core/joypad.hpp"
#include "gb/core/ppu.hpp"
#include "gb/core/timer.hpp"
#include "gb/core/types.hpp"

namespace gb {

class Bus {
public:
    struct State {
        std::array<u8, 0x2000> vram{};
        std::array<u8, 0x2000> vramBank1{};
        std::array<u8, 0x2000> wram{};
        std::array<u8, 0x6000> wramExt{};
        std::array<u8, 0xA0> oam{};
        std::array<u8, 0x7F> hram{};
        Timer::State timer{};
        Joypad::State joypad{};
        PPU::State ppu{};
        APU::State apu{};
        bool cgbMode = false;
        bool doubleSpeed = false;
        bool speedSwitchArmed = false;
        u8 vbk = 0;
        u8 svbk = 1;
        u8 bgpi = 0;
        u8 obpi = 0;
        std::array<u8, 0x40> bgPalette{};
        std::array<u8, 0x40> objPalette{};
        u8 hdma1 = 0;
        u8 hdma2 = 0;
        u8 hdma3 = 0;
        u8 hdma4 = 0;
        u8 hdma5 = 0xFF;
        bool hdmaActive = false;
        u8 serialSb = 0x00;
        u8 serialSc = 0x7E;
        bool serialTransferRequested = false;
        bool bootRomEnabled = false;
        u8 ie = 0;
        u8 iflag = 0xE1;
    };

    struct MemoryReadEvent {
        u16 address = 0;
        u8 value = 0;
    };

    struct MemoryWriteEvent {
        u16 address = 0;
        u8 value = 0;
    };

    explicit Bus(Cartridge& cartridge);
    void syncCartridgeMode();
    void setHardwareMode(bool cgbMode);
    [[nodiscard]] bool cgbMode() const;

    u8 read(u16 address);
    u8 peek(u16 address) const;
    void write(u16 address, u8 value);

    void tick(u32 cycles);
    [[nodiscard]] u32 peripheralCyclesFromCpuCycles(u32 cpuCycles) const;
    [[nodiscard]] bool isDoubleSpeed() const;
    bool trySpeedSwitch();
    bool consumeSerialTransfer(u8& outData);
    void completeSerialTransfer(u8 inData);
    void setBootRomData(const std::vector<u8>& data);
    void clearBootRom();
    [[nodiscard]] bool bootRomEnabled() const;

    void requestInterrupt(int bit);
    [[nodiscard]] u8 interruptEnable() const;
    [[nodiscard]] u8 interruptFlags() const;
    void setInterruptFlags(u8 value);

    [[nodiscard]] PPU& ppu();
    [[nodiscard]] const PPU& ppu() const;
    [[nodiscard]] Joypad& joypad();
    [[nodiscard]] APU& apu();
    [[nodiscard]] std::vector<MemoryReadEvent> snapshotRecentReads(std::size_t maxItems) const;
    [[nodiscard]] std::vector<MemoryWriteEvent> snapshotRecentWrites(std::size_t maxItems) const;
    [[nodiscard]] State state() const;
    void loadState(const State& state);

private:
    u8 readInternal(u16 address) const;
    void logRead(u16 address, u8 value);
    void logWrite(u16 address, u8 value);
    void doOamDma(u8 page);
    void doHdmaTransfer(u16 lengthBytes);
    void tickHdmaHBlank();
    [[nodiscard]] u8 wramBankValue() const;
    [[nodiscard]] u8 readWramMapped(u16 address) const;
    void writeWramMapped(u16 address, u8 value);

    Cartridge& cartridge_;

    std::array<u8, 0x2000> vram_{};
    std::array<u8, 0x2000> vramBank1_{};
    std::array<u8, 0x2000> wram_{};
    std::array<u8, 0x6000> wramExt_{};
    std::array<u8, 0xA0> oam_{};
    std::array<u8, 0x7F> hram_{};

    Timer timer_;
    Joypad joypad_;
    PPU ppu_;
    APU apu_;

    u8 ie_ = 0;
    u8 if_ = 0xE1;
    bool cgbMode_ = false;
    bool doubleSpeed_ = false;
    bool speedSwitchArmed_ = false;
    u8 vbk_ = 0;
    u8 svbk_ = 1;
    u8 bgpi_ = 0;
    u8 obpi_ = 0;
    std::array<u8, 0x40> bgPalette_{};
    std::array<u8, 0x40> objPalette_{};
    u8 hdma1_ = 0;
    u8 hdma2_ = 0;
    u8 hdma3_ = 0;
    u8 hdma4_ = 0;
    u8 hdma5_ = 0xFF;
    bool hdmaActive_ = false;
    u8 serialSb_ = 0x00;
    u8 serialSc_ = 0x7E;
    bool serialTransferRequested_ = false;
    std::vector<u8> bootRom_{};
    bool bootRomEnabled_ = false;

    std::array<MemoryReadEvent, 512> readLog_{};
    std::size_t readLogHead_ = 0;
    std::size_t readLogCount_ = 0;
    std::array<MemoryWriteEvent, 512> writeLog_{};
    std::size_t writeLogHead_ = 0;
    std::size_t writeLogCount_ = 0;
};

} // namespace gb
