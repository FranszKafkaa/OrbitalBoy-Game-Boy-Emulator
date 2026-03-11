#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gb/core/gameboy.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

namespace {

constexpr gb::u8 Z = 0x80;
constexpr gb::u8 N = 0x40;
constexpr gb::u8 H = 0x20;
constexpr gb::u8 C = 0x10;

void loadOrThrow(gb::GameBoy& gb, const std::filesystem::path& romPath) {
    T_REQUIRE(gb.loadRom(romPath.string()));
}

} // namespace

TEST_CASE("cpu", "register_helpers_roundtrip") {
    gb::Registers r{};

    r.setAf(0xABCD);
    T_EQ(r.a, 0xAB);
    T_EQ(r.f, 0xC0);
    T_EQ(r.af(), 0xABC0);

    r.setBc(0x1234);
    r.setDe(0x5678);
    r.setHl(0x9ABC);

    T_EQ(r.bc(), 0x1234);
    T_EQ(r.de(), 0x5678);
    T_EQ(r.hl(), 0x9ABC);
}

TEST_CASE("cpu", "daa_adjusts_bcd") {
    tests::RomSpec rom{};
    rom.name = "cpu_daa";
    rom.program = {
        0x3E, 0x15,
        0xC6, 0x27,
        0x27,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.a, 0x42);
    T_EQ(regs.f & (Z | N | H | C), 0x00);
}

TEST_CASE("cpu", "cb_rotate_bit_set_res") {
    tests::RomSpec rom{};
    rom.name = "cpu_cb_ops";
    rom.program = {
        0x06, 0x01,
        0xCB, 0x00,
        0xCB, 0x78,
        0xCB, 0xC0,
        0xCB, 0x80,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.b, 0x02);
    T_REQUIRE((regs.f & Z) != 0);
}

TEST_CASE("cpu", "dma_copies_oam") {
    tests::RomSpec rom{};
    rom.name = "cpu_dma";
    rom.program = {
        0x3E, 0x12,
        0xEA, 0x00, 0xC0,
        0x3E, 0x34,
        0xEA, 0x01, 0xC0,
        0x3E, 0xC0,
        0xE0, 0x46,
        0xFA, 0x00, 0xFE,
        0x47,
        0xFA, 0x01, 0xFE,
        0x4F,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.b, 0x12);
    T_EQ(regs.c, 0x34);
}

TEST_CASE("cpu", "call_and_ret") {
    tests::RomSpec rom{};
    rom.name = "cpu_call_ret";
    rom.program.assign(0x40, 0x00);

    rom.program[0x00] = 0xCD;
    rom.program[0x01] = 0x10;
    rom.program[0x02] = 0x01;
    rom.program[0x03] = 0x76;

    rom.program[0x10] = 0x3E;
    rom.program[0x11] = 0x99;
    rom.program[0x12] = 0xC9;

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);
    T_EQ(gb.cpu().regs().a, 0x99);
}

TEST_CASE("cpu", "jr_taken_and_not_taken") {
    tests::RomSpec rom{};
    rom.name = "cpu_jr_conditions";
    rom.program = {
        0xAF,
        0x20, 0x02,
        0x06, 0x11,
        0x28, 0x02,
        0x06, 0x22,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    T_EQ(gb.cpu().regs().b, 0x11);
}

TEST_CASE("cpu", "adc_and_sbc_with_carry") {
    tests::RomSpec rom{};
    rom.name = "cpu_adc_sbc";
    rom.program = {
        0x3E, 0xFE,
        0x37,
        0xCE, 0x01,
        0xDE, 0x00,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.a, 0xFF);
    T_REQUIRE((regs.f & N) != 0);
    T_REQUIRE((regs.f & H) != 0);
    T_REQUIRE((regs.f & C) != 0);
}

TEST_CASE("cpu", "alu_logic_ops_and_cp_flags") {
    tests::RomSpec rom{};
    rom.name = "cpu_logic_cp";
    rom.program = {
        0x3E, 0xF0,
        0xE6, 0x0F,
        0xF6, 0x01,
        0xEE, 0x01,
        0xFE, 0x00,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.a, 0x00);
    T_REQUIRE((regs.f & Z) != 0);
    T_REQUIRE((regs.f & N) != 0);
    T_REQUIRE((regs.f & C) == 0);
}

TEST_CASE("cpu", "inc_dec_memory_updates_flags") {
    tests::RomSpec rom{};
    rom.name = "cpu_inc_dec_mem";
    rom.program = {
        0x21, 0x00, 0xC0,
        0x36, 0x0F,
        0x34,
        0x35,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(gb.bus().peek(0xC000), 0x0F);
    T_REQUIRE((regs.f & N) != 0);
    T_REQUIRE((regs.f & H) != 0);
    T_REQUIRE((regs.f & Z) == 0);
}

TEST_CASE("cpu", "hli_hld_indirect_load_store") {
    tests::RomSpec rom{};
    rom.name = "cpu_hli_hld";
    rom.program = {
        0x21, 0x00, 0xC0,
        0x3E, 0x12,
        0x22,
        0x3E, 0x34,
        0x32,
        0x2A,
        0x47,
        0x3A,
        0x4F,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.b, 0x12);
    T_EQ(regs.c, 0x34);
    T_EQ(regs.hl(), 0xC000);
    T_EQ(gb.bus().peek(0xC000), 0x12);
    T_EQ(gb.bus().peek(0xC001), 0x34);
}

TEST_CASE("cpu", "push_pop_register_pairs") {
    tests::RomSpec rom{};
    rom.name = "cpu_push_pop";
    rom.program = {
        0x01, 0x34, 0x12,
        0x11, 0x00, 0x00,
        0xC5,
        0xD1,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    T_EQ(gb.cpu().regs().de(), 0x1234);
}

TEST_CASE("cpu", "push_pop_af_masks_low_flags_nibble") {
    tests::RomSpec rom{};
    rom.name = "cpu_pop_af_mask";
    rom.program = {
        0x3E, 0xFF,
        0x37,
        0xF5,
        0xAF,
        0xF1,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.a, 0xFF);
    T_EQ(regs.f & 0x0F, 0x00);
}

TEST_CASE("cpu", "ldh_and_ff00_plus_c_access") {
    tests::RomSpec rom{};
    rom.name = "cpu_ldh_io";
    rom.program = {
        0x3E, 0x5A,
        0xE0, 0x80,
        0x3E, 0x00,
        0xF0, 0x80,
        0x0E, 0x81,
        0xE2,
        0x3E, 0x00,
        0xF2,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    T_EQ(gb.cpu().regs().a, 0x5A);
    T_EQ(gb.bus().peek(0xFF80), 0x5A);
    T_EQ(gb.bus().peek(0xFF81), 0x5A);
}

TEST_CASE("cpu", "add_sp_signed_and_ld_hl_sp_plus_signed") {
    tests::RomSpec rom{};
    rom.name = "cpu_add_sp_signed";
    rom.program = {
        0x31, 0xF8, 0xFF,
        0xE8, 0x08,
        0xF8, 0xFF,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const auto& regs = gb.cpu().regs();
    T_EQ(regs.sp, 0x0000);
    T_EQ(regs.hl(), 0xFFFF);
    T_EQ(regs.f & (Z | N | H | C), 0x00);
}

TEST_CASE("cpu", "rst_pushes_return_address") {
    tests::RomSpec rom{};
    rom.name = "cpu_rst";
    rom.program = {
        0xFF,
        0x76,
    };
    rom.patches.push_back(tests::RomPatch{0x0038, {0x3E, 0x77, 0xC9}});

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    T_EQ(gb.cpu().regs().a, 0x77);
    T_EQ(gb.cpu().regs().sp, 0xFFFE);
}

TEST_CASE("cpu", "interrupt_service_after_ei") {
    tests::RomSpec rom{};
    rom.name = "cpu_irq_ei";
    rom.program = {
        0xFB,
        0x00,
        0x76,
    };
    rom.patches.push_back(tests::RomPatch{0x0040, {0x3E, 0x66, 0xD9}});

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);

    gb.bus().write(0xFFFF, 0x01);
    gb.bus().write(0xFF0F, 0x01);

    tests::runUntilHalt(gb);

    T_EQ(gb.cpu().regs().a, 0x66);
    T_REQUIRE((gb.bus().interruptFlags() & 0x01) == 0);
    T_REQUIRE(gb.cpu().state().ime);
}

TEST_CASE("cpu", "halted_cpu_wakes_on_pending_interrupt_with_ime_off") {
    tests::RomSpec rom{};
    rom.name = "cpu_halt_wakeup";
    rom.program = {
        0x76,
        0x3C,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);

    gb.bus().write(0xFFFF, 0x01);
    gb.bus().write(0xFF0F, 0x00);

    gb.step();
    T_REQUIRE(gb.cpu().isHalted());

    gb.bus().write(0xFF0F, 0x01);
    gb.step();
    T_EQ(gb.cpu().regs().a, 0x02);

    gb.bus().write(0xFF0F, 0x00);
    gb.step();
    T_REQUIRE(gb.cpu().isHalted());
}

TEST_CASE("cpu", "hardware_mode_sets_initial_a_for_cgb_rom") {
    tests::RomSpec rom{};
    rom.name = "cpu_cgb_a";
    rom.cgbFlag = 0x80;
    rom.program = {0x76};

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    T_EQ(gb.cpu().regs().a, 0x11);
}

TEST_CASE("cpu", "last_executed_pc_and_opcode") {
    tests::RomSpec rom{};
    rom.name = "cpu_last_opcode";
    rom.program = {
        0x00,
        0x76,
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);

    gb.step();
    T_EQ(gb.cpu().lastExecutedPc(), 0x0100);
    T_EQ(gb.cpu().lastExecutedOpcode(), 0x00);

    gb.step();
    T_EQ(gb.cpu().lastExecutedPc(), 0x0101);
    T_EQ(gb.cpu().lastExecutedOpcode(), 0x76);
}

TEST_CASE("cpu", "halt_bug_repeats_next_opcode_when_interrupt_pending_with_ime_off") {
    tests::RomSpec rom{};
    rom.name = "cpu_halt_bug";
    rom.program = {
        0xF3,       // DI
        0x76,       // HALT (com interrupcao pendente -> HALT bug)
        0x04,       // INC B (deve executar duas vezes)
        0x3E, 0x00, // LD A,0
        0xE0, 0x0F, // LDH (IF),A
        0x76,       // HALT normal
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);

    gb.bus().write(0xFFFF, 0x01);
    gb.bus().write(0xFF0F, 0x01);

    gb.step(); // DI
    tests::runUntilHalt(gb);

    T_EQ(gb.cpu().regs().b, 0x02);
}

TEST_CASE("cpu", "cgb_stop_switches_double_speed_when_key1_armed") {
    tests::RomSpec rom{};
    rom.name = "cpu_cgb_stop_speed";
    rom.cgbFlag = 0x80;
    rom.program = {
        0x3E, 0x01, // LD A,1
        0xE0, 0x4D, // LDH (KEY1),A
        0x10, 0x00, // STOP
        0xF0, 0x4D, // LDH A,(KEY1)
        0x47,       // LD B,A
        0x76,       // HALT
    };

    const auto path = tests::writeTempRom(rom);
    tests::ScopedPath cleanup(path);

    gb::GameBoy gb;
    loadOrThrow(gb, path);
    tests::runUntilHalt(gb);

    const gb::u8 key1 = gb.cpu().regs().b;
    T_REQUIRE((key1 & 0x80) != 0); // double-speed ativo
    T_REQUIRE((key1 & 0x01) == 0); // arm bit limpo apos STOP
    T_REQUIRE(gb.bus().isDoubleSpeed());
}
