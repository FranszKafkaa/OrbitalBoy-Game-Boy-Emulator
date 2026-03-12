#pragma once

#include <array>
#include <vector>

#include "gb/core/types.hpp"

namespace gb::gba {

class Memory {
public:
    static constexpr std::size_t EwramSize = 0x40000; // 256 KiB
    static constexpr std::size_t IwramSize = 0x8000;  // 32 KiB
    static constexpr std::size_t PramSize = 0x400;    // 1 KiB (palette RAM)
    static constexpr std::size_t VramSize = 0x18000;  // 96 KiB
    static constexpr std::size_t OamSize = 0x400;     // 1 KiB (sprite attributes)
    static constexpr std::size_t IoSize = 0x400;      // 1 KiB

    static constexpr u32 IoBase = 0x04000000U;
    static constexpr u32 PramBase = 0x05000000U;
    static constexpr u32 VramBase = 0x06000000U;
    static constexpr u32 OamBase = 0x07000000U;
    static constexpr u32 KeyInputOffset = 0x130U;
    static constexpr u32 KeyControlOffset = 0x132U;
    static constexpr u32 IeOffset = 0x200U;
    static constexpr u32 IfOffset = 0x202U;
    static constexpr u32 ImeOffset = 0x208U;
    static constexpr u16 DefaultKeyInput = 0xFFFFU; // bits 0..9 soltos, 10..15 lidos como 1

    bool loadRom(const std::vector<u8>& romData);
    void reset();
    void step(int cpuCycles);

    [[nodiscard]] u8 read8(u32 address) const;
    [[nodiscard]] u16 read16(u32 address) const;
    [[nodiscard]] u32 read32(u32 address) const;

    void write8(u32 address, u8 value);
    void write16(u32 address, u16 value);
    void write32(u32 address, u32 value);

    [[nodiscard]] const std::vector<u8>& rom() const;
    [[nodiscard]] const std::array<u8, EwramSize>& ewram() const;
    [[nodiscard]] const std::array<u8, IwramSize>& iwram() const;
    [[nodiscard]] const std::array<u8, PramSize>& pram() const;
    [[nodiscard]] const std::array<u8, VramSize>& vram() const;
    [[nodiscard]] const std::array<u8, OamSize>& oam() const;

    [[nodiscard]] u16 readIo16(u32 ioOffset) const;
    void writeIo16(u32 ioOffset, u16 value);
    void setKeyInputRaw(u16 value);
    [[nodiscard]] u16 keyInputRaw() const;
    [[nodiscard]] u16 keyControlRaw() const;

    void requestInterrupt(u16 mask);
    void clearInterrupt(u16 mask);
    [[nodiscard]] u16 interruptEnableRaw() const;
    [[nodiscard]] u16 interruptFlagsRaw() const;
    [[nodiscard]] bool interruptMasterEnabled() const;
    [[nodiscard]] u16 pendingInterrupts() const;
    void triggerDmaStart(u16 startTiming);

private:
    struct TimerState {
        u16 reload = 0;
        u16 counter = 0;
        u16 control = 0;
        std::uint32_t prescalerCycles = 0;
    };

    [[nodiscard]] static constexpr std::size_t timerOffset(std::size_t timerIndex) {
        return 0x100U + timerIndex * 4U;
    }
    [[nodiscard]] static constexpr std::size_t dmaOffset(std::size_t channel) {
        return 0xB0U + channel * 12U;
    }

    [[nodiscard]] static std::uint32_t timerPrescaler(u16 control);
    void writeTimerCounter(std::size_t timerIndex, u16 value);
    void writeTimerControl(std::size_t timerIndex, u16 value);
    void stepTimers(std::uint32_t cpuCycles);
    [[nodiscard]] std::uint32_t tickTimer(std::size_t timerIndex, std::uint32_t cpuCycles, std::uint32_t cascadedTicks);

    void executeDmaTransfer(std::size_t channel, u16 triggeredStartTiming);
    void updateKeypadInterrupt();

    [[nodiscard]] u8 readRom8(u32 address) const;
    [[nodiscard]] u8* writableBytePointer(u32 address);
    [[nodiscard]] const u8* writableBytePointer(u32 address) const;

    std::vector<u8> rom_{};
    std::array<u8, EwramSize> ewram_{};
    std::array<u8, IwramSize> iwram_{};
    std::array<u8, PramSize> pram_{};
    std::array<u8, VramSize> vram_{};
    std::array<u8, OamSize> oam_{};
    std::array<u8, IoSize> io_{};
    std::array<TimerState, 4> timers_{};
};

} // namespace gb::gba
