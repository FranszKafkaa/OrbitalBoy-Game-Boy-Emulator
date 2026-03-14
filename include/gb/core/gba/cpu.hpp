#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "gb/core/gba/memory.hpp"
#include "gb/core/types.hpp"

namespace gb::gba {

class CpuArm7tdmi {
public:
    static constexpr u32 ResetPc = 0x08000000U;

    void connectMemory(Memory* memory);
    void reset();

    [[nodiscard]] int step();

    [[nodiscard]] u32 reg(int index) const;
    void setReg(int index, u32 value);

    [[nodiscard]] u32 pc() const;
    void setPc(u32 value);

    [[nodiscard]] u32 cpsr() const;

    [[nodiscard]] bool flagN() const;
    [[nodiscard]] bool flagZ() const;
    [[nodiscard]] bool flagC() const;
    [[nodiscard]] bool flagV() const;
    [[nodiscard]] bool thumbMode() const;

    void setThumbMode(bool enabled);

    [[nodiscard]] std::uint64_t executedInstructions() const;

private:
    struct BankedRegisters {
        u32 sp = 0;
        u32 lr = 0;
        u32 spsr = 0;
    };
    struct IrqContext {
        std::array<u32, 15> regs{};
        u32 cpsr = 0;
        u32 resumeAddress = 0;
        u32 entrySp = 0;
    };
    struct LogFlags {
        bool badPc = false;
        bool biosExec = false;
        bool unknown = false;
        bool armWindow = false;
        bool stateSwitch = false;
        bool bl = false;
        bool swi = false;
        bool irq = false;
    };

    [[nodiscard]] static u32 rotateRight(u32 value, unsigned amount);
    [[nodiscard]] static u32 arithmeticShiftRight(u32 value, unsigned amount);
    [[nodiscard]] static bool modeHasSpsr(u32 mode);
    [[nodiscard]] static bool modeUsesUserBank(u32 mode);
    [[nodiscard]] u32 readArmRegister(std::size_t index, bool forStore = false) const;
    [[nodiscard]] u32 readUserBankRegister(std::size_t index) const;
    void writeUserBankRegister(std::size_t index, u32 value);

    [[nodiscard]] bool conditionPassed(u8 cond) const;
    [[nodiscard]] u32 readOperand2(u32 instruction, bool& shifterCarry);
    [[nodiscard]] bool executeThumbInstruction(u16 instruction, u32 currentPc);

    [[nodiscard]] bool executePsrTransfer(u32 instruction);
    [[nodiscard]] bool executeDataProcessing(u32 instruction);
    [[nodiscard]] bool executeSingleDataTransfer(u32 instruction);
    [[nodiscard]] bool executeHalfwordDataTransfer(u32 instruction);
    [[nodiscard]] bool executeBlockDataTransfer(u32 instruction);
    [[nodiscard]] bool executeMultiply(u32 instruction);
    [[nodiscard]] bool executeSwap(u32 instruction);
    [[nodiscard]] bool executeBranch(u32 instruction);
    [[nodiscard]] bool executeBranchExchange(u32 instruction);
    [[nodiscard]] bool executeSoftwareInterrupt(u32 instruction);
    [[nodiscard]] bool executeThumbSoftwareInterrupt(u16 instruction);
    [[nodiscard]] bool handlePendingInterrupt();
    [[nodiscard]] bool tryReturnFromIrqTrampoline(u32 target);

    void handleSwi(u32 swiNumber);
    void alignPcForCurrentState();
    void enterException(u32 newMode, u32 vectorAddress, u32 lrValue, bool maskIrq, bool maskFiq);
    void enterUndefinedException(u32 lrValue);
    void enterPrefetchAbortException(u32 faultAddress);
    void writeCpsr(u32 value);
    [[nodiscard]] u32 readSpsr() const;
    void writeSpsr(u32 value);
    void switchMode(u32 newMode);
    void saveModeBank(u32 mode);
    void loadModeBank(u32 mode);

    void updateNz(u32 result);
    void updateAddFlags(u32 lhs, u32 rhs, u32 result);
    void updateSubFlags(u32 lhs, u32 rhs, u32 result);
    void setFlag(u32 mask, bool enabled);
    void refreshLogFlags();

    Memory* memory_ = nullptr;
    std::array<u32, 16> regs_{};
    u32 cpsr_ = 0;
    BankedRegisters userBank_{};
    BankedRegisters irqBank_{};
    BankedRegisters svcBank_{};
    BankedRegisters abtBank_{};
    BankedRegisters undBank_{};
    BankedRegisters fiqBank_{};
    std::array<u32, 5> sharedR8ToR12_{};
    std::array<u32, 5> fiqR8ToR12_{};
    std::array<IrqContext, 8> irqContextStack_{};
    std::size_t irqContextDepth_ = 0;
    std::uint64_t executedInstructions_ = 0;
    bool halted_ = false;
    bool waitingForInterrupt_ = false;
    u16 waitingInterruptMask_ = 0;
    u32 lastExecutablePc_ = ResetPc;
    bool thumbBlPrefixPending_ = false;
    u32 thumbBlPrefixValue_ = 0;
    LogFlags logFlags_{};
};

} // namespace gb::gba
