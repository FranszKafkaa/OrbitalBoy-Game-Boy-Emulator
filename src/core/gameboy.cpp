#include "gb/core/gameboy.hpp"

namespace gb {

bool GameBoy::loadRom(const std::string& path) {
    if (!cartridge_.loadFromFile(path)) {
        return false;
    }
    bus_.syncCartridgeMode();
    cpu_.setHardwareMode(cartridge_.shouldRunInCgbMode());
    return true;
}

u32 GameBoy::step() {
    const u32 cpuCycles = cpu_.step();
    bus_.tick(bus_.peripheralCyclesFromCpuCycles(cpuCycles));
    return cpuCycles;
}

void GameBoy::runFrame() {
    constexpr u32 frameCycles = 70224;

    u32 elapsed = 0;
    while (elapsed < frameCycles) {
        elapsed += step();
    }
}

const Cartridge& GameBoy::cartridge() const {
    return cartridge_;
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
