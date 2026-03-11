#pragma once

#include <string>

#include "gb/bus.hpp"
#include "gb/cartridge.hpp"
#include "gb/cpu.hpp"

namespace gb {

class GameBoy {
public:
    struct SaveState {
        Cartridge::State cartridge{};
        Bus::State bus{};
        CPU::State cpu{};
    };

    bool loadRom(const std::string& path);

    u32 step();
    void runFrame();

    [[nodiscard]] const Cartridge& cartridge() const;
    [[nodiscard]] const CPU& cpu() const;
    [[nodiscard]] const PPU& ppu() const;
    [[nodiscard]] const Bus& bus() const;
    [[nodiscard]] Joypad& joypad();
    [[nodiscard]] APU& apu();
    [[nodiscard]] SaveState saveState() const;
    void loadState(const SaveState& state);
    bool saveStateToFile(const std::string& path) const;
    bool loadStateFromFile(const std::string& path);

private:
    Cartridge cartridge_;
    Bus bus_{cartridge_};
    CPU cpu_{bus_};
};

} // namespace gb
