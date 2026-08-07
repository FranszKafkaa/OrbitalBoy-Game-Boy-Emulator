#include "gb/core/gameboy.hpp"

#include <fstream>
#include <iterator>
#include <vector>

namespace gb {

bool GameBoy::loadRom(const std::string& path) {
    if (!cartridge_.loadFromFile(path)) {
        return false;
    }
    setHardwareMode(cartridge_.shouldRunInCgbMode());
    return true;
}

void GameBoy::setHardwareMode(bool cgbMode) {
    bus_.setHardwareMode(cgbMode);
    cpu_.setHardwareMode(cgbMode);
}

bool GameBoy::runningInCgbMode() const {
    return bus_.cgbMode();
}

bool GameBoy::loadBootRomFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    const auto begin = std::istreambuf_iterator<char>(in);
    const auto end = std::istreambuf_iterator<char>();
    std::vector<u8> data{begin, end};
    if (data.empty()) {
        return false;
    }
    if (data.size() > 0x1000) {
        data.resize(0x1000);
    }
    bus_.setBootRomData(data);
    return true;
}

void GameBoy::clearBootRom() {
    bus_.clearBootRom();
}

void GameBoy::setPreciseTiming(bool enabled) {
    preciseTiming_ = enabled;
}

bool GameBoy::preciseTiming() const {
    return preciseTiming_;
}

u32 GameBoy::step() {
    const u32 cpuCycles = cpu_.step();
    bus_.tick(bus_.peripheralCyclesFromCpuCycles(cpuCycles));
    return cpuCycles;
}

void GameBoy::runFrame() {
    if (preciseTiming_) {
        bool seenVblank = false;
        u32 elapsed = 0;
        // Guarda contra travamento se o jogo desligar LCD (LY pode parar de variar).
        constexpr u32 maxGuardCycles = 70224 * 3;
        while (elapsed < maxGuardCycles) {
            elapsed += step();
            const u8 ly = bus_.peek(0xFF44);
            if (!seenVblank && ly >= 144) {
                seenVblank = true;
            } else if (seenVblank && ly < 144) {
                break;
            }
        }
        return;
    }

    constexpr u32 frameCycles = 70224;

    u32 elapsed = 0;
    while (elapsed < frameCycles) {
        elapsed += step();
    }
}

const Cartridge& GameBoy::cartridge() const {
    return cartridge_;
}

CPU& GameBoy::cpu() {
    return cpu_;
}

const CPU& GameBoy::cpu() const {
    return cpu_;
}

const PPU& GameBoy::ppu() const {
    return bus_.ppu();
}

Bus& GameBoy::bus() {
    return bus_;
}

const Bus& GameBoy::bus() const {
    return bus_;
}

Joypad& GameBoy::joypad() {
    return bus_.joypad();
}

APU& GameBoy::apu() {
    return bus_.apu();
}

GameBoy::SaveState GameBoy::saveState() const {
    SaveState s{};
    s.cartridge = cartridge_.state();
    s.bus = bus_.state();
    s.cpu = cpu_.state();
    return s;
}

void GameBoy::loadState(const SaveState& s) {
    cartridge_.loadState(s.cartridge);
    bus_.loadState(s.bus);
    cpu_.loadState(s.cpu);
}

bool GameBoy::loadBatteryRamFromFile(const std::string& path) {
    if (!cartridge_.hasRam()) {
        return false;
    }
    return cartridge_.loadRamFromFile(path);
}

bool GameBoy::saveBatteryRamToFile(const std::string& path) const {
    if (!cartridge_.hasRam()) {
        return false;
    }
    return cartridge_.saveRamToFile(path);
}

bool GameBoy::loadRtcFromFile(const std::string& path) {
    if (!cartridge_.hasRtc()) {
        return false;
    }
    return cartridge_.loadRtcFromFile(path);
}

bool GameBoy::saveRtcToFile(const std::string& path) const {
    if (!cartridge_.hasRtc()) {
        return false;
    }
    return cartridge_.saveRtcToFile(path);
}

} // namespace gb
