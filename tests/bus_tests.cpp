#include <filesystem>
#include <vector>

#include "gb/core/gameboy.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

namespace {

void loadBlankRom(gb::GameBoy& gb, tests::ScopedPath& cleanup, bool cgbMode = false) {
    tests::RomSpec spec{};
    spec.name = cgbMode ? "bus_cgb_rom" : "bus_dmg_rom";
    spec.program = {0x76};
    spec.cgbFlag = cgbMode ? 0x80 : 0x00;

    const auto path = tests::writeTempRom(spec);
    cleanup = tests::ScopedPath(path);
    T_REQUIRE(gb.loadRom(path.string()));
}

} // namespace

TEST_CASE("bus", "wram_and_echo_mirror") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xC123, 0x4A);
    T_EQ(gb.bus().read(0xE123), 0x4A);

    gb.bus().write(0xE456, 0x7B);
    T_EQ(gb.bus().read(0xC456), 0x7B);
}

TEST_CASE("bus", "unusable_range_reads_ff") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFEA0, 0x11);
    T_EQ(gb.bus().read(0xFEA0), 0xFF);
}

TEST_CASE("bus", "hram_read_write") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFF80, 0x99);
    T_EQ(gb.bus().read(0xFF80), 0x99);
}

TEST_CASE("bus", "interrupt_flag_masking_and_enable") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFF0F, 0x00);
    T_EQ(gb.bus().interruptFlags(), 0xE0);

    gb.bus().requestInterrupt(2);
    T_REQUIRE((gb.bus().interruptFlags() & (1 << 2)) != 0);

    gb.bus().setInterruptFlags(0x01);
    T_EQ(gb.bus().interruptFlags(), 0xE1);

    gb.bus().write(0xFFFF, 0x1F);
    T_EQ(gb.bus().interruptEnable(), 0x1F);
}

TEST_CASE("bus", "oam_dma_copies_160_bytes") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    for (int i = 0; i < 0xA0; ++i) {
        gb.bus().write(static_cast<gb::u16>(0xC000 + i), static_cast<gb::u8>(i));
    }

    gb.bus().write(0xFF46, 0xC0);

    T_EQ(gb.bus().read(0xFE00), 0x00);
    T_EQ(gb.bus().read(0xFE7F), 0x7F);
    T_EQ(gb.bus().read(0xFE9F), 0x9F);
}

TEST_CASE("bus", "timer_interrupt_propagates_to_if") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFF07, 0x05);
    gb.bus().write(0xFF06, 0xAB);
    gb.bus().write(0xFF05, 0xFF);

    gb.bus().tick(20);

    T_EQ(gb.bus().read(0xFF05), 0xAB);
    T_REQUIRE((gb.bus().interruptFlags() & (1 << 2)) != 0);
}

TEST_CASE("bus", "joypad_interrupt_propagates_to_if") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFF00, 0x10);
    gb.joypad().setButton(gb::Button::A, true);
    gb.bus().tick(4);

    T_REQUIRE((gb.bus().interruptFlags() & (1 << 4)) != 0);
    T_REQUIRE((gb.bus().read(0xFF00) & 0x01) == 0);
}

TEST_CASE("bus", "read_log_tracks_read_not_peek") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xC000, 0x12);
    (void)gb.bus().peek(0xC000);

    auto events = gb.bus().snapshotRecentReads(8);
    T_EQ(events.size(), static_cast<std::size_t>(0));

    (void)gb.bus().read(0xC000);
    events = gb.bus().snapshotRecentReads(8);

    T_EQ(events.size(), static_cast<std::size_t>(1));
    T_EQ(events[0].address, 0xC000);
    T_EQ(events[0].value, 0x12);
}

TEST_CASE("bus", "write_log_tracks_writes_newest_first") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xC000, 0x11);
    gb.bus().write(0xC001, 0x22);
    gb.bus().write(0xC002, 0x33);

    const auto events = gb.bus().snapshotRecentWrites(2);
    T_EQ(events.size(), static_cast<std::size_t>(2));
    T_EQ(events[0].address, 0xC002);
    T_EQ(events[0].value, 0x33);
    T_EQ(events[1].address, 0xC001);
    T_EQ(events[1].value, 0x22);
}

TEST_CASE("bus", "read_log_orders_newest_first_and_respects_limit") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xC000, 0x01);
    gb.bus().write(0xC001, 0x02);
    gb.bus().write(0xC002, 0x03);

    (void)gb.bus().read(0xC000);
    (void)gb.bus().read(0xC001);
    (void)gb.bus().read(0xC002);

    const auto events = gb.bus().snapshotRecentReads(2);
    T_EQ(events.size(), static_cast<std::size_t>(2));
    T_EQ(events[0].address, 0xC002);
    T_EQ(events[1].address, 0xC001);
}

TEST_CASE("bus", "state_roundtrip_restores_memory_and_interrupts") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xC000, 0xA1);
    gb.bus().write(0xFF80, 0xB2);
    gb.bus().write(0xFFFF, 0x1F);
    gb.bus().write(0xFF0F, 0x05);

    const auto saved = gb.bus().state();

    gb.bus().write(0xC000, 0x00);
    gb.bus().write(0xFF80, 0x00);
    gb.bus().write(0xFFFF, 0x00);
    gb.bus().write(0xFF0F, 0x00);

    gb.bus().loadState(saved);

    T_EQ(gb.bus().read(0xC000), 0xA1);
    T_EQ(gb.bus().read(0xFF80), 0xB2);
    T_EQ(gb.bus().interruptEnable(), 0x1F);
    T_EQ(gb.bus().interruptFlags(), 0xE5);
}

TEST_CASE("bus", "cgb_registers_hidden_in_dmg_mode") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    T_EQ(gb.bus().read(0xFF4F), 0xFF);
    T_EQ(gb.bus().read(0xFF70), 0xFF);
    T_EQ(gb.bus().read(0xFF69), 0xFF);

    gb.bus().write(0xFF4F, 0x01);
    gb.bus().write(0x8000, 0x55);
    T_EQ(gb.bus().read(0x8000), 0x55);
}

TEST_CASE("bus", "cgb_vram_bank_switching") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, true);

    gb.bus().write(0xFF4F, 0x00);
    gb.bus().write(0x8000, 0x11);

    gb.bus().write(0xFF4F, 0x01);
    gb.bus().write(0x8000, 0x22);
    T_EQ(gb.bus().read(0x8000), 0x22);

    gb.bus().write(0xFF4F, 0x00);
    T_EQ(gb.bus().read(0x8000), 0x11);
}

TEST_CASE("bus", "cgb_wram_bank_switching") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, true);

    gb.bus().write(0xFF70, 0x02);
    gb.bus().write(0xD000, 0x42);

    gb.bus().write(0xFF70, 0x03);
    gb.bus().write(0xD000, 0x84);

    gb.bus().write(0xFF70, 0x02);
    T_EQ(gb.bus().read(0xD000), 0x42);

    gb.bus().write(0xFF70, 0x03);
    T_EQ(gb.bus().read(0xD000), 0x84);

    gb.bus().write(0xFF70, 0x00);
    gb.bus().write(0xD000, 0x11);
    gb.bus().write(0xFF70, 0x01);
    T_EQ(gb.bus().read(0xD000), 0x11);
}

TEST_CASE("bus", "cgb_palette_data_auto_increment") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, true);

    gb.bus().write(0xFF68, 0x80);
    gb.bus().write(0xFF69, 0x12);
    gb.bus().write(0xFF69, 0x34);

    gb.bus().write(0xFF68, 0x00);
    T_EQ(gb.bus().read(0xFF69), 0x12);

    gb.bus().write(0xFF68, 0x01);
    T_EQ(gb.bus().read(0xFF69), 0x34);
}

TEST_CASE("bus", "cgb_key1_register_exposes_low_bit") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, true);

    T_EQ(gb.bus().read(0xFF4D) & 0x01, 0x00);
    gb.bus().write(0xFF4D, 0x01);
    T_EQ(gb.bus().read(0xFF4D) & 0x01, 0x01);
}

TEST_CASE("bus", "serial_transfer_request_and_completion_flow") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFF01, 0xA5);
    gb.bus().write(0xFF02, 0x81);

    gb::u8 outData = 0;
    T_REQUIRE(gb.bus().consumeSerialTransfer(outData));
    T_EQ(outData, 0xA5);
    T_REQUIRE(!gb.bus().consumeSerialTransfer(outData));

    gb.bus().completeSerialTransfer(0x5A);
    T_EQ(gb.bus().read(0xFF01), 0x5A);
    T_EQ(gb.bus().read(0xFF02) & 0x80, 0x00);
    T_REQUIRE((gb.bus().interruptFlags() & (1 << 3)) != 0);
}

TEST_CASE("bus", "serial_state_roundtrip_restores_pending_transfer") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFF01, 0xC3);
    gb.bus().write(0xFF02, 0x81);
    const auto saved = gb.bus().state();

    gb.bus().write(0xFF01, 0x00);
    gb.bus().write(0xFF02, 0x00);
    gb.bus().loadState(saved);

    gb::u8 outData = 0;
    T_REQUIRE(gb.bus().consumeSerialTransfer(outData));
    T_EQ(outData, 0xC3);
}

TEST_CASE("bus", "boot_rom_overrides_cartridge_until_ff50") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().setBootRomData(std::vector<gb::u8>{0xEA, 0x00, 0x00});
    T_REQUIRE(gb.bus().bootRomEnabled());
    T_EQ(gb.bus().read(0x0000), 0xEA);

    gb.bus().write(0xFF50, 0x01);
    T_REQUIRE(!gb.bus().bootRomEnabled());
    T_EQ(gb.bus().read(0x0000), 0x00);
}

TEST_CASE("bus", "ppu_register_passthrough") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, false);

    gb.bus().write(0xFF42, 0x77);
    gb.bus().write(0xFF43, 0x88);

    T_EQ(gb.bus().read(0xFF42), 0x77);
    T_EQ(gb.bus().read(0xFF43), 0x88);
}

TEST_CASE("bus", "sync_cartridge_mode_sets_cgb_palette_defaults") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, true);

    gb.bus().write(0xFF68, 0x00);
    T_EQ(gb.bus().read(0xFF69), 0xFF);

    gb.bus().write(0xFF6A, 0x00);
    T_EQ(gb.bus().read(0xFF6B), 0xFF);
}

TEST_CASE("bus", "cgb_hdma_general_transfer_copies_to_vram") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, true);

    for (int i = 0; i < 0x10; ++i) {
        gb.bus().write(static_cast<gb::u16>(0xC000 + i), static_cast<gb::u8>(0x80 + i));
    }

    gb.bus().write(0xFF51, 0xC0);
    gb.bus().write(0xFF52, 0x00);
    gb.bus().write(0xFF53, 0x00);
    gb.bus().write(0xFF54, 0x00);
    gb.bus().write(0xFF55, 0x00); // 1 bloco de 0x10 bytes

    for (int i = 0; i < 0x10; ++i) {
        T_EQ(gb.bus().read(static_cast<gb::u16>(0x8000 + i)), static_cast<gb::u8>(0x80 + i));
    }
    T_EQ(gb.bus().read(0xFF55), 0xFF);
}

TEST_CASE("bus", "double_speed_halves_peripheral_cycles") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup, true);

    T_EQ(gb.bus().peripheralCyclesFromCpuCycles(16), 16);

    gb.bus().write(0xFF4D, 0x01);
    T_REQUIRE(gb.bus().trySpeedSwitch());
    T_REQUIRE(gb.bus().isDoubleSpeed());
    T_EQ(gb.bus().peripheralCyclesFromCpuCycles(16), 8);
}
