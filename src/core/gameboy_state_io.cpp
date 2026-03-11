#include "gb/core/gameboy.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

namespace gb {

namespace {

constexpr std::uint32_t SaveMagic = 0x31534247; // GBS1
constexpr std::uint32_t SaveVersion = 3;

template <typename T>
bool writePod(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(os);
}

template <typename T>
bool readPod(std::istream& is, T& value) {
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(is);
}

bool writeBool(std::ostream& os, bool value) {
    const u8 v = value ? 1 : 0;
    return writePod(os, v);
}

bool readBool(std::istream& is, bool& value) {
    u8 v = 0;
    if (!readPod(is, v)) {
        return false;
    }
    value = v != 0;
    return true;
}

template <std::size_t N>
bool writeArray(std::ostream& os, const std::array<u8, N>& arr) {
    os.write(reinterpret_cast<const char*>(arr.data()), static_cast<std::streamsize>(arr.size()));
    return static_cast<bool>(os);
}

template <std::size_t N>
bool readArray(std::istream& is, std::array<u8, N>& arr) {
    is.read(reinterpret_cast<char*>(arr.data()), static_cast<std::streamsize>(arr.size()));
    return static_cast<bool>(is);
}

bool writeVector(std::ostream& os, const std::vector<u8>& vec) {
    const u32 size = static_cast<u32>(vec.size());
    if (!writePod(os, size)) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    os.write(reinterpret_cast<const char*>(vec.data()), size);
    return static_cast<bool>(os);
}

bool readVector(std::istream& is, std::vector<u8>& vec, u32 maxSize = 8 * 1024 * 1024) {
    u32 size = 0;
    if (!readPod(is, size)) {
        return false;
    }
    if (size > maxSize) {
        return false;
    }
    vec.assign(size, 0);
    if (size == 0) {
        return true;
    }
    is.read(reinterpret_cast<char*>(vec.data()), size);
    return static_cast<bool>(is);
}

bool writeRegisters(std::ostream& os, const Registers& r) {
    return writePod(os, r.a) && writePod(os, r.f) && writePod(os, r.b) && writePod(os, r.c)
        && writePod(os, r.d) && writePod(os, r.e) && writePod(os, r.h) && writePod(os, r.l)
        && writePod(os, r.sp) && writePod(os, r.pc);
}

bool readRegisters(std::istream& is, Registers& r) {
    return readPod(is, r.a) && readPod(is, r.f) && readPod(is, r.b) && readPod(is, r.c)
        && readPod(is, r.d) && readPod(is, r.e) && readPod(is, r.h) && readPod(is, r.l)
        && readPod(is, r.sp) && readPod(is, r.pc);
}

bool writeTimerState(std::ostream& os, const Timer::State& s) {
    return writePod(os, s.divCounter) && writePod(os, s.timaCounter) && writePod(os, s.div)
        && writePod(os, s.tima) && writePod(os, s.tma) && writePod(os, s.tac)
        && writeBool(os, s.overflowPending) && writePod(os, s.overflowDelay)
        && writeBool(os, s.timerInterruptRequested);
}

bool readTimerState(std::istream& is, Timer::State& s) {
    return readPod(is, s.divCounter) && readPod(is, s.timaCounter) && readPod(is, s.div)
        && readPod(is, s.tima) && readPod(is, s.tma) && readPod(is, s.tac)
        && readBool(is, s.overflowPending) && readPod(is, s.overflowDelay)
        && readBool(is, s.timerInterruptRequested);
}

bool writeJoypadState(std::ostream& os, const Joypad::State& s) {
    for (bool b : s.pressed) {
        if (!writeBool(os, b)) {
            return false;
        }
    }
    return writePod(os, s.select) && writeBool(os, s.interruptRequested);
}

bool readJoypadState(std::istream& is, Joypad::State& s) {
    for (bool& b : s.pressed) {
        if (!readBool(is, b)) {
            return false;
        }
    }
    return readPod(is, s.select) && readBool(is, s.interruptRequested);
}

bool writePpuState(std::ostream& os, const PPU::State& s) {
    return writePod(os, s.modeClock) && writePod(os, s.mode) && writePod(os, s.lcdc)
        && writePod(os, s.stat) && writePod(os, s.scy) && writePod(os, s.scx)
        && writePod(os, s.ly) && writePod(os, s.lyc) && writePod(os, s.bgp)
        && writePod(os, s.obp0) && writePod(os, s.obp1) && writePod(os, s.wy)
        && writePod(os, s.wx) && writePod(os, s.dma)
        && writeBool(os, s.vblankInterruptRequested) && writeBool(os, s.lcdInterruptRequested)
        && writeArray(os, s.framebuffer);
}

bool readPpuState(std::istream& is, PPU::State& s) {
    return readPod(is, s.modeClock) && readPod(is, s.mode) && readPod(is, s.lcdc)
        && readPod(is, s.stat) && readPod(is, s.scy) && readPod(is, s.scx)
        && readPod(is, s.ly) && readPod(is, s.lyc) && readPod(is, s.bgp)
        && readPod(is, s.obp0) && readPod(is, s.obp1) && readPod(is, s.wy)
        && readPod(is, s.wx) && readPod(is, s.dma)
        && readBool(is, s.vblankInterruptRequested) && readBool(is, s.lcdInterruptRequested)
        && readArray(is, s.framebuffer);
}

bool writeApuSquare(std::ostream& os, const APU::SquareChannelState& s) {
    return writePod(os, s.nrx1) && writePod(os, s.nrx2) && writePod(os, s.nrx3)
        && writePod(os, s.nrx4) && writeBool(os, s.enabled) && writeBool(os, s.dacEnabled)
        && writePod(os, s.dutyStep) && writePod(os, s.timerCycles)
        && writePod(os, s.lengthCounter) && writeBool(os, s.lengthEnabled)
        && writePod(os, s.currentVolume) && writePod(os, s.envelopePeriod)
        && writePod(os, s.envelopeTimer) && writeBool(os, s.envelopeIncrease);
}

bool readApuSquare(std::istream& is, APU::SquareChannelState& s) {
    return readPod(is, s.nrx1) && readPod(is, s.nrx2) && readPod(is, s.nrx3)
        && readPod(is, s.nrx4) && readBool(is, s.enabled) && readBool(is, s.dacEnabled)
        && readPod(is, s.dutyStep) && readPod(is, s.timerCycles)
        && readPod(is, s.lengthCounter) && readBool(is, s.lengthEnabled)
        && readPod(is, s.currentVolume) && readPod(is, s.envelopePeriod)
        && readPod(is, s.envelopeTimer) && readBool(is, s.envelopeIncrease);
}

bool writeApuWave(std::ostream& os, const APU::WaveChannelState& s) {
    return writePod(os, s.nr30) && writePod(os, s.nr31) && writePod(os, s.nr32)
        && writePod(os, s.nr33) && writePod(os, s.nr34)
        && writeBool(os, s.enabled) && writeBool(os, s.dacEnabled)
        && writePod(os, s.waveStep) && writePod(os, s.timerCycles)
        && writePod(os, s.lengthCounter) && writeBool(os, s.lengthEnabled);
}

bool readApuWave(std::istream& is, APU::WaveChannelState& s) {
    return readPod(is, s.nr30) && readPod(is, s.nr31) && readPod(is, s.nr32)
        && readPod(is, s.nr33) && readPod(is, s.nr34)
        && readBool(is, s.enabled) && readBool(is, s.dacEnabled)
        && readPod(is, s.waveStep) && readPod(is, s.timerCycles)
        && readPod(is, s.lengthCounter) && readBool(is, s.lengthEnabled);
}

bool writeApuState(std::ostream& os, const APU::State& s) {
    return writeApuSquare(os, s.ch1) && writeApuSquare(os, s.ch2) && writeApuWave(os, s.ch3)
        && writePod(os, s.nr10)
        && writeBool(os, s.sweepEnabled) && writePod(os, s.sweepPeriod)
        && writePod(os, s.sweepTimer) && writeBool(os, s.sweepNegate)
        && writePod(os, s.sweepShift) && writePod(os, s.sweepShadowFreq)
        && writeArray(os, s.waveRam)
        && writePod(os, s.nr50) && writePod(os, s.nr51) && writePod(os, s.nr52)
        && writePod(os, s.frameSeqCycles) && writePod(os, s.frameSeqStep)
        && writePod(os, s.sampleCyclesAccum)
        && writePod(os, s.hpPrevInL) && writePod(os, s.hpPrevOutL)
        && writePod(os, s.hpPrevInR) && writePod(os, s.hpPrevOutR);
}

bool readApuState(std::istream& is, APU::State& s) {
    return readApuSquare(is, s.ch1) && readApuSquare(is, s.ch2) && readApuWave(is, s.ch3)
        && readPod(is, s.nr10)
        && readBool(is, s.sweepEnabled) && readPod(is, s.sweepPeriod)
        && readPod(is, s.sweepTimer) && readBool(is, s.sweepNegate)
        && readPod(is, s.sweepShift) && readPod(is, s.sweepShadowFreq)
        && readArray(is, s.waveRam)
        && readPod(is, s.nr50) && readPod(is, s.nr51) && readPod(is, s.nr52)
        && readPod(is, s.frameSeqCycles) && readPod(is, s.frameSeqStep)
        && readPod(is, s.sampleCyclesAccum)
        && readPod(is, s.hpPrevInL) && readPod(is, s.hpPrevOutL)
        && readPod(is, s.hpPrevInR) && readPod(is, s.hpPrevOutR);
}

bool writeBusState(std::ostream& os, const Bus::State& s) {
    return writeArray(os, s.vram) && writeArray(os, s.vramBank1)
        && writeArray(os, s.wram) && writeArray(os, s.wramExt) && writeArray(os, s.oam)
        && writeArray(os, s.hram) && writeTimerState(os, s.timer)
        && writeJoypadState(os, s.joypad) && writePpuState(os, s.ppu)
        && writeApuState(os, s.apu)
        && writeBool(os, s.cgbMode) && writeBool(os, s.doubleSpeed) && writeBool(os, s.speedSwitchArmed)
        && writePod(os, s.vbk) && writePod(os, s.svbk) && writePod(os, s.bgpi) && writePod(os, s.obpi)
        && writeArray(os, s.bgPalette) && writeArray(os, s.objPalette)
        && writePod(os, s.hdma1) && writePod(os, s.hdma2) && writePod(os, s.hdma3)
        && writePod(os, s.hdma4) && writePod(os, s.hdma5) && writeBool(os, s.hdmaActive)
        && writePod(os, s.serialSb) && writePod(os, s.serialSc)
        && writeBool(os, s.serialTransferRequested)
        && writePod(os, s.ie) && writePod(os, s.iflag);
}

bool readBusState(std::istream& is, Bus::State& s, bool hasSerialState) {
    const bool ok = readArray(is, s.vram) && readArray(is, s.vramBank1)
        && readArray(is, s.wram) && readArray(is, s.wramExt) && readArray(is, s.oam)
        && readArray(is, s.hram) && readTimerState(is, s.timer)
        && readJoypadState(is, s.joypad) && readPpuState(is, s.ppu)
        && readApuState(is, s.apu)
        && readBool(is, s.cgbMode) && readBool(is, s.doubleSpeed) && readBool(is, s.speedSwitchArmed)
        && readPod(is, s.vbk) && readPod(is, s.svbk) && readPod(is, s.bgpi) && readPod(is, s.obpi)
        && readArray(is, s.bgPalette) && readArray(is, s.objPalette)
        && readPod(is, s.hdma1) && readPod(is, s.hdma2) && readPod(is, s.hdma3)
        && readPod(is, s.hdma4) && readPod(is, s.hdma5) && readBool(is, s.hdmaActive);
    if (!ok) {
        return false;
    }
    if (hasSerialState) {
        if (!readPod(is, s.serialSb)
            || !readPod(is, s.serialSc)
            || !readBool(is, s.serialTransferRequested)) {
            return false;
        }
    } else {
        s.serialSb = 0x00;
        s.serialSc = 0x7E;
        s.serialTransferRequested = false;
    }
    return readPod(is, s.ie) && readPod(is, s.iflag);
}

bool writeCpuState(std::ostream& os, const CPU::State& s) {
    return writeRegisters(os, s.regs) && writeBool(os, s.ime) && writeBool(os, s.enableImeNext)
        && writeBool(os, s.halted) && writePod(os, s.lastPc) && writePod(os, s.lastOpcode);
}

bool readCpuState(std::istream& is, CPU::State& s) {
    return readRegisters(is, s.regs) && readBool(is, s.ime) && readBool(is, s.enableImeNext)
        && readBool(is, s.halted) && readPod(is, s.lastPc) && readPod(is, s.lastOpcode);
}

bool writeCartridgeState(std::ostream& os, const Cartridge::State& s) {
    return writePod(os, s.type) && writeVector(os, s.ram) && writeVector(os, s.mapper);
}

bool readCartridgeState(std::istream& is, Cartridge::State& s) {
    return readPod(is, s.type) && readVector(is, s.ram) && readVector(is, s.mapper);
}

} // namespace

bool GameBoy::saveStateToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    const auto s = saveState();
    if (!writePod(out, SaveMagic) || !writePod(out, SaveVersion)
        || !writeCartridgeState(out, s.cartridge)
        || !writeBusState(out, s.bus)
        || !writeCpuState(out, s.cpu)) {
        return false;
    }

    return static_cast<bool>(out);
}

bool GameBoy::loadStateFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!readPod(in, magic) || !readPod(in, version)) {
        return false;
    }
    if (magic != SaveMagic || (version != 2 && version != SaveVersion)) {
        return false;
    }

    SaveState s{};
    if (!readCartridgeState(in, s.cartridge)
        || !readBusState(in, s.bus, version >= 3)
        || !readCpuState(in, s.cpu)) {
        return false;
    }

    loadState(s);
    return true;
}

} // namespace gb
