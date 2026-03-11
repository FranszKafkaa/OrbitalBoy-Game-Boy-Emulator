#include "gb/bus.hpp"

namespace gb {

Bus::Bus(Cartridge& cartridge)
    : cartridge_(cartridge) {
    hram_.fill(0);
    wram_.fill(0);
    vram_.fill(0);
    oam_.fill(0);
}

u8 Bus::read(u16 address) {
    const u8 value = readInternal(address);
    logRead(address, value);
    return value;
}

u8 Bus::peek(u16 address) const {
    if (address <= 0x7FFF) {
        return cartridge_.read(address);
    }
    if (address <= 0x9FFF) {
        return vram_[address - 0x8000];
    }
    if (address <= 0xBFFF) {
        return cartridge_.read(address);
    }
    if (address <= 0xDFFF) {
        return wram_[address - 0xC000];
    }
    if (address <= 0xFDFF) {
        return wram_[address - 0xE000];
    }
    if (address <= 0xFE9F) {
        return oam_[address - 0xFE00];
    }
    if (address <= 0xFEFF) {
        return 0xFF;
    }

    if (address >= 0xFF10 && address <= 0xFF3F) {
        return apu_.read(address);
    }

    if (address >= 0xFF40 && address <= 0xFF4B) {
        return ppu_.read(address);
    }

    switch (address) {
    case 0xFF00: return joypad_.read();
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07:
        return timer_.read(address);
    case 0xFF0F: return static_cast<u8>(if_ | 0xE0);
    case 0xFFFF: return ie_;
    default:
        break;
    }

    if (address >= 0xFF80 && address <= 0xFFFE) {
        return hram_[address - 0xFF80];
    }

    return 0xFF;
}

u8 Bus::readInternal(u16 address) {
    if (address <= 0x7FFF) {
        return cartridge_.read(address);
    }
    if (address <= 0x9FFF) {
        return vram_[address - 0x8000];
    }
    if (address <= 0xBFFF) {
        return cartridge_.read(address);
    }
    if (address <= 0xDFFF) {
        return wram_[address - 0xC000];
    }
    if (address <= 0xFDFF) {
        return wram_[address - 0xE000];
    }
    if (address <= 0xFE9F) {
        return oam_[address - 0xFE00];
    }
    if (address <= 0xFEFF) {
        return 0xFF;
    }

    if (address >= 0xFF10 && address <= 0xFF3F) {
        return apu_.read(address);
    }

    if (address >= 0xFF40 && address <= 0xFF4B) {
        return ppu_.read(address);
    }

    switch (address) {
    case 0xFF00: return joypad_.read();
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07:
        return timer_.read(address);
    case 0xFF0F: return static_cast<u8>(if_ | 0xE0);
    case 0xFFFF: return ie_;
    default:
        break;
    }

    if (address >= 0xFF80 && address <= 0xFFFE) {
        return hram_[address - 0xFF80];
    }

    return 0xFF;
}

void Bus::write(u16 address, u8 value) {
    if (address <= 0x7FFF) {
        cartridge_.write(address, value);
        return;
    }
    if (address <= 0x9FFF) {
        vram_[address - 0x8000] = value;
        return;
    }
    if (address <= 0xBFFF) {
        cartridge_.write(address, value);
        return;
    }
    if (address <= 0xDFFF) {
        wram_[address - 0xC000] = value;
        return;
    }
    if (address <= 0xFDFF) {
        wram_[address - 0xE000] = value;
        return;
    }
    if (address <= 0xFE9F) {
        oam_[address - 0xFE00] = value;
        return;
    }
    if (address <= 0xFEFF) {
        return;
    }

    if (address >= 0xFF10 && address <= 0xFF3F) {
        apu_.write(address, value);
        return;
    }

    if (address == 0xFF46) {
        doOamDma(value);
        ppu_.write(address, value);
        return;
    }

    if (address >= 0xFF40 && address <= 0xFF4B) {
        ppu_.write(address, value);
        return;
    }

    switch (address) {
    case 0xFF00:
        joypad_.write(value);
        return;
    case 0xFF04:
    case 0xFF05:
    case 0xFF06:
    case 0xFF07:
        timer_.write(address, value);
        return;
    case 0xFF0F:
        if_ = static_cast<u8>(value | 0xE0);
        return;
    case 0xFFFF:
        ie_ = value;
        return;
    default:
        break;
    }

    if (address >= 0xFF80 && address <= 0xFFFE) {
        hram_[address - 0xFF80] = value;
    }
}

void Bus::tick(u32 cycles) {
    timer_.tick(cycles);
    ppu_.tick(cycles, vram_, oam_);
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
    s.wram = wram_;
    s.oam = oam_;
    s.hram = hram_;
    s.timer = timer_.state();
    s.joypad = joypad_.state();
    s.ppu = ppu_.state();
    s.apu = apu_.state();
    s.ie = ie_;
    s.iflag = if_;
    return s;
}

void Bus::loadState(const State& s) {
    vram_ = s.vram;
    wram_ = s.wram;
    oam_ = s.oam;
    hram_ = s.hram;
    timer_.loadState(s.timer);
    joypad_.loadState(s.joypad);
    ppu_.loadState(s.ppu);
    apu_.loadState(s.apu);
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

void Bus::logRead(u16 address, u8 value) {
    readLog_[readLogHead_] = MemoryReadEvent{address, value};
    readLogHead_ = (readLogHead_ + 1) % readLog_.size();
    if (readLogCount_ < readLog_.size()) {
        ++readLogCount_;
    }
}

void Bus::doOamDma(u8 page) {
    const u16 source = static_cast<u16>(page) << 8;
    for (u16 i = 0; i < 0xA0; ++i) {
        oam_[i] = read(static_cast<u16>(source + i));
    }
}

} // namespace gb
