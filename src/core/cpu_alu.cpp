#include "gb/core/cpu.hpp"

namespace gb {

u32 CPU::executeCb(u8 opcode) {
    const int group = opcode >> 6;
    const int y = (opcode >> 3) & 0x07;
    const int z = opcode & 0x07;

    u8 value = readReg8(z);

    if (group == 0) {
        switch (y) {
        case 0: {
            const u8 carry = static_cast<u8>((value & 0x80) >> 7);
            value = static_cast<u8>((value << 1) | carry);
            setFlag(C, carry != 0);
            break;
        }
        case 1: {
            const u8 carry = static_cast<u8>(value & 0x01);
            value = static_cast<u8>((value >> 1) | (carry << 7));
            setFlag(C, carry != 0);
            break;
        }
        case 2: {
            const u8 carryIn = getFlag(C) ? 1 : 0;
            const u8 carryOut = static_cast<u8>((value & 0x80) >> 7);
            value = static_cast<u8>((value << 1) | carryIn);
            setFlag(C, carryOut != 0);
            break;
        }
        case 3: {
            const u8 carryIn = getFlag(C) ? 1 : 0;
            const u8 carryOut = static_cast<u8>(value & 0x01);
            value = static_cast<u8>((value >> 1) | (carryIn << 7));
            setFlag(C, carryOut != 0);
            break;
        }
        case 4: {
            const u8 carry = static_cast<u8>((value & 0x80) >> 7);
            value = static_cast<u8>(value << 1);
            setFlag(C, carry != 0);
            break;
        }
        case 5: {
            const u8 carry = static_cast<u8>(value & 0x01);
            const u8 msb = static_cast<u8>(value & 0x80);
            value = static_cast<u8>((value >> 1) | msb);
            setFlag(C, carry != 0);
            break;
        }
        case 6:
            value = static_cast<u8>((value << 4) | (value >> 4));
            setFlag(C, false);
            break;
        case 7: {
            const u8 carry = static_cast<u8>(value & 0x01);
            value = static_cast<u8>(value >> 1);
            setFlag(C, carry != 0);
            break;
        }
        default:
            break;
        }

        setFlag(Z, value == 0);
        setFlag(N, false);
        setFlag(H, false);
        writeReg8(z, value);
        return z == 6 ? 16 : 8;
    }

    if (group == 1) {
        setFlag(Z, (value & (1u << y)) == 0);
        setFlag(N, false);
        setFlag(H, true);
        return z == 6 ? 12 : 8;
    }

    if (group == 2) {
        value = static_cast<u8>(value & ~(1u << y));
        writeReg8(z, value);
        return z == 6 ? 16 : 8;
    }

    value = static_cast<u8>(value | (1u << y));
    writeReg8(z, value);
    return z == 6 ? 16 : 8;
}

void CPU::aluAdd(u8 value, bool carry) {
    const u8 c = (carry && getFlag(C)) ? 1 : 0;
    const u16 result = static_cast<u16>(r_.a) + value + c;

    setFlag(Z, (result & 0xFF) == 0);
    setFlag(N, false);
    setFlag(H, ((r_.a & 0x0F) + (value & 0x0F) + c) > 0x0F);
    setFlag(C, result > 0xFF);

    r_.a = static_cast<u8>(result & 0xFF);
}

void CPU::aluSub(u8 value, bool carry) {
    const u8 c = (carry && getFlag(C)) ? 1 : 0;
    const i16 result = static_cast<i16>(r_.a) - value - c;

    setFlag(Z, (result & 0xFF) == 0);
    setFlag(N, true);
    setFlag(H, (r_.a & 0x0F) < ((value & 0x0F) + c));
    setFlag(C, result < 0);

    r_.a = static_cast<u8>(result & 0xFF);
}

void CPU::aluAnd(u8 value) {
    r_.a &= value;
    setFlag(Z, r_.a == 0);
    setFlag(N, false);
    setFlag(H, true);
    setFlag(C, false);
}

void CPU::aluOr(u8 value) {
    r_.a |= value;
    setFlag(Z, r_.a == 0);
    setFlag(N, false);
    setFlag(H, false);
    setFlag(C, false);
}

void CPU::aluXor(u8 value) {
    r_.a ^= value;
    setFlag(Z, r_.a == 0);
    setFlag(N, false);
    setFlag(H, false);
    setFlag(C, false);
}

void CPU::aluCp(u8 value) {
    const i16 result = static_cast<i16>(r_.a) - value;
    setFlag(Z, (result & 0xFF) == 0);
    setFlag(N, true);
    setFlag(H, (r_.a & 0x0F) < (value & 0x0F));
    setFlag(C, result < 0);
}

void CPU::aluAddHl(u16 value) {
    const u32 result = static_cast<u32>(r_.hl()) + value;
    setFlag(N, false);
    setFlag(H, ((r_.hl() & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF);
    setFlag(C, result > 0xFFFF);
    r_.setHl(static_cast<u16>(result));
}

void CPU::aluAddSpSigned(i8 value, bool storeInHl) {
    const u16 base = r_.sp;
    const u16 result = static_cast<u16>(base + value);
    const u8 raw = static_cast<u8>(value);

    setFlag(Z, false);
    setFlag(N, false);
    setFlag(H, ((base & 0x0F) + (raw & 0x0F)) > 0x0F);
    setFlag(C, ((base & 0xFF) + raw) > 0xFF);

    if (storeInHl) {
        r_.setHl(result);
    } else {
        r_.sp = result;
    }
}

void CPU::aluDaa() {
    u8 adjust = 0;
    bool carry = getFlag(C);

    if (!getFlag(N)) {
        if (getFlag(H) || (r_.a & 0x0F) > 0x09) {
            adjust |= 0x06;
        }
        if (carry || r_.a > 0x99) {
            adjust |= 0x60;
            carry = true;
        }
        r_.a = static_cast<u8>(r_.a + adjust);
    } else {
        if (getFlag(H)) {
            adjust |= 0x06;
        }
        if (carry) {
            adjust |= 0x60;
        }
        r_.a = static_cast<u8>(r_.a - adjust);
    }

    setFlag(Z, r_.a == 0);
    setFlag(H, false);
    setFlag(C, carry);
}

void CPU::setFlag(Flag flag, bool state) {
    if (state) {
        r_.f = static_cast<u8>(r_.f | flag);
    } else {
        r_.f = static_cast<u8>(r_.f & ~flag);
    }
    r_.f &= 0xF0;
}

bool CPU::getFlag(Flag flag) const {
    return (r_.f & flag) != 0;
}

} // namespace gb
