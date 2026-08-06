#include <array>
#include <cstdint>

#include "gb/achievements/adapters/gameboy_memory_reader.hpp"
#include "gb/achievements/adapters/gba_memory_reader.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/core/gba/system.hpp"

#include "../../test_framework.hpp"

namespace {

using gb::achievements::adapters::makeGameBoyMemoryReader;
using gb::achievements::adapters::makeGbaMemoryReader;

TEST_CASE("achievements_memory_adapters", "reads_gameboy_bus_and_short_reads_invalid") {
    gb::GameBoy gameBoy;
    gameBoy.bus().write(0xC000U, 0x42U);
    const auto reader = makeGameBoyMemoryReader(gameBoy);
    std::array<std::uint8_t, 2U> bytes{};
    T_EQ(reader(0xC000U, bytes.data(), bytes.size()), 2U);
    T_EQ(bytes[0], 0x42U);
    T_EQ(bytes[1], 0x00U);

    T_EQ(reader(0x10000U, bytes.data(), bytes.size()), 0U);
}

TEST_CASE("achievements_memory_adapters", "reads_gba_ram_and_io_but_rejects_unmapped") {
    gb::gba::System system;
    system.memory().write8(0x02000000U, 0x11U);
    system.memory().write8(0x03000000U, 0x22U);
    system.memory().write8(0x04000000U, 0x33U);
    const auto reader = makeGbaMemoryReader(system);
    std::array<std::uint8_t, 1U> byte{};
    T_EQ(reader(0x02000000U, byte.data(), 1U), 1U);
    T_EQ(byte[0], 0x11U);
    T_EQ(reader(0x03000000U, byte.data(), 1U), 1U);
    T_EQ(byte[0], 0x22U);
    T_EQ(reader(0x04000000U, byte.data(), 1U), 1U);
    T_EQ(byte[0], 0x33U);
    T_EQ(reader(0x01000000U, byte.data(), 1U), 0U);
}

} // namespace
