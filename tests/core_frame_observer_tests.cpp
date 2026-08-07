#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "gb/core/gameboy.hpp"
#include "gb/core/gba/system.hpp"

#include "test_framework.hpp"

namespace {

TEST_CASE("core_frame_observer", "gameboy_calls_once_and_replacement_wins") {
    gb::GameBoy gameBoy;
    int first = 0;
    int second = 0;
    gameBoy.setFrameObserver([&first] { ++first; });
    gameBoy.runFrame();
    gameBoy.setFrameObserver([&second] { ++second; });
    gameBoy.runFrame();
    T_EQ(first, 1);
    T_EQ(second, 1);
}

TEST_CASE("core_frame_observer", "gba_calls_once_after_loaded_frame") {
    const std::string path = "/tmp/orbital_phase4c_observer.gba";
    std::vector<std::uint8_t> rom(0x200U, 0U);
    rom[0xB2U] = 0x96U;
    for (std::size_t offset = 0U; offset + 4U <= rom.size(); offset += 4U) {
        rom[offset] = 0x00U;
        rom[offset + 1U] = 0x00U;
        rom[offset + 2U] = 0xA0U;
        rom[offset + 3U] = 0xE1U;
    }
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    }
    gb::gba::System system;
    T_REQUIRE(system.loadRomFromFile(path));
    int calls = 0;
    system.setFrameObserver([&calls] { ++calls; });
    system.runFrame(false);
    T_EQ(calls, 1);
    std::remove(path.c_str());
}

} // namespace
