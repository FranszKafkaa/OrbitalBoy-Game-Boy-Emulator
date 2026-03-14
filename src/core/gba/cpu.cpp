#include "gb/core/gba/cpu.hpp"
#include "gb/core/environment.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace gb::gba {

namespace {

constexpr u32 kCpsrN = 1U << 31U;
constexpr u32 kCpsrZ = 1U << 30U;
constexpr u32 kCpsrC = 1U << 29U;
constexpr u32 kCpsrV = 1U << 28U;
constexpr u32 kCpsrF = 1U << 6U;
constexpr u32 kCpsrI = 1U << 7U;
constexpr u32 kCpsrT = 1U << 5U;
constexpr u32 kCpsrModeMask = 0x1FU;

constexpr u32 kModeUser = 0x10U;
constexpr u32 kModeFiq = 0x11U;
constexpr u32 kModeIrq = 0x12U;
constexpr u32 kModeSupervisor = 0x13U;
constexpr u32 kModeAbort = 0x17U;
constexpr u32 kModeUndefined = 0x1BU;
constexpr u32 kModeSystem = 0x1FU;

constexpr u32 kCondMask = 0xF0000000U;
constexpr u32 kPsrByteMaskControl = 0x000000FFU;
constexpr u32 kPsrByteMaskExtension = 0x0000FF00U;
constexpr u32 kPsrByteMaskStatus = 0x00FF0000U;
constexpr u32 kPsrByteMaskFlags = 0xFF000000U;
constexpr u32 kIrqReturnTrampoline = 0xFFFF0010U;
constexpr u32 kVectorUndefined = 0x00000004U;
constexpr u32 kVectorPrefetchAbort = 0x0000000CU;
constexpr double kTwoPi = 6.2831853071795864769;
constexpr std::size_t kAffineTrigTableSize = 256U;

bool isValidExecuteAddress(u32 address) {
    const u8 region = static_cast<u8>(address >> 24U);
    if (region == 0x00U) {
        return address < 0x00004000U;
    }
    return region == 0x02U || region == 0x03U || (region >= 0x08U && region <= 0x0DU);
}

template <typename T>
T clampToType(std::int64_t value) {
    const auto minValue = static_cast<std::int64_t>(std::numeric_limits<T>::min());
    const auto maxValue = static_cast<std::int64_t>(std::numeric_limits<T>::max());
    if (value < minValue) {
        return static_cast<T>(minValue);
    }
    if (value > maxValue) {
        return static_cast<T>(maxValue);
    }
    return static_cast<T>(value);
}

const std::array<std::int16_t, kAffineTrigTableSize>& affineSinTable() {
    static const auto table = []() {
        std::array<std::int16_t, kAffineTrigTableSize> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double radians = (static_cast<double>(i) * kTwoPi) / static_cast<double>(out.size());
            out[i] = static_cast<std::int16_t>(std::llround(std::sin(radians) * 256.0));
        }
        return out;
    }();
    return table;
}

std::int32_t affineSin256(u16 angle) {
    return affineSinTable()[static_cast<std::size_t>(angle & 0x00FFU)];
}

std::int32_t affineCos256(u16 angle) {
    return affineSinTable()[static_cast<std::size_t>((angle + 64U) & 0x00FFU)];
}

std::int32_t roundedShift8(std::int64_t value) {
    if (value >= 0) {
        return static_cast<std::int32_t>((value + 0x80LL) >> 8U);
    }
    return -static_cast<std::int32_t>(((-value) + 0x80LL) >> 8U);
}

bool zeroMemoryRange(Memory& memory, u32 address, std::size_t size) {
    if (u8* dst = memory.directWritePointer(address, size); dst != nullptr) {
        std::fill(dst, dst + static_cast<std::ptrdiff_t>(size), static_cast<u8>(0U));
        return true;
    }
    return false;
}

bool copyHalfwords(Memory& memory, u32 dst, u32 src, u32 count) {
    const std::size_t bytes = static_cast<std::size_t>(count) * 2U;
    const u8* srcPtr = memory.directReadPointer(src, bytes);
    u8* dstPtr = memory.directWritePointer(dst, bytes);
    if (srcPtr == nullptr || dstPtr == nullptr) {
        return false;
    }
    std::memcpy(dstPtr, srcPtr, bytes);
    return true;
}

bool copyWords(Memory& memory, u32 dst, u32 src, u32 count) {
    const std::size_t bytes = static_cast<std::size_t>(count) * 4U;
    const u8* srcPtr = memory.directReadPointer(src, bytes);
    u8* dstPtr = memory.directWritePointer(dst, bytes);
    if (srcPtr == nullptr || dstPtr == nullptr) {
        return false;
    }
    std::memcpy(dstPtr, srcPtr, bytes);
    return true;
}

bool fillHalfwords(Memory& memory, u32 dst, u16 value, u32 count) {
    const std::size_t bytes = static_cast<std::size_t>(count) * 2U;
    u8* dstPtr = memory.directWritePointer(dst, bytes);
    if (dstPtr == nullptr) {
        return false;
    }
    auto* dstWords = reinterpret_cast<u16*>(dstPtr);
    std::fill_n(dstWords, static_cast<std::ptrdiff_t>(count), value);
    return true;
}

bool fillWords(Memory& memory, u32 dst, u32 value, u32 count) {
    const std::size_t bytes = static_cast<std::size_t>(count) * 4U;
    u8* dstPtr = memory.directWritePointer(dst, bytes);
    if (dstPtr == nullptr) {
        return false;
    }
    auto* dstWords = reinterpret_cast<u32*>(dstPtr);
    std::fill_n(dstWords, static_cast<std::ptrdiff_t>(count), value);
    return true;
}

void writeDecodedBuffer(Memory& memory, u32 dst, const std::vector<u8>& decoded, bool vramMode) {
    const std::size_t directSize = decoded.size() + ((vramMode && (decoded.size() & 1U) != 0U) ? 1U : 0U);
    if (u8* directDst = memory.directWritePointer(dst, directSize); directDst != nullptr) {
        if (!decoded.empty()) {
            std::memcpy(directDst, decoded.data(), decoded.size());
        }
        if ((decoded.size() & 1U) != 0U) {
            directDst[decoded.size()] = 0U;
        }
        return;
    }
    if (!vramMode) {
        for (std::size_t i = 0; i < decoded.size(); ++i) {
            memory.write8(dst + static_cast<u32>(i), decoded[i]);
        }
        return;
    }
    for (std::size_t i = 0; i < decoded.size(); i += 2U) {
        const u16 lo = decoded[i];
        const u16 hi = (i + 1U < decoded.size()) ? static_cast<u16>(decoded[i + 1U]) : 0U;
        memory.write16(dst + static_cast<u32>(i), static_cast<u16>(lo | static_cast<u16>(hi << 8U)));
    }
}

void runLz77UnComp(Memory& memory, u32 src, u32 dst, bool vramMode) {
    const u32 header = memory.read32(src);
    if ((header & 0xFFU) != 0x10U) {
        return;
    }
    const u32 outputSize = header >> 8U;
    src += 4U;
    std::vector<u8> decoded(static_cast<std::size_t>(outputSize), 0U);

    u32 written = 0;
    while (written < outputSize) {
        const u8 flags = memory.read8(src++);
        for (int bit = 7; bit >= 0 && written < outputSize; --bit) {
            if ((flags & static_cast<u8>(1U << bit)) == 0U) {
                decoded[written] = memory.read8(src++);
                ++written;
                continue;
            }

            const u8 b1 = memory.read8(src++);
            const u8 b2 = memory.read8(src++);
            const u32 length = static_cast<u32>((b1 >> 4U) + 3U);
            const u32 displacement = (static_cast<u32>(b1 & 0x0FU) << 8U) | static_cast<u32>(b2);
            const u32 back = displacement + 1U;

            for (u32 i = 0; i < length && written < outputSize; ++i) {
                u8 value = 0U;
                if (written >= back) {
                    value = decoded[written - back];
                }
                decoded[written] = value;
                ++written;
            }
        }
    }
    writeDecodedBuffer(memory, dst, decoded, vramMode);
}

void runRlUnComp(Memory& memory, u32 src, u32 dst, bool vramMode) {
    const u32 header = memory.read32(src);
    if ((header & 0xFFU) != 0x30U) {
        return;
    }
    const u32 outputSize = header >> 8U;
    src += 4U;
    std::vector<u8> decoded(static_cast<std::size_t>(outputSize), 0U);

    u32 written = 0;
    while (written < outputSize) {
        const u8 block = memory.read8(src++);
        if ((block & 0x80U) != 0U) {
            const u32 count = static_cast<u32>(block & 0x7FU) + 3U;
            const u8 value = memory.read8(src++);
            for (u32 i = 0; i < count && written < outputSize; ++i) {
                decoded[written] = value;
                ++written;
            }
            continue;
        }
        const u32 count = static_cast<u32>(block & 0x7FU) + 1U;
        for (u32 i = 0; i < count && written < outputSize; ++i) {
            decoded[written] = memory.read8(src++);
            ++written;
        }
    }
    writeDecodedBuffer(memory, dst, decoded, vramMode);
}

} // namespace

void CpuArm7tdmi::connectMemory(Memory* memory) {
    memory_ = memory;
}

void CpuArm7tdmi::refreshLogFlags() {
    logFlags_.badPc = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_BAD_PC");
    logFlags_.biosExec = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_BIOS_EXEC");
    logFlags_.unknown = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_UNKNOWN");
    logFlags_.armWindow = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_ARM_WINDOW");
    logFlags_.stateSwitch = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_STATE_SWITCH");
    logFlags_.bl = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_BL");
    logFlags_.swi = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_SWI");
    logFlags_.irq = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_IRQ");
}

void CpuArm7tdmi::reset() {
    regs_.fill(0);
    userBank_ = BankedRegisters{};
    irqBank_ = BankedRegisters{};
    svcBank_ = BankedRegisters{};
    abtBank_ = BankedRegisters{};
    undBank_ = BankedRegisters{};
    fiqBank_ = BankedRegisters{};
    sharedR8ToR12_.fill(0);
    fiqR8ToR12_.fill(0);

    // Padroes praticos usados pela inicializacao tipica de jogos GBA.
    userBank_.sp = 0x03007F00U;
    irqBank_.sp = 0x03007FA0U;
    svcBank_.sp = 0x03007FE0U;

    cpsr_ = kModeSystem;
    loadModeBank(kModeSystem);
    for (auto& context : irqContextStack_) {
        context = IrqContext{};
    }
    irqContextDepth_ = 0;
    executedInstructions_ = 0;
    halted_ = false;
    waitingForInterrupt_ = false;
    waitingInterruptMask_ = 0;
    lastExecutablePc_ = ResetPc;
    thumbBlPrefixPending_ = false;
    thumbBlPrefixValue_ = 0;
    refreshLogFlags();
    regs_[15] = ResetPc;
}

int CpuArm7tdmi::step() {
    if (memory_ == nullptr) {
        return 0;
    }

    alignPcForCurrentState();
    const u32 executeAddressMask = thumbMode() ? ~1U : ~3U;
    if (!isValidExecuteAddress(regs_[15] & executeAddressMask)) {
        const u32 blockedPc = regs_[15] & executeAddressMask;
        enterPrefetchAbortException(blockedPc);
        waitingForInterrupt_ = false;
        waitingInterruptMask_ = 0U;
        if (logFlags_.badPc) {
            std::cerr << "[GBA][CPU] blocked execute pc=0x" << std::hex << blockedPc
                      << " vector=0x" << regs_[15]
                      << " cpsr=0x" << cpsr_ << std::dec << '\n';
        }
        ++executedInstructions_;
        return 1;
    }
    lastExecutablePc_ = regs_[15];

    (void)handlePendingInterrupt();
    if (waitingForInterrupt_) {
        const u16 mask = waitingInterruptMask_ == 0U ? 0x0001U : waitingInterruptMask_;
        const u16 matched = static_cast<u16>(memory_->interruptFlagsRaw() & mask);
        if (matched != 0U) {
            memory_->clearInterrupt(matched);
            regs_[0] = 0U;
            waitingForInterrupt_ = false;
            halted_ = false;
        }
    }
    if (halted_) {
        if (waitingForInterrupt_) {
            ++executedInstructions_;
            return 1;
        }
        if (memory_->pendingInterrupts() != 0U) {
            halted_ = false;
        } else {
            ++executedInstructions_;
            return 1;
        }
    }

    if (thumbMode()) {
        const u32 currentPc = regs_[15];
        if (logFlags_.biosExec && currentPc < 0x00004000U) {
            static std::uint64_t biosThumbCount = 0;
            if (biosThumbCount < 128U) {
                ++biosThumbCount;
                std::cerr << "[GBA][CPU] bios-thumb pc=0x" << std::hex << currentPc
                          << " lr=0x" << regs_[14]
                          << " sp=0x" << regs_[13]
                          << " cpsr=0x" << cpsr_
                          << std::dec << '\n';
            }
        }
        const u16 instruction = memory_->read16(currentPc);
        regs_[15] = currentPc + 2U;
        const bool executedThumb = executeThumbInstruction(instruction, currentPc);
        if (!executedThumb) {
            if (logFlags_.unknown) {
                static std::uint64_t unknownThumbCount = 0;
                if (unknownThumbCount < 64U) {
                    ++unknownThumbCount;
                    std::cerr << "[GBA][CPU] unknown THUMB op=0x" << std::hex << instruction
                              << " pc=0x" << currentPc << " cpsr=0x" << cpsr_ << std::dec << '\n';
                }
            }
            // THUMB undefined instruction: retorno esperado via LR_und + estado em SPSR_und.
            enterUndefinedException(currentPc + 2U);
        }
        if (logFlags_.badPc
            && regs_[15] < 0x00004000U
            && currentPc >= 0x02000000U) {
            std::cerr << "[GBA][CPU] low-target THUMB op=0x" << std::hex << instruction
                      << " from=0x" << currentPc
                      << " to=0x" << regs_[15]
                      << " lr=0x" << regs_[14]
                      << " cpsr=0x" << cpsr_
                      << std::dec << '\n';
        }
        if (logFlags_.badPc) {
            const u32 pc = regs_[15];
            if (!isValidExecuteAddress(pc & ~1U)) {
                std::cerr << "[GBA][CPU] bad PC after THUMB op=0x" << std::hex << instruction
                          << " from=0x" << currentPc
                          << " to=0x" << pc
                          << " cpsr=0x" << cpsr_
                          << std::dec << '\n';
            }
        }
        ++executedInstructions_;
        return 1;
    }

    const u32 currentPc = regs_[15];
    if (logFlags_.biosExec && currentPc < 0x00004000U) {
        static std::uint64_t biosArmCount = 0;
        if (biosArmCount < 128U) {
            ++biosArmCount;
            std::cerr << "[GBA][CPU] bios-arm pc=0x" << std::hex << currentPc
                      << " lr=0x" << regs_[14]
                      << " sp=0x" << regs_[13]
                      << " cpsr=0x" << cpsr_
                      << std::dec << '\n';
        }
    }
    const u32 instruction = memory_->read32(currentPc);
    regs_[15] = currentPc + 4U;
    if (logFlags_.armWindow
        && currentPc >= 0x03005260U
        && currentPc <= 0x030052A0U) {
        std::cerr << "[GBA][CPU] arm-window pc=0x" << std::hex << currentPc
                  << " op=0x" << instruction
                  << " cpsr=0x" << cpsr_
                  << std::dec << '\n';
    }

    const u8 cond = static_cast<u8>((instruction & kCondMask) >> 28U);
    if (!conditionPassed(cond)) {
        ++executedInstructions_;
        return 1;
    }

    bool executed = false;
    if (executeBranchExchange(instruction)) {
        executed = true;
    } else if (executeSoftwareInterrupt(instruction)) {
        executed = true;
    } else if (executeBranch(instruction)) {
        executed = true;
    } else if (executeBlockDataTransfer(instruction)) {
        executed = true;
    } else if (executeMultiply(instruction)) {
        executed = true;
    } else if (executeSwap(instruction)) {
        executed = true;
    } else if (executeHalfwordDataTransfer(instruction)) {
        executed = true;
    } else if (executeSingleDataTransfer(instruction)) {
        executed = true;
    } else if (executePsrTransfer(instruction)) {
        executed = true;
    } else if (executeDataProcessing(instruction)) {
        executed = true;
    }

    if (!executed) {
        if (logFlags_.unknown) {
            static std::uint64_t unknownArmCount = 0;
            if (unknownArmCount < 64U) {
                ++unknownArmCount;
                std::cerr << "[GBA][CPU] unknown ARM op=0x" << std::hex << instruction
                          << " pc=0x" << currentPc << " cpsr=0x" << cpsr_ << std::dec << '\n';
            }
        }
        // ARM undefined instruction retorna via MOVS pc, lr (LR_und = endereco da proxima instrucao).
        enterUndefinedException(currentPc + 4U);
    }
    if (logFlags_.badPc
        && regs_[15] < 0x00004000U
        && currentPc >= 0x02000000U) {
        std::cerr << "[GBA][CPU] low-target ARM op=0x" << std::hex << instruction
                  << " from=0x" << currentPc
                  << " to=0x" << regs_[15]
                  << " lr=0x" << regs_[14]
                  << " cpsr=0x" << cpsr_
                  << std::dec << '\n';
    }
    if (logFlags_.badPc) {
        const u32 pc = regs_[15];
        if (!isValidExecuteAddress(pc & ~3U)) {
            std::cerr << "[GBA][CPU] bad PC after ARM op=0x" << std::hex << instruction
                      << " from=0x" << currentPc
                      << " to=0x" << pc
                      << " cpsr=0x" << cpsr_
                      << std::dec << '\n';
        }
    }
    ++executedInstructions_;
    return 1;
}

u32 CpuArm7tdmi::reg(int index) const {
    if (index < 0 || index >= static_cast<int>(regs_.size())) {
        return 0;
    }
    return regs_[static_cast<std::size_t>(index)];
}

void CpuArm7tdmi::setReg(int index, u32 value) {
    if (index < 0 || index >= static_cast<int>(regs_.size())) {
        return;
    }
    regs_[static_cast<std::size_t>(index)] = value;
}

u32 CpuArm7tdmi::pc() const {
    return regs_[15];
}

void CpuArm7tdmi::setPc(u32 value) {
    regs_[15] = value;
}

u32 CpuArm7tdmi::cpsr() const {
    return cpsr_;
}

bool CpuArm7tdmi::flagN() const {
    return (cpsr_ & kCpsrN) != 0;
}

bool CpuArm7tdmi::flagZ() const {
    return (cpsr_ & kCpsrZ) != 0;
}

bool CpuArm7tdmi::flagC() const {
    return (cpsr_ & kCpsrC) != 0;
}

bool CpuArm7tdmi::flagV() const {
    return (cpsr_ & kCpsrV) != 0;
}

bool CpuArm7tdmi::thumbMode() const {
    return (cpsr_ & kCpsrT) != 0;
}

void CpuArm7tdmi::setThumbMode(bool enabled) {
    const bool previous = thumbMode();
    setFlag(kCpsrT, enabled);
    if (logFlags_.stateSwitch && previous != enabled) {
        std::cerr << "[GBA][CPU] state-switch " << (previous ? "THUMB" : "ARM")
                  << " -> " << (enabled ? "THUMB" : "ARM")
                  << " pc=0x" << std::hex << regs_[15]
                  << " lr=0x" << regs_[14]
                  << " cpsr=0x" << cpsr_
                  << std::dec << '\n';
    }
}

std::uint64_t CpuArm7tdmi::executedInstructions() const {
    return executedInstructions_;
}

u32 CpuArm7tdmi::rotateRight(u32 value, unsigned amount) {
    const unsigned shift = amount & 31U;
    if (shift == 0U) {
        return value;
    }
    return (value >> shift) | (value << (32U - shift));
}

u32 CpuArm7tdmi::arithmeticShiftRight(u32 value, unsigned amount) {
    if (amount == 0U) {
        return value;
    }
    if (amount >= 32U) {
        return (value & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0x00000000U;
    }
    if ((value & 0x80000000U) == 0U) {
        return value >> amount;
    }
    const u32 shifted = value >> amount;
    const u32 mask = 0xFFFFFFFFU << (32U - amount);
    return shifted | mask;
}

bool CpuArm7tdmi::modeHasSpsr(u32 mode) {
    return mode == kModeFiq
        || mode == kModeIrq
        || mode == kModeSupervisor
        || mode == kModeAbort
        || mode == kModeUndefined;
}

bool CpuArm7tdmi::modeUsesUserBank(u32 mode) {
    return mode == kModeUser || mode == kModeSystem;
}

u32 CpuArm7tdmi::readArmRegister(std::size_t index, bool forStore) const {
    index &= 0x0FU;
    if (index != 15U) {
        return regs_[index];
    }
    // Durante execucao ARM, regs_[15] ja aponta para current+4.
    // Leituras de R15 observam current+8; stores usam current+12.
    return regs_[15] + (forStore ? 8U : 4U);
}

u32 CpuArm7tdmi::readUserBankRegister(std::size_t index) const {
    index &= 0x0FU;
    const u32 mode = cpsr_ & kCpsrModeMask;
    if (index <= 7U) {
        return regs_[index];
    }
    if (index <= 12U) {
        if (mode == kModeFiq) {
            return sharedR8ToR12_[index - 8U];
        }
        return regs_[index];
    }
    if (index == 13U) {
        return modeUsesUserBank(mode) ? regs_[13] : userBank_.sp;
    }
    if (index == 14U) {
        return modeUsesUserBank(mode) ? regs_[14] : userBank_.lr;
    }
    return readArmRegister(15U, true);
}

void CpuArm7tdmi::writeUserBankRegister(std::size_t index, u32 value) {
    index &= 0x0FU;
    const u32 mode = cpsr_ & kCpsrModeMask;
    if (index <= 7U) {
        regs_[index] = value;
        return;
    }
    if (index <= 12U) {
        if (mode == kModeFiq) {
            sharedR8ToR12_[index - 8U] = value;
            return;
        }
        regs_[index] = value;
        sharedR8ToR12_[index - 8U] = value;
        return;
    }
    if (index == 13U) {
        userBank_.sp = value;
        if (modeUsesUserBank(mode)) {
            regs_[13] = value;
        }
        return;
    }
    if (index == 14U) {
        userBank_.lr = value;
        if (modeUsesUserBank(mode)) {
            regs_[14] = value;
        }
        return;
    }
    regs_[15] = value;
}

void CpuArm7tdmi::saveModeBank(u32 mode) {
    mode &= kCpsrModeMask;
    if (mode == kModeFiq) {
        for (std::size_t i = 0; i < fiqR8ToR12_.size(); ++i) {
            fiqR8ToR12_[i] = regs_[8U + i];
        }
    } else {
        for (std::size_t i = 0; i < sharedR8ToR12_.size(); ++i) {
            sharedR8ToR12_[i] = regs_[8U + i];
        }
    }
    if (modeUsesUserBank(mode)) {
        userBank_.sp = regs_[13];
        userBank_.lr = regs_[14];
        return;
    }
    if (mode == kModeIrq) {
        irqBank_.sp = regs_[13];
        irqBank_.lr = regs_[14];
        return;
    }
    if (mode == kModeSupervisor) {
        svcBank_.sp = regs_[13];
        svcBank_.lr = regs_[14];
        return;
    }
    if (mode == kModeAbort) {
        abtBank_.sp = regs_[13];
        abtBank_.lr = regs_[14];
        return;
    }
    if (mode == kModeUndefined) {
        undBank_.sp = regs_[13];
        undBank_.lr = regs_[14];
        return;
    }
    if (mode == kModeFiq) {
        fiqBank_.sp = regs_[13];
        fiqBank_.lr = regs_[14];
    }
}

void CpuArm7tdmi::loadModeBank(u32 mode) {
    mode &= kCpsrModeMask;
    if (mode == kModeFiq) {
        for (std::size_t i = 0; i < fiqR8ToR12_.size(); ++i) {
            regs_[8U + i] = fiqR8ToR12_[i];
        }
    } else {
        for (std::size_t i = 0; i < sharedR8ToR12_.size(); ++i) {
            regs_[8U + i] = sharedR8ToR12_[i];
        }
    }
    if (modeUsesUserBank(mode)) {
        regs_[13] = userBank_.sp;
        regs_[14] = userBank_.lr;
        return;
    }
    if (mode == kModeIrq) {
        regs_[13] = irqBank_.sp;
        regs_[14] = irqBank_.lr;
        return;
    }
    if (mode == kModeSupervisor) {
        regs_[13] = svcBank_.sp;
        regs_[14] = svcBank_.lr;
        return;
    }
    if (mode == kModeAbort) {
        regs_[13] = abtBank_.sp;
        regs_[14] = abtBank_.lr;
        return;
    }
    if (mode == kModeUndefined) {
        regs_[13] = undBank_.sp;
        regs_[14] = undBank_.lr;
        return;
    }
    if (mode == kModeFiq) {
        regs_[13] = fiqBank_.sp;
        regs_[14] = fiqBank_.lr;
    }
}

void CpuArm7tdmi::switchMode(u32 newMode) {
    const u32 oldMode = cpsr_ & kCpsrModeMask;
    newMode &= kCpsrModeMask;
    if (newMode != kModeUser
        && newMode != kModeFiq
        && newMode != kModeIrq
        && newMode != kModeSupervisor
        && newMode != kModeAbort
        && newMode != kModeUndefined
        && newMode != kModeSystem) {
        newMode = kModeSystem;
    }
    if (oldMode == newMode) {
        return;
    }
    saveModeBank(oldMode);
    cpsr_ = (cpsr_ & ~kCpsrModeMask) | newMode;
    loadModeBank(newMode);
}

u32 CpuArm7tdmi::readSpsr() const {
    const u32 mode = cpsr_ & kCpsrModeMask;
    if (!modeHasSpsr(mode)) {
        return cpsr_;
    }
    if (mode == kModeIrq) {
        return irqBank_.spsr;
    }
    if (mode == kModeSupervisor) {
        return svcBank_.spsr;
    }
    if (mode == kModeAbort) {
        return abtBank_.spsr;
    }
    if (mode == kModeUndefined) {
        return undBank_.spsr;
    }
    return fiqBank_.spsr;
}

void CpuArm7tdmi::writeSpsr(u32 value) {
    const u32 mode = cpsr_ & kCpsrModeMask;
    if (!modeHasSpsr(mode)) {
        return;
    }
    if (mode == kModeIrq) {
        irqBank_.spsr = value;
        return;
    }
    if (mode == kModeSupervisor) {
        svcBank_.spsr = value;
        return;
    }
    if (mode == kModeAbort) {
        abtBank_.spsr = value;
        return;
    }
    if (mode == kModeUndefined) {
        undBank_.spsr = value;
        return;
    }
    fiqBank_.spsr = value;
}

void CpuArm7tdmi::writeCpsr(u32 value) {
    const u32 oldMode = cpsr_ & kCpsrModeMask;
    u32 newMode = value & kCpsrModeMask;
    if (newMode != kModeUser
        && newMode != kModeFiq
        && newMode != kModeIrq
        && newMode != kModeSupervisor
        && newMode != kModeAbort
        && newMode != kModeUndefined
        && newMode != kModeSystem) {
        newMode = kModeSystem;
    }

    if (oldMode != newMode) {
        saveModeBank(oldMode);
    }
    cpsr_ = (value & ~kCpsrModeMask) | newMode;
    if (oldMode != newMode) {
        loadModeBank(newMode);
    }
}

void CpuArm7tdmi::alignPcForCurrentState() {
    if (thumbMode()) {
        regs_[15] &= ~1U;
    } else {
        regs_[15] &= ~3U;
    }
}

void CpuArm7tdmi::enterException(u32 newMode, u32 vectorAddress, u32 lrValue, bool maskIrq, bool maskFiq) {
    const u32 oldCpsr = cpsr_;
    switchMode(newMode);
    writeSpsr(oldCpsr);
    regs_[14] = lrValue;
    if (maskIrq) {
        setFlag(kCpsrI, true);
    }
    if (maskFiq) {
        setFlag(kCpsrF, true);
    }
    setThumbMode(false);
    regs_[15] = vectorAddress;
    alignPcForCurrentState();
    thumbBlPrefixPending_ = false;
    halted_ = false;
}

void CpuArm7tdmi::enterUndefinedException(u32 lrValue) {
    enterException(kModeUndefined, kVectorUndefined, lrValue, true, false);
}

void CpuArm7tdmi::enterPrefetchAbortException(u32 faultAddress) {
    enterException(kModeAbort, kVectorPrefetchAbort, faultAddress + 4U, true, false);
}

bool CpuArm7tdmi::conditionPassed(u8 cond) const {
    switch (cond & 0x0FU) {
    case 0x0: // EQ
        return flagZ();
    case 0x1: // NE
        return !flagZ();
    case 0x2: // CS
        return flagC();
    case 0x3: // CC
        return !flagC();
    case 0x4: // MI
        return flagN();
    case 0x5: // PL
        return !flagN();
    case 0x6: // VS
        return flagV();
    case 0x7: // VC
        return !flagV();
    case 0x8: // HI
        return flagC() && !flagZ();
    case 0x9: // LS
        return !flagC() || flagZ();
    case 0xA: // GE
        return flagN() == flagV();
    case 0xB: // LT
        return flagN() != flagV();
    case 0xC: // GT
        return !flagZ() && (flagN() == flagV());
    case 0xD: // LE
        return flagZ() || (flagN() != flagV());
    case 0xE: // AL
        return true;
    default:
        return false;
    }
}

u32 CpuArm7tdmi::readOperand2(u32 instruction, bool& shifterCarry) {
    const bool immediate = (instruction & (1U << 25U)) != 0U;
    if (immediate) {
        const u32 imm8 = instruction & 0xFFU;
        const unsigned rotate = static_cast<unsigned>((instruction >> 8U) & 0x0FU) * 2U;
        const u32 value = rotateRight(imm8, rotate);
        if (rotate == 0U) {
            shifterCarry = flagC();
        } else {
            shifterCarry = (value & 0x80000000U) != 0U;
        }
        return value;
    }

    const std::size_t rmIndex = static_cast<std::size_t>(instruction & 0x0FU);
    u32 rm = readArmRegister(rmIndex);
    if ((instruction & (1U << 4U)) != 0U) {
        const std::size_t rsIndex = static_cast<std::size_t>((instruction >> 8U) & 0x0FU);
        u32 rs = readArmRegister(rsIndex);
        const unsigned amount = static_cast<unsigned>(rs & 0xFFU);
        const unsigned shiftType = static_cast<unsigned>((instruction >> 5U) & 0x03U);

        if (amount == 0U) {
            shifterCarry = flagC();
            return rm;
        }

        switch (shiftType) {
        case 0U: // LSL
            if (amount < 32U) {
                shifterCarry = ((rm >> (32U - amount)) & 1U) != 0U;
                return rm << amount;
            }
            if (amount == 32U) {
                shifterCarry = (rm & 1U) != 0U;
                return 0U;
            }
            shifterCarry = false;
            return 0U;
        case 1U: // LSR
            if (amount < 32U) {
                shifterCarry = ((rm >> (amount - 1U)) & 1U) != 0U;
                return rm >> amount;
            }
            if (amount == 32U) {
                shifterCarry = (rm & 0x80000000U) != 0U;
                return 0U;
            }
            shifterCarry = false;
            return 0U;
        case 2U: // ASR
            if (amount < 32U) {
                shifterCarry = ((rm >> (amount - 1U)) & 1U) != 0U;
                return arithmeticShiftRight(rm, amount);
            }
            shifterCarry = (rm & 0x80000000U) != 0U;
            return shifterCarry ? 0xFFFFFFFFU : 0U;
        case 3U: { // ROR
            const unsigned rotate = amount & 31U;
            if (rotate == 0U) {
                shifterCarry = (rm & 0x80000000U) != 0U;
                return rm;
            }
            const u32 value = rotateRight(rm, rotate);
            shifterCarry = (value & 0x80000000U) != 0U;
            return value;
        }
        default:
            shifterCarry = flagC();
            return rm;
        }
    }

    const unsigned shiftType = static_cast<unsigned>((instruction >> 5U) & 0x03U);
    const unsigned shiftImm = static_cast<unsigned>((instruction >> 7U) & 0x1FU);

    switch (shiftType) {
    case 0U: { // LSL
        if (shiftImm == 0U) {
            shifterCarry = flagC();
            return rm;
        }
        shifterCarry = ((rm >> (32U - shiftImm)) & 1U) != 0U;
        return rm << shiftImm;
    }
    case 1U: { // LSR
        const unsigned amount = shiftImm == 0U ? 32U : shiftImm;
        if (amount == 32U) {
            shifterCarry = (rm & 0x80000000U) != 0U;
            return 0;
        }
        shifterCarry = ((rm >> (amount - 1U)) & 1U) != 0U;
        return rm >> amount;
    }
    case 2U: { // ASR
        const unsigned amount = shiftImm == 0U ? 32U : shiftImm;
        if (amount >= 32U) {
            shifterCarry = (rm & 0x80000000U) != 0U;
            return (rm & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0x00000000U;
        }
        shifterCarry = ((rm >> (amount - 1U)) & 1U) != 0U;
        return arithmeticShiftRight(rm, amount);
    }
    case 3U: { // ROR / RRX
        if (shiftImm == 0U) {
            const u32 carryIn = flagC() ? 0x80000000U : 0U;
            shifterCarry = (rm & 1U) != 0U;
            return carryIn | (rm >> 1U);
        }
        const u32 value = rotateRight(rm, shiftImm);
        shifterCarry = (value & 0x80000000U) != 0U;
        return value;
    }
    default:
        shifterCarry = flagC();
        return rm;
    }
}

bool CpuArm7tdmi::executeThumbInstruction(u16 instruction, u32 currentPc) {
    const auto regIndex = [](u16 value, unsigned shift) -> std::size_t {
        return static_cast<std::size_t>((value >> shift) & 0x7U);
    };
    const auto signExtend8 = [](u32 value) -> std::int32_t {
        return static_cast<std::int32_t>(static_cast<std::int8_t>(value & 0xFFU));
    };
    const auto signExtend11 = [](u32 value) -> std::int32_t {
        const std::int32_t raw = static_cast<std::int32_t>(value & 0x07FFU);
        return (raw & 0x0400) != 0 ? static_cast<std::int32_t>(raw | ~0x07FF) : raw;
    };
    const bool isBlPrefix = (instruction & 0xF800U) == 0xF000U;
    const bool isBlSuffix = (instruction & 0xF800U) == 0xF800U;
    if (!isBlPrefix && !isBlSuffix) {
        thumbBlPrefixPending_ = false;
    }

    // Shift by immediate / add-subtract.
    if ((instruction & 0xF800U) == 0x1800U) {
        const bool immediateOperand = (instruction & 0x0400U) != 0U;
        const bool subtract = (instruction & 0x0200U) != 0U;
        const u32 lhs = regs_[regIndex(instruction, 3)];
        const u32 rhs = immediateOperand
            ? static_cast<u32>((instruction >> 6U) & 0x7U)
            : regs_[regIndex(instruction, 6)];
        const u32 result = subtract ? lhs - rhs : lhs + rhs;
        regs_[regIndex(instruction, 0)] = result;
        if (subtract) {
            updateSubFlags(lhs, rhs, result);
        } else {
            updateAddFlags(lhs, rhs, result);
        }
        return true;
    }
    if ((instruction & 0xE000U) == 0x0000U) {
        const u8 op = static_cast<u8>((instruction >> 11U) & 0x3U);
        const unsigned shift = static_cast<unsigned>((instruction >> 6U) & 0x1FU);
        const u32 value = regs_[regIndex(instruction, 3)];
        u32 result = value;
        bool carry = flagC();

        if (op == 0U) { // LSL
            if (shift != 0U) {
                carry = ((value >> (32U - shift)) & 1U) != 0U;
                result = value << shift;
            }
        } else if (op == 1U) { // LSR
            const unsigned amount = shift == 0U ? 32U : shift;
            if (amount == 32U) {
                carry = (value & 0x80000000U) != 0U;
                result = 0U;
            } else {
                carry = ((value >> (amount - 1U)) & 1U) != 0U;
                result = value >> amount;
            }
        } else if (op == 2U) { // ASR
            const unsigned amount = shift == 0U ? 32U : shift;
            if (amount >= 32U) {
                carry = (value & 0x80000000U) != 0U;
                result = carry ? 0xFFFFFFFFU : 0x00000000U;
            } else {
                carry = ((value >> (amount - 1U)) & 1U) != 0U;
                result = arithmeticShiftRight(value, amount);
            }
        } else {
            return false;
        }

        regs_[regIndex(instruction, 0)] = result;
        updateNz(result);
        setFlag(kCpsrC, carry);
        return true;
    }

    // Immediate MOV/CMP/ADD/SUB.
    if ((instruction & 0xE000U) == 0x2000U) {
        const u8 op = static_cast<u8>((instruction >> 11U) & 0x3U);
        const std::size_t rd = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const u32 imm8 = static_cast<u32>(instruction & 0x00FFU);
        if (op == 0U) { // MOV
            regs_[rd] = imm8;
            updateNz(imm8);
            return true;
        }
        if (op == 1U) { // CMP
            const u32 result = regs_[rd] - imm8;
            updateSubFlags(regs_[rd], imm8, result);
            return true;
        }
        if (op == 2U) { // ADD
            const u32 lhs = regs_[rd];
            const u32 result = lhs + imm8;
            regs_[rd] = result;
            updateAddFlags(lhs, imm8, result);
            return true;
        }
        if (op == 3U) { // SUB
            const u32 lhs = regs_[rd];
            const u32 result = lhs - imm8;
            regs_[rd] = result;
            updateSubFlags(lhs, imm8, result);
            return true;
        }
    }

    // ALU ops register.
    if ((instruction & 0xFC00U) == 0x4000U) {
        const u8 op = static_cast<u8>((instruction >> 6U) & 0x0FU);
        const std::size_t rs = regIndex(instruction, 3);
        const std::size_t rd = regIndex(instruction, 0);
        const u32 lhs = regs_[rd];
        const u32 rhs = regs_[rs];
        u32 result = lhs;
        bool writeResult = true;

        switch (op) {
        case 0x0: // AND
            result = lhs & rhs;
            updateNz(result);
            break;
        case 0x1: // EOR
            result = lhs ^ rhs;
            updateNz(result);
            break;
        case 0x2: { // LSL (register)
            const unsigned amount = static_cast<unsigned>(rhs & 0xFFU);
            if (amount == 0U) {
                result = lhs;
            } else if (amount < 32U) {
                setFlag(kCpsrC, ((lhs >> (32U - amount)) & 1U) != 0U);
                result = lhs << amount;
            } else if (amount == 32U) {
                setFlag(kCpsrC, (lhs & 1U) != 0U);
                result = 0U;
            } else {
                setFlag(kCpsrC, false);
                result = 0U;
            }
            updateNz(result);
            break;
        }
        case 0x3: { // LSR (register)
            const unsigned amount = static_cast<unsigned>(rhs & 0xFFU);
            if (amount == 0U) {
                result = lhs;
            } else if (amount < 32U) {
                setFlag(kCpsrC, ((lhs >> (amount - 1U)) & 1U) != 0U);
                result = lhs >> amount;
            } else if (amount == 32U) {
                setFlag(kCpsrC, (lhs & 0x80000000U) != 0U);
                result = 0U;
            } else {
                setFlag(kCpsrC, false);
                result = 0U;
            }
            updateNz(result);
            break;
        }
        case 0x4: { // ASR (register)
            const unsigned amount = static_cast<unsigned>(rhs & 0xFFU);
            if (amount == 0U) {
                result = lhs;
            } else if (amount < 32U) {
                setFlag(kCpsrC, ((lhs >> (amount - 1U)) & 1U) != 0U);
                result = arithmeticShiftRight(lhs, amount);
            } else {
                const bool sign = (lhs & 0x80000000U) != 0U;
                setFlag(kCpsrC, sign);
                result = sign ? 0xFFFFFFFFU : 0x00000000U;
            }
            updateNz(result);
            break;
        }
        case 0x5: { // ADC
            const u32 carry = flagC() ? 1U : 0U;
            result = lhs + rhs + carry;
            updateAddFlags(lhs, rhs + carry, result);
            break;
        }
        case 0x6: { // SBC
            const u32 borrow = flagC() ? 0U : 1U;
            result = lhs - rhs - borrow;
            updateSubFlags(lhs, rhs + borrow, result);
            break;
        }
        case 0x7: { // ROR
            const unsigned amount = static_cast<unsigned>(rhs & 0x1FU);
            if (amount == 0U) {
                result = lhs;
            } else {
                result = rotateRight(lhs, amount);
                setFlag(kCpsrC, (result & 0x80000000U) != 0U);
            }
            updateNz(result);
            break;
        }
        case 0x8: // TST
            writeResult = false;
            result = lhs & rhs;
            updateNz(result);
            break;
        case 0x9: // NEG
            result = 0U - rhs;
            updateSubFlags(0U, rhs, result);
            break;
        case 0xA: // CMP
            writeResult = false;
            result = lhs - rhs;
            updateSubFlags(lhs, rhs, result);
            break;
        case 0xB: // CMN
            writeResult = false;
            result = lhs + rhs;
            updateAddFlags(lhs, rhs, result);
            break;
        case 0xC: // ORR
            result = lhs | rhs;
            updateNz(result);
            break;
        case 0xD: // MUL
            result = lhs * rhs;
            updateNz(result);
            break;
        case 0xE: // BIC
            result = lhs & ~rhs;
            updateNz(result);
            break;
        case 0xF: // MVN
            result = ~rhs;
            updateNz(result);
            break;
        default:
            return false;
        }

        if (writeResult) {
            regs_[rd] = result;
        }
        return true;
    }

    // High register ops and BX.
    if ((instruction & 0xFC00U) == 0x4400U) {
        const u8 op = static_cast<u8>((instruction >> 8U) & 0x3U);
        const std::size_t rs = static_cast<std::size_t>(((instruction >> 3U) & 0x7U) | ((instruction >> 3U) & 0x8U));
        const std::size_t rd = static_cast<std::size_t>((instruction & 0x7U) | ((instruction >> 4U) & 0x8U));
        const u32 rsValue = rs == 15U
            ? static_cast<u32>((currentPc + 4U) & ~1U)
            : regs_[rs];
        const u32 rdValue = rd == 15U
            ? static_cast<u32>((currentPc + 4U) & ~1U)
            : regs_[rd];

        if (op == 0U) { // ADD
            regs_[rd] = rdValue + rsValue;
            if (rd == 15U) {
                regs_[15] &= ~1U;
            }
            return true;
        }
        if (op == 1U) { // CMP
            const u32 result = rdValue - rsValue;
            updateSubFlags(rdValue, rsValue, result);
            return true;
        }
        if (op == 2U) { // MOV
            regs_[rd] = rsValue;
            if (rd == 15U) {
                regs_[15] &= ~1U;
            }
            return true;
        }

        // BX
        const u32 target = rsValue;
        if (tryReturnFromIrqTrampoline(target)) {
            return true;
        }
        setThumbMode((target & 1U) != 0U);
        regs_[15] = target;
        alignPcForCurrentState();
        return true;
    }

    // LDR literal (PC relative).
    if ((instruction & 0xF800U) == 0x4800U) {
        const std::size_t rd = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const u32 imm = static_cast<u32>(instruction & 0x00FFU) << 2U;
        const u32 base = (currentPc + 4U) & ~3U;
        regs_[rd] = memory_->read32(base + imm);
        return true;
    }

    // Register offset loads/stores (+signed).
    if ((instruction & 0xF000U) == 0x5000U) {
        const u8 op = static_cast<u8>((instruction >> 9U) & 0x7U);
        const u32 addr = regs_[regIndex(instruction, 3)] + regs_[regIndex(instruction, 6)];
        const std::size_t rd = regIndex(instruction, 0);
        switch (op) {
        case 0x0: // STR
            memory_->write32(addr, regs_[rd]);
            return true;
        case 0x1: // STRH
            memory_->write16(addr, static_cast<u16>(regs_[rd] & 0xFFFFU));
            return true;
        case 0x2: // STRB
            memory_->write8(addr, static_cast<u8>(regs_[rd] & 0xFFU));
            return true;
        case 0x3: // LDSB
            regs_[rd] = static_cast<u32>(static_cast<std::int32_t>(static_cast<std::int8_t>(memory_->read8(addr))));
            return true;
        case 0x4: // LDR
            regs_[rd] = memory_->read32(addr);
            return true;
        case 0x5: // LDRH
            regs_[rd] = memory_->read16(addr);
            return true;
        case 0x6: // LDRB
            regs_[rd] = memory_->read8(addr);
            return true;
        case 0x7: // LDSH
            regs_[rd] = static_cast<u32>(static_cast<std::int32_t>(static_cast<std::int16_t>(memory_->read16(addr))));
            return true;
        default:
            return false;
        }
    }

    // Immediate offset loads/stores (word/byte).
    if ((instruction & 0xE000U) == 0x6000U) {
        const bool load = (instruction & 0x0800U) != 0U;
        const bool byteTransfer = (instruction & 0x1000U) != 0U;
        const u32 imm5 = static_cast<u32>((instruction >> 6U) & 0x1FU);
        const u32 addr = regs_[regIndex(instruction, 3)] + (byteTransfer ? imm5 : (imm5 << 2U));
        const std::size_t rd = regIndex(instruction, 0);
        if (load) {
            regs_[rd] = byteTransfer ? static_cast<u32>(memory_->read8(addr)) : memory_->read32(addr);
        } else {
            if (byteTransfer) {
                memory_->write8(addr, static_cast<u8>(regs_[rd] & 0xFFU));
            } else {
                memory_->write32(addr, regs_[rd]);
            }
        }
        return true;
    }

    // Halfword immediate.
    if ((instruction & 0xF000U) == 0x8000U) {
        const bool load = (instruction & 0x0800U) != 0U;
        const u32 imm5 = static_cast<u32>((instruction >> 6U) & 0x1FU);
        const u32 addr = regs_[regIndex(instruction, 3)] + (imm5 << 1U);
        const std::size_t rd = regIndex(instruction, 0);
        if (load) {
            regs_[rd] = memory_->read16(addr);
        } else {
            memory_->write16(addr, static_cast<u16>(regs_[rd] & 0xFFFFU));
        }
        return true;
    }

    // SP relative load/store.
    if ((instruction & 0xF000U) == 0x9000U) {
        const bool load = (instruction & 0x0800U) != 0U;
        const std::size_t rd = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const u32 addr = regs_[13] + (static_cast<u32>(instruction & 0xFFU) << 2U);
        if (load) {
            regs_[rd] = memory_->read32(addr);
        } else {
            memory_->write32(addr, regs_[rd]);
        }
        return true;
    }

    // Load address.
    if ((instruction & 0xF000U) == 0xA000U) {
        const std::size_t rd = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const u32 imm = static_cast<u32>(instruction & 0xFFU) << 2U;
        const bool useSp = (instruction & 0x0800U) != 0U;
        const u32 base = useSp ? regs_[13] : ((currentPc + 4U) & ~3U);
        regs_[rd] = base + imm;
        return true;
    }

    // Add/subtract immediate to SP.
    if ((instruction & 0xFF00U) == 0xB000U) {
        const u32 imm = static_cast<u32>(instruction & 0x7FU) << 2U;
        if ((instruction & 0x0080U) == 0U) {
            regs_[13] += imm;
        } else {
            regs_[13] -= imm;
        }
        return true;
    }

    // PUSH/POP.
    if ((instruction & 0xFE00U) == 0xB400U || (instruction & 0xFE00U) == 0xBC00U) {
        const bool load = (instruction & 0x0800U) != 0U;
        const bool extraReg = (instruction & 0x0100U) != 0U;
        const u16 regList = static_cast<u16>(instruction & 0x00FFU);

        if (!load) { // PUSH
            u32 addr = regs_[13];
            if (extraReg) {
                addr -= 4U;
                memory_->write32(addr, regs_[14]);
            }
            for (int r = 7; r >= 0; --r) {
                if ((regList & static_cast<u16>(1U << r)) == 0U) {
                    continue;
                }
                addr -= 4U;
                memory_->write32(addr, regs_[static_cast<std::size_t>(r)]);
            }
            regs_[13] = addr;
            return true;
        }

        // POP
        u32 addr = regs_[13];
        for (int r = 0; r < 8; ++r) {
            if ((regList & static_cast<u16>(1U << r)) == 0U) {
                continue;
            }
            regs_[static_cast<std::size_t>(r)] = memory_->read32(addr);
            addr += 4U;
        }
        if (extraReg) {
            const u32 target = memory_->read32(addr);
            addr += 4U;
            regs_[15] = target & ~1U;
        }
        regs_[13] = addr;
        return true;
    }

    // Multiple load/store.
    if ((instruction & 0xF000U) == 0xC000U) {
        const bool load = (instruction & 0x0800U) != 0U;
        const std::size_t rb = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const u16 regList = static_cast<u16>(instruction & 0x00FFU);
        u32 addr = regs_[rb];
        for (int r = 0; r < 8; ++r) {
            if ((regList & static_cast<u16>(1U << r)) == 0U) {
                continue;
            }
            if (load) {
                regs_[static_cast<std::size_t>(r)] = memory_->read32(addr);
            } else {
                memory_->write32(addr, regs_[static_cast<std::size_t>(r)]);
            }
            addr += 4U;
        }
        regs_[rb] = addr;
        return true;
    }

    // Conditional branch and SWI.
    if ((instruction & 0xF000U) == 0xD000U) {
        const u8 cond = static_cast<u8>((instruction >> 8U) & 0x0FU);
        if (cond == 0x0FU) {
            return executeThumbSoftwareInterrupt(instruction);
        }
        if (cond == 0x0EU) {
            return true;
        }
        if (conditionPassed(cond)) {
            const std::int32_t offset = signExtend8(instruction & 0x00FFU) * 2;
            const u32 base = currentPc + 4U;
            const u32 target = static_cast<u32>(static_cast<std::int64_t>(base) + offset);
            regs_[15] = target;
        }
        return true;
    }

    // Unconditional branch.
    if ((instruction & 0xF800U) == 0xE000U) {
        const std::int32_t offset = signExtend11(instruction & 0x07FFU) * 2;
        const u32 base = currentPc + 4U;
        const u32 target = static_cast<u32>(static_cast<std::int64_t>(base) + offset);
        regs_[15] = target;
        return true;
    }

    // Long branch with link.
    if ((instruction & 0xF800U) == 0xF000U) {
        const std::int32_t offset = signExtend11(instruction & 0x07FFU) * 4096;
        const u32 prefix = static_cast<u32>(static_cast<std::int64_t>(currentPc + 4U) + offset);
        regs_[14] = prefix;
        thumbBlPrefixPending_ = true;
        thumbBlPrefixValue_ = prefix;
        if (logFlags_.bl) {
            static std::uint64_t blPrefixCount = 0;
            if (blPrefixCount < 128U) {
                ++blPrefixCount;
                std::cerr << "[GBA][CPU] bl-prefix pc=0x" << std::hex << currentPc
                          << " insn=0x" << instruction
                          << " prefix=0x" << prefix
                          << std::dec << '\n';
            }
        }
        return true;
    }
    if ((instruction & 0xF800U) == 0xF800U) {
        const u32 offset = static_cast<u32>(instruction & 0x07FFU) << 1U;
        const u32 base = thumbBlPrefixPending_ ? thumbBlPrefixValue_ : regs_[14];
        const u32 target = base + offset;
        thumbBlPrefixPending_ = false;
        regs_[14] = (currentPc + 2U) | 1U;
        if (logFlags_.bl) {
            static std::uint64_t blSuffixCount = 0;
            if (blSuffixCount < 128U) {
                ++blSuffixCount;
                std::cerr << "[GBA][CPU] bl-suffix pc=0x" << std::hex << currentPc
                          << " insn=0x" << instruction
                          << " base=0x" << base
                          << " target=0x" << target
                          << " lr=0x" << regs_[14]
                          << std::dec << '\n';
            }
            if (target <= 0x00004000U) {
                std::cerr << "[GBA][CPU] bl-suffix-low-target pc=0x" << std::hex << currentPc
                          << " insn=0x" << instruction
                          << " base=0x" << base
                          << " target=0x" << target
                          << " lr=0x" << regs_[14]
                          << std::dec << '\n';
            }
        }
        regs_[15] = target & ~1U;
        return true;
    }

    return false;
}

bool CpuArm7tdmi::executePsrTransfer(u32 instruction) {
    if ((instruction & 0x0C000000U) != 0x00000000U) {
        return false;
    }

    const bool immediate = (instruction & (1U << 25U)) != 0U;
    const u32 opHi = (instruction >> 23U) & 0x1FU;
    const u32 opLow = (instruction >> 20U) & 0x3U;

    const bool maybeMrs = !immediate && opHi == 0x02U && opLow == 0x0U;
    if (maybeMrs
        && ((instruction >> 16U) & 0x0FU) == 0x0FU
        && (instruction & 0x0FFFU) == 0U) {
        const std::size_t rd = static_cast<std::size_t>((instruction >> 12U) & 0x0FU);
        const bool readSpsrSource = (instruction & (1U << 22U)) != 0U;
        regs_[rd] = readSpsrSource ? readSpsr() : cpsr_;
        if (rd == 15U) {
            alignPcForCurrentState();
        }
        return true;
    }

    const bool maybeMsrReg = !immediate && opHi == 0x02U && opLow == 0x2U;
    const bool maybeMsrImm = immediate && opHi == 0x06U && opLow == 0x2U;
    if ((!maybeMsrReg && !maybeMsrImm) || ((instruction >> 12U) & 0x0FU) != 0x0FU) {
        return false;
    }

    const u32 fieldMask = (instruction >> 16U) & 0x0FU;
    if (fieldMask == 0U) {
        return true;
    }

    u32 operand = 0U;
    if (maybeMsrReg) {
        const std::size_t rm = static_cast<std::size_t>(instruction & 0x0FU);
        operand = regs_[rm];
    } else {
        const u32 imm8 = instruction & 0xFFU;
        const unsigned rotate = static_cast<unsigned>((instruction >> 8U) & 0x0FU) * 2U;
        operand = rotateRight(imm8, rotate);
    }

    u32 byteMask = 0U;
    if ((fieldMask & 0x1U) != 0U) {
        byteMask |= kPsrByteMaskControl;
    }
    if ((fieldMask & 0x2U) != 0U) {
        byteMask |= kPsrByteMaskExtension;
    }
    if ((fieldMask & 0x4U) != 0U) {
        byteMask |= kPsrByteMaskStatus;
    }
    if ((fieldMask & 0x8U) != 0U) {
        byteMask |= kPsrByteMaskFlags;
    }

    const bool targetSpsr = (instruction & (1U << 22U)) != 0U;
    if (targetSpsr) {
        const u32 current = readSpsr();
        writeSpsr((current & ~byteMask) | (operand & byteMask));
        return true;
    }

    const u32 nextCpsr = (cpsr_ & ~byteMask) | (operand & byteMask);
    writeCpsr(nextCpsr);
    alignPcForCurrentState();
    return true;
}

bool CpuArm7tdmi::executeDataProcessing(u32 instruction) {
    if ((instruction & 0x0C000000U) != 0x00000000U) {
        return false;
    }

    if ((instruction & 0x0FC000F0U) == 0x00000090U) {
        // Multiplicacao e tratada por executeMultiply.
        return false;
    }

    const u8 opcode = static_cast<u8>((instruction >> 21U) & 0x0FU);
    const bool setFlags = (instruction & (1U << 20U)) != 0U;
    const u8 rn = static_cast<u8>((instruction >> 16U) & 0x0FU);
    const u8 rd = static_cast<u8>((instruction >> 12U) & 0x0FU);

    bool shifterCarry = flagC();
    const u32 op2 = readOperand2(instruction, shifterCarry);
    const u32 lhs = readArmRegister(rn);

    u32 result = 0;
    bool writeResult = true;
    bool updateFlagsLogical = false;
    bool updateFlagsAdd = false;
    bool updateFlagsSub = false;
    u32 lhsForFlags = lhs;
    u32 rhsForFlags = op2;

    switch (opcode) {
    case 0x0: // AND
        result = lhs & op2;
        updateFlagsLogical = true;
        break;
    case 0x1: // EOR
        result = lhs ^ op2;
        updateFlagsLogical = true;
        break;
    case 0x2: // SUB
        result = lhs - op2;
        updateFlagsSub = true;
        break;
    case 0x3: // RSB
        result = op2 - lhs;
        updateFlagsSub = true;
        lhsForFlags = op2;
        rhsForFlags = lhs;
        break;
    case 0x4: // ADD
        result = lhs + op2;
        updateFlagsAdd = true;
        break;
    case 0x5: { // ADC
        const u32 carry = flagC() ? 1U : 0U;
        result = lhs + op2 + carry;
        updateFlagsAdd = true;
        rhsForFlags = op2 + carry;
        break;
    }
    case 0x6: { // SBC
        const u32 borrow = flagC() ? 0U : 1U;
        result = lhs - op2 - borrow;
        updateFlagsSub = true;
        rhsForFlags = op2 + borrow;
        break;
    }
    case 0x7: { // RSC
        const u32 borrow = flagC() ? 0U : 1U;
        result = op2 - lhs - borrow;
        updateFlagsSub = true;
        lhsForFlags = op2;
        rhsForFlags = lhs + borrow;
        break;
    }
    case 0x8: // TST
        result = lhs & op2;
        writeResult = false;
        updateFlagsLogical = true;
        break;
    case 0x9: // TEQ
        result = lhs ^ op2;
        writeResult = false;
        updateFlagsLogical = true;
        break;
    case 0xA: // CMP
        result = lhs - op2;
        writeResult = false;
        updateFlagsSub = true;
        break;
    case 0xB: // CMN
        result = lhs + op2;
        writeResult = false;
        updateFlagsAdd = true;
        break;
    case 0xC: // ORR
        result = lhs | op2;
        updateFlagsLogical = true;
        break;
    case 0xD: // MOV
        result = op2;
        updateFlagsLogical = true;
        break;
    case 0xE: // BIC
        result = lhs & ~op2;
        updateFlagsLogical = true;
        break;
    case 0xF: // MVN
        result = ~op2;
        updateFlagsLogical = true;
        break;
    default:
        return false;
    }

    if (writeResult) {
        regs_[rd] = result;
    }

    const bool restoreFromSpsr = setFlags && writeResult && rd == 15U;
    const bool shouldSetFlags = (setFlags || !writeResult) && !restoreFromSpsr;
    if (shouldSetFlags) {
        if (updateFlagsLogical) {
            updateNz(result);
            setFlag(kCpsrC, shifterCarry);
        } else if (updateFlagsAdd) {
            updateAddFlags(lhsForFlags, rhsForFlags, result);
        } else if (updateFlagsSub) {
            updateSubFlags(lhsForFlags, rhsForFlags, result);
        }
    }

    if (restoreFromSpsr) {
        writeCpsr(readSpsr());
    }
    if (writeResult && rd == 15U) {
        alignPcForCurrentState();
    }

    return true;
}

bool CpuArm7tdmi::executeSingleDataTransfer(u32 instruction) {
    if ((instruction & 0x0C000000U) != 0x04000000U) {
        return false;
    }

    const bool immediateOffsetIsRegister = (instruction & (1U << 25U)) != 0U;

    const bool preIndex = (instruction & (1U << 24U)) != 0U;
    const bool addOffset = (instruction & (1U << 23U)) != 0U;
    const bool byteTransfer = (instruction & (1U << 22U)) != 0U;
    const bool writeBack = (instruction & (1U << 21U)) != 0U;
    const bool load = (instruction & (1U << 20U)) != 0U;

    const u8 rn = static_cast<u8>((instruction >> 16U) & 0x0FU);
    const u8 rd = static_cast<u8>((instruction >> 12U) & 0x0FU);
    u32 offset = instruction & 0x0FFFU;
    if (immediateOffsetIsRegister) {
        const std::size_t rmIndex = static_cast<std::size_t>(instruction & 0x0FU);
        offset = readArmRegister(rmIndex);
        const unsigned shiftType = static_cast<unsigned>((instruction >> 5U) & 0x03U);
        const unsigned shiftImm = static_cast<unsigned>((instruction >> 7U) & 0x1FU);
        if ((instruction & (1U << 4U)) != 0U) {
            // Formato invalido para endereco por registrador (evita cair em data-processing).
            return true;
        }
        switch (shiftType) {
        case 0U: // LSL
            offset = shiftImm == 0U ? offset : (offset << shiftImm);
            break;
        case 1U: // LSR
            if (shiftImm == 0U) {
                offset = 0U;
            } else {
                offset >>= shiftImm;
            }
            break;
        case 2U: // ASR
            if (shiftImm == 0U) {
                offset = (offset & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0U;
            } else {
                offset = arithmeticShiftRight(offset, shiftImm);
            }
            break;
        case 3U: // ROR
            if (shiftImm == 0U) {
                const u32 carryIn = flagC() ? 0x80000000U : 0U;
                offset = carryIn | (offset >> 1U);
            } else {
                offset = rotateRight(offset, shiftImm);
            }
            break;
        default:
            break;
        }
    }

    const u32 base = readArmRegister(rn);
    const u32 effective = addOffset ? base + offset : base - offset;
    const u32 address = preIndex ? effective : base;

    if (load) {
        if (byteTransfer) {
            regs_[rd] = static_cast<u32>(memory_->read8(address));
        } else {
            regs_[rd] = memory_->read32(address);
        }
        if (rd == 15U) {
            alignPcForCurrentState();
        }
    } else {
        const u32 storeValue = readArmRegister(rd, true);
        if (byteTransfer) {
            memory_->write8(address, static_cast<u8>(storeValue & 0xFFU));
        } else {
            memory_->write32(address, storeValue);
        }
    }

    if (!preIndex || writeBack) {
        regs_[rn] = effective;
    }

    return true;
}

bool CpuArm7tdmi::executeHalfwordDataTransfer(u32 instruction) {
    if ((instruction & 0x0E000090U) != 0x00000090U) {
        return false;
    }
    if ((instruction & 0x0F8000F0U) == 0x00800090U) {
        return false; // long multiply
    }
    if ((instruction & 0x0FC000F0U) == 0x00000090U) {
        return false; // mul/mla
    }
    if ((instruction & 0x0FB00FF0U) == 0x01000090U) {
        return false; // swp/swpb
    }

    const bool preIndex = (instruction & (1U << 24U)) != 0U;
    const bool addOffset = (instruction & (1U << 23U)) != 0U;
    const bool immediateOffset = (instruction & (1U << 22U)) != 0U;
    const bool writeBack = (instruction & (1U << 21U)) != 0U;
    const bool load = (instruction & (1U << 20U)) != 0U;
    const u8 rn = static_cast<u8>((instruction >> 16U) & 0x0FU);
    const u8 rd = static_cast<u8>((instruction >> 12U) & 0x0FU);
    const u8 sh = static_cast<u8>((instruction >> 5U) & 0x03U);

    u32 offset = 0;
    if (immediateOffset) {
        const u32 high = (instruction >> 8U) & 0x0FU;
        const u32 low = instruction & 0x0FU;
        offset = (high << 4U) | low;
    } else {
        const u8 rm = static_cast<u8>(instruction & 0x0FU);
        offset = readArmRegister(rm);
    }

    const u32 base = readArmRegister(rn);
    const u32 effective = addOffset ? base + offset : base - offset;
    const u32 address = preIndex ? effective : base;

    if (load) {
        if (sh == 0x01U) { // LDRH
            regs_[rd] = static_cast<u32>(memory_->read16(address));
        } else if (sh == 0x02U) { // LDRSB
            regs_[rd] = static_cast<u32>(static_cast<std::int32_t>(static_cast<std::int8_t>(memory_->read8(address))));
        } else if (sh == 0x03U) { // LDRSH
            if ((address & 1U) != 0U) {
                regs_[rd] = static_cast<u32>(
                    static_cast<std::int32_t>(static_cast<std::int8_t>(memory_->read8(address)))
                );
            } else {
                regs_[rd] = static_cast<u32>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(memory_->read16(address)))
                );
            }
        } else {
            return false;
        }
        if (rd == 15U) {
            alignPcForCurrentState();
        }
    } else {
        if (sh != 0x01U) {
            return false;
        }
        memory_->write16(address, static_cast<u16>(readArmRegister(rd, true) & 0xFFFFU));
    }

    if (!preIndex || writeBack) {
        regs_[rn] = effective;
    }

    return true;
}

bool CpuArm7tdmi::executeBlockDataTransfer(u32 instruction) {
    if ((instruction & 0x0E000000U) != 0x08000000U) {
        return false;
    }

    const bool preIndex = (instruction & (1U << 24U)) != 0U;
    const bool addOffset = (instruction & (1U << 23U)) != 0U;
    const bool psrAndForceUser = (instruction & (1U << 22U)) != 0U;
    const bool writeBack = (instruction & (1U << 21U)) != 0U;
    const bool load = (instruction & (1U << 20U)) != 0U;
    const std::size_t rn = static_cast<std::size_t>((instruction >> 16U) & 0x0FU);
    const u16 regList = static_cast<u16>(instruction & 0xFFFFU);

    int count = 0;
    for (int i = 0; i < 16; ++i) {
        if ((regList & static_cast<u16>(1U << i)) != 0U) {
            ++count;
        }
    }
    if (count == 0) {
        return true;
    }

    const bool restoringFromSpsr = load && psrAndForceUser && (regList & (1U << 15U)) != 0U;
    const bool useUserBank = psrAndForceUser && !restoringFromSpsr;

    const u32 base = readArmRegister(rn);

    // Enderecamento ARM LDM/STM:
    // U=1 -> incrementa; U=0 -> decrementa.
    // P define before/after.
    u32 address = 0;
    if (addOffset) {
        address = preIndex ? (base + 4U) : base; // IB / IA
    } else {
        const u32 blockBytes = static_cast<u32>(count) * 4U;
        address = preIndex ? (base - blockBytes) : (base - blockBytes + 4U); // DB / DA
    }

    for (int r = 0; r < 16; ++r) {
        if ((regList & static_cast<u16>(1U << r)) == 0U) {
            continue;
        }
        const auto regIndex = static_cast<std::size_t>(r);
        if (load) {
            const u32 value = memory_->read32(address);
            if (useUserBank && regIndex != 15U) {
                writeUserBankRegister(regIndex, value);
            } else {
                regs_[regIndex] = value;
            }
        } else {
            const u32 value = (useUserBank && regIndex != 15U)
                ? readUserBankRegister(regIndex)
                : readArmRegister(regIndex, true);
            memory_->write32(address, value);
        }
        address += 4U;
    }

    if (writeBack) {
        const u32 blockBytes = static_cast<u32>(count) * 4U;
        regs_[rn] = addOffset ? (regs_[rn] + blockBytes) : (regs_[rn] - blockBytes);
    }
    if (load && (regList & (1U << 15U)) != 0U) {
        if (restoringFromSpsr) {
            writeCpsr(readSpsr());
        }
        alignPcForCurrentState();
    }
    return true;
}

bool CpuArm7tdmi::executeMultiply(u32 instruction) {
    if ((instruction & 0x0FC000F0U) == 0x00000090U) {
        const bool accumulate = (instruction & (1U << 21U)) != 0U;
        const bool setFlags = (instruction & (1U << 20U)) != 0U;
        const std::size_t rd = static_cast<std::size_t>((instruction >> 16U) & 0x0FU);
        const std::size_t rn = static_cast<std::size_t>((instruction >> 12U) & 0x0FU);
        const std::size_t rs = static_cast<std::size_t>((instruction >> 8U) & 0x0FU);
        const std::size_t rm = static_cast<std::size_t>(instruction & 0x0FU);

        u32 result = regs_[rm] * regs_[rs];
        if (accumulate) {
            result = result + regs_[rn];
        }
        regs_[rd] = result;
        if (setFlags) {
            updateNz(result);
        }
        return true;
    }

    // Long multiply variants: UMULL/UMLAL/SMULL/SMLAL.
    if ((instruction & 0x0F8000F0U) == 0x00800090U) {
        const bool signedMultiply = (instruction & (1U << 22U)) != 0U;
        const bool accumulate = (instruction & (1U << 21U)) != 0U;
        const bool setFlags = (instruction & (1U << 20U)) != 0U;
        const std::size_t rdHi = static_cast<std::size_t>((instruction >> 16U) & 0x0FU);
        const std::size_t rdLo = static_cast<std::size_t>((instruction >> 12U) & 0x0FU);
        const std::size_t rs = static_cast<std::size_t>((instruction >> 8U) & 0x0FU);
        const std::size_t rm = static_cast<std::size_t>(instruction & 0x0FU);

        std::uint64_t result = 0U;
        if (signedMultiply) {
            const auto lhs = static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rm]));
            const auto rhs = static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rs]));
            result = static_cast<std::uint64_t>(lhs * rhs);
        } else {
            result = static_cast<std::uint64_t>(regs_[rm]) * static_cast<std::uint64_t>(regs_[rs]);
        }
        if (accumulate) {
            const std::uint64_t acc = (static_cast<std::uint64_t>(regs_[rdHi]) << 32U)
                | static_cast<std::uint64_t>(regs_[rdLo]);
            result += acc;
        }

        regs_[rdLo] = static_cast<u32>(result & 0xFFFFFFFFULL);
        regs_[rdHi] = static_cast<u32>(result >> 32U);
        if (setFlags) {
            setFlag(kCpsrN, (result & (1ULL << 63U)) != 0U);
            setFlag(kCpsrZ, result == 0U);
        }
        return true;
    }

    return false;
}

bool CpuArm7tdmi::executeSwap(u32 instruction) {
    if ((instruction & 0x0FB00FF0U) != 0x01000090U) {
        return false;
    }

    const bool byteTransfer = (instruction & (1U << 22U)) != 0U;
    const std::size_t rn = static_cast<std::size_t>((instruction >> 16U) & 0x0FU);
    const std::size_t rd = static_cast<std::size_t>((instruction >> 12U) & 0x0FU);
    const std::size_t rm = static_cast<std::size_t>(instruction & 0x0FU);
    const u32 address = readArmRegister(rn);
    const u32 sourceValue = readArmRegister(rm, true);

    if (byteTransfer) {
        const u8 value = memory_->read8(address);
        memory_->write8(address, static_cast<u8>(sourceValue & 0xFFU));
        regs_[rd] = value;
        return true;
    }

    const u32 value = memory_->read32(address);
    memory_->write32(address, sourceValue);
    regs_[rd] = value;
    if (rd == 15U) {
        alignPcForCurrentState();
    }
    return true;
}

bool CpuArm7tdmi::executeBranch(u32 instruction) {
    if ((instruction & 0x0E000000U) != 0x0A000000U) {
        return false;
    }

    const bool link = (instruction & (1U << 24U)) != 0U;
    u32 imm24 = instruction & 0x00FFFFFFU;
    if ((imm24 & 0x00800000U) != 0U) {
        imm24 |= 0xFF000000U;
    }

    const u32 offset = imm24 << 2U;
    const u32 nextPc = regs_[15];
    const u32 branchBase = nextPc + 4U; // PC visto pelo branch = current + 8.
    const u32 target = branchBase + offset;
    if (link) {
        regs_[14] = nextPc;
    }
    regs_[15] = target;
    alignPcForCurrentState();
    return true;
}

bool CpuArm7tdmi::executeBranchExchange(u32 instruction) {
    if ((instruction & 0x0FFFFFF0U) != 0x012FFF10U) {
        return false;
    }

    const u8 rn = static_cast<u8>(instruction & 0x0FU);
    const u32 target = readArmRegister(rn);
    if (tryReturnFromIrqTrampoline(target)) {
        return true;
    }
    setThumbMode((target & 1U) != 0U);
    regs_[15] = target;
    alignPcForCurrentState();
    return true;
}

bool CpuArm7tdmi::executeSoftwareInterrupt(u32 instruction) {
    if ((instruction & 0x0F000000U) != 0x0F000000U) {
        return false;
    }
    handleSwi(instruction & 0x00FFFFFFU);
    return true;
}

bool CpuArm7tdmi::executeThumbSoftwareInterrupt(u16 instruction) {
    if ((instruction & 0xFF00U) != 0xDF00U) {
        return false;
    }
    handleSwi(static_cast<u32>(instruction & 0x00FFU));
    return true;
}

bool CpuArm7tdmi::handlePendingInterrupt() {
    if (memory_ == nullptr) {
        return false;
    }
    if (!memory_->interruptMasterEnabled()) {
        return false;
    }
    if ((cpsr_ & kCpsrI) != 0U) {
        return false;
    }

    const u16 pending = memory_->pendingInterrupts();
    if (pending == 0U) {
        return false;
    }

    const u16 irq = static_cast<u16>(pending & static_cast<u16>(0U - pending));
    (void)irq;

    if (irqContextDepth_ >= irqContextStack_.size()) {
        return false;
    }

    IrqContext context{};
    const u32 oldCpsr = cpsr_;
    for (std::size_t i = 0; i < context.regs.size(); ++i) {
        context.regs[i] = regs_[i];
    }
    context.cpsr = oldCpsr;
    context.resumeAddress = regs_[15] | (thumbMode() ? 1U : 0U);
    context.entrySp = 0U;
    if (!isValidExecuteAddress(context.resumeAddress & ~1U)) {
        return false;
    }
    const u32 handler = memory_->read32(0x03007FFCU);
    const u32 target = handler != 0U ? handler : 0x00000018U;
    if (!isValidExecuteAddress(target & ~1U)) {
        return false;
    }
    irqContextStack_[irqContextDepth_] = context;
    ++irqContextDepth_;

    const u32 irqReturnMarker = kIrqReturnTrampoline | 1U;
    switchMode(kModeIrq);
    writeSpsr(oldCpsr);
    irqContextStack_[irqContextDepth_ - 1U].entrySp = regs_[13];
    // Marca de retorno para handlers THUMB que fazem POP {r0}; BX r0.
    regs_[13] -= 4U;
    memory_->write32(regs_[13], irqReturnMarker);
    regs_[14] = irqReturnMarker;
    // BIOS IRQ ABI: r0 aponta para IO base (0x04000000) quando chama handler.
    regs_[0] = 0x04000000U;
    setFlag(kCpsrI, true);
    setThumbMode((target & 1U) != 0U);
    regs_[15] = target & ~1U;
    alignPcForCurrentState();
    if (logFlags_.irq) {
        static std::uint64_t irqEnterCount = 0;
        if (irqEnterCount < 128U) {
            ++irqEnterCount;
            std::cerr << "[GBA][IRQ] enter pending=0x" << std::hex << pending
                      << " irq=0x" << irq
                      << " handler=0x" << target
                      << " resume=0x" << context.resumeAddress
                      << " depth=" << std::dec << irqContextDepth_ << '\n';
        }
    }
    halted_ = false;
    return true;
}

bool CpuArm7tdmi::tryReturnFromIrqTrampoline(u32 target) {
    if ((target & ~1U) != kIrqReturnTrampoline) {
        return false;
    }
    if (irqContextDepth_ == 0U) {
        return false;
    }
    const IrqContext context = irqContextStack_[irqContextDepth_ - 1U];
    if (context.resumeAddress == 0U) {
        return false;
    }
    --irqContextDepth_;
    writeCpsr(context.cpsr);
    if (context.entrySp != 0U) {
        irqBank_.sp = context.entrySp;
    }
    irqBank_.lr = 0U;
    for (std::size_t i = 0; i < context.regs.size(); ++i) {
        regs_[i] = context.regs[i];
    }
    if (isValidExecuteAddress(context.resumeAddress & ~1U)) {
        regs_[15] = context.resumeAddress;
    } else {
        regs_[15] = ResetPc;
        setThumbMode(false);
    }
    alignPcForCurrentState();
    if (logFlags_.irq) {
        static std::uint64_t irqReturnCount = 0;
        if (irqReturnCount < 128U) {
            ++irqReturnCount;
            std::cerr << "[GBA][IRQ] return resume=0x" << std::hex << regs_[15]
                      << " cpsr=0x" << cpsr_
                      << " depth=" << std::dec << irqContextDepth_ << '\n';
        }
    }
    return true;
}

void CpuArm7tdmi::handleSwi(u32 swiNumber) {
    const u32 id = swiNumber & 0xFFU;
    static std::array<std::uint64_t, 256> swiCounts{};
    ++swiCounts[id];
    const bool logSwi = logFlags_.swi;
    if (logSwi && swiCounts[id] <= 8U) {
        std::cerr << "[GBA][SWI] id=0x" << std::hex << id << " count=" << std::dec << swiCounts[id]
                  << " pc=0x" << std::hex << regs_[15]
                  << " r0=0x" << regs_[0]
                  << " r1=0x" << regs_[1]
                  << " r2=0x" << regs_[2]
                  << " r3=0x" << regs_[3]
                  << std::dec << '\n';
        if (id == 0x00U && memory_ != nullptr) {
            std::cerr << "[GBA][SWI] softreset flag[0x3007FFA]=0x" << std::hex
                      << static_cast<unsigned>(memory_->read8(0x03007FFAU))
                      << std::dec << '\n';
        }
    }

    switch (id) {
    case 0x00: // SoftReset
        {
            const u8 bootFlag = memory_ != nullptr ? memory_->read8(0x03007FFAU) : 0U;
            regs_.fill(0);
            userBank_ = BankedRegisters{};
            irqBank_ = BankedRegisters{};
            svcBank_ = BankedRegisters{};
            abtBank_ = BankedRegisters{};
            undBank_ = BankedRegisters{};
            fiqBank_ = BankedRegisters{};
            sharedR8ToR12_.fill(0);
            fiqR8ToR12_.fill(0);
            userBank_.sp = 0x03007F00U;
            irqBank_.sp = 0x03007FA0U;
            svcBank_.sp = 0x03007FE0U;
            for (auto& context : irqContextStack_) {
                context = IrqContext{};
            }
            irqContextDepth_ = 0;
            writeCpsr(kModeSupervisor | kCpsrI);
            regs_[15] = (bootFlag & 1U) != 0U ? 0x02000000U : ResetPc;
            alignPcForCurrentState();
            return;
        }
    case 0x01: // RegisterRamReset
        if (memory_ != nullptr) {
            const u32 flags = regs_[0] & 0xFFU;
            if ((flags & 0x01U) != 0U) { // EWRAM
                if (!zeroMemoryRange(*memory_, 0x02000000U, Memory::EwramSize)) {
                    for (u32 i = 0; i < Memory::EwramSize; ++i) {
                        memory_->write8(0x02000000U + i, 0U);
                    }
                }
            }
            if ((flags & 0x02U) != 0U) { // IWRAM
                if (!zeroMemoryRange(*memory_, 0x03000000U, 0x7E00U)) {
                    for (u32 i = 0; i < 0x7E00U; ++i) { // BIOS preserva os 0x200 bytes finais.
                        memory_->write8(0x03000000U + i, 0U);
                    }
                }
            }
            if ((flags & 0x04U) != 0U) { // PRAM
                if (!zeroMemoryRange(*memory_, 0x05000000U, Memory::PramSize)) {
                    for (u32 i = 0; i < Memory::PramSize; ++i) {
                        memory_->write8(0x05000000U + i, 0U);
                    }
                }
            }
            if ((flags & 0x08U) != 0U) { // VRAM
                if (!zeroMemoryRange(*memory_, 0x06000000U, Memory::VramSize)) {
                    for (u32 i = 0; i < Memory::VramSize; ++i) {
                        memory_->write8(0x06000000U + i, 0U);
                    }
                }
            }
            if ((flags & 0x10U) != 0U) { // OAM
                if (!zeroMemoryRange(*memory_, 0x07000000U, Memory::OamSize)) {
                    for (u32 i = 0; i < Memory::OamSize; ++i) {
                        memory_->write8(0x07000000U + i, 0U);
                    }
                }
            }
        }
        return;
    case 0x02: // Halt
        waitingForInterrupt_ = false;
        waitingInterruptMask_ = 0U;
        halted_ = true;
        return;
    case 0x03: // Stop
        waitingForInterrupt_ = false;
        waitingInterruptMask_ = 0U;
        halted_ = true;
        return;
    case 0x04: // IntrWait
    case 0x05: { // VBlankIntrWait
        if (memory_ == nullptr) {
            halted_ = true;
            waitingForInterrupt_ = true;
            waitingInterruptMask_ = 0x0001U;
            return;
        }
        const u16 requestedMask = id == 0x05U
            ? 0x0001U
            : static_cast<u16>(regs_[1] & 0x3FFFU);
        const u16 mask = requestedMask == 0U ? 0x0001U : requestedMask;
        if (id == 0x05U || (regs_[0] & 1U) != 0U) {
            memory_->clearInterrupt(mask);
        }
        const u16 matched = static_cast<u16>(memory_->interruptFlagsRaw() & mask);
        if (matched != 0U) {
            memory_->clearInterrupt(matched);
            regs_[0] = 0U;
            waitingForInterrupt_ = false;
            waitingInterruptMask_ = 0U;
            halted_ = false;
            return;
        }
        waitingForInterrupt_ = true;
        waitingInterruptMask_ = mask;
        regs_[0] = 0;
        halted_ = true;
        return;
    }
    case 0x06: { // Div
        const std::int32_t numerator = static_cast<std::int32_t>(regs_[0]);
        const std::int32_t denominator = static_cast<std::int32_t>(regs_[1]);
        if (denominator == 0) {
            regs_[0] = numerator >= 0 ? 0x7FFFFFFFU : 0x80000000U;
            regs_[1] = static_cast<u32>(numerator);
            regs_[3] = 0x7FFFFFFFU;
            return;
        }
        const std::int32_t quotient = numerator / denominator;
        const std::int32_t remainder = numerator % denominator;
        regs_[0] = static_cast<u32>(quotient);
        regs_[1] = static_cast<u32>(remainder);
        regs_[3] = static_cast<u32>(quotient < 0 ? -quotient : quotient);
        return;
    }
    case 0x07: { // DivArm (r1/r0)
        const std::int32_t numerator = static_cast<std::int32_t>(regs_[1]);
        const std::int32_t denominator = static_cast<std::int32_t>(regs_[0]);
        if (denominator == 0) {
            regs_[0] = numerator >= 0 ? 0x7FFFFFFFU : 0x80000000U;
            regs_[1] = static_cast<u32>(numerator);
            regs_[3] = 0x7FFFFFFFU;
            return;
        }
        const std::int32_t quotient = numerator / denominator;
        const std::int32_t remainder = numerator % denominator;
        regs_[0] = static_cast<u32>(quotient);
        regs_[1] = static_cast<u32>(remainder);
        regs_[3] = static_cast<u32>(quotient < 0 ? -quotient : quotient);
        return;
    }
    case 0x08: { // Sqrt
        const double value = static_cast<double>(regs_[0]);
        const u32 result = value <= 0.0 ? 0U : static_cast<u32>(std::floor(std::sqrt(value)));
        regs_[0] = result;
        return;
    }
    case 0x09: { // ArcTan
        // Entrada tipica: tan(angle) em formato fixo.
        const std::int32_t tanValue = static_cast<std::int32_t>(regs_[0]);
        const double t = static_cast<double>(tanValue) / 16384.0;
        const double angle = std::atan(t); // [-pi/2, +pi/2]
        const std::int32_t gbaAngle = static_cast<std::int32_t>(
            std::llround((angle * 65536.0) / kTwoPi)
        );
        regs_[0] = static_cast<u32>(static_cast<u16>(gbaAngle & 0xFFFF));
        return;
    }
    case 0x0A: { // ArcTan2
        const std::int32_t x = static_cast<std::int32_t>(regs_[0]);
        const std::int32_t y = static_cast<std::int32_t>(regs_[1]);
        double angle = std::atan2(static_cast<double>(y), static_cast<double>(x)); // [-pi, +pi]
        if (angle < 0.0) {
            angle += kTwoPi;
        }
        const std::uint32_t gbaAngle = static_cast<std::uint32_t>(
            std::llround((angle * 65536.0) / kTwoPi)
        ) & 0xFFFFU;
        regs_[0] = gbaAngle;
        return;
    }
    case 0x0B: { // CpuSet
        const u32 ctrl = regs_[2];
        const bool fill = (ctrl & (1U << 24U)) != 0U;
        const bool transfer32 = (ctrl & (1U << 26U)) != 0U;
        u32 src = regs_[0];
        u32 dst = regs_[1];
        if (transfer32) {
            src &= ~3U;
            dst &= ~3U;
        } else {
            src &= ~1U;
            dst &= ~1U;
        }
        const u32 count = ctrl & 0x001FFFFFU;
        if (count == 0U) {
            return;
        }
        if (transfer32) {
            const u32 fillValue = memory_->read32(src);
            if ((fill && fillWords(*memory_, dst, fillValue, count))
                || (!fill && copyWords(*memory_, dst, src, count))) {
                return;
            }
            for (u32 i = 0; i < count; ++i) {
                const u32 value = fill ? fillValue : memory_->read32(src);
                memory_->write32(dst, value);
                dst += 4U;
                if (!fill) {
                    src += 4U;
                }
            }
        } else {
            const u16 fillValue = memory_->read16(src);
            if ((fill && fillHalfwords(*memory_, dst, fillValue, count))
                || (!fill && copyHalfwords(*memory_, dst, src, count))) {
                return;
            }
            for (u32 i = 0; i < count; ++i) {
                const u16 value = fill ? fillValue : memory_->read16(src);
                memory_->write16(dst, value);
                dst += 2U;
                if (!fill) {
                    src += 2U;
                }
            }
        }
        return;
    }
    case 0x0C: { // CpuFastSet
        u32 src = regs_[0] & ~3U;
        u32 dst = regs_[1] & ~3U;
        const u32 ctrl = regs_[2];
        const bool fill = (ctrl & (1U << 24U)) != 0U;
        // GBA BIOS: bits 0..20 representam quantidade em words (32-bit).
        // CpuFastSet processa em blocos de 8 words (32 bytes), entao os
        // 3 bits baixos sao ignorados.
        const u32 requestedWords = ctrl & 0x001FFFFFU;
        const u32 wordCount = requestedWords & ~0x7U;
        if (wordCount == 0U) {
            return;
        }
        const u32 fillValue = memory_->read32(src);
        if ((fill && fillWords(*memory_, dst, fillValue, wordCount))
            || (!fill && copyWords(*memory_, dst, src, wordCount))) {
            return;
        }
        for (u32 i = 0; i < wordCount; ++i) {
            const u32 value = fill ? fillValue : memory_->read32(src);
            memory_->write32(dst, value);
            dst += 4U;
            if (!fill) {
                src += 4U;
            }
        }
        return;
    }
    case 0x0D: // GetBiosChecksum
        regs_[0] = 0xBAAE187FU;
        return;
    case 0x0E: { // BgAffineSet
        u32 src = regs_[0];
        u32 dst = regs_[1];
        const u32 count = regs_[2];
        for (u32 i = 0; i < count; ++i) {
            const std::int32_t texX = static_cast<std::int32_t>(memory_->read32(src + 0U));
            const std::int32_t texY = static_cast<std::int32_t>(memory_->read32(src + 4U));
            const std::int32_t screenX = static_cast<std::int16_t>(memory_->read16(src + 8U));
            const std::int32_t screenY = static_cast<std::int16_t>(memory_->read16(src + 10U));
            const std::int32_t scaleX = static_cast<std::int16_t>(memory_->read16(src + 12U));
            const std::int32_t scaleY = static_cast<std::int16_t>(memory_->read16(src + 14U));
            const u16 angle = memory_->read16(src + 16U);
            const std::int32_t sine = affineSin256(angle);
            const std::int32_t cosine = affineCos256(angle);

            const std::int32_t pa = roundedShift8(static_cast<std::int64_t>(cosine) * static_cast<std::int64_t>(scaleX));
            const std::int32_t pb = roundedShift8(-static_cast<std::int64_t>(sine) * static_cast<std::int64_t>(scaleX));
            const std::int32_t pc = roundedShift8(static_cast<std::int64_t>(sine) * static_cast<std::int64_t>(scaleY));
            const std::int32_t pd = roundedShift8(static_cast<std::int64_t>(cosine) * static_cast<std::int64_t>(scaleY));

            const std::int64_t dx = static_cast<std::int64_t>(texX)
                - static_cast<std::int64_t>(pa) * static_cast<std::int64_t>(screenX)
                - static_cast<std::int64_t>(pb) * static_cast<std::int64_t>(screenY);
            const std::int64_t dy = static_cast<std::int64_t>(texY)
                - static_cast<std::int64_t>(pc) * static_cast<std::int64_t>(screenX)
                - static_cast<std::int64_t>(pd) * static_cast<std::int64_t>(screenY);

            memory_->write16(dst + 0U, static_cast<u16>(clampToType<std::int16_t>(pa)));
            memory_->write16(dst + 2U, static_cast<u16>(clampToType<std::int16_t>(pb)));
            memory_->write16(dst + 4U, static_cast<u16>(clampToType<std::int16_t>(pc)));
            memory_->write16(dst + 6U, static_cast<u16>(clampToType<std::int16_t>(pd)));
            memory_->write32(dst + 8U, static_cast<u32>(clampToType<std::int32_t>(dx)));
            memory_->write32(dst + 12U, static_cast<u32>(clampToType<std::int32_t>(dy)));

            src += 20U;
            dst += 16U;
        }
        return;
    }
    case 0x0F: { // ObjAffineSet
        u32 src = regs_[0];
        u32 dst = regs_[1];
        const u32 count = regs_[2];
        const u32 offset = regs_[3] & 0xFFFFU;
        if (offset == 0U) {
            return;
        }

        for (u32 i = 0; i < count; ++i) {
            const std::int32_t scaleX = static_cast<std::int16_t>(memory_->read16(src + 0U));
            const std::int32_t scaleY = static_cast<std::int16_t>(memory_->read16(src + 2U));
            const u16 angle = memory_->read16(src + 4U);
            const std::int32_t sine = affineSin256(angle);
            const std::int32_t cosine = affineCos256(angle);

            const std::int32_t pa = roundedShift8(static_cast<std::int64_t>(cosine) * static_cast<std::int64_t>(scaleX));
            const std::int32_t pb = roundedShift8(-static_cast<std::int64_t>(sine) * static_cast<std::int64_t>(scaleX));
            const std::int32_t pc = roundedShift8(static_cast<std::int64_t>(sine) * static_cast<std::int64_t>(scaleY));
            const std::int32_t pd = roundedShift8(static_cast<std::int64_t>(cosine) * static_cast<std::int64_t>(scaleY));

            memory_->write16(dst + offset * 0U, static_cast<u16>(clampToType<std::int16_t>(pa)));
            memory_->write16(dst + offset * 1U, static_cast<u16>(clampToType<std::int16_t>(pb)));
            memory_->write16(dst + offset * 2U, static_cast<u16>(clampToType<std::int16_t>(pc)));
            memory_->write16(dst + offset * 3U, static_cast<u16>(clampToType<std::int16_t>(pd)));

            src += 8U;
            dst += offset * 4U;
        }
        return;
    }
    case 0x11: // LZ77UnCompWram
        runLz77UnComp(*memory_, regs_[0], regs_[1], false);
        return;
    case 0x12: // LZ77UnCompVram
        runLz77UnComp(*memory_, regs_[0], regs_[1], true);
        return;
    case 0x14: // RLUnCompWram
        runRlUnComp(*memory_, regs_[0], regs_[1], false);
        return;
    case 0x15: // RLUnCompVram
        runRlUnComp(*memory_, regs_[0], regs_[1], true);
        return;
    default:
        return;
    }
}

void CpuArm7tdmi::updateNz(u32 result) {
    setFlag(kCpsrN, (result & 0x80000000U) != 0U);
    setFlag(kCpsrZ, result == 0U);
}

void CpuArm7tdmi::updateAddFlags(u32 lhs, u32 rhs, u32 result) {
    updateNz(result);

    const std::uint64_t sum = static_cast<std::uint64_t>(lhs) + static_cast<std::uint64_t>(rhs);
    setFlag(kCpsrC, (sum >> 32U) != 0U);

    const bool overflow = ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80000000U) != 0U;
    setFlag(kCpsrV, overflow);
}

void CpuArm7tdmi::updateSubFlags(u32 lhs, u32 rhs, u32 result) {
    updateNz(result);

    setFlag(kCpsrC, lhs >= rhs);
    const bool overflow = (((lhs ^ rhs) & (lhs ^ result)) & 0x80000000U) != 0U;
    setFlag(kCpsrV, overflow);
}

void CpuArm7tdmi::setFlag(u32 mask, bool enabled) {
    if (enabled) {
        cpsr_ |= mask;
    } else {
        cpsr_ &= ~mask;
    }
}

} // namespace gb::gba
