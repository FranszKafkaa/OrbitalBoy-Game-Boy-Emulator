#pragma once

#include <string>

#include "gb/core/bus.hpp"
#include "gb/core/cartridge.hpp"
#include "gb/core/cpu.hpp"

namespace gb {

class GameBoy {
public:
    struct SaveState {
        Cartridge::State cartridge{};
        Bus::State bus{};
        CPU::State cpu{};
    };

    bool loadRom(const std::string& path);
    bool loadBootRomFromFile(const std::string& path);
    void clearBootRom();
    void setPreciseTiming(bool enabled);
    [[nodiscard]] bool preciseTiming() const;

    u32 step();
    void runFrame();

    [[nodiscard]] const Cartridge& cartridge() const;
    [[nodiscard]] const CPU& cpu() const;
    [[nodiscard]] const PPU& ppu() const;
    [[nodiscard]] Bus& bus();
    [[nodiscard]] const Bus& bus() const;
    [[nodiscard]] Joypad& joypad();
    [[nodiscard]] APU& apu();
    [[nodiscard]] SaveState saveState() const;
    void loadState(const SaveState& state);
    bool saveStateToFile(const std::string& path) const;
    bool loadStateFromFile(const std::string& path);
    bool loadBatteryRamFromFile(const std::string& path);
    bool saveBatteryRamToFile(const std::string& path) const;
    bool loadRtcFromFile(const std::string& path);
    bool saveRtcToFile(const std::string& path) const;

private:
    Cartridge cartridge_;
    Bus bus_{cartridge_};
    CPU cpu_{bus_};
    bool preciseTiming_ = false;
};

} // namespace gb
