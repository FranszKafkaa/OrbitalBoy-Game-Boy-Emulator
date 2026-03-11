#include "gb/core/cpu.hpp"

#include <array>

namespace gb
{

    u16 Registers::af() const { return static_cast<u16>((a << 8) | f); }
    u16 Registers::bc() const { return static_cast<u16>((b << 8) | c); }
    u16 Registers::de() const { return static_cast<u16>((d << 8) | e); }
    u16 Registers::hl() const { return static_cast<u16>((h << 8) | l); }

    void Registers::setAf(u16 value)
    {
        a = static_cast<u8>(value >> 8);
        f = static_cast<u8>(value & 0xF0);
    }
    void Registers::setBc(u16 value)
    {
        b = static_cast<u8>(value >> 8);
        c = static_cast<u8>(value & 0xFF);
    }
    void Registers::setDe(u16 value)
    {
        d = static_cast<u8>(value >> 8);
        e = static_cast<u8>(value & 0xFF);
    }
    void Registers::setHl(u16 value)
    {
        h = static_cast<u8>(value >> 8);
        l = static_cast<u8>(value & 0xFF);
    }

    CPU::CPU(Bus &bus)
        : bus_(bus) {}

    u32 CPU::step()
    {
        if (enableImeNext_)
        {
            ime_ = true;
            enableImeNext_ = false;
        }

        if (serviceInterrupts())
        {
            return 20;
        }

        if (halted_)
        {
            return 4;
        }

        const auto inc8 = [this](u8 &reg)
        {
            const u8 old = reg;
            reg = static_cast<u8>(reg + 1);
            setFlag(Z, reg == 0);
            setFlag(N, false);
            setFlag(H, ((old & 0x0F) + 1) > 0x0F);
        };

        const auto dec8 = [this](u8 &reg)
        {
            const u8 old = reg;
            reg = static_cast<u8>(reg - 1);
            setFlag(Z, reg == 0);
            setFlag(N, true);
            setFlag(H, (old & 0x0F) == 0);
        };

        const auto cond = [this](int idx) -> bool
        {
            switch (idx)
            {
            case 0:
                return !getFlag(Z);
            case 1:
                return getFlag(Z);
            case 2:
                return !getFlag(C);
            case 3:
                return getFlag(C);
            default:
                return false;
            }
        };

        lastPc_ = r_.pc;
        const u8 opcode = fetch8();
        lastOpcode_ = opcode;
        ++totalInstructions_;
        ++opcodeHistogram_[opcode];

        if (opcode >= 0x40 && opcode <= 0x7F)
        {
            if (opcode == 0x76)
            {
                if (!ime_)
                {
                    const u8 pending = static_cast<u8>(bus_.interruptEnable() & bus_.interruptFlags() & 0x1F);
                    if (pending != 0)
                    {
                        haltBug_ = true;
                        return 4;
                    }
                }
                halted_ = true;
                return 4;
            }
            const int dst = (opcode >> 3) & 0x07;
            const int src = opcode & 0x07;
            const u8 value = readReg8(src);
            writeReg8(dst, value);
            return (src == 6 || dst == 6) ? 8 : 4;
        }

        switch (opcode)
        {
        case 0x00:
            return 4;
        case 0x01:
            r_.setBc(fetch16());
            return 12;
        case 0x06:
            r_.b = fetch8();
            return 8;
        case 0x08:
        {
            const u16 addr = fetch16();
            bus_.write(addr, static_cast<u8>(r_.sp & 0xFF));
            bus_.write(static_cast<u16>(addr + 1), static_cast<u8>(r_.sp >> 8));
            return 20;
        }
        case 0x0E:
            r_.c = fetch8();
            return 8;
        case 0x11:
            r_.setDe(fetch16());
            return 12;
        case 0x16:
            r_.d = fetch8();
            return 8;
        case 0x1E:
            r_.e = fetch8();
            return 8;
        case 0x21:
            r_.setHl(fetch16());
            return 12;
        case 0x26:
            r_.h = fetch8();
            return 8;
        case 0x2E:
            r_.l = fetch8();
            return 8;
        case 0x31:
            r_.sp = fetch16();
            return 12;
        case 0x36:
            bus_.write(r_.hl(), fetch8());
            return 12;
        case 0x3E:
            r_.a = fetch8();
            return 8;

        case 0x02:
            bus_.write(r_.bc(), r_.a);
            return 8;
        case 0x0A:
            r_.a = bus_.read(r_.bc());
            return 8;
        case 0x12:
            bus_.write(r_.de(), r_.a);
            return 8;
        case 0x1A:
            r_.a = bus_.read(r_.de());
            return 8;
        case 0x22:
            bus_.write(r_.hl(), r_.a);
            r_.setHl(static_cast<u16>(r_.hl() + 1));
            return 8;
        case 0x2A:
            r_.a = bus_.read(r_.hl());
            r_.setHl(static_cast<u16>(r_.hl() + 1));
            return 8;
        case 0x32:
            bus_.write(r_.hl(), r_.a);
            r_.setHl(static_cast<u16>(r_.hl() - 1));
            return 8;
        case 0x3A:
            r_.a = bus_.read(r_.hl());
            r_.setHl(static_cast<u16>(r_.hl() - 1));
            return 8;

        case 0x03:
            r_.setBc(static_cast<u16>(r_.bc() + 1));
            return 8;
        case 0x13:
            r_.setDe(static_cast<u16>(r_.de() + 1));
            return 8;
        case 0x23:
            r_.setHl(static_cast<u16>(r_.hl() + 1));
            return 8;
        case 0x33:
            ++r_.sp;
            return 8;
        case 0x0B:
            r_.setBc(static_cast<u16>(r_.bc() - 1));
            return 8;
        case 0x1B:
            r_.setDe(static_cast<u16>(r_.de() - 1));
            return 8;
        case 0x2B:
            r_.setHl(static_cast<u16>(r_.hl() - 1));
            return 8;
        case 0x3B:
            --r_.sp;
            return 8;

        case 0x04:
            inc8(r_.b);
            return 4;
        case 0x0C:
            inc8(r_.c);
            return 4;
        case 0x14:
            inc8(r_.d);
            return 4;
        case 0x1C:
            inc8(r_.e);
            return 4;
        case 0x24:
            inc8(r_.h);
            return 4;
        case 0x2C:
            inc8(r_.l);
            return 4;
        case 0x34:
        {
            u8 v = bus_.read(r_.hl());
            const u8 old = v;
            v = static_cast<u8>(v + 1);
            bus_.write(r_.hl(), v);
            setFlag(Z, v == 0);
            setFlag(N, false);
            setFlag(H, ((old & 0x0F) + 1) > 0x0F);
            return 12;
        }
        case 0x3C:
            inc8(r_.a);
            return 4;

        case 0x05:
            dec8(r_.b);
            return 4;
        case 0x0D:
            dec8(r_.c);
            return 4;
        case 0x15:
            dec8(r_.d);
            return 4;
        case 0x1D:
            dec8(r_.e);
            return 4;
        case 0x25:
            dec8(r_.h);
            return 4;
        case 0x2D:
            dec8(r_.l);
            return 4;
        case 0x35:
        {
            u8 v = bus_.read(r_.hl());
            const u8 old = v;
            v = static_cast<u8>(v - 1);
            bus_.write(r_.hl(), v);
            setFlag(Z, v == 0);
            setFlag(N, true);
            setFlag(H, (old & 0x0F) == 0);
            return 12;
        }
        case 0x3D:
            dec8(r_.a);
            return 4;

        case 0x07:
        {
            const u8 carry = static_cast<u8>((r_.a & 0x80) >> 7);
            r_.a = static_cast<u8>((r_.a << 1) | carry);
            setFlag(Z, false);
            setFlag(N, false);
            setFlag(H, false);
            setFlag(C, carry != 0);
            return 4;
        }
        case 0x0F:
        {
            const u8 carry = static_cast<u8>(r_.a & 0x01);
            r_.a = static_cast<u8>((r_.a >> 1) | (carry << 7));
            setFlag(Z, false);
            setFlag(N, false);
            setFlag(H, false);
            setFlag(C, carry != 0);
            return 4;
        }
        case 0x17:
        {
            const u8 carryIn = getFlag(C) ? 1 : 0;
            const u8 carryOut = static_cast<u8>((r_.a & 0x80) >> 7);
            r_.a = static_cast<u8>((r_.a << 1) | carryIn);
            setFlag(Z, false);
            setFlag(N, false);
            setFlag(H, false);
            setFlag(C, carryOut != 0);
            return 4;
        }
        case 0x1F:
        {
            const u8 carryIn = getFlag(C) ? 1 : 0;
            const u8 carryOut = static_cast<u8>(r_.a & 0x01);
            r_.a = static_cast<u8>((r_.a >> 1) | (carryIn << 7));
            setFlag(Z, false);
            setFlag(N, false);
            setFlag(H, false);
            setFlag(C, carryOut != 0);
            return 4;
        }

        case 0x09:
            aluAddHl(r_.bc());
            return 8;
        case 0x19:
            aluAddHl(r_.de());
            return 8;
        case 0x29:
            aluAddHl(r_.hl());
            return 8;
        case 0x39:
            aluAddHl(r_.sp);
            return 8;

        case 0x10:
            (void)fetch8();
            if (bus_.trySpeedSwitch())
            {
                return 4;
            }
            halted_ = true;
            return 4;
        case 0x18:
        {
            const i8 off = static_cast<i8>(fetch8());
            r_.pc = static_cast<u16>(r_.pc + off);
            return 12;
        }
        case 0x20:
        case 0x28:
        case 0x30:
        case 0x38:
        {
            const i8 off = static_cast<i8>(fetch8());
            const int condIdx = (opcode >> 3) & 0x03;
            if (cond(condIdx))
            {
                r_.pc = static_cast<u16>(r_.pc + off);
                return 12;
            }
            return 8;
        }

        case 0x27:
            aluDaa();
            return 4;
        case 0x2F:
            r_.a = static_cast<u8>(~r_.a);
            setFlag(N, true);
            setFlag(H, true);
            return 4;
        case 0x37:
            setFlag(N, false);
            setFlag(H, false);
            setFlag(C, true);
            return 4;
        case 0x3F:
            setFlag(N, false);
            setFlag(H, false);
            setFlag(C, !getFlag(C));
            return 4;

        case 0x80:
            aluAdd(r_.b, false);
            return 4;
        case 0x81:
            aluAdd(r_.c, false);
            return 4;
        case 0x82:
            aluAdd(r_.d, false);
            return 4;
        case 0x83:
            aluAdd(r_.e, false);
            return 4;
        case 0x84:
            aluAdd(r_.h, false);
            return 4;
        case 0x85:
            aluAdd(r_.l, false);
            return 4;
        case 0x86:
            aluAdd(bus_.read(r_.hl()), false);
            return 8;
        case 0x87:
            aluAdd(r_.a, false);
            return 4;
        case 0x88:
            aluAdd(r_.b, true);
            return 4;
        case 0x89:
            aluAdd(r_.c, true);
            return 4;
        case 0x8A:
            aluAdd(r_.d, true);
            return 4;
        case 0x8B:
            aluAdd(r_.e, true);
            return 4;
        case 0x8C:
            aluAdd(r_.h, true);
            return 4;
        case 0x8D:
            aluAdd(r_.l, true);
            return 4;
        case 0x8E:
            aluAdd(bus_.read(r_.hl()), true);
            return 8;
        case 0x8F:
            aluAdd(r_.a, true);
            return 4;

        case 0x90:
            aluSub(r_.b, false);
            return 4;
        case 0x91:
            aluSub(r_.c, false);
            return 4;
        case 0x92:
            aluSub(r_.d, false);
            return 4;
        case 0x93:
            aluSub(r_.e, false);
            return 4;
        case 0x94:
            aluSub(r_.h, false);
            return 4;
        case 0x95:
            aluSub(r_.l, false);
            return 4;
        case 0x96:
            aluSub(bus_.read(r_.hl()), false);
            return 8;
        case 0x97:
            aluSub(r_.a, false);
            return 4;
        case 0x98:
            aluSub(r_.b, true);
            return 4;
        case 0x99:
            aluSub(r_.c, true);
            return 4;
        case 0x9A:
            aluSub(r_.d, true);
            return 4;
        case 0x9B:
            aluSub(r_.e, true);
            return 4;
        case 0x9C:
            aluSub(r_.h, true);
            return 4;
        case 0x9D:
            aluSub(r_.l, true);
            return 4;
        case 0x9E:
            aluSub(bus_.read(r_.hl()), true);
            return 8;
        case 0x9F:
            aluSub(r_.a, true);
            return 4;

        case 0xA0:
            aluAnd(r_.b);
            return 4;
        case 0xA1:
            aluAnd(r_.c);
            return 4;
        case 0xA2:
            aluAnd(r_.d);
            return 4;
        case 0xA3:
            aluAnd(r_.e);
            return 4;
        case 0xA4:
            aluAnd(r_.h);
            return 4;
        case 0xA5:
            aluAnd(r_.l);
            return 4;
        case 0xA6:
            aluAnd(bus_.read(r_.hl()));
            return 8;
        case 0xA7:
            aluAnd(r_.a);
            return 4;

        case 0xA8:
            aluXor(r_.b);
            return 4;
        case 0xA9:
            aluXor(r_.c);
            return 4;
        case 0xAA:
            aluXor(r_.d);
            return 4;
        case 0xAB:
            aluXor(r_.e);
            return 4;
        case 0xAC:
            aluXor(r_.h);
            return 4;
        case 0xAD:
            aluXor(r_.l);
            return 4;
        case 0xAE:
            aluXor(bus_.read(r_.hl()));
            return 8;
        case 0xAF:
            aluXor(r_.a);
            return 4;

        case 0xB0:
            aluOr(r_.b);
            return 4;
        case 0xB1:
            aluOr(r_.c);
            return 4;
        case 0xB2:
            aluOr(r_.d);
            return 4;
        case 0xB3:
            aluOr(r_.e);
            return 4;
        case 0xB4:
            aluOr(r_.h);
            return 4;
        case 0xB5:
            aluOr(r_.l);
            return 4;
        case 0xB6:
            aluOr(bus_.read(r_.hl()));
            return 8;
        case 0xB7:
            aluOr(r_.a);
            return 4;

        case 0xB8:
            aluCp(r_.b);
            return 4;
        case 0xB9:
            aluCp(r_.c);
            return 4;
        case 0xBA:
            aluCp(r_.d);
            return 4;
        case 0xBB:
            aluCp(r_.e);
            return 4;
        case 0xBC:
            aluCp(r_.h);
            return 4;
        case 0xBD:
            aluCp(r_.l);
            return 4;
        case 0xBE:
            aluCp(bus_.read(r_.hl()));
            return 8;
        case 0xBF:
            aluCp(r_.a);
            return 4;

        case 0xC0:
        case 0xC8:
        case 0xD0:
        case 0xD8:
        {
            const int condIdx = (opcode >> 3) & 0x03;
            if (cond(condIdx))
            {
                r_.pc = pop16();
                return 20;
            }
            return 8;
        }
        case 0xC1:
            r_.setBc(pop16());
            return 12;
        case 0xC2:
        case 0xCA:
        case 0xD2:
        case 0xDA:
        {
            const u16 addr = fetch16();
            const int condIdx = (opcode >> 3) & 0x03;
            if (cond(condIdx))
            {
                r_.pc = addr;
                return 16;
            }
            return 12;
        }
        case 0xC3:
            r_.pc = fetch16();
            return 16;
        case 0xC4:
        case 0xCC:
        case 0xD4:
        case 0xDC:
        {
            const u16 addr = fetch16();
            const int condIdx = (opcode >> 3) & 0x03;
            if (cond(condIdx))
            {
                push16(r_.pc);
                r_.pc = addr;
                return 24;
            }
            return 12;
        }
        case 0xC5:
            push16(r_.bc());
            return 16;
        case 0xC6:
            aluAdd(fetch8(), false);
            return 8;
        case 0xC7:
            push16(r_.pc);
            r_.pc = 0x00;
            return 16;
        case 0xC9:
            r_.pc = pop16();
            return 16;
        case 0xCB:
            return executeCb(fetch8());
        case 0xCD:
        {
            const u16 addr = fetch16();
            push16(r_.pc);
            r_.pc = addr;
            return 24;
        }
        case 0xCE:
            aluAdd(fetch8(), true);
            return 8;
        case 0xCF:
            push16(r_.pc);
            r_.pc = 0x08;
            return 16;

        case 0xD1:
            r_.setDe(pop16());
            return 12;
        case 0xD5:
            push16(r_.de());
            return 16;
        case 0xD6:
            aluSub(fetch8(), false);
            return 8;
        case 0xD7:
            push16(r_.pc);
            r_.pc = 0x10;
            return 16;
        case 0xD9:
            r_.pc = pop16();
            ime_ = true;
            return 16;
        case 0xDE:
            aluSub(fetch8(), true);
            return 8;
        case 0xDF:
            push16(r_.pc);
            r_.pc = 0x18;
            return 16;

        case 0xE0:
        {
            const u16 addr = static_cast<u16>(0xFF00 + fetch8());
            bus_.write(addr, r_.a);
            return 12;
        }
        case 0xE1:
            r_.setHl(pop16());
            return 12;
        case 0xE2:
            bus_.write(static_cast<u16>(0xFF00 + r_.c), r_.a);
            return 8;
        case 0xE5:
            push16(r_.hl());
            return 16;
        case 0xE6:
            aluAnd(fetch8());
            return 8;
        case 0xE7:
            push16(r_.pc);
            r_.pc = 0x20;
            return 16;
        case 0xE8:
            aluAddSpSigned(static_cast<i8>(fetch8()), false);
            return 16;
        case 0xE9:
            r_.pc = r_.hl();
            return 4;
        case 0xEA:
        {
            const u16 addr = fetch16();
            bus_.write(addr, r_.a);
            return 16;
        }
        case 0xEE:
            aluXor(fetch8());
            return 8;
        case 0xEF:
            push16(r_.pc);
            r_.pc = 0x28;
            return 16;

        case 0xF0:
        {
            const u16 addr = static_cast<u16>(0xFF00 + fetch8());
            r_.a = bus_.read(addr);
            return 12;
        }
        case 0xF1:
            r_.setAf(pop16());
            return 12;
        case 0xF2:
            r_.a = bus_.read(static_cast<u16>(0xFF00 + r_.c));
            return 8;
        case 0xF3:
            ime_ = false;
            return 4;
        case 0xF5:
            push16(r_.af());
            return 16;
        case 0xF6:
            aluOr(fetch8());
            return 8;
        case 0xF7:
            push16(r_.pc);
            r_.pc = 0x30;
            return 16;
        case 0xF8:
            aluAddSpSigned(static_cast<i8>(fetch8()), true);
            return 12;
        case 0xF9:
            r_.sp = r_.hl();
            return 8;
        case 0xFA:
        {
            const u16 addr = fetch16();
            r_.a = bus_.read(addr);
            return 16;
        }
        case 0xFB:
            enableImeNext_ = true;
            return 4;
        case 0xFE:
            aluCp(fetch8());
            return 8;
        case 0xFF:
            push16(r_.pc);
            r_.pc = 0x38;
            return 16;

        default:
            return 4;
        }
    }

    void CPU::requestInterrupts()
    {
        bus_.tick(4);
    }

    const Registers &CPU::regs() const
    {
        return r_;
    }

    bool CPU::isHalted() const
    {
        return halted_;
    }

    u16 CPU::lastExecutedPc() const
    {
        return lastPc_;
    }

    u8 CPU::lastExecutedOpcode() const
    {
        return lastOpcode_;
    }

    const std::array<std::uint64_t, 256>& CPU::opcodeHistogram() const
    {
        return opcodeHistogram_;
    }

    std::uint64_t CPU::totalInstructions() const
    {
        return totalInstructions_;
    }

    void CPU::resetProfiler()
    {
        opcodeHistogram_.fill(0);
        totalInstructions_ = 0;
    }

    CPU::State CPU::state() const
    {
        return State{
            r_,
            ime_,
            enableImeNext_,
            halted_,
            lastPc_,
            lastOpcode_,
        };
    }

    void CPU::loadState(const State &state)
    {
        r_ = state.regs;
        ime_ = state.ime;
        enableImeNext_ = state.enableImeNext;
        halted_ = state.halted;
        haltBug_ = false;
        lastPc_ = state.lastPc;
        lastOpcode_ = state.lastOpcode;
    }

    void CPU::setHardwareMode(bool cgbMode)
    {
        // Emula o valor de A após boot ROM para seleção DMG/CGB em ROMs dual-mode.
        r_.a = cgbMode ? 0x11 : 0x01;
    }

    u8 CPU::fetch8()
    {
        const u8 value = bus_.read(r_.pc);
        if (haltBug_)
        {
            haltBug_ = false;
        }
        else
        {
            ++r_.pc;
        }
        return value;
    }

    u16 CPU::fetch16()
    {
        const u8 lo = fetch8();
        const u8 hi = fetch8();
        return static_cast<u16>((hi << 8) | lo);
    }

    void CPU::push16(u16 value)
    {
        bus_.write(--r_.sp, static_cast<u8>(value >> 8));
        bus_.write(--r_.sp, static_cast<u8>(value & 0xFF));
    }

    u16 CPU::pop16()
    {
        const u8 lo = bus_.read(r_.sp++);
        const u8 hi = bus_.read(r_.sp++);
        return static_cast<u16>((hi << 8) | lo);
    }

    u8 CPU::readReg8(int index)
    {
        switch (index)
        {
        case 0:
            return r_.b;
        case 1:
            return r_.c;
        case 2:
            return r_.d;
        case 3:
            return r_.e;
        case 4:
            return r_.h;
        case 5:
            return r_.l;
        case 6:
            return bus_.read(r_.hl());
        case 7:
            return r_.a;
        default:
            return 0xFF;
        }
    }

    void CPU::writeReg8(int index, u8 value)
    {
        switch (index)
        {
        case 0:
            r_.b = value;
            break;
        case 1:
            r_.c = value;
            break;
        case 2:
            r_.d = value;
            break;
        case 3:
            r_.e = value;
            break;
        case 4:
            r_.h = value;
            break;
        case 5:
            r_.l = value;
            break;
        case 6:
            bus_.write(r_.hl(), value);
            break;
        case 7:
            r_.a = value;
            break;
        default:
            break;
        }
    }

    bool CPU::serviceInterrupts()
    {
        const u8 pending = static_cast<u8>(bus_.interruptEnable() & bus_.interruptFlags() & 0x1F);

        if (pending == 0)
        {
            return false;
        }

        if (halted_)
        {
            halted_ = false;
        }

        if (!ime_)
        {
            return false;
        }

        ime_ = false;

        static constexpr std::array<u16, 5> vectors = {0x40, 0x48, 0x50, 0x58, 0x60};
        for (int bit = 0; bit < 5; ++bit)
        {
            if ((pending & (1 << bit)) != 0)
            {
                bus_.setInterruptFlags(static_cast<u8>(bus_.interruptFlags() & ~(1 << bit)));
                push16(r_.pc);
                r_.pc = vectors[bit];
                return true;
            }
        }

        return false;
    }

} // namespace gb
