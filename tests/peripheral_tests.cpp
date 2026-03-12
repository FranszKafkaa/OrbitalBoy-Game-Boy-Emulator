#include <array>
#include <cstdint>

#include "gb/core/apu.hpp"
#include "gb/core/joypad.hpp"
#include "gb/core/ppu.hpp"
#include "gb/core/timer.hpp"

#include "test_framework.hpp"

namespace {

struct PpuContext {
    std::array<gb::u8, 0x2000> vram0{};
    std::array<gb::u8, 0x2000> vram1{};
    std::array<gb::u8, 0xA0> oam{};
    std::array<gb::u8, 0x40> bgPalette{};
    std::array<gb::u8, 0x40> objPalette{};
};

void advanceOneLine(gb::PPU& ppu, PpuContext& ctx, bool cgbMode = false) {
    ppu.tick(80, ctx.vram0, ctx.vram1, ctx.oam, cgbMode, ctx.bgPalette, ctx.objPalette);
    ppu.tick(172, ctx.vram0, ctx.vram1, ctx.oam, cgbMode, ctx.bgPalette, ctx.objPalette);
    ppu.tick(204, ctx.vram0, ctx.vram1, ctx.oam, cgbMode, ctx.bgPalette, ctx.objPalette);
}

constexpr gb::u16 grayColorFromShade(gb::u8 shade) {
    const gb::u8 v = static_cast<gb::u8>(31 - (shade * 10));
    return static_cast<gb::u16>((v << 10) | (v << 5) | v);
}

} // namespace

TEST_CASE("timer", "div_increments_every_256_cycles") {
    gb::Timer timer;
    timer.tick(255);
    T_EQ(timer.read(0xFF04), 0x00);

    timer.tick(1);
    T_EQ(timer.read(0xFF04), 0x01);
}

TEST_CASE("timer", "write_div_resets_div_and_counter") {
    gb::Timer timer;
    timer.tick(600);
    T_REQUIRE(timer.read(0xFF04) > 0);

    timer.write(0xFF04, 0x00);
    T_EQ(timer.read(0xFF04), 0x00);
}

TEST_CASE("timer", "disabled_timer_does_not_increment_tima") {
    gb::Timer timer;
    timer.write(0xFF05, 0x10);
    timer.write(0xFF07, 0x00);
    timer.tick(4096);
    T_EQ(timer.read(0xFF05), 0x10);
}

TEST_CASE("timer", "tima_increments_for_all_frequencies") {
    struct Mode {
        gb::u8 tac;
        gb::u32 cycles;
    };

    const std::array<Mode, 4> modes = {{
        {0x04 | 0x00, 1024},
        {0x04 | 0x01, 16},
        {0x04 | 0x02, 64},
        {0x04 | 0x03, 256},
    }};

    for (const auto& mode : modes) {
        gb::Timer timer;
        timer.write(0xFF05, 0x00);
        timer.write(0xFF07, mode.tac);
        timer.tick(mode.cycles);
        T_EQ(timer.read(0xFF05), 0x01);
    }
}

TEST_CASE("timer", "overflow_loads_tma_and_requests_interrupt") {
    gb::Timer timer;
    timer.write(0xFF06, 0xAB);
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF07, 0x05);

    timer.tick(20);
    T_EQ(timer.read(0xFF05), 0xAB);
    T_REQUIRE(timer.consumeInterrupt());
    T_REQUIRE(!timer.consumeInterrupt());
}

TEST_CASE("timer", "overflow_reload_happens_after_4_cycles") {
    gb::Timer timer;
    timer.write(0xFF06, 0x66);
    timer.write(0xFF05, 0xFF);
    timer.write(0xFF07, 0x05);

    timer.tick(16);
    T_EQ(timer.read(0xFF05), 0x00);
    T_REQUIRE(!timer.consumeInterrupt());

    timer.tick(3);
    T_EQ(timer.read(0xFF05), 0x00);
    T_REQUIRE(!timer.consumeInterrupt());

    timer.tick(1);
    T_EQ(timer.read(0xFF05), 0x66);
    T_REQUIRE(timer.consumeInterrupt());
}

TEST_CASE("timer", "state_roundtrip") {
    gb::Timer timer;
    timer.write(0xFF06, 0x44);
    timer.write(0xFF05, 0xFE);
    timer.write(0xFF07, 0x05);
    timer.tick(32);

    const auto s = timer.state();

    gb::Timer other;
    other.loadState(s);
    T_EQ(other.read(0xFF06), timer.read(0xFF06));
    T_EQ(other.read(0xFF05), timer.read(0xFF05));
    T_EQ(other.read(0xFF07), timer.read(0xFF07));
}

TEST_CASE("joypad", "default_state_reads_all_released") {
    gb::Joypad joy;
    T_EQ(joy.read(), 0xFF);
}

TEST_CASE("joypad", "direction_selection_maps_low_bits") {
    gb::Joypad joy;
    joy.write(0x20);
    joy.setButton(gb::Button::Right, true);
    joy.setButton(gb::Button::Up, true);

    const gb::u8 value = joy.read();
    T_REQUIRE((value & 0x01) == 0);
    T_REQUIRE((value & 0x04) == 0);
}

TEST_CASE("joypad", "action_selection_maps_low_bits") {
    gb::Joypad joy;
    joy.write(0x10);
    joy.setButton(gb::Button::A, true);
    joy.setButton(gb::Button::Start, true);

    const gb::u8 value = joy.read();
    T_REQUIRE((value & 0x01) == 0);
    T_REQUIRE((value & 0x08) == 0);
}

TEST_CASE("joypad", "interrupt_only_on_press_edge") {
    gb::Joypad joy;
    joy.setButton(gb::Button::A, true);
    T_REQUIRE(joy.consumeInterrupt());
    T_REQUIRE(!joy.consumeInterrupt());

    joy.setButton(gb::Button::A, true);
    T_REQUIRE(!joy.consumeInterrupt());

    joy.setButton(gb::Button::A, false);
    joy.setButton(gb::Button::A, true);
    T_REQUIRE(joy.consumeInterrupt());
}

TEST_CASE("joypad", "state_roundtrip") {
    gb::Joypad joy;
    joy.write(0x10);
    joy.setButton(gb::Button::B, true);
    const auto s = joy.state();

    gb::Joypad other;
    other.loadState(s);
    T_EQ(other.read(), joy.read());
}

TEST_CASE("ppu", "register_read_write_roundtrip") {
    gb::PPU ppu;
    ppu.write(0xFF42, 0x12);
    ppu.write(0xFF43, 0x34);
    ppu.write(0xFF45, 0x56);

    T_EQ(ppu.read(0xFF42), 0x12);
    T_EQ(ppu.read(0xFF43), 0x34);
    T_EQ(ppu.read(0xFF45), 0x56);
}

TEST_CASE("ppu", "lcd_off_forces_mode0_and_ly0") {
    gb::PPU ppu;
    PpuContext ctx;

    ppu.write(0xFF40, 0x00);
    ppu.tick(200, ctx.vram0, ctx.vram1, ctx.oam, false, ctx.bgPalette, ctx.objPalette);

    T_EQ(ppu.read(0xFF44), 0x00);
    T_EQ(ppu.state().mode, 0x00);
}

TEST_CASE("ppu", "mode_progression_across_scanline") {
    gb::PPU ppu;
    PpuContext ctx;

    T_EQ(ppu.state().mode, 0x02);

    ppu.tick(79, ctx.vram0, ctx.vram1, ctx.oam, false, ctx.bgPalette, ctx.objPalette);
    T_EQ(ppu.state().mode, 0x02);

    ppu.tick(1, ctx.vram0, ctx.vram1, ctx.oam, false, ctx.bgPalette, ctx.objPalette);
    T_EQ(ppu.state().mode, 0x03);

    ppu.tick(172, ctx.vram0, ctx.vram1, ctx.oam, false, ctx.bgPalette, ctx.objPalette);
    T_EQ(ppu.state().mode, 0x00);

    ppu.tick(204, ctx.vram0, ctx.vram1, ctx.oam, false, ctx.bgPalette, ctx.objPalette);
    T_EQ(ppu.state().mode, 0x02);
    T_EQ(ppu.read(0xFF44), 0x01);
}

TEST_CASE("ppu", "vblank_interrupt_triggered_at_line_144") {
    gb::PPU ppu;
    PpuContext ctx;

    for (int line = 0; line < 144; ++line) {
        advanceOneLine(ppu, ctx, false);
    }

    T_EQ(ppu.read(0xFF44), 144);
    T_REQUIRE(ppu.consumeVBlankInterrupt());
    T_REQUIRE(!ppu.consumeVBlankInterrupt());
}

TEST_CASE("ppu", "lyc_interrupt_when_enabled") {
    gb::PPU ppu;
    PpuContext ctx;

    ppu.write(0xFF41, 0x40);
    ppu.write(0xFF45, 0x01);

    advanceOneLine(ppu, ctx, false);

    T_EQ(ppu.read(0xFF44), 0x01);
    T_REQUIRE((ppu.read(0xFF41) & 0x04) != 0);
    T_REQUIRE(ppu.consumeLcdInterrupt());
}

TEST_CASE("ppu", "renders_background_pixel_from_tile_data") {
    gb::PPU ppu;
    PpuContext ctx;

    ppu.write(0xFF47, 0xE4);

    ctx.vram0[0x1800] = 0x00;
    ctx.vram0[0x0000] = 0x80;
    ctx.vram0[0x0001] = 0x00;

    ppu.tick(80, ctx.vram0, ctx.vram1, ctx.oam, false, ctx.bgPalette, ctx.objPalette);
    ppu.tick(172, ctx.vram0, ctx.vram1, ctx.oam, false, ctx.bgPalette, ctx.objPalette);

    T_EQ(ppu.framebuffer()[0], 0x01);
    T_EQ(ppu.colorFramebuffer()[0], grayColorFromShade(0x01));
}

TEST_CASE("ppu", "state_roundtrip_restores_registers_and_framebuffer") {
    gb::PPU ppu;
    auto s = ppu.state();
    s.ly = 7;
    s.framebuffer[0] = 2;

    ppu.loadState(s);

    T_EQ(ppu.read(0xFF44), 0x07);
    T_EQ(ppu.framebuffer()[0], 0x02);
    T_EQ(ppu.colorFramebuffer()[0], grayColorFromShade(0x02));
}

TEST_CASE("apu", "master_off_resets_and_blocks_channel_writes") {
    gb::APU apu;

    apu.write(0xFF24, 0x77);
    apu.write(0xFF25, 0xF3);
    apu.write(0xFF26, 0x00);

    T_EQ(apu.read(0xFF24), 0x00);
    T_EQ(apu.read(0xFF25), 0x00);
    T_EQ(apu.read(0xFF26) & 0x80, 0x00);

    apu.write(0xFF12, 0xF3);
    T_EQ(apu.read(0xFF12), 0x00);

    apu.write(0xFF30, 0xAB);
    T_EQ(apu.read(0xFF30), 0xAB);
}

TEST_CASE("apu", "trigger_square_generates_samples") {
    gb::APU apu;

    apu.write(0xFF26, 0x80);
    apu.write(0xFF24, 0x77);
    apu.write(0xFF25, 0x11);
    apu.write(0xFF11, 0x80);
    apu.write(0xFF12, 0xF3);
    apu.write(0xFF13, 0x00);
    apu.write(0xFF14, 0x80);

    apu.tick(20000);
    const auto samples = apu.takeSamples();

    T_REQUIRE(!samples.empty());
    T_EQ(samples.size() % 2, static_cast<std::size_t>(0));
}

TEST_CASE("apu", "trigger_noise_generates_samples") {
    gb::APU apu;

    apu.write(0xFF26, 0x80);
    apu.write(0xFF24, 0x77);
    apu.write(0xFF25, 0x88);
    apu.write(0xFF21, 0xF3);
    apu.write(0xFF22, 0x05);
    apu.write(0xFF23, 0x80);

    const gb::u8 statusBefore = apu.read(0xFF26);
    T_REQUIRE((statusBefore & 0x08) != 0);

    apu.tick(20000);
    const auto samples = apu.takeSamples();

    T_REQUIRE(!samples.empty());
    T_EQ(samples.size() % 2, static_cast<std::size_t>(0));
}

TEST_CASE("apu", "take_samples_clears_internal_buffer") {
    gb::APU apu;

    apu.write(0xFF26, 0x80);
    apu.write(0xFF24, 0x77);
    apu.write(0xFF25, 0x11);
    apu.write(0xFF11, 0x80);
    apu.write(0xFF12, 0xF3);
    apu.write(0xFF14, 0x80);
    apu.tick(10000);

    const auto first = apu.takeSamples();
    const auto second = apu.takeSamples();

    T_REQUIRE(!first.empty());
    T_REQUIRE(second.empty());
}

TEST_CASE("apu", "nr52_reports_enabled_channels") {
    gb::APU apu;

    apu.write(0xFF26, 0x80);

    apu.write(0xFF11, 0x80);
    apu.write(0xFF12, 0xF3);
    apu.write(0xFF14, 0x80);

    apu.write(0xFF16, 0x80);
    apu.write(0xFF17, 0xF3);
    apu.write(0xFF19, 0x80);

    apu.write(0xFF1A, 0x80);
    apu.write(0xFF1C, 0x20);
    apu.write(0xFF1E, 0x80);

    apu.write(0xFF21, 0xF3);
    apu.write(0xFF22, 0x04);
    apu.write(0xFF23, 0x80);

    const gb::u8 status = apu.read(0xFF26);
    T_REQUIRE((status & 0x01) != 0);
    T_REQUIRE((status & 0x02) != 0);
    T_REQUIRE((status & 0x04) != 0);
    T_REQUIRE((status & 0x08) != 0);
}

TEST_CASE("apu", "state_roundtrip_preserves_registers_and_wave_ram") {
    gb::APU apu;

    apu.write(0xFF26, 0x80);
    apu.write(0xFF24, 0x45);
    apu.write(0xFF25, 0x93);
    apu.write(0xFF30, 0x12);
    apu.write(0xFF31, 0x34);
    apu.write(0xFF21, 0xA2);
    apu.write(0xFF22, 0x67);

    const auto s = apu.state();

    gb::APU other;
    other.loadState(s);

    T_EQ(other.read(0xFF24), 0x45);
    T_EQ(other.read(0xFF25), 0x93);
    T_EQ(other.read(0xFF30), 0x12);
    T_EQ(other.read(0xFF31), 0x34);
    T_EQ(other.read(0xFF21), 0xA2);
    T_EQ(other.read(0xFF22), 0x67);
}
