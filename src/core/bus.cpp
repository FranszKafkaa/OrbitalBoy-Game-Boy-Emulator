#include "gb/core/bus.hpp"

#include <algorithm>

namespace gb {

namespace {

constexpr u16 RomEnd = 0x7FFF;
constexpr u16 VramBegin = 0x8000;
constexpr u16 VramEnd = 0x9FFF;
constexpr u16 ExtRamBegin = 0xA000;
constexpr u16 ExtRamEnd = 0xBFFF;
constexpr u16 WramBegin = 0xC000;
constexpr u16 WramEnd = 0xDFFF;
constexpr u16 EchoBegin = 0xE000;
constexpr u16 EchoEnd = 0xFDFF;
constexpr u16 OamBegin = 0xFE00;
constexpr u16 OamEnd = 0xFE9F;
constexpr u16 UnusableBegin = 0xFEA0;
constexpr u16 UnusableEnd = 0xFEFF;
constexpr u16 ApuBegin = 0xFF10;
constexpr u16 ApuEnd = 0xFF3F;
constexpr u16 PpuBegin = 0xFF40;
constexpr u16 PpuEnd = 0xFF4B;
constexpr u16 DmaRegister = 0xFF46;
constexpr u16 CgbKey1 = 0xFF4D;
constexpr u16 CgbVbk = 0xFF4F;
constexpr u16 CgbHdma1 = 0xFF51;
constexpr u16 CgbHdma2 = 0xFF52;
constexpr u16 CgbHdma3 = 0xFF53;
constexpr u16 CgbHdma4 = 0xFF54;
constexpr u16 CgbHdma5 = 0xFF55;
constexpr u16 CgbBgpi = 0xFF68;
constexpr u16 CgbBgpd = 0xFF69;
constexpr u16 CgbObpi = 0xFF6A;
constexpr u16 CgbObpd = 0xFF6B;
constexpr u16 CgbSvbk = 0xFF70;
constexpr u16 BootRomDisable = 0xFF50;
constexpr u16 JoypadRegister = 0xFF00;
constexpr u16 SerialSb = 0xFF01;
constexpr u16 SerialSc = 0xFF02;
constexpr u16 TimerDiv = 0xFF04;
constexpr u16 TimerTac = 0xFF07;
constexpr u16 InterruptFlag = 0xFF0F;
constexpr u16 HramBegin = 0xFF80;
constexpr u16 HramEnd = 0xFFFE;
constexpr u16 InterruptEnable = 0xFFFF;

[[nodiscard]] bool inRange(u16 value, u16 begin, u16 end) {
    return value >= begin && value <= end;
}

} // namespace

Bus::Bus(Cartridge& cartridge)
    : cartridge_(cartridge) {
    hram_.fill(0);
    wram_.fill(0);
    wramExt_.fill(0);
    vram_.fill(0);
    vramBank1_.fill(0);
    oam_.fill(0);
    bgPalette_.fill(0);
    objPalette_.fill(0);
    cgbMode_ = cartridge_.shouldRunInCgbMode();
    svbk_ = 1;
}

void Bus::syncCartridgeMode() {
    cgbMode_ = cartridge_.shouldRunInCgbMode();
    if (cgbMode_) {
        doubleSpeed_ = false;
        speedSwitchArmed_ = false;
        hdma1_ = 0;
        hdma2_ = 0;
        hdma3_ = 0;
        hdma4_ = 0;
        hdma5_ = 0xFF;
        hdmaActive_ = false;
        // Fallback visivel ate a ROM programar BG/OBJ palettes reais.
        static constexpr std::array<u16, 4> kGray = {
            0x7FFF, // branco
            0x5294, // cinza claro
            0x294A, // cinza escuro
            0x0000, // preto
        };
        for (std::size_t pal = 0; pal < 8; ++pal) {
            for (std::size_t c = 0; c < 4; ++c) {
                const std::size_t idx = pal * 8 + c * 2;
                const u16 v = kGray[c];
                bgPalette_[idx] = static_cast<u8>(v & 0xFF);
                bgPalette_[idx + 1] = static_cast<u8>(v >> 8);
                objPalette_[idx] = static_cast<u8>(v & 0xFF);
                objPalette_[idx + 1] = static_cast<u8>(v >> 8);
            }
        }
    } else {
        vbk_ = 0;
        svbk_ = 1;
        doubleSpeed_ = false;
        speedSwitchArmed_ = false;
        bgpi_ = 0;
        obpi_ = 0;
        hdma1_ = 0;
        hdma2_ = 0;
        hdma3_ = 0;
        hdma4_ = 0;
        hdma5_ = 0xFF;
        hdmaActive_ = false;
    }
    bootRomEnabled_ = !bootRom_.empty();
}

u8 Bus::read(u16 address) {
    const u8 value = readInternal(address);
    logRead(address, value);
    return value;
}

u8 Bus::peek(u16 address) const {
    return readInternal(address);
}

u8 Bus::readInternal(u16 address) const {
    if (address <= RomEnd) {
        if (bootRomEnabled_ && static_cast<std::size_t>(address) < bootRom_.size()) {
            return bootRom_[address];
        }
        return cartridge_.read(address);
    }
    if (inRange(address, VramBegin, VramEnd)) {
        const std::size_t off = static_cast<std::size_t>(address - VramBegin);
        if (cgbMode_ && (vbk_ & 0x01)) {
            return vramBank1_[off];
        }
        return vram_[off];
    }
    if (inRange(address, ExtRamBegin, ExtRamEnd)) {
        return cartridge_.read(address);
    }
    if (inRange(address, WramBegin, WramEnd)) {
        return readWramMapped(address);
    }
    if (inRange(address, EchoBegin, EchoEnd)) {
        return readWramMapped(static_cast<u16>(address - 0x2000));
    }
    if (inRange(address, OamBegin, OamEnd)) {
        return oam_[address - OamBegin];
    }
    if (inRange(address, UnusableBegin, UnusableEnd)) {
        return 0xFF;
    }

    if (inRange(address, ApuBegin, ApuEnd)) {
        return apu_.read(address);
    }
    if (inRange(address, PpuBegin, PpuEnd)) {
        return ppu_.read(address);
    }

    switch (address) {
    case JoypadRegister:
        return joypad_.read();
    case SerialSb:
        return serialSb_;
    case SerialSc:
        return static_cast<u8>(serialSc_ | 0x7C);
    case TimerDiv:
    case 0xFF05:
    case 0xFF06:
    case TimerTac:
        return timer_.read(address);
    case InterruptFlag:
        return static_cast<u8>(if_ | 0xE0);
    case InterruptEnable:
        return ie_;
    case CgbKey1:
        if (!cgbMode_) return 0xFF;
        return static_cast<u8>(0x7E
            | (doubleSpeed_ ? 0x80 : 0x00)
            | (speedSwitchArmed_ ? 0x01 : 0x00));
    case CgbVbk:
        return cgbMode_ ? static_cast<u8>(0xFE | (vbk_ & 0x01)) : 0xFF;
    case CgbHdma1:
        return cgbMode_ ? hdma1_ : 0xFF;
    case CgbHdma2:
        return cgbMode_ ? static_cast<u8>(hdma2_ | 0x0F) : 0xFF;
    case CgbHdma3:
        return cgbMode_ ? static_cast<u8>(hdma3_ | 0xE0) : 0xFF;
    case CgbHdma4:
        return cgbMode_ ? static_cast<u8>(hdma4_ | 0x0F) : 0xFF;
    case CgbHdma5:
        if (!cgbMode_) return 0xFF;
        return hdmaActive_ ? static_cast<u8>(hdma5_ & 0x7F) : 0xFF;
    case CgbBgpi:
        return cgbMode_ ? static_cast<u8>(0x40 | (bgpi_ & 0xBF)) : 0xFF;
    case CgbBgpd:
        if (!cgbMode_) return 0xFF;
        return bgPalette_[bgpi_ & 0x3F];
    case CgbObpi:
        return cgbMode_ ? static_cast<u8>(0x40 | (obpi_ & 0xBF)) : 0xFF;
    case CgbObpd:
        if (!cgbMode_) return 0xFF;
        return objPalette_[obpi_ & 0x3F];
    case CgbSvbk:
        return cgbMode_ ? static_cast<u8>(0xF8 | (svbk_ & 0x07)) : 0xFF;
    default:
        break;
    }

    if (inRange(address, HramBegin, HramEnd)) {
        return hram_[address - HramBegin];
    }

    return 0xFF;
}

void Bus::write(u16 address, u8 value) {
    logWrite(address, value);

    if (address <= RomEnd) {
        cartridge_.write(address, value);
        return;
    }
    if (inRange(address, VramBegin, VramEnd)) {
        const std::size_t off = static_cast<std::size_t>(address - VramBegin);
        if (cgbMode_ && (vbk_ & 0x01)) {
            vramBank1_[off] = value;
        } else {
            vram_[off] = value;
        }
        return;
    }
    if (inRange(address, ExtRamBegin, ExtRamEnd)) {
        cartridge_.write(address, value);
        return;
    }
    if (inRange(address, WramBegin, WramEnd)) {
        writeWramMapped(address, value);
        return;
    }
    if (inRange(address, EchoBegin, EchoEnd)) {
        writeWramMapped(static_cast<u16>(address - 0x2000), value);
        return;
    }
    if (inRange(address, OamBegin, OamEnd)) {
        oam_[address - OamBegin] = value;
        return;
    }
    if (inRange(address, UnusableBegin, UnusableEnd)) {
        return;
    }

    if (inRange(address, ApuBegin, ApuEnd)) {
        apu_.write(address, value);
        return;
    }

    if (address == DmaRegister) {
        doOamDma(value);
        ppu_.write(address, value);
        return;
    }
    if (inRange(address, PpuBegin, PpuEnd)) {
        ppu_.write(address, value);
        return;
    }

    switch (address) {
    case JoypadRegister:
        joypad_.write(value);
        return;
    case SerialSb:
        serialSb_ = value;
        return;
    case SerialSc:
        serialSc_ = static_cast<u8>(0x7C | (value & 0x83));
        if ((value & 0x80) != 0) {
            serialTransferRequested_ = true;
        }
        return;
    case TimerDiv:
    case 0xFF05:
    case 0xFF06:
    case TimerTac:
        timer_.write(address, value);
        return;
    case InterruptFlag:
        if_ = static_cast<u8>(value | 0xE0);
        return;
    case InterruptEnable:
        ie_ = value;
        return;
    case CgbKey1:
        if (cgbMode_) {
            speedSwitchArmed_ = (value & 0x01) != 0;
        }
        return;
    case CgbVbk:
        if (cgbMode_) {
            vbk_ = static_cast<u8>(value & 0x01);
        }
        return;
    case CgbHdma1:
        if (cgbMode_) {
            hdma1_ = value;
        }
        return;
    case CgbHdma2:
        if (cgbMode_) {
            hdma2_ = static_cast<u8>(value & 0xF0);
        }
        return;
    case CgbHdma3:
        if (cgbMode_) {
            hdma3_ = static_cast<u8>(value & 0x1F);
        }
        return;
    case CgbHdma4:
        if (cgbMode_) {
            hdma4_ = static_cast<u8>(value & 0xF0);
        }
        return;
    case CgbHdma5:
        if (!cgbMode_) {
            return;
        }
        if (hdmaActive_ && (value & 0x80) == 0) {
            hdmaActive_ = false;
            hdma5_ = static_cast<u8>(value & 0x7F);
            return;
        }
        hdma5_ = static_cast<u8>(value & 0x7F);
        hdmaActive_ = (value & 0x80) != 0;
        doHdmaTransfer(static_cast<u16>((static_cast<u16>(hdma5_) + 1) * 0x10));
        hdmaActive_ = false;
        hdma5_ = 0xFF;
        return;
    case CgbBgpi:
        if (cgbMode_) {
            bgpi_ = value;
        }
        return;
    case CgbBgpd:
        if (cgbMode_) {
            const u8 idx = static_cast<u8>(bgpi_ & 0x3F);
            bgPalette_[idx] = value;
            if (bgpi_ & 0x80) {
                bgpi_ = static_cast<u8>((bgpi_ & 0x80) | ((idx + 1) & 0x3F));
            }
        }
        return;
    case CgbObpi:
        if (cgbMode_) {
            obpi_ = value;
        }
        return;
    case CgbObpd:
        if (cgbMode_) {
            const u8 idx = static_cast<u8>(obpi_ & 0x3F);
            objPalette_[idx] = value;
            if (obpi_ & 0x80) {
                obpi_ = static_cast<u8>((obpi_ & 0x80) | ((idx + 1) & 0x3F));
            }
        }
        return;
    case CgbSvbk:
        if (cgbMode_) {
            svbk_ = static_cast<u8>(value & 0x07);
        }
        return;
    case BootRomDisable:
        if (value != 0) {
            bootRomEnabled_ = false;
        }
        return;
    default:
        break;
    }

    if (inRange(address, HramBegin, HramEnd)) {
        hram_[address - HramBegin] = value;
    }
}

u8 Bus::wramBankValue() const {
    u8 bank = static_cast<u8>(svbk_ & 0x07);
    if (bank == 0) {
        bank = 1;
    }
    return bank;
}

u8 Bus::readWramMapped(u16 address) const {
    if (address >= 0xC000 && address <= 0xCFFF) {
        return wram_[address - 0xC000];
    }
    if (address >= 0xD000 && address <= 0xDFFF) {
        const std::size_t off = static_cast<std::size_t>(address - 0xD000);
        if (!cgbMode_) {
            return wram_[0x1000 + off];
        }
        const u8 bank = wramBankValue();
        if (bank == 1) {
            return wram_[0x1000 + off];
        }
        return wramExt_[(static_cast<std::size_t>(bank - 2) * 0x1000) + off];
    }
    return 0xFF;
}

void Bus::writeWramMapped(u16 address, u8 value) {
    if (address >= 0xC000 && address <= 0xCFFF) {
        wram_[address - 0xC000] = value;
        return;
    }
    if (address >= 0xD000 && address <= 0xDFFF) {
        const std::size_t off = static_cast<std::size_t>(address - 0xD000);
        if (!cgbMode_) {
            wram_[0x1000 + off] = value;
            return;
        }
        const u8 bank = wramBankValue();
        if (bank == 1) {
            wram_[0x1000 + off] = value;
            return;
        }
        wramExt_[(static_cast<std::size_t>(bank - 2) * 0x1000) + off] = value;
    }
}

void Bus::tick(u32 cycles) {
    timer_.tick(cycles);
    ppu_.tick(cycles, vram_, vramBank1_, oam_, cgbMode_, bgPalette_, objPalette_);
    apu_.tick(cycles);

    if (timer_.consumeInterrupt()) {
        requestInterrupt(2);
    }
    if (ppu_.consumeLcdInterrupt()) {
        requestInterrupt(1);
    }
    if (ppu_.consumeVBlankInterrupt()) {
        requestInterrupt(0);
    }
    if (joypad_.consumeInterrupt()) {
        requestInterrupt(4);
    }
}

u32 Bus::peripheralCyclesFromCpuCycles(u32 cpuCycles) const {
    if (!doubleSpeed_) {
        return cpuCycles;
    }
    return cpuCycles / 2;
}

bool Bus::isDoubleSpeed() const {
    return doubleSpeed_;
}

bool Bus::trySpeedSwitch() {
    if (!cgbMode_ || !speedSwitchArmed_) {
        return false;
    }
    doubleSpeed_ = !doubleSpeed_;
    speedSwitchArmed_ = false;
    return true;
}

bool Bus::consumeSerialTransfer(u8& outData) {
    if (!serialTransferRequested_) {
        return false;
    }
    serialTransferRequested_ = false;
    outData = serialSb_;
    return true;
}

void Bus::completeSerialTransfer(u8 inData) {
    serialSb_ = inData;
    serialSc_ = static_cast<u8>(serialSc_ & ~0x80);
    requestInterrupt(3);
}

void Bus::setBootRomData(const std::vector<u8>& data) {
    bootRom_ = data;
    bootRomEnabled_ = !bootRom_.empty();
}

void Bus::clearBootRom() {
    bootRom_.clear();
    bootRomEnabled_ = false;
}

bool Bus::bootRomEnabled() const {
    return bootRomEnabled_;
}

void Bus::requestInterrupt(int bit) {
    if_ = static_cast<u8>(if_ | (1u << bit));
}

u8 Bus::interruptEnable() const {
    return ie_;
}

u8 Bus::interruptFlags() const {
    return if_;
}

void Bus::setInterruptFlags(u8 value) {
    if_ = static_cast<u8>(value | 0xE0);
}

PPU& Bus::ppu() {
    return ppu_;
}

const PPU& Bus::ppu() const {
    return ppu_;
}

Joypad& Bus::joypad() {
    return joypad_;
}

APU& Bus::apu() {
    return apu_;
}

Bus::State Bus::state() const {
    State s{};
    s.vram = vram_;
    s.vramBank1 = vramBank1_;
    s.wram = wram_;
    s.wramExt = wramExt_;
    s.oam = oam_;
    s.hram = hram_;
    s.timer = timer_.state();
    s.joypad = joypad_.state();
    s.ppu = ppu_.state();
    s.apu = apu_.state();
    s.cgbMode = cgbMode_;
    s.doubleSpeed = doubleSpeed_;
    s.speedSwitchArmed = speedSwitchArmed_;
    s.vbk = vbk_;
    s.svbk = svbk_;
    s.bgpi = bgpi_;
    s.obpi = obpi_;
    s.bgPalette = bgPalette_;
    s.objPalette = objPalette_;
    s.hdma1 = hdma1_;
    s.hdma2 = hdma2_;
    s.hdma3 = hdma3_;
    s.hdma4 = hdma4_;
    s.hdma5 = hdma5_;
    s.hdmaActive = hdmaActive_;
    s.serialSb = serialSb_;
    s.serialSc = serialSc_;
    s.serialTransferRequested = serialTransferRequested_;
    s.bootRomEnabled = bootRomEnabled_;
    s.ie = ie_;
    s.iflag = if_;
    return s;
}

void Bus::loadState(const State& s) {
    vram_ = s.vram;
    vramBank1_ = s.vramBank1;
    wram_ = s.wram;
    wramExt_ = s.wramExt;
    oam_ = s.oam;
    hram_ = s.hram;
    timer_.loadState(s.timer);
    joypad_.loadState(s.joypad);
    ppu_.loadState(s.ppu);
    apu_.loadState(s.apu);
    cgbMode_ = s.cgbMode;
    doubleSpeed_ = s.doubleSpeed;
    speedSwitchArmed_ = s.speedSwitchArmed;
    vbk_ = s.vbk;
    svbk_ = s.svbk;
    bgpi_ = s.bgpi;
    obpi_ = s.obpi;
    bgPalette_ = s.bgPalette;
    objPalette_ = s.objPalette;
    hdma1_ = s.hdma1;
    hdma2_ = s.hdma2;
    hdma3_ = s.hdma3;
    hdma4_ = s.hdma4;
    hdma5_ = s.hdma5;
    hdmaActive_ = s.hdmaActive;
    serialSb_ = s.serialSb;
    serialSc_ = s.serialSc;
    serialTransferRequested_ = s.serialTransferRequested;
    bootRomEnabled_ = s.bootRomEnabled && !bootRom_.empty();
    ie_ = s.ie;
    if_ = s.iflag;
}

std::vector<Bus::MemoryReadEvent> Bus::snapshotRecentReads(std::size_t maxItems) const {
    std::vector<MemoryReadEvent> out;
    if (maxItems == 0 || readLogCount_ == 0) {
        return out;
    }

    const std::size_t count = std::min(maxItems, readLogCount_);
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = (readLogHead_ + readLog_.size() - 1 - i) % readLog_.size();
        out.push_back(readLog_[idx]);
    }
    return out;
}

std::vector<Bus::MemoryWriteEvent> Bus::snapshotRecentWrites(std::size_t maxItems) const {
    std::vector<MemoryWriteEvent> out;
    if (maxItems == 0 || writeLogCount_ == 0) {
        return out;
    }

    const std::size_t count = std::min(maxItems, writeLogCount_);
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = (writeLogHead_ + writeLog_.size() - 1 - i) % writeLog_.size();
        out.push_back(writeLog_[idx]);
    }
    return out;
}

void Bus::logRead(u16 address, u8 value) {
    readLog_[readLogHead_] = MemoryReadEvent{address, value};
    readLogHead_ = (readLogHead_ + 1) % readLog_.size();
    if (readLogCount_ < readLog_.size()) {
        ++readLogCount_;
    }
}

void Bus::logWrite(u16 address, u8 value) {
    writeLog_[writeLogHead_] = MemoryWriteEvent{address, value};
    writeLogHead_ = (writeLogHead_ + 1) % writeLog_.size();
    if (writeLogCount_ < writeLog_.size()) {
        ++writeLogCount_;
    }
}

void Bus::doOamDma(u8 page) {
    const u16 source = static_cast<u16>(page) << 8;
    for (u16 i = 0; i < 0xA0; ++i) {
        oam_[i] = read(static_cast<u16>(source + i));
    }
}

void Bus::doHdmaTransfer(u16 lengthBytes) {
    u16 source = static_cast<u16>((static_cast<u16>(hdma1_) << 8) | (hdma2_ & 0xF0));
    u16 destination = static_cast<u16>(0x8000 | ((static_cast<u16>(hdma3_ & 0x1F) << 8) | (hdma4_ & 0xF0)));

    for (u16 i = 0; i < lengthBytes; ++i) {
        const u8 value = readInternal(static_cast<u16>(source + i));
        const u16 vramAddress = static_cast<u16>(destination + i);
        if (vramAddress < 0x8000 || vramAddress > 0x9FFF) {
            continue;
        }
        const std::size_t offset = static_cast<std::size_t>(vramAddress - 0x8000);
        if (vbk_ & 0x01) {
            vramBank1_[offset] = value;
        } else {
            vram_[offset] = value;
        }
    }

    source = static_cast<u16>(source + lengthBytes);
    destination = static_cast<u16>(destination + lengthBytes);
    const u16 dstMasked = static_cast<u16>(destination & 0x1FF0);

    hdma1_ = static_cast<u8>(source >> 8);
    hdma2_ = static_cast<u8>(source & 0xF0);
    hdma3_ = static_cast<u8>((dstMasked >> 8) & 0x1F);
    hdma4_ = static_cast<u8>(dstMasked & 0xF0);
}

} // namespace gb
