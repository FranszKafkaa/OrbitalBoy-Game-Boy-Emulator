#include "gb/core/gba/memory.hpp"
#include "gb/core/environment.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

namespace gb::gba {

namespace {

constexpr std::array<u16, 4> kTimerIrqMasks = {
    static_cast<u16>(1U << 3U),
    static_cast<u16>(1U << 4U),
    static_cast<u16>(1U << 5U),
    static_cast<u16>(1U << 6U),
};
constexpr u16 kKeypadIrqMask = static_cast<u16>(1U << 12U);
constexpr u32 kSoundCntHOffset = 0x0082U;
constexpr u32 kFifoAOffset = 0x00A0U;
constexpr u32 kFifoBOffset = 0x00A4U;
constexpr u32 kVramMirrorStart = 0x18000U;
constexpr u32 kVramMirrorSubtract = 0x8000U;
constexpr u32 kBackupSramFlashBase = 0x0E000000U;
constexpr u32 kBackupSramFlashMask = 0x0000FFFFU;
constexpr u32 kFlashUnlockAddr1 = 0x5555U;
constexpr u32 kFlashUnlockAddr2 = 0x2AAAU;
constexpr std::size_t kSramSize = 0x8000U;
constexpr std::size_t kFlash64Size = 0x10000U;
constexpr std::size_t kFlash128Size = 0x20000U;
constexpr std::size_t kEepromMaxSize = 0x2000U; // 8 KiB
constexpr std::size_t kEepromWordBytes = 8U;
constexpr std::array<int, 4> kGamePakNonSequentialCycles = {4, 3, 2, 8};
constexpr std::array<int, 2> kGamePakSequentialCycles0 = {2, 1};
constexpr std::array<int, 2> kGamePakSequentialCycles1 = {4, 1};
constexpr std::array<int, 2> kGamePakSequentialCycles2 = {8, 1};

std::pair<u8, u8> defaultFlashIdPair(bool flash128) {
    return flash128
        ? std::pair<u8, u8>{static_cast<u8>(0x62U), static_cast<u8>(0x13U)}
        : std::pair<u8, u8>{static_cast<u8>(0xBFU), static_cast<u8>(0xD4U)};
}

std::pair<u8, u8> flashIdPairFromEnvironment(bool flash128) {
    const auto configured = gb::readEnvironmentVariable("GBEMU_GBA_FLASH_ID");
    if (!configured.has_value()) {
        return defaultFlashIdPair(flash128);
    }

    const std::string value = *configured;
    if (value == "panasonic") {
        return {static_cast<u8>(0x32U), static_cast<u8>(0x1BU)};
    }
    if (value == "macronix") {
        return {static_cast<u8>(0xC2U), static_cast<u8>(0x1CU)};
    }
    if (value == "sanyo") {
        return {static_cast<u8>(0x62U), static_cast<u8>(0x13U)};
    }
    if (value == "atmel") {
        return {static_cast<u8>(0x1FU), static_cast<u8>(0x3DU)};
    }
    return defaultFlashIdPair(flash128);
}

std::pair<u8, u8> resolveFlashIdPair(bool flash128, int vendorOverride, int deviceOverride) {
    if (vendorOverride >= 0 && vendorOverride <= 0xFF && deviceOverride >= 0 && deviceOverride <= 0xFF) {
        return {
            static_cast<u8>(vendorOverride & 0xFF),
            static_cast<u8>(deviceOverride & 0xFF),
        };
    }
    return flashIdPairFromEnvironment(flash128);
}

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

bool containsAsciiTag(const std::vector<u8>& data, const char* tag) {
    if (data.empty() || tag == nullptr || *tag == '\0') {
        return false;
    }
    const std::string needle(tag);
    if (needle.empty() || needle.size() > data.size()) {
        return false;
    }
    return std::search(
        data.begin(),
        data.end(),
        needle.begin(),
        needle.end()
    ) != data.end();
}

std::size_t backupStorageSize(gb::gba::Memory::BackupType type) {
    switch (type) {
    case gb::gba::Memory::BackupType::Sram:
        return kSramSize;
    case gb::gba::Memory::BackupType::Flash64:
        return kFlash64Size;
    case gb::gba::Memory::BackupType::Flash128:
        return kFlash128Size;
    case gb::gba::Memory::BackupType::Eeprom:
        return kEepromMaxSize;
    case gb::gba::Memory::BackupType::None:
    default:
        return 0U;
    }
}

std::string backupTypeToText(gb::gba::Memory::BackupType type) {
    switch (type) {
    case gb::gba::Memory::BackupType::Sram:
        return "SRAM";
    case gb::gba::Memory::BackupType::Flash64:
        return "FLASH64";
    case gb::gba::Memory::BackupType::Flash128:
        return "FLASH128";
    case gb::gba::Memory::BackupType::Eeprom:
        return "EEPROM";
    case gb::gba::Memory::BackupType::None:
    default:
        return "NONE";
    }
}

bool allBytesEqual(const std::vector<u8>& data, u8 value) {
    return !data.empty() && std::all_of(data.begin(), data.end(), [value](u8 byte) {
        return byte == value;
    });
}

bool backupLoggingEnabled() {
    static const bool enabled = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_BACKUP");
    return enabled;
}

void logBackupEvent(const std::string& message) {
    if (!backupLoggingEnabled()) {
        return;
    }
    static int emitted = 0;
    if (emitted >= 512) {
        if (emitted == 512) {
            std::cerr << "[GBA][BACKUP] log limit reached\n";
            ++emitted;
        }
        return;
    }
    std::cerr << "[GBA][BACKUP] " << message << "\n";
    ++emitted;
}

} // namespace

bool Memory::loadRom(const std::vector<u8>& romData) {
    if (romData.empty()) {
        return false;
    }
    rom_ = romData;
    detectBackupType();
    const u8 backupInit = static_cast<u8>(0xFFU);
    backupStorage_.assign(backupStorageSize(backupType_), backupInit);
    backupDirty_ = false;
    resetBackupState();
    reset();
    return true;
}

void Memory::reset() {
    std::fill(ewram_.begin(), ewram_.end(), static_cast<u8>(0U));
    std::fill(iwram_.begin(), iwram_.end(), static_cast<u8>(0U));
    std::fill(pram_.begin(), pram_.end(), static_cast<u8>(0U));
    std::fill(vram_.begin(), vram_.end(), static_cast<u8>(0U));
    std::fill(oam_.begin(), oam_.end(), static_cast<u8>(0U));
    std::fill(io_.begin(), io_.end(), static_cast<u8>(0U));

    for (auto& timer : timers_) {
        timer = TimerState{};
    }

    setKeyInputRaw(DefaultKeyInput);
    writeIo16(KeyControlOffset, 0);
    writeIo16(IeOffset, 0);
    writeIo16(IfOffset, 0);
    writeIo16(ImeOffset, 0);
    writeIo16(WaitcntOffset, 0);
    resetAudioFifo(0);
    resetAudioFifo(1);
    deferredBusCycles_ = 0;
    resetBackupState();
    accessTimingActive_ = false;
    accessCycles_ = 0;
    lastAccessAddress_ = 0;
    lastAccessBytes_ = 0;
    lastAccessValid_ = false;
    lastAccessWasWrite_ = false;
}

void Memory::step(int cpuCycles) {
    if (cpuCycles <= 0) {
        return;
    }
    stepTimers(static_cast<std::uint32_t>(cpuCycles));
}

void Memory::beginAccessTiming() {
    accessTimingActive_ = true;
    accessCycles_ = 0;
    lastAccessAddress_ = 0;
    lastAccessBytes_ = 0;
    lastAccessValid_ = false;
    lastAccessWasWrite_ = false;
}

int Memory::consumeAccessTiming() {
    const int cycles = accessCycles_;
    accessTimingActive_ = false;
    accessCycles_ = 0;
    lastAccessAddress_ = 0;
    lastAccessBytes_ = 0;
    lastAccessValid_ = false;
    lastAccessWasWrite_ = false;
    return cycles;
}

int Memory::consumeDeferredBusCycles() {
    const int cycles = deferredBusCycles_;
    deferredBusCycles_ = 0;
    return cycles;
}

std::size_t Memory::audioFifoLevel(int fifoIndex) const {
    if (fifoIndex < 0 || fifoIndex >= static_cast<int>(audioFifos_.size())) {
        return 0U;
    }
    return audioFifos_[static_cast<std::size_t>(fifoIndex)].size;
}

u8 Memory::audioFifoLastSample(int fifoIndex) const {
    if (fifoIndex < 0 || fifoIndex >= static_cast<int>(audioFifos_.size())) {
        return 0U;
    }
    return audioFifos_[static_cast<std::size_t>(fifoIndex)].lastSample;
}

Memory::AudioFifoState& Memory::audioFifo(int fifoIndex) {
    return audioFifos_[static_cast<std::size_t>(fifoIndex)];
}

const Memory::AudioFifoState& Memory::audioFifo(int fifoIndex) const {
    return audioFifos_[static_cast<std::size_t>(fifoIndex)];
}

bool Memory::isSoundFifoAddress(u32 address, int& fifoIndex) {
    const u32 offset = address & 0x3FFU;
    if (offset >= kFifoAOffset && offset < kFifoAOffset + 4U) {
        fifoIndex = 0;
        return true;
    }
    if (offset >= kFifoBOffset && offset < kFifoBOffset + 4U) {
        fifoIndex = 1;
        return true;
    }
    fifoIndex = -1;
    return false;
}

void Memory::resetAudioFifo(int fifoIndex) {
    if (fifoIndex < 0 || fifoIndex >= static_cast<int>(audioFifos_.size())) {
        return;
    }
    audioFifo(fifoIndex) = AudioFifoState{};
}

void Memory::writeAudioFifo(int fifoIndex, u32 value, int bytes) {
    if (fifoIndex < 0 || fifoIndex >= static_cast<int>(audioFifos_.size()) || bytes <= 0) {
        return;
    }
    AudioFifoState& fifo = audioFifo(fifoIndex);
    const int clampedBytes = std::min(bytes, 4);
    for (int i = 0; i < clampedBytes; ++i) {
        const u8 sample = static_cast<u8>((value >> (i * 8)) & 0xFFU);
        if (fifo.size < fifo.data.size()) {
            const std::size_t writePos = (fifo.readPos + fifo.size) % fifo.data.size();
            fifo.data[writePos] = sample;
            ++fifo.size;
        } else {
            fifo.data[fifo.readPos] = sample;
            fifo.readPos = (fifo.readPos + 1U) % fifo.data.size();
        }
    }
}

bool Memory::isSoundFifoTimerSelected(int fifoIndex, std::size_t timerIndex) const {
    if (timerIndex > 1U || fifoIndex < 0 || fifoIndex >= static_cast<int>(audioFifos_.size())) {
        return false;
    }
    const u16 soundCntH = readIo16(kSoundCntHOffset);
    if (fifoIndex == 0) {
        const bool routed = (soundCntH & 0x0300U) != 0U;
        const std::size_t selectedTimer = (soundCntH & 0x0400U) != 0U ? 1U : 0U;
        return routed && selectedTimer == timerIndex;
    }
    const bool routed = (soundCntH & 0x3000U) != 0U;
    const std::size_t selectedTimer = (soundCntH & 0x4000U) != 0U ? 1U : 0U;
    return routed && selectedTimer == timerIndex;
}

void Memory::triggerSoundFifoDma(int fifoIndex) {
    const u32 fifoAddress = fifoIndex == 0 ? (IoBase + kFifoAOffset) : (IoBase + kFifoBOffset);
    for (std::size_t channel = 1; channel <= 2; ++channel) {
        const std::size_t base = dmaOffset(channel);
        const u32 dest = static_cast<u32>(io_[base + 4U])
            | (static_cast<u32>(io_[base + 5U]) << 8U)
            | (static_cast<u32>(io_[base + 6U]) << 16U)
            | (static_cast<u32>(io_[base + 7U]) << 24U);
        const u16 control = static_cast<u16>(io_[base + 10U] | (static_cast<u16>(io_[base + 11U]) << 8U));
        const bool enabled = (control & 0x8000U) != 0U;
        const u16 startTiming = static_cast<u16>((control >> 12U) & 0x3U);
        if (!enabled || startTiming != 3U) {
            continue;
        }
        if ((dest & ~0x3U) != fifoAddress) {
            continue;
        }
        executeDmaTransfer(channel, 3U);
    }
}

void Memory::tickAudioFifosForTimer(std::size_t timerIndex, std::uint32_t overflowCount) {
    if (overflowCount == 0U || timerIndex > 1U) {
        return;
    }
    for (int fifoIndex = 0; fifoIndex < 2; ++fifoIndex) {
        if (!isSoundFifoTimerSelected(fifoIndex, timerIndex)) {
            continue;
        }
        AudioFifoState& fifo = audioFifo(fifoIndex);
        for (std::uint32_t i = 0; i < overflowCount; ++i) {
            if (fifo.size != 0U) {
                fifo.lastSample = fifo.data[fifo.readPos];
                fifo.readPos = (fifo.readPos + 1U) % fifo.data.size();
                --fifo.size;
            }
            if (fifo.size <= 16U) {
                triggerSoundFifoDma(fifoIndex);
            }
        }
    }
}

void Memory::handleSoundControlWrite(u16 value) {
    if ((value & 0x0800U) != 0U) {
        resetAudioFifo(0);
    }
    if ((value & 0x8000U) != 0U) {
        resetAudioFifo(1);
    }
}

u8 Memory::read8(u32 address) const {
    accountAccessTiming(address, 1, false);
    return read8Raw(address);
}

u16 Memory::read16(u32 address) const {
    accountAccessTiming(address, 2, false);
    if ((address >> 24U) == 0x0DU && isEepromBackup()) {
        return static_cast<u16>(readEepromBit() & 0x1U);
    }
    if ((address >> 24U) == 0x0EU && hasPersistentBackup()) {
        const u8 v = readBackup8(address);
        return static_cast<u16>(v | static_cast<u16>(v << 8U));
    }
    const u32 aligned = address & ~1U;
    const u16 lo = static_cast<u16>(read8Raw(aligned));
    const u16 hi = static_cast<u16>(read8Raw(aligned + 1U));
    return static_cast<u16>(lo | static_cast<u16>(hi << 8U));
}

u32 Memory::read32(u32 address) const {
    accountAccessTiming(address, 4, false);
    if ((address >> 24U) == 0x0DU && isEepromBackup()) {
        const u32 bit0 = static_cast<u32>(readEepromBit() & 0x1U);
        const u32 bit1 = static_cast<u32>(readEepromBit() & 0x1U);
        return bit0 | (bit1 << 16U);
    }
    if ((address >> 24U) == 0x0EU && hasPersistentBackup()) {
        const u32 v = static_cast<u32>(readBackup8(address));
        return v | (v << 8U) | (v << 16U) | (v << 24U);
    }
    const u32 aligned = address & ~3U;
    const u32 b0 = static_cast<u32>(read8Raw(aligned));
    const u32 b1 = static_cast<u32>(read8Raw(aligned + 1U));
    const u32 b2 = static_cast<u32>(read8Raw(aligned + 2U));
    const u32 b3 = static_cast<u32>(read8Raw(aligned + 3U));
    const u32 raw = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
    const unsigned rotate = static_cast<unsigned>((address & 0x3U) * 8U);
    return rotateRight32(raw, rotate);
}

void Memory::write8(u32 address, u8 value) {
    accountAccessTiming(address, 1, true);
    const u32 region = address >> 24U;
    if (region == 0x04U) {
        int fifoIndex = -1;
        if (isSoundFifoAddress(address, fifoIndex)) {
            writeAudioFifo(fifoIndex, value, 1);
            return;
        }
    }
    if (region == 0x0DU && isEepromBackup()) {
        handleEepromWriteBit(static_cast<u8>(value & 0x1U));
        return;
    }
    if (region == 0x0EU && hasPersistentBackup()) {
        writeBackup8(address, value);
        return;
    }
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
    accountAccessTiming(address, 2, true);
    if ((address >> 24U) == 0x04U) {
        int fifoIndex = -1;
        if (isSoundFifoAddress(address, fifoIndex)) {
            writeAudioFifo(fifoIndex, value, 2);
            return;
        }
    }
    if ((address >> 24U) == 0x0DU && isEepromBackup()) {
        handleEepromWriteBit(static_cast<u8>(value & 0x1U));
        return;
    }
    if ((address >> 24U) == 0x0EU && hasPersistentBackup()) {
        // Backup bus (SRAM/FLASH) e 8-bit: halfword write afeta somente byte baixo.
        writeBackup8(address, static_cast<u8>(value & 0xFFU));
        return;
    }
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
    accountAccessTiming(address, 4, true);
    if ((address >> 24U) == 0x04U) {
        int fifoIndex = -1;
        if (isSoundFifoAddress(address, fifoIndex)) {
            writeAudioFifo(fifoIndex, value, 4);
            return;
        }
    }
    if ((address >> 24U) == 0x0DU && isEepromBackup()) {
        handleEepromWriteBit(static_cast<u8>(value & 0x1U));
        return;
    }
    if ((address >> 24U) == 0x0EU && hasPersistentBackup()) {
        // Backup bus (SRAM/FLASH) e 8-bit: word write afeta somente byte baixo.
        writeBackup8(address, static_cast<u8>(value & 0xFFU));
        return;
    }
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

u8 Memory::read8Raw(u32 address) const {
    if (const u8* ptr = writableBytePointer(address); ptr != nullptr) {
        return *ptr;
    }

    const u32 region = address >> 24U;
    if (region == 0x0DU && isEepromBackup()) {
        return readEepromBit();
    }
    if (region == 0x0EU && hasPersistentBackup()) {
        return readBackup8(address);
    }
    if (region >= 0x08U && region <= 0x0DU) {
        return readRom8(address);
    }
    return 0;
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

const u8* Memory::directReadPointer(u32 address, std::size_t size) const {
    if (size == 0U) {
        return nullptr;
    }

    const u32 region = address >> 24U;
    switch (region) {
    case 0x02U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFFFU);
        if (idx + size > ewram_.size()) {
            return nullptr;
        }
        return ewram_.data() + idx;
    }
    case 0x03U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x7FFFU);
        if (idx + size > iwram_.size()) {
            return nullptr;
        }
        return iwram_.data() + idx;
    }
    case 0x05U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        if (idx + size > pram_.size()) {
            return nullptr;
        }
        return pram_.data() + idx;
    }
    case 0x06U: {
        const u32 rawOffset = address & 0x1FFFFU;
        if (rawOffset >= 0x18000U) {
            return nullptr;
        }
        const std::size_t idx = mapVramOffset(address);
        if (idx + size > vram_.size()) {
            return nullptr;
        }
        return vram_.data() + idx;
    }
    case 0x07U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        if (idx + size > oam_.size()) {
            return nullptr;
        }
        return oam_.data() + idx;
    }
    default:
        break;
    }

    if (region >= 0x08U && region <= 0x0DU && !rom_.empty()) {
        const u32 romAddress = address - 0x08000000U;
        const std::size_t idx = static_cast<std::size_t>(romAddress);
        if (idx + size > rom_.size()) {
            return nullptr;
        }
        return rom_.data() + idx;
    }

    return nullptr;
}

u8* Memory::directWritePointer(u32 address, std::size_t size) {
    if (size == 0U) {
        return nullptr;
    }

    const u32 region = address >> 24U;
    switch (region) {
    case 0x02U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFFFU);
        if (idx + size > ewram_.size()) {
            return nullptr;
        }
        return ewram_.data() + idx;
    }
    case 0x03U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x7FFFU);
        if (idx + size > iwram_.size()) {
            return nullptr;
        }
        return iwram_.data() + idx;
    }
    case 0x05U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        if (idx + size > pram_.size()) {
            return nullptr;
        }
        return pram_.data() + idx;
    }
    case 0x06U: {
        const u32 rawOffset = address & 0x1FFFFU;
        if (rawOffset >= 0x18000U) {
            return nullptr;
        }
        const std::size_t idx = mapVramOffset(address);
        if (idx + size > vram_.size()) {
            return nullptr;
        }
        return vram_.data() + idx;
    }
    case 0x07U: {
        const std::size_t idx = static_cast<std::size_t>(address & 0x3FFU);
        if (idx + size > oam_.size()) {
            return nullptr;
        }
        return oam_.data() + idx;
    }
    default:
        return nullptr;
    }
}

void Memory::accountAccessTiming(u32 address, int accessBytes, bool write) const {
    if (accessBytes <= 0) {
        return;
    }

    const u8 region = static_cast<u8>(address >> 24U);
    bool sequential = false;
    if (lastAccessValid_
        && lastAccessWasWrite_ == write
        && static_cast<u8>(lastAccessAddress_ >> 24U) == region
        && address == lastAccessAddress_ + static_cast<u32>(lastAccessBytes_)) {
        sequential = true;
    }

    accessCycles_ += accessCyclesForRegion(address, accessBytes, sequential);
    lastAccessAddress_ = address;
    lastAccessBytes_ = accessBytes;
    lastAccessValid_ = true;
    lastAccessWasWrite_ = write;
}

int Memory::accessCyclesForRegion(u32 address, int accessBytes, bool sequential) const {
    const auto waitcnt = readIo16(WaitcntOffset);
    const auto gamePakCycles16 = [&](int waitStateIndex, bool seq) {
        const int nonSequentialShift = 2 + waitStateIndex * 3;
        const int sequentialShift = 4 + waitStateIndex * 3;
        const int nonSequentialIndex = static_cast<int>((waitcnt >> nonSequentialShift) & 0x3U);
        const int sequentialIndex = static_cast<int>((waitcnt >> sequentialShift) & 0x1U);
        if (!seq) {
            return kGamePakNonSequentialCycles[static_cast<std::size_t>(nonSequentialIndex)];
        }
        switch (waitStateIndex) {
        case 0:
            return kGamePakSequentialCycles0[static_cast<std::size_t>(sequentialIndex)];
        case 1:
            return kGamePakSequentialCycles1[static_cast<std::size_t>(sequentialIndex)];
        default:
            return kGamePakSequentialCycles2[static_cast<std::size_t>(sequentialIndex)];
        }
    };

    const auto accessCycles16 = [&](u32 halfwordAddress, bool seq) {
        const u8 region = static_cast<u8>(halfwordAddress >> 24U);
        switch (region) {
        case 0x00U: // BIOS
        case 0x03U: // IWRAM
        case 0x04U: // IO
        case 0x05U: // PRAM
        case 0x06U: // VRAM
        case 0x07U: // OAM
            return 1;
        case 0x02U: // EWRAM
            return 3;
        case 0x08U:
        case 0x09U:
            return gamePakCycles16(0, seq);
        case 0x0AU:
        case 0x0BU:
            return gamePakCycles16(1, seq);
        case 0x0CU:
        case 0x0DU:
            return gamePakCycles16(2, seq);
        case 0x0EU: {
            const int sramIndex = static_cast<int>(waitcnt & 0x3U);
            return kGamePakNonSequentialCycles[static_cast<std::size_t>(sramIndex)];
        }
        default:
            return 1;
        }
    };

    if (accessBytes <= 2) {
        return accessCycles16(address, sequential);
    }

    const int first = accessCycles16(address, sequential);
    const int second = accessCycles16(address + 2U, true);
    return first + second;
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

    if (ioOffset == kSoundCntHOffset) {
        handleSoundControlWrite(value);
        value = static_cast<u16>(value & ~static_cast<u16>(0x8800U));
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

bool Memory::hasPersistentBackup() const {
    return backupType_ != BackupType::None;
}

const std::string& Memory::backupTypeName() const {
    return backupTypeName_;
}

void Memory::configureBackupBehavior(int forcedEepromAddressBits, bool strictBackupFileSize) {
    if (forcedEepromAddressBits == 6 || forcedEepromAddressBits == 14) {
        forcedEepromAddressBits_ = forcedEepromAddressBits;
    } else {
        forcedEepromAddressBits_ = 0;
    }
    strictBackupFileSize_ = strictBackupFileSize;
    resetBackupState();
}

void Memory::setFlashIdOverride(int vendorId, int deviceId) {
    flashVendorIdOverride_ = (vendorId >= 0 && vendorId <= 0xFF) ? vendorId : -1;
    flashDeviceIdOverride_ = (deviceId >= 0 && deviceId <= 0xFF) ? deviceId : -1;
}

void Memory::setFlashCompatibilityMode(bool enabled) {
    flashCompatibilityMode_ = enabled;
    if (!flashCompatibilityMode_ || !isFlashBackup() || backupStorage_.empty()) {
        return;
    }
    const bool looksPristine = std::all_of(backupStorage_.begin(), backupStorage_.end(), [](u8 v) {
        return v == 0xFFU;
    });
    if (looksPristine) {
        std::fill(backupStorage_.begin(), backupStorage_.end(), static_cast<u8>(0x00U));
    }
}

bool Memory::flashCompatibilityMode() const {
    return flashCompatibilityMode_;
}

std::size_t Memory::expectedBackupFileSize() const {
    return backupPersistSizeBytes();
}

bool Memory::loadBackupFromFile(const std::string& path) {
    if (backupStorage_.empty() || path.empty()) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<u8> fileData{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    };
    if (fileData.empty()) {
        return false;
    }
    int loadedEepromAddressBits = 0;
    if (backupType_ == BackupType::Eeprom && forcedEepromAddressBits_ == 0) {
        if (fileData.size() >= kEepromMaxSize) {
            loadedEepromAddressBits = 14;
        } else if (fileData.size() >= 512U) {
            loadedEepromAddressBits = 6;
        }
    }
    if (strictBackupFileSize_ && fileData.size() != backupPersistSizeBytes()) {
        if (backupType_ != BackupType::Eeprom) {
            return false;
        }
    }
    std::fill(backupStorage_.begin(), backupStorage_.end(), static_cast<u8>(0xFFU));
    const std::size_t copySize = std::min(fileData.size(), backupStorage_.size());
    std::copy(fileData.begin(), fileData.begin() + static_cast<std::ptrdiff_t>(copySize), backupStorage_.begin());

    resetBackupState();
    if (backupType_ == BackupType::Eeprom && loadedEepromAddressBits != 0 && forcedEepromAddressBits_ == 0) {
        eepromAddressBits_ = loadedEepromAddressBits;
    }
    if (isFlashBackup()) {
        if (!flashCompatibilityMode_ && allBytesEqual(backupStorage_, static_cast<u8>(0x00U))) {
            std::fill(backupStorage_.begin(), backupStorage_.end(), static_cast<u8>(0xFFU));
        } else if (flashCompatibilityMode_ && allBytesEqual(backupStorage_, static_cast<u8>(0xFFU))) {
            std::fill(backupStorage_.begin(), backupStorage_.end(), static_cast<u8>(0x00U));
        }
    }
    backupDirty_ = false;
    return true;
}

bool Memory::saveBackupToFile(const std::string& path) const {
    if (backupStorage_.empty() || path.empty()) {
        return false;
    }
    const std::filesystem::path filePath(path);
    std::error_code ec;
    if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    const std::size_t persistSize = backupPersistSizeBytes();
    if (persistSize == 0U || persistSize > backupStorage_.size()) {
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(backupStorage_.data()),
        static_cast<std::streamsize>(persistSize)
    );
    return static_cast<bool>(out);
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

    if (overflowCount != 0U) {
        tickAudioFifosForTimer(timerIndex, overflowCount);
    }

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

    const u32 fifoADest = IoBase + kFifoAOffset;
    const u32 fifoBDest = IoBase + kFifoBOffset;
    const bool soundFifoDma = startTiming == 3U
        && (channel == 1U || channel == 2U)
        && transfer32
        && ((destInit & ~0x3U) == fifoADest || (destInit & ~0x3U) == fifoBDest);

    const u32 maxCount = channel == 3U ? 0x10000U : 0x4000U;
    u32 units = soundFifoDma ? 4U : (count == 0U ? maxCount : static_cast<u32>(count));

    const u16 dstCtrl = soundFifoDma ? 2U : static_cast<u16>((control >> 5U) & 0x3U);
    const u16 srcCtrl = static_cast<u16>((control >> 7U) & 0x3U);
    const u32 stride = transfer32 ? 4U : 2U;
    const bool eepromWriteTransfer = isEepromBackup()
        && !transfer32
        && ((destInit >> 24U) == 0x0DU);
    if (eepromWriteTransfer) {
        eepromExpectedWriteBits_ = static_cast<int>(units);
        logBackupEvent(
            "dma-eeprom-write bits=" + std::to_string(units)
            + " src=0x" + std::to_string(sourceInit)
        );
    }

    const int accessBefore = accessCycles_;
    const bool timingSessionActive = accessTimingActive_;
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

    const bool keepEnabled = (repeat || soundFifoDma) && startTiming != 0U;
    if (!keepEnabled) {
        control = static_cast<u16>(control & ~0x8000U);
        io_[base + 10U] = static_cast<u8>(control & 0xFFU);
        io_[base + 11U] = static_cast<u8>((control >> 8U) & 0xFFU);
    }

    if (!timingSessionActive) {
        deferredBusCycles_ += accessCycles_ - accessBefore;
        accessCycles_ = accessBefore;
        lastAccessValid_ = false;
        lastAccessBytes_ = 0;
    }

    if (irqOnEnd) {
        requestInterrupt(static_cast<u16>(1U << (8U + static_cast<u16>(channel))));
    }
    if (eepromWriteTransfer) {
        eepromExpectedWriteBits_ = 0;
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

void Memory::detectBackupType() {
    backupType_ = BackupType::None;
    if (containsAsciiTag(rom_, "EEPROM_V")) {
        backupType_ = BackupType::Eeprom;
        return;
    }
    if (containsAsciiTag(rom_, "FLASH1M_V")) {
        backupType_ = BackupType::Flash128;
        return;
    }
    if (containsAsciiTag(rom_, "FLASH512_V")) {
        backupType_ = BackupType::Flash64;
        return;
    }
    if (containsAsciiTag(rom_, "FLASH_V")) {
        backupType_ = BackupType::Flash64;
        return;
    }
    if (containsAsciiTag(rom_, "SRAM_V") || containsAsciiTag(rom_, "SRAM_F_V")) {
        backupType_ = BackupType::Sram;
        return;
    }
}

std::size_t Memory::effectiveEepromAddressBits() const {
    if (forcedEepromAddressBits_ == 6 || forcedEepromAddressBits_ == 14) {
        return static_cast<std::size_t>(forcedEepromAddressBits_);
    }
    if (eepromAddressBits_ == 6 || eepromAddressBits_ == 14) {
        return static_cast<std::size_t>(eepromAddressBits_);
    }
    // Heuristica comum: ROM > 16 MiB tende a usar EEPROM 8 KiB (14-bit).
    return rom_.size() > 0x01000000U ? 14U : 6U;
}

std::size_t Memory::backupPersistSizeBytes() const {
    switch (backupType_) {
    case BackupType::Sram:
        return kSramSize;
    case BackupType::Flash64:
        return kFlash64Size;
    case BackupType::Flash128:
        return kFlash128Size;
    case BackupType::Eeprom:
        return effectiveEepromAddressBits() <= 6U ? 512U : kEepromMaxSize;
    case BackupType::None:
    default:
        return 0U;
    }
}

void Memory::resetBackupState() {
    backupTypeName_ = backupTypeToText(backupType_);
    if (backupType_ == BackupType::Eeprom) {
        eepromAddressBits_ = static_cast<int>(effectiveEepromAddressBits());
    } else {
        eepromAddressBits_ = 0;
    }
    eepromWriteBits_.clear();
    eepromReadBits_.clear();
    eepromReadCursor_ = 0;
    eepromExpectedWriteBits_ = 0;
    flashStage_ = FlashStage::Ready;
    flashIdMode_ = false;
    flashBank_ = 0;
}

u8 Memory::readBackup8(u32 address) const {
    if (backupStorage_.empty()) {
        return 0xFFU;
    }

    if (isFlashBackup()) {
        if (flashCompatibilityMode_) {
            const std::size_t idx = static_cast<std::size_t>(address & kBackupSramFlashMask) % backupStorage_.size();
            return backupStorage_[idx];
        }
        return readFlash8(address);
    }

    if (backupType_ == BackupType::Sram) {
        const std::size_t idx = static_cast<std::size_t>(address & kBackupSramFlashMask) % backupStorage_.size();
        return backupStorage_[idx];
    }

    return 0xFFU;
}

void Memory::writeBackup8(u32 address, u8 value) {
    if (backupStorage_.empty()) {
        return;
    }

    if (isFlashBackup()) {
        if (flashCompatibilityMode_) {
            const std::size_t idx = static_cast<std::size_t>(address & kBackupSramFlashMask) % backupStorage_.size();
            if (backupStorage_[idx] != value) {
                backupStorage_[idx] = value;
                backupDirty_ = true;
            }
            return;
        }
        handleFlashCommandWrite(address, value);
        return;
    }

    if (backupType_ == BackupType::Sram) {
        const std::size_t idx = static_cast<std::size_t>(address & kBackupSramFlashMask) % backupStorage_.size();
        if (backupStorage_[idx] != value) {
            backupStorage_[idx] = value;
            backupDirty_ = true;
        }
    }
}

bool Memory::isEepromBackup() const {
    return backupType_ == BackupType::Eeprom;
}

bool Memory::isFlashBackup() const {
    return backupType_ == BackupType::Flash64 || backupType_ == BackupType::Flash128;
}

u8 Memory::readEepromBit() const {
    if (!isEepromBackup()) {
        return 1U;
    }
    if (eepromReadCursor_ < eepromReadBits_.size()) {
        const u8 bit = static_cast<u8>(eepromReadBits_[eepromReadCursor_] & 0x1U);
        ++eepromReadCursor_;
        if (eepromReadCursor_ >= eepromReadBits_.size()) {
            eepromReadBits_.clear();
            eepromReadCursor_ = 0;
        }
        return bit;
    }
    // Linha serial ociosa permanece em alto.
    return 1U;
}

void Memory::pushEepromReadBlock(u32 addressWord) {
    if (!isEepromBackup() || backupStorage_.empty()) {
        return;
    }
    const std::size_t blockCount = eepromAddressBits_ <= 6 ? 64U : 1024U;
    const std::size_t safeBlockCount = std::min(blockCount, backupStorage_.size() / kEepromWordBytes);
    if (safeBlockCount == 0U) {
        return;
    }
    const std::size_t block = static_cast<std::size_t>(addressWord) & (safeBlockCount - 1U);
    const std::size_t base = block * kEepromWordBytes;

    eepromReadBits_.clear();
    eepromReadCursor_ = 0;
    // Datasheet: 4 bits dummy e depois 64 bits de dados.
    eepromReadBits_.insert(eepromReadBits_.end(), 4U, 0U);
    for (std::size_t i = 0; i < kEepromWordBytes; ++i) {
        const u8 byte = backupStorage_[base + i];
        for (int bit = 7; bit >= 0; --bit) {
            eepromReadBits_.push_back(static_cast<u8>((byte >> bit) & 0x1U));
        }
    }
}

void Memory::handleEepromWriteBit(u8 bit) {
    if (!isEepromBackup()) {
        return;
    }
    eepromWriteBits_.push_back(static_cast<u8>(bit & 0x1U));
    maybeCommitEepromCommand();
}

void Memory::maybeCommitEepromCommand() {
    if (eepromExpectedWriteBits_ > 0) {
        const std::size_t expected = static_cast<std::size_t>(eepromExpectedWriteBits_);
        if (eepromWriteBits_.size() < expected) {
            return;
        }
        if (eepromWriteBits_.size() > expected) {
            eepromWriteBits_.clear();
            return;
        }

        auto decodeRead = [&](int addressBits) {
            if (expected != static_cast<std::size_t>(addressBits == 6 ? 9 : 17)) {
                return false;
            }
            if (eepromWriteBits_[0] != 1U || eepromWriteBits_[1] != 1U) {
                return false;
            }
            u32 addressWord = 0U;
            for (int i = 0; i < addressBits; ++i) {
                addressWord = (addressWord << 1U) | static_cast<u32>(eepromWriteBits_[2U + static_cast<std::size_t>(i)] & 0x1U);
            }
            eepromAddressBits_ = addressBits;
            logBackupEvent(
                "eeprom-read bits=" + std::to_string(expected)
                + " addrBits=" + std::to_string(addressBits)
                + " addrWord=" + std::to_string(addressWord)
            );
            pushEepromReadBlock(addressWord);
            eepromWriteBits_.clear();
            return true;
        };

        auto decodeWrite = [&](int addressBits) {
            if (expected != static_cast<std::size_t>(addressBits == 6 ? 73 : 81) || backupStorage_.empty()) {
                return false;
            }
            if (eepromWriteBits_[0] != 1U || eepromWriteBits_[1] != 0U) {
                return false;
            }
            u32 addressWord = 0U;
            for (int i = 0; i < addressBits; ++i) {
                addressWord = (addressWord << 1U) | static_cast<u32>(eepromWriteBits_[2U + static_cast<std::size_t>(i)] & 0x1U);
            }
            logBackupEvent(
                "eeprom-write bits=" + std::to_string(expected)
                + " addrBits=" + std::to_string(addressBits)
                + " addrWord=" + std::to_string(addressWord)
            );
            const std::size_t blockCount = addressBits <= 6 ? 64U : 1024U;
            const std::size_t safeBlockCount = std::min(blockCount, backupStorage_.size() / kEepromWordBytes);
            const std::size_t block = safeBlockCount == 0U ? 0U : (static_cast<std::size_t>(addressWord) & (safeBlockCount - 1U));
            const std::size_t base = block * kEepromWordBytes;
            for (std::size_t byteIndex = 0; byteIndex < kEepromWordBytes; ++byteIndex) {
                u8 out = 0U;
                for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
                    const std::size_t src = 2U + static_cast<std::size_t>(addressBits) + byteIndex * 8U + static_cast<std::size_t>(bitIndex);
                    out = static_cast<u8>((out << 1U) | (eepromWriteBits_[src] & 0x1U));
                }
                if (base + byteIndex < backupStorage_.size() && backupStorage_[base + byteIndex] != out) {
                    backupStorage_[base + byteIndex] = out;
                    backupDirty_ = true;
                }
            }
            eepromAddressBits_ = addressBits;
            eepromWriteBits_.clear();
            return true;
        };

        if (decodeRead(14) || decodeRead(6) || decodeWrite(14) || decodeWrite(6)) {
            return;
        }
        eepromWriteBits_.clear();
        return;
    }

    if (eepromWriteBits_.size() < 2U) {
        return;
    }

    const u8 op = static_cast<u8>((eepromWriteBits_[0] << 1U) | eepromWriteBits_[1]);
    if (op != 0x2U && op != 0x3U) {
        eepromWriteBits_.clear();
        return;
    }

    const auto commitRead = [&](int addressBits, std::size_t totalBits) {
        if (eepromWriteBits_.size() != totalBits) {
            return false;
        }
        u32 addressWord = 0U;
        for (int i = 0; i < addressBits; ++i) {
            addressWord = (addressWord << 1U) | static_cast<u32>(eepromWriteBits_[2U + static_cast<std::size_t>(i)] & 0x1U);
        }
        eepromAddressBits_ = addressBits;
        pushEepromReadBlock(addressWord);
        eepromWriteBits_.clear();
        return true;
    };

    const auto commitWrite = [&](int addressBits, std::size_t totalBits) {
        if (eepromWriteBits_.size() != totalBits || backupStorage_.empty()) {
            return false;
        }
        u32 addressWord = 0U;
        for (int i = 0; i < addressBits; ++i) {
            addressWord = (addressWord << 1U) | static_cast<u32>(eepromWriteBits_[2U + static_cast<std::size_t>(i)] & 0x1U);
        }

        const std::size_t blockCount = addressBits <= 6 ? 64U : 1024U;
        const std::size_t safeBlockCount = std::min(blockCount, backupStorage_.size() / kEepromWordBytes);
        const std::size_t block = safeBlockCount == 0U ? 0U : (static_cast<std::size_t>(addressWord) & (safeBlockCount - 1U));
        const std::size_t base = block * kEepromWordBytes;
        for (std::size_t byteIndex = 0; byteIndex < kEepromWordBytes; ++byteIndex) {
            u8 out = 0U;
            for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
                const std::size_t src = 2U + static_cast<std::size_t>(addressBits) + byteIndex * 8U + static_cast<std::size_t>(bitIndex);
                out = static_cast<u8>((out << 1U) | (eepromWriteBits_[src] & 0x1U));
            }
            if (base + byteIndex < backupStorage_.size() && backupStorage_[base + byteIndex] != out) {
                backupStorage_[base + byteIndex] = out;
                backupDirty_ = true;
            }
        }

        eepromAddressBits_ = addressBits;
        eepromWriteBits_.clear();
        return true;
    };

    if (op == 0x3U) { // READ
        if (eepromAddressBits_ == 6) {
            if (commitRead(6, 9U)) {
                return;
            }
            if (commitRead(14, 17U)) {
                return;
            }
        } else {
            if (commitRead(14, 17U)) {
                return;
            }
            if (commitRead(6, 9U)) {
                return;
            }
        }
    } else { // WRITE
        if (eepromAddressBits_ == 6) {
            if (commitWrite(6, 73U)) {
                return;
            }
            if (commitWrite(14, 81U)) {
                return;
            }
        } else {
            if (commitWrite(14, 81U)) {
                return;
            }
            if (commitWrite(6, 73U)) {
                return;
            }
        }
    }

    // Evita crescimento indefinido caso o jogo envie comando inesperado.
    if (eepromWriteBits_.size() > 96U) {
        eepromWriteBits_.clear();
    }
}

void Memory::handleFlashCommandWrite(u32 address, u8 value) {
    const u32 local = (address - kBackupSramFlashBase) & kBackupSramFlashMask;
    if (value == 0xF0U) {
        flashIdMode_ = false;
        flashStage_ = FlashStage::Ready;
        logBackupEvent("flash-reset-id");
        return;
    }

    switch (flashStage_) {
    case FlashStage::Ready:
        if (local == kFlashUnlockAddr1 && value == 0xAAU) {
            flashStage_ = FlashStage::Unlock1;
        }
        break;
    case FlashStage::Unlock1:
        flashStage_ = (local == kFlashUnlockAddr2 && value == 0x55U)
            ? FlashStage::Unlock2
            : FlashStage::Ready;
        break;
    case FlashStage::Unlock2:
        if (local != kFlashUnlockAddr1) {
            flashStage_ = FlashStage::Ready;
            break;
        }
        switch (value) {
        case 0x90U:
            flashIdMode_ = true;
            flashStage_ = FlashStage::Ready;
            logBackupEvent("flash-enter-id");
            break;
        case 0xA0U:
            flashStage_ = FlashStage::ProgramByte;
            logBackupEvent("flash-program-next");
            break;
        case 0x80U:
            flashStage_ = FlashStage::EraseUnlock1;
            logBackupEvent("flash-erase-sequence");
            break;
        case 0xB0U:
            flashStage_ = FlashStage::BankSelect;
            logBackupEvent("flash-bank-select-next");
            break;
        default:
            flashStage_ = FlashStage::Ready;
            break;
        }
        break;
    case FlashStage::ProgramByte:
        logBackupEvent(
            "flash-program addr=0x" + std::to_string(local)
            + " value=" + std::to_string(value)
        );
        programFlashByte(address, value);
        flashStage_ = FlashStage::Ready;
        break;
    case FlashStage::EraseUnlock1:
        flashStage_ = (local == kFlashUnlockAddr1 && value == 0xAAU)
            ? FlashStage::EraseUnlock2
            : FlashStage::Ready;
        break;
    case FlashStage::EraseUnlock2:
        flashStage_ = (local == kFlashUnlockAddr2 && value == 0x55U)
            ? FlashStage::EraseCommand
            : FlashStage::Ready;
        break;
    case FlashStage::EraseCommand:
        if (local == kFlashUnlockAddr1 && value == 0x10U) {
            logBackupEvent("flash-erase-chip");
            eraseFlashChip();
        } else if (value == 0x30U) {
            logBackupEvent("flash-erase-sector addr=0x" + std::to_string(local));
            eraseFlashSector(address);
        }
        flashStage_ = FlashStage::Ready;
        break;
    case FlashStage::BankSelect:
        if (backupType_ == BackupType::Flash128 && local == 0U) {
            flashBank_ = static_cast<u8>(value & 0x1U);
            logBackupEvent("flash-bank=" + std::to_string(flashBank_));
        }
        flashStage_ = FlashStage::Ready;
        break;
    default:
        flashStage_ = FlashStage::Ready;
        break;
    }
}

u8 Memory::readFlash8(u32 address) const {
    const u32 local = (address - kBackupSramFlashBase) & kBackupSramFlashMask;
    if (flashIdMode_) {
        const auto [vendorId, deviceId] = resolveFlashIdPair(
            backupType_ == BackupType::Flash128,
            flashVendorIdOverride_,
            flashDeviceIdOverride_
        );
        if (local == 0U) {
            return vendorId;
        }
        if (local == 1U) {
            return deviceId;
        }
        return 0xFFU;
    }

    if (backupStorage_.empty()) {
        return 0xFFU;
    }
    std::size_t idx = static_cast<std::size_t>(local);
    if (backupType_ == BackupType::Flash128) {
        idx += static_cast<std::size_t>(flashBank_ & 0x1U) * kFlash64Size;
    }
    idx %= backupStorage_.size();
    return backupStorage_[idx];
}

void Memory::programFlashByte(u32 address, u8 value) {
    if (backupStorage_.empty()) {
        return;
    }
    const u32 local = (address - kBackupSramFlashBase) & kBackupSramFlashMask;
    std::size_t idx = static_cast<std::size_t>(local);
    if (backupType_ == BackupType::Flash128) {
        idx += static_cast<std::size_t>(flashBank_ & 0x1U) * kFlash64Size;
    }
    idx %= backupStorage_.size();

    const u8 programmed = static_cast<u8>(backupStorage_[idx] & value);
    if (backupStorage_[idx] != programmed) {
        backupStorage_[idx] = programmed;
        backupDirty_ = true;
    }
}

void Memory::eraseFlashSector(u32 address) {
    if (backupStorage_.empty()) {
        return;
    }
    const u32 local = (address - kBackupSramFlashBase) & kBackupSramFlashMask;
    std::size_t idx = static_cast<std::size_t>(local & ~0x0FFFU);
    if (backupType_ == BackupType::Flash128) {
        idx += static_cast<std::size_t>(flashBank_ & 0x1U) * kFlash64Size;
    }
    if (idx >= backupStorage_.size()) {
        return;
    }
    const std::size_t end = std::min(idx + 0x1000U, backupStorage_.size());
    std::fill(
        backupStorage_.begin() + static_cast<std::ptrdiff_t>(idx),
        backupStorage_.begin() + static_cast<std::ptrdiff_t>(end),
        static_cast<u8>(0xFFU)
    );
    backupDirty_ = true;
}

void Memory::eraseFlashChip() {
    if (backupStorage_.empty()) {
        return;
    }
    std::fill(backupStorage_.begin(), backupStorage_.end(), static_cast<u8>(0xFFU));
    backupDirty_ = true;
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
