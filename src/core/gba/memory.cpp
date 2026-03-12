#include "gb/core/gba/memory.hpp"

#include <algorithm>

namespace gb::gba {

namespace {

constexpr std::array<u16, 4> kTimerIrqMasks = {
    static_cast<u16>(1U << 3U),
    static_cast<u16>(1U << 4U),
    static_cast<u16>(1U << 5U),
    static_cast<u16>(1U << 6U),
};
constexpr u16 kKeypadIrqMask = static_cast<u16>(1U << 12U);
constexpr u32 kVramMirrorStart = 0x18000U;
constexpr u32 kVramMirrorSubtract = 0x8000U;

u32 rotateRight32(u32 value, unsigned amount) {
    const unsigned shift = amount & 31U;
    if (shift == 0U) {
        return value;
    }
    return (value >> shift) | (value << (32U - shift));
}

std::size_t mapVramOffset(u32 address) {
    u32 offset = address & 0x1FFFFU;
    if (offset >= kVramMirrorStart) {
        // 0x06018000-0x0601FFFF espelha 0x06010000-0x06017FFF.
        offset -= kVramMirrorSubtract;
    }
    return static_cast<std::size_t>(offset);
}

constexpr bool isTimerReloadOffset(u32 offset) {
    return (offset >= 0x100U) && (offset <= 0x10CU) && ((offset % 4U) == 0U);
}

constexpr bool isTimerControlOffset(u32 offset) {
    return (offset >= 0x102U) && (offset <= 0x10EU) && ((offset % 4U) == 2U);
}

constexpr bool isDmaCountOffset(u32 offset) {
    return (offset >= 0x0B8U) && (offset <= 0x0DEU) && (((offset - 0x0B0U) % 12U) == 8U);
}

constexpr bool isDmaControlOffset(u32 offset) {
    return (offset >= 0x0BAU) && (offset <= 0x0E0U) && (((offset - 0x0B0U) % 12U) == 10U);
}

} // namespace

bool Memory::loadRom(const std::vector<u8>& romData) {
    if (romData.empty()) {
        return false;
    }
    rom_ = romData;
    reset();
    return true;
}

void Memory::reset() {
    std::fill(ewram_.begin(), ewram_.end(), 0);
    std::fill(iwram_.begin(), iwram_.end(), 0);
    std::fill(pram_.begin(), pram_.end(), 0);
    std::fill(vram_.begin(), vram_.end(), 0);
    std::fill(oam_.begin(), oam_.end(), 0);
    std::fill(io_.begin(), io_.end(), 0);

    for (auto& timer : timers_) {
        timer = TimerState{};
    }

    setKeyInputRaw(DefaultKeyInput);
    writeIo16(KeyControlOffset, 0);
    writeIo16(IeOffset, 0);
    writeIo16(IfOffset, 0);
    writeIo16(ImeOffset, 0);
}

void Memory::step(int cpuCycles) {
    if (cpuCycles <= 0) {
        return;
    }
    stepTimers(static_cast<std::uint32_t>(cpuCycles));
}

u8 Memory::read8(u32 address) const {
    if (const u8* ptr = writableBytePointer(address); ptr != nullptr) {
        return *ptr;
    }

    const u32 region = address >> 24U;
    if (region >= 0x08U && region <= 0x0DU) {
        return readRom8(address);
    }
    return 0;
}

u16 Memory::read16(u32 address) const {
    const u32 aligned = address & ~1U;
    const u16 lo = static_cast<u16>(read8(aligned));
    const u16 hi = static_cast<u16>(read8(aligned + 1U));
    return static_cast<u16>(lo | static_cast<u16>(hi << 8U));
}

u32 Memory::read32(u32 address) const {
    const u32 aligned = address & ~3U;
    const u32 b0 = static_cast<u32>(read8(aligned));
    const u32 b1 = static_cast<u32>(read8(aligned + 1U));
    const u32 b2 = static_cast<u32>(read8(aligned + 2U));
    const u32 b3 = static_cast<u32>(read8(aligned + 3U));
    const u32 raw = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
    const unsigned rotate = static_cast<unsigned>((address & 0x3U) * 8U);
    return rotateRight32(raw, rotate);
}

void Memory::write8(u32 address, u8 value) {
    const u32 region = address >> 24U;
    if (region == 0x07U) {
        // OAM nao suporta byte-write (ignorado no hardware).
        return;
    }
    if (region == 0x05U || region == 0x06U) {
        // PRAM/VRAM sao barramentos de 16-bit: byte-write replica em ambos bytes do halfword.
        const u32 aligned = address & ~1U;
        u8* lo = writableBytePointer(aligned);
        u8* hi = writableBytePointer(aligned + 1U);
        if (lo != nullptr && hi != nullptr) {
            *lo = value;
            *hi = value;
        }
        return;
    }
    if (u8* ptr = writableBytePointer(address); ptr != nullptr) {
        *ptr = value;
    }
}

void Memory::write16(u32 address, u16 value) {
    if ((address >> 24U) == 0x04U) {
        writeIo16((address & 0x3FFU) & ~1U, value);
        return;
    }
    const u32 aligned = address & ~1U;
    u8* lo = writableBytePointer(aligned);
    u8* hi = writableBytePointer(aligned + 1U);
    if (lo != nullptr && hi != nullptr) {
        *lo = static_cast<u8>(value & 0xFFU);
        *hi = static_cast<u8>((value >> 8U) & 0xFFU);
    }
}

void Memory::write32(u32 address, u32 value) {
    if ((address >> 24U) == 0x04U) {
        const u32 offset = (address & 0x3FFU) & ~3U;
        writeIo16(offset, static_cast<u16>(value & 0xFFFFU));
        writeIo16(offset + 2U, static_cast<u16>((value >> 16U) & 0xFFFFU));
        return;
    }

    const u32 aligned = address & ~3U;
    u8* b0 = writableBytePointer(aligned);
    u8* b1 = writableBytePointer(aligned + 1U);
    u8* b2 = writableBytePointer(aligned + 2U);
    u8* b3 = writableBytePointer(aligned + 3U);
    if (b0 != nullptr && b1 != nullptr && b2 != nullptr && b3 != nullptr) {
        *b0 = static_cast<u8>(value & 0xFFU);
        *b1 = static_cast<u8>((value >> 8U) & 0xFFU);
        *b2 = static_cast<u8>((value >> 16U) & 0xFFU);
        *b3 = static_cast<u8>((value >> 24U) & 0xFFU);
    }
}

const std::vector<u8>& Memory::rom() const {
    return rom_;
}

const std::array<u8, Memory::EwramSize>& Memory::ewram() const {
    return ewram_;
}

const std::array<u8, Memory::IwramSize>& Memory::iwram() const {
    return iwram_;
}

const std::array<u8, Memory::PramSize>& Memory::pram() const {
    return pram_;
}

const std::array<u8, Memory::VramSize>& Memory::vram() const {
    return vram_;
}

const std::array<u8, Memory::OamSize>& Memory::oam() const {
    return oam_;
}

u16 Memory::readIo16(u32 ioOffset) const {
    if (ioOffset + 1U >= IoSize) {
        return 0;
    }
    const u16 lo = static_cast<u16>(io_[static_cast<std::size_t>(ioOffset)]);
    const u16 hi = static_cast<u16>(io_[static_cast<std::size_t>(ioOffset + 1U)]);
    return static_cast<u16>(lo | static_cast<u16>(hi << 8U));
}

void Memory::writeIo16(u32 ioOffset, u16 value) {
    if (ioOffset + 1U >= IoSize) {
        return;
    }
    if (ioOffset == KeyInputOffset) {
        return; // read-only de input
    }

    if (isTimerReloadOffset(ioOffset)) {
        writeTimerCounter((ioOffset - 0x100U) / 4U, value);
        return;
    }
    if (isTimerControlOffset(ioOffset)) {
        writeTimerControl((ioOffset - 0x102U) / 4U, value);
        return;
    }

    if (ioOffset == IfOffset) {
        clearInterrupt(value);
        return;
    }

    io_[static_cast<std::size_t>(ioOffset)] = static_cast<u8>(value & 0xFFU);
    io_[static_cast<std::size_t>(ioOffset + 1U)] = static_cast<u8>((value >> 8U) & 0xFFU);

    if (ioOffset == KeyControlOffset) {
        updateKeypadInterrupt();
        return;
    }

    if (isDmaCountOffset(ioOffset)) {
        return;
    }
    if (isDmaControlOffset(ioOffset)) {
        const std::size_t channel = (ioOffset - 0x0BAU) / 12U;
        const bool enabled = (value & 0x8000U) != 0U;
        const u16 startTiming = static_cast<u16>((value >> 12U) & 0x3U);
        if (enabled && startTiming == 0U) {
            executeDmaTransfer(channel, 0U);
        }
    }
}

void Memory::setKeyInputRaw(u16 value) {
    const u16 sanitized = static_cast<u16>((value & 0x03FFU) | 0xFC00U);
    io_[static_cast<std::size_t>(KeyInputOffset)] = static_cast<u8>(sanitized & 0xFFU);
    io_[static_cast<std::size_t>(KeyInputOffset + 1U)] = static_cast<u8>((sanitized >> 8U) & 0xFFU);
    updateKeypadInterrupt();
}

u16 Memory::keyInputRaw() const {
    return readIo16(KeyInputOffset);
}

u16 Memory::keyControlRaw() const {
    return readIo16(KeyControlOffset);
}

void Memory::requestInterrupt(u16 mask) {
    const u16 current = interruptFlagsRaw();
    const u16 next = static_cast<u16>(current | mask);
    io_[static_cast<std::size_t>(IfOffset)] = static_cast<u8>(next & 0xFFU);
    io_[static_cast<std::size_t>(IfOffset + 1U)] = static_cast<u8>((next >> 8U) & 0xFFU);
}

void Memory::clearInterrupt(u16 mask) {
    const u16 current = interruptFlagsRaw();
    const u16 next = static_cast<u16>(current & ~mask);
    io_[static_cast<std::size_t>(IfOffset)] = static_cast<u8>(next & 0xFFU);
    io_[static_cast<std::size_t>(IfOffset + 1U)] = static_cast<u8>((next >> 8U) & 0xFFU);
}

u16 Memory::interruptEnableRaw() const {
    return readIo16(IeOffset);
}

u16 Memory::interruptFlagsRaw() const {
    return readIo16(IfOffset);
}

bool Memory::interruptMasterEnabled() const {
    return (readIo16(ImeOffset) & 0x0001U) != 0U;
}

u16 Memory::pendingInterrupts() const {
    return static_cast<u16>(interruptEnableRaw() & interruptFlagsRaw());
}

void Memory::triggerDmaStart(u16 startTiming) {
    for (std::size_t channel = 0; channel < 4; ++channel) {
        const std::size_t controlOffset = dmaOffset(channel) + 10U;
        const u16 control = static_cast<u16>(
            static_cast<u16>(io_[controlOffset])
            | static_cast<u16>(static_cast<u16>(io_[controlOffset + 1U]) << 8U)
        );
        const bool enabled = (control & 0x8000U) != 0U;
        const u16 channelStartTiming = static_cast<u16>((control >> 12U) & 0x3U);
        if (enabled && channelStartTiming == startTiming) {
            executeDmaTransfer(channel, startTiming);
        }
    }
}

std::uint32_t Memory::timerPrescaler(u16 control) {
    switch (control & 0x3U) {
    case 0U:
        return 1U;
    case 1U:
        return 64U;
    case 2U:
        return 256U;
    case 3U:
        return 1024U;
    default:
        return 1U;
    }
}

void Memory::writeTimerCounter(std::size_t timerIndex, u16 value) {
    if (timerIndex >= timers_.size()) {
        return;
    }

    TimerState& timer = timers_[timerIndex];
    timer.reload = value;
    timer.counter = value;

    const std::size_t base = timerOffset(timerIndex);
    io_[base] = static_cast<u8>(value & 0xFFU);
    io_[base + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
}

void Memory::writeTimerControl(std::size_t timerIndex, u16 value) {
    if (timerIndex >= timers_.size()) {
        return;
    }

    TimerState& timer = timers_[timerIndex];
    const bool wasEnabled = (timer.control & 0x0080U) != 0U;
    timer.control = static_cast<u16>(value & 0x00C7U);
    const bool nowEnabled = (timer.control & 0x0080U) != 0U;
    if (!wasEnabled && nowEnabled) {
        timer.counter = timer.reload;
        timer.prescalerCycles = 0;
        const std::size_t base = timerOffset(timerIndex);
        io_[base] = static_cast<u8>(timer.counter & 0xFFU);
        io_[base + 1U] = static_cast<u8>((timer.counter >> 8U) & 0xFFU);
    }
    if (!nowEnabled) {
        timer.prescalerCycles = 0;
    }

    const std::size_t ctrlOffset = timerOffset(timerIndex) + 2U;
    io_[ctrlOffset] = static_cast<u8>(timer.control & 0xFFU);
    io_[ctrlOffset + 1U] = static_cast<u8>((timer.control >> 8U) & 0xFFU);
}

void Memory::stepTimers(std::uint32_t cpuCycles) {
    std::uint32_t cascadedTicks = 0;
    for (std::size_t timerIndex = 0; timerIndex < timers_.size(); ++timerIndex) {
        cascadedTicks = tickTimer(timerIndex, cpuCycles, cascadedTicks);
    }
}

std::uint32_t Memory::tickTimer(std::size_t timerIndex, std::uint32_t cpuCycles, std::uint32_t cascadedTicks) {
    TimerState& timer = timers_[timerIndex];
    const bool enabled = (timer.control & 0x0080U) != 0U;
    if (!enabled) {
        return 0;
    }

    std::uint32_t increments = 0;
    const bool cascadeMode = timerIndex > 0 && (timer.control & 0x0004U) != 0U;
    if (cascadeMode) {
        increments = cascadedTicks;
    } else {
        const std::uint32_t prescaler = timerPrescaler(timer.control);
        timer.prescalerCycles += cpuCycles;
        increments = timer.prescalerCycles / prescaler;
        timer.prescalerCycles %= prescaler;
    }
    if (increments == 0U) {
        return 0;
    }

    std::uint32_t overflowCount = 0;
    for (std::uint32_t i = 0; i < increments; ++i) {
        const u32 next = static_cast<u32>(timer.counter) + 1U;
        if (next > 0xFFFFU) {
            timer.counter = timer.reload;
            ++overflowCount;
            if ((timer.control & 0x0040U) != 0U) {
                requestInterrupt(kTimerIrqMasks[timerIndex]);
            }
        } else {
            timer.counter = static_cast<u16>(next);
        }
    }

    const std::size_t base = timerOffset(timerIndex);
    io_[base] = static_cast<u8>(timer.counter & 0xFFU);
    io_[base + 1U] = static_cast<u8>((timer.counter >> 8U) & 0xFFU);

    return overflowCount;
}

void Memory::executeDmaTransfer(std::size_t channel, u16 triggeredStartTiming) {
    if (channel > 3U) {
        return;
    }

    const std::size_t base = dmaOffset(channel);

    const auto readIo32 = [this](std::size_t offset) -> u32 {
        const u32 b0 = static_cast<u32>(io_[offset]);
        const u32 b1 = static_cast<u32>(io_[offset + 1U]);
        const u32 b2 = static_cast<u32>(io_[offset + 2U]);
        const u32 b3 = static_cast<u32>(io_[offset + 3U]);
        return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
    };
    const auto writeIo32 = [this](std::size_t offset, u32 value) {
        io_[offset] = static_cast<u8>(value & 0xFFU);
        io_[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
        io_[offset + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
        io_[offset + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
    };

    const u32 sourceInit = readIo32(base + 0U);
    const u32 destInit = readIo32(base + 4U);
    u32 source = sourceInit;
    u32 dest = destInit;
    u16 count = static_cast<u16>(io_[base + 8U] | (static_cast<u16>(io_[base + 9U]) << 8U));
    u16 control = static_cast<u16>(io_[base + 10U] | (static_cast<u16>(io_[base + 11U]) << 8U));

    const bool irqOnEnd = (control & 0x4000U) != 0U;
    const bool transfer32 = (control & 0x0400U) != 0U;
    const bool repeat = (control & 0x0200U) != 0U;
    const u16 startTiming = static_cast<u16>((control >> 12U) & 0x3U);
    if (startTiming != triggeredStartTiming) {
        return;
    }

    const u32 maxCount = channel == 3U ? 0x10000U : 0x4000U;
    u32 units = count == 0U ? maxCount : static_cast<u32>(count);

    const u16 dstCtrl = static_cast<u16>((control >> 5U) & 0x3U);
    const u16 srcCtrl = static_cast<u16>((control >> 7U) & 0x3U);
    const u32 stride = transfer32 ? 4U : 2U;

    for (u32 i = 0; i < units; ++i) {
        if (transfer32) {
            const u32 value = read32(source);
            write32(dest, value);
        } else {
            const u16 value = read16(source);
            write16(dest, value);
        }

        switch (srcCtrl) {
        case 0U:
            source += stride;
            break;
        case 1U:
            source -= stride;
            break;
        default:
            break;
        }

        switch (dstCtrl) {
        case 0U:
        case 3U:
            dest += stride;
            break;
        case 1U:
            dest -= stride;
            break;
        default:
            break;
        }
    }

    if (dstCtrl == 3U) {
        dest = destInit;
    }

    writeIo32(base + 0U, source);
    writeIo32(base + 4U, dest);
    io_[base + 8U] = static_cast<u8>(count & 0xFFU);
    io_[base + 9U] = static_cast<u8>((count >> 8U) & 0xFFU);

    const bool keepEnabled = repeat && startTiming != 0U;
    if (!keepEnabled) {
        control = static_cast<u16>(control & ~0x8000U);
        io_[base + 10U] = static_cast<u8>(control & 0xFFU);
        io_[base + 11U] = static_cast<u8>((control >> 8U) & 0xFFU);
    }

    if (irqOnEnd) {
        requestInterrupt(static_cast<u16>(1U << (8U + static_cast<u16>(channel))));
    }
}

void Memory::updateKeypadInterrupt() {
    const u16 keycnt = keyControlRaw();
    if ((keycnt & 0x4000U) == 0U) {
        return;
    }

    const u16 mask = static_cast<u16>(keycnt & 0x03FFU);
    if (mask == 0U) {
        return;
    }

    const u16 pressed = static_cast<u16>((~keyInputRaw()) & 0x03FFU);
    const bool andMode = (keycnt & 0x8000U) != 0U;
    const bool matched = andMode
        ? (pressed & mask) == mask
        : (pressed & mask) != 0U;

    if (matched) {
        requestInterrupt(kKeypadIrqMask);
    }
}

u8 Memory::readRom8(u32 address) const {
    if (rom_.empty()) {
        return 0xFF;
    }
    const u32 romAddress = address - 0x08000000U;
    const std::size_t idx = static_cast<std::size_t>(romAddress & 0x01FFFFFFU) % rom_.size();
    return rom_[idx];
}

u8* Memory::writableBytePointer(u32 address) {
    const u32 region = address >> 24U;
    if (region == 0x02U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFFFU);
        return &ewram_[idx];
    }
    if (region == 0x03U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x7FFFU);
        return &iwram_[idx];
    }
    if (region == 0x04U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        if (idx == KeyInputOffset || idx == KeyInputOffset + 1U) {
            return nullptr; // read-only para KEYINPUT
        }
        return &io_[idx];
    }
    if (region == 0x05U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        return &pram_[idx];
    }
    if (region == 0x06U) {
        const std::size_t idx = mapVramOffset(address);
        return &vram_[idx];
    }
    if (region == 0x07U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        return &oam_[idx];
    }
    return nullptr;
}

const u8* Memory::writableBytePointer(u32 address) const {
    const u32 region = address >> 24U;
    if (region == 0x02U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFFFU);
        return &ewram_[idx];
    }
    if (region == 0x03U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x7FFFU);
        return &iwram_[idx];
    }
    if (region == 0x04U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        return &io_[idx];
    }
    if (region == 0x05U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        return &pram_[idx];
    }
    if (region == 0x06U) {
        const std::size_t idx = mapVramOffset(address);
        return &vram_[idx];
    }
    if (region == 0x07U) {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        return &oam_[idx];
    }
    return nullptr;
}

} // namespace gb::gba
