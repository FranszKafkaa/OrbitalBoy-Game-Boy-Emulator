#pragma once

#include "gb/core/bus.hpp"
#include "gb/core/types.hpp"

namespace gb {

struct Registers {
    u8 a = 0x01;
    u8 f = 0xB0;
    u8 b = 0x00;
    u8 c = 0x13;
    u8 d = 0x00;
    u8 e = 0xD8;
    u8 h = 0x01;
    u8 l = 0x4D;
    u16 sp = 0xFFFE;
    u16 pc = 0x0100;

    [[nodiscard]] u16 af() const;
    [[nodiscard]] u16 bc() const;
    [[nodiscard]] u16 de() const;
    [[nodiscard]] u16 hl() const;

    void setAf(u16 value);
    void setBc(u16 value);
    void setDe(u16 value);
    void setHl(u16 value);
};

class CPU {
public:
    struct State {
        Registers regs{};
        bool ime = false;
        bool enableImeNext = false;
        bool halted = false;
        u16 lastPc = 0x0100;
        u8 lastOpcode = 0x00;
    };

    explicit CPU(Bus& bus);

    u32 step();

    void requestInterrupts();

    [[nodiscard]] const Registers& regs() const;
    [[nodiscard]] bool isHalted() const;
    [[nodiscard]] u16 lastExecutedPc() const;
    [[nodiscard]] u8 lastExecutedOpcode() const;
    [[nodiscard]] State state() const;
    void loadState(const State& state);
    void setHardwareMode(bool cgbMode);

private:
    enum Flag : u8 {
        Z = 0x80,
        N = 0x40,
        H = 0x20,
        C = 0x10,
    };

    u8 fetch8();
    u16 fetch16();

    void push16(u16 value);
    u16 pop16();

    u8 readReg8(int index);
    void writeReg8(int index, u8 value);
    u32 executeCb(u8 opcode);

    void aluAdd(u8 value, bool carry);
    void aluSub(u8 value, bool carry);
    void aluAnd(u8 value);
    void aluOr(u8 value);
    void aluXor(u8 value);
    void aluCp(u8 value);
    void aluAddHl(u16 value);
    void aluAddSpSigned(i8 value, bool storeInHl);
    void aluDaa();

    void setFlag(Flag flag, bool state);
    [[nodiscard]] bool getFlag(Flag flag) const;

    bool serviceInterrupts();

    Bus& bus_;
    Registers r_{};

    bool ime_ = false;
    bool enableImeNext_ = false;
    bool halted_ = false;
    bool haltBug_ = false;
    u16 lastPc_ = 0x0100;
    u8 lastOpcode_ = 0x00;
};

} // namespace gb
