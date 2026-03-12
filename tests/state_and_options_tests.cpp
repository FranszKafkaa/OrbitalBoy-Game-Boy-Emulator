#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <string>
#include <vector>

#include "gb/app/app_options.hpp"
#include "gb/app/rom_suite_runner.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/core/gba/system.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

namespace {

bool parseArgs(const std::vector<std::string>& args, gb::AppOptions& options, std::string& error) {
    std::vector<std::string> storage = args;
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& s : storage) {
        argv.push_back(s.data());
    }
    return gb::parseAppOptions(static_cast<int>(argv.size()), argv.data(), options, error);
}

void loadRomOrThrow(gb::GameBoy& gb, const tests::RomSpec& spec, tests::ScopedPath& cleanup) {
    const auto romPath = tests::writeTempRom(spec);
    cleanup = tests::ScopedPath(romPath);
    T_REQUIRE(gb.loadRom(romPath.string()));
}

std::vector<gb::u8> buildGbaTestRomImage(const std::string& title, const std::string& gameCode, const std::string& makerCode) {
    std::vector<gb::u8> rom(0x200, 0x00);
    const std::array<gb::u8, 156> logo = {
        0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A,
        0x84, 0xE4, 0x09, 0xAD, 0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21,
        0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20, 0x10, 0x46, 0x4A, 0x4A,
        0xF8, 0x27, 0x31, 0xEC, 0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF,
        0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0xC1, 0x94, 0x56, 0x8A, 0xC0,
        0x13, 0x72, 0xA7, 0xFC, 0x9F, 0x84, 0x4D, 0x73, 0xA3, 0xCA, 0x9A, 0x61,
        0x58, 0x97, 0xA3, 0x27, 0xFC, 0x03, 0x98, 0x76, 0x23, 0x1D, 0xC7, 0x61,
        0x03, 0x04, 0xAE, 0x56, 0xBF, 0x38, 0x84, 0x00, 0x40, 0xA7, 0x0E, 0xFD,
        0xFF, 0x52, 0xFE, 0x03, 0x6F, 0x95, 0x30, 0xF1, 0x97, 0xFB, 0xC0, 0x85,
        0x60, 0xD6, 0x80, 0x25, 0xA9, 0x63, 0xBE, 0x03, 0x01, 0x4E, 0x38, 0xE2,
        0xF9, 0xA2, 0x34, 0xFF, 0xBB, 0x3E, 0x03, 0x44, 0x78, 0x00, 0x90, 0xCB,
        0x88, 0x11, 0x3A, 0x94, 0x65, 0xC0, 0x7C, 0x63, 0x87, 0xF0, 0x3C, 0xAF,
        0xD6, 0x25, 0xE4, 0x8B, 0x38, 0x0A, 0xAC, 0x72, 0x21, 0xD4, 0xF8, 0x07,
    };

    for (std::size_t i = 0; i < logo.size(); ++i) {
        rom[0x04 + i] = logo[i];
    }

    for (std::size_t i = 0; i < title.size() && i < 12; ++i) {
        rom[0xA0 + i] = static_cast<gb::u8>(title[i]);
    }
    for (std::size_t i = 0; i < gameCode.size() && i < 4; ++i) {
        rom[0xAC + i] = static_cast<gb::u8>(gameCode[i]);
    }
    for (std::size_t i = 0; i < makerCode.size() && i < 2; ++i) {
        rom[0xB0 + i] = static_cast<gb::u8>(makerCode[i]);
    }
    rom[0xB2] = 0x96;
    rom[0xB3] = 0x00;
    rom[0xB4] = 0x00;
    rom[0xBC] = 0x00;

    gb::u8 sum = 0;
    for (std::size_t i = 0xA0; i <= 0xBC; ++i) {
        sum = static_cast<gb::u8>(sum + rom[i]);
    }
    rom[0xBD] = static_cast<gb::u8>(0U - static_cast<gb::u8>(0x19U + sum));
    return rom;
}

} // namespace

TEST_CASE("state", "load_rom_invalid_path_fails") {
    gb::GameBoy gb;
    T_REQUIRE(!gb.loadRom("/tmp/definitely_missing_rom_987654.gb"));
}

TEST_CASE("state", "savestate_roundtrip_in_memory") {
    tests::RomSpec spec{};
    spec.name = "state_mem_roundtrip";
    spec.program = {
        0x3E, 0x42,
        0xEA, 0x00, 0xC0,
        0x76,
    };

    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadRomOrThrow(gb, spec, cleanup);

    tests::runUntilHalt(gb);
    const auto saved = gb.saveState();

    auto mutated = saved;
    mutated.cpu.regs.a = 0x11;
    mutated.bus.wram[0] = 0x00;
    gb.loadState(mutated);

    T_EQ(gb.cpu().regs().a, 0x11);
    T_EQ(gb.bus().peek(0xC000), 0x00);

    gb.loadState(saved);
    T_EQ(gb.cpu().regs().a, 0x42);
    T_EQ(gb.bus().peek(0xC000), 0x42);
}

TEST_CASE("state", "savestate_file_roundtrip") {
    tests::RomSpec spec{};
    spec.name = "state_file_roundtrip";
    spec.program = {
        0x3E, 0x7A,
        0xEA, 0x00, 0xC0,
        0x76,
    };

    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadRomOrThrow(gb, spec, cleanup);
    tests::runUntilHalt(gb);

    const auto statePath = tests::makeTempPath("gb_state", ".bin");
    tests::ScopedPath cleanupState(statePath);

    T_REQUIRE(gb.saveStateToFile(statePath.string()));

    gb.bus().write(0xC000, 0x00);
    auto changed = gb.saveState();
    changed.cpu.regs.a = 0x00;
    gb.loadState(changed);

    T_REQUIRE(gb.loadStateFromFile(statePath.string()));
    T_EQ(gb.cpu().regs().a, 0x7A);
    T_EQ(gb.bus().peek(0xC000), 0x7A);
}

TEST_CASE("state", "load_state_file_rejects_invalid_header") {
    tests::RomSpec spec{};
    spec.name = "state_invalid_header";
    spec.program = {0x76};

    gb::GameBoy gb;
    tests::ScopedPath cleanupRom;
    loadRomOrThrow(gb, spec, cleanupRom);

    const auto path = tests::makeTempPath("gb_state_bad", ".bin");
    tests::ScopedPath cleanupState(path);
    T_REQUIRE(tests::writeBinaryFile(path, {0x00, 0x11, 0x22, 0x33}));

    T_REQUIRE(!gb.loadStateFromFile(path.string()));
}

TEST_CASE("state", "load_state_file_missing_returns_false") {
    tests::RomSpec spec{};
    spec.name = "state_missing_file";
    spec.program = {0x76};

    gb::GameBoy gb;
    tests::ScopedPath cleanupRom;
    loadRomOrThrow(gb, spec, cleanupRom);

    T_REQUIRE(!gb.loadStateFromFile("/tmp/gb_state_missing_abcdef.bin"));
}

TEST_CASE("state", "battery_ram_roundtrip_file") {
    tests::RomSpec spec{};
    spec.name = "battery_ram_roundtrip";
    spec.cartridgeType = 0x08;
    spec.ramCode = 0x02;
    spec.program = {
        0x3E, 0x5B,
        0xEA, 0x00, 0xA0,
        0x76,
    };

    gb::GameBoy writer;
    tests::ScopedPath cleanupWriter;
    loadRomOrThrow(writer, spec, cleanupWriter);
    tests::runUntilHalt(writer);

    const auto ramPath = tests::makeTempPath("battery_ram", ".sav");
    tests::ScopedPath cleanupRam(ramPath);
    T_REQUIRE(writer.saveBatteryRamToFile(ramPath.string()));

    gb::GameBoy reader;
    tests::ScopedPath cleanupReader;
    loadRomOrThrow(reader, spec, cleanupReader);
    T_REQUIRE(reader.loadBatteryRamFromFile(ramPath.string()));
    T_EQ(reader.bus().peek(0xA000), 0x5B);
}

TEST_CASE("state", "battery_ram_ops_fail_without_ram") {
    tests::RomSpec spec{};
    spec.name = "battery_no_ram";
    spec.cartridgeType = 0x00;
    spec.ramCode = 0x00;
    spec.program = {0x76};

    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadRomOrThrow(gb, spec, cleanup);

    const auto ramPath = tests::makeTempPath("battery_no_ram", ".sav");
    tests::ScopedPath cleanupRam(ramPath);

    T_REQUIRE(!gb.saveBatteryRamToFile(ramPath.string()));
    T_REQUIRE(!gb.loadBatteryRamFromFile(ramPath.string()));
}

TEST_CASE("state", "rtc_roundtrip_file_via_gameboy") {
    tests::RomSpec spec{};
    spec.name = "state_rtc_roundtrip";
    spec.cartridgeType = 0x10;
    spec.ramCode = 0x03;
    spec.program = {0x76};

    gb::GameBoy writer;
    tests::ScopedPath cleanupWriter;
    loadRomOrThrow(writer, spec, cleanupWriter);

    writer.bus().write(0x0000, 0x0A);
    writer.bus().write(0x4000, 0x08);
    writer.bus().write(0xA000, 0x1C);
    writer.bus().write(0x6000, 0x00);
    writer.bus().write(0x6000, 0x01);
    T_EQ(writer.bus().read(0xA000), 0x1C);

    const auto rtcPath = tests::makeTempPath("state_rtc", ".rtc");
    tests::ScopedPath cleanupRtc(rtcPath);
    T_REQUIRE(writer.saveRtcToFile(rtcPath.string()));

    gb::GameBoy reader;
    tests::ScopedPath cleanupReader;
    loadRomOrThrow(reader, spec, cleanupReader);
    T_REQUIRE(reader.loadRtcFromFile(rtcPath.string()));
    reader.bus().write(0x0000, 0x0A);
    reader.bus().write(0x4000, 0x08);
    reader.bus().write(0x6000, 0x00);
    reader.bus().write(0x6000, 0x01);
    T_EQ(reader.bus().read(0xA000), 0x1C);
}

TEST_CASE("state", "rtc_file_applies_offline_elapsed_time_for_mbc3") {
    tests::RomSpec spec{};
    spec.name = "state_rtc_offline_elapsed";
    spec.cartridgeType = 0x10;
    spec.ramCode = 0x03;
    spec.program = {0x76};

    gb::GameBoy writer;
    tests::ScopedPath cleanupWriter;
    loadRomOrThrow(writer, spec, cleanupWriter);

    writer.bus().write(0x0000, 0x0A);
    writer.bus().write(0x4000, 0x08);
    writer.bus().write(0xA000, 0x00); // seconds
    writer.bus().write(0x4000, 0x09);
    writer.bus().write(0xA000, 0x00); // minutes
    writer.bus().write(0x4000, 0x0A);
    writer.bus().write(0xA000, 0x00); // hours
    writer.bus().write(0x4000, 0x0B);
    writer.bus().write(0xA000, 0x00); // day low
    writer.bus().write(0x4000, 0x0C);
    writer.bus().write(0xA000, 0x00); // day high

    const auto rtcPath = tests::makeTempPath("state_rtc_offline", ".rtc");
    tests::ScopedPath cleanupRtc(rtcPath);
    T_REQUIRE(writer.saveRtcToFile(rtcPath.string()));

    auto rtcFile = tests::readBinaryFile(rtcPath);
    T_REQUIRE(rtcFile.size() >= static_cast<std::size_t>(13 + 23));
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const std::uint64_t past = static_cast<std::uint64_t>(now - 65);
    const std::size_t tsOffset = 13 + 15;
    for (int i = 0; i < 8; ++i) {
        rtcFile[tsOffset + static_cast<std::size_t>(i)] = static_cast<gb::u8>((past >> (i * 8)) & 0xFF);
    }
    T_REQUIRE(tests::writeBinaryFile(rtcPath, rtcFile));

    gb::GameBoy reader;
    tests::ScopedPath cleanupReader;
    loadRomOrThrow(reader, spec, cleanupReader);
    T_REQUIRE(reader.loadRtcFromFile(rtcPath.string()));
    reader.bus().write(0x0000, 0x0A);
    reader.bus().write(0x4000, 0x08);
    reader.bus().write(0x6000, 0x00);
    reader.bus().write(0x6000, 0x01);
    const gb::u8 seconds = reader.bus().read(0xA000);
    T_REQUIRE(seconds >= 4 && seconds <= 8);
}

TEST_CASE("state", "boot_rom_file_maps_until_ff50_disable") {
    tests::RomSpec spec{};
    spec.name = "boot_rom_mapping";
    spec.program = {0x76};

    const auto bootPath = tests::makeTempPath("bootrom", ".bin");
    tests::ScopedPath cleanupBoot(bootPath);
    T_REQUIRE(tests::writeBinaryFile(bootPath, {0xEA, 0x00, 0x00}));

    gb::GameBoy gb;
    T_REQUIRE(gb.loadBootRomFromFile(bootPath.string()));

    tests::ScopedPath cleanupRom;
    loadRomOrThrow(gb, spec, cleanupRom);
    T_EQ(gb.bus().read(0x0000), 0xEA);

    gb.bus().write(0xFF50, 0x01);
    T_EQ(gb.bus().read(0x0000), 0x00);
}

TEST_CASE("state", "precise_timing_flag_updates_runtime_mode") {
    gb::GameBoy gb;
    T_REQUIRE(!gb.preciseTiming());
    gb.setPreciseTiming(true);
    T_REQUIRE(gb.preciseTiming());
    gb.setPreciseTiming(false);
    T_REQUIRE(!gb.preciseTiming());
}

TEST_CASE("state", "precise_timing_runframe_returns_when_lcd_off") {
    tests::RomSpec spec{};
    spec.name = "precise_lcd_off";
    spec.program = {0x76};

    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadRomOrThrow(gb, spec, cleanup);
    gb.setPreciseTiming(true);

    // LCD off: em alguns jogos/transicoes o LY pode ficar estavel.
    gb.bus().write(0xFF40, 0x00);
    gb.runFrame();

    // Se chegou ate aqui, nao travou em loop infinito.
    T_REQUIRE(true);
}

TEST_CASE("options", "defaults_when_no_args") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu"}, options, error));
    T_EQ(options.romPath, std::string(""));
    T_REQUIRE(!options.headless);
    T_REQUIRE(!options.chooseRom);
    T_REQUIRE(!options.preciseTiming);
    T_EQ(static_cast<int>(options.hardwareMode), static_cast<int>(gb::HardwareModePreference::Auto));
    T_EQ(static_cast<int>(options.targetSystem), static_cast<int>(gb::TargetSystemPreference::Auto));
    T_EQ(options.bootRomPath, std::string(""));
    T_EQ(options.linkConnect, std::string(""));
    T_EQ(options.netplayConnect, std::string(""));
    T_EQ(options.linkHostPort, 0);
    T_EQ(options.netplayHostPort, 0);
    T_EQ(options.netplayDelayFrames, 0);
    T_EQ(options.frames, 120);
    T_EQ(options.scale, 4);
    T_EQ(options.audioBuffer, 1024);
}

TEST_CASE("options", "positional_rom_argument") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "roms/game.gb"}, options, error));
    T_EQ(options.romPath, std::string("roms/game.gb"));
}

TEST_CASE("options", "explicit_rom_argument") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--rom", "a.gb"}, options, error));
    T_EQ(options.romPath, std::string("a.gb"));
}

TEST_CASE("options", "choose_rom_flag") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--choose-rom"}, options, error));
    T_REQUIRE(options.chooseRom);
}

TEST_CASE("options", "headless_without_value_uses_default_frames") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--headless"}, options, error));
    T_REQUIRE(options.headless);
    T_EQ(options.frames, 120);
}

TEST_CASE("options", "headless_with_value") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--headless", "240"}, options, error));
    T_REQUIRE(options.headless);
    T_EQ(options.frames, 240);
}

TEST_CASE("options", "headless_frames_clamped_to_min_1") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--headless", "0"}, options, error));
    T_EQ(options.frames, 1);
}

TEST_CASE("options", "scale_clamped_to_min_1") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--scale", "0"}, options, error));
    T_EQ(options.scale, 1);
}

TEST_CASE("options", "audio_buffer_clamped_to_min") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--audio-buffer", "128"}, options, error));
    T_EQ(options.audioBuffer, 256);
}

TEST_CASE("options", "audio_buffer_clamped_to_max") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--audio-buffer", "99999"}, options, error));
    T_EQ(options.audioBuffer, 8192);
}

TEST_CASE("options", "unknown_option_returns_false") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(!parseArgs({"gbemu", "--naoexiste"}, options, error));
    T_REQUIRE(error.find("opcao invalida") != std::string::npos);
}

TEST_CASE("options", "numeric_argument_after_rom_enables_headless") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "game.gb", "180"}, options, error));
    T_REQUIRE(options.headless);
    T_EQ(options.frames, 180);
    T_EQ(options.romPath, std::string("game.gb"));
}

TEST_CASE("options", "combined_options_parse") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--rom", "z.gb", "--scale", "6", "--audio-buffer", "512"}, options, error));
    T_EQ(options.romPath, std::string("z.gb"));
    T_EQ(options.scale, 6);
    T_EQ(options.audioBuffer, 512);
}

TEST_CASE("options", "rom_suite_option_enables_headless") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--rom-suite", "roms/tests/manifest.txt"}, options, error));
    T_EQ(options.romSuiteManifest, std::string("roms/tests/manifest.txt"));
    T_REQUIRE(options.headless);
}

TEST_CASE("options", "boot_rom_precise_and_link_flags_parse") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs(
        {"gbemu", "--boot-rom", "boot.bin", "--precise-timing", "--link-host", "6000", "--link-connect", "127.0.0.1:6001"},
        options,
        error
    ));
    T_EQ(options.bootRomPath, std::string("boot.bin"));
    T_REQUIRE(options.preciseTiming);
    T_EQ(options.linkHostPort, 6000);
    T_EQ(options.linkConnect, std::string("127.0.0.1:6001"));
}

TEST_CASE("options", "hardware_mode_flag_parse") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--hardware", "dmg"}, options, error));
    T_EQ(static_cast<int>(options.hardwareMode), static_cast<int>(gb::HardwareModePreference::Dmg));

    T_REQUIRE(parseArgs({"gbemu", "--hardware", "cgb"}, options, error));
    T_EQ(static_cast<int>(options.hardwareMode), static_cast<int>(gb::HardwareModePreference::Cgb));

    T_REQUIRE(parseArgs({"gbemu", "--hardware", "auto"}, options, error));
    T_EQ(static_cast<int>(options.hardwareMode), static_cast<int>(gb::HardwareModePreference::Auto));
}

TEST_CASE("options", "hardware_mode_invalid_value_returns_false") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(!parseArgs({"gbemu", "--hardware", "abc"}, options, error));
    T_REQUIRE(error.find("valor invalido para --hardware") != std::string::npos);
}

TEST_CASE("options", "target_system_flag_parse") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--system", "gb"}, options, error));
    T_EQ(static_cast<int>(options.targetSystem), static_cast<int>(gb::TargetSystemPreference::Gb));

    T_REQUIRE(parseArgs({"gbemu", "--system", "gba"}, options, error));
    T_EQ(static_cast<int>(options.targetSystem), static_cast<int>(gb::TargetSystemPreference::Gba));

    T_REQUIRE(parseArgs({"gbemu", "--system", "auto"}, options, error));
    T_EQ(static_cast<int>(options.targetSystem), static_cast<int>(gb::TargetSystemPreference::Auto));
}

TEST_CASE("options", "target_system_invalid_value_returns_false") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(!parseArgs({"gbemu", "--system", "xyz"}, options, error));
    T_REQUIRE(error.find("valor invalido para --system") != std::string::npos);
}

TEST_CASE("options", "netplay_flags_parse") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--netplay-host", "6100", "--netplay-connect", "10.0.0.2:6100", "--netplay-delay", "3"}, options, error));
    T_EQ(options.netplayHostPort, 6100);
    T_EQ(options.netplayConnect, std::string("10.0.0.2:6100"));
    T_EQ(options.netplayDelayFrames, 3);
}

TEST_CASE("options", "netplay_delay_is_clamped") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu", "--netplay-delay", "-4"}, options, error));
    T_EQ(options.netplayDelayFrames, 0);

    T_REQUIRE(parseArgs({"gbemu", "--netplay-delay", "99"}, options, error));
    T_EQ(options.netplayDelayFrames, 10);
}

TEST_CASE("options", "invalid_numeric_argument_returns_false") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(!parseArgs({"gbemu", "--scale", "abc"}, options, error));
    T_REQUIRE(error.find("valor invalido") != std::string::npos);
}

TEST_CASE("options", "invalid_port_argument_returns_false") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(!parseArgs({"gbemu", "--link-host", "99999"}, options, error));
    T_REQUIRE(error.find("porta invalida") != std::string::npos);
}

TEST_CASE("state", "rom_suite_runner_executes_manifest_case") {
    tests::RomSpec spec{};
    spec.name = "suite_runner_case";
    spec.program = {0x76}; // HALT imediato
    spec.title = "SUITETEST";

    const auto romPath = tests::writeTempRom(spec);
    tests::ScopedPath cleanupRom(romPath);

    const auto manifestPath = tests::makeTempPath("rom_suite_manifest", ".txt");
    tests::ScopedPath cleanupManifest(manifestPath);

    std::ofstream out(manifestPath, std::ios::trunc);
    T_REQUIRE(static_cast<bool>(out));
    out << "quick_halt|" << romPath.string() << "|2|title=SUITETEST|halted=1\n";
    out.close();

    const int rc = gb::runRomCompatibilitySuite(manifestPath.string());
    T_EQ(rc, 0);
}

TEST_CASE("state", "gba_rom_parser_reads_metadata_and_checksum") {
    const auto romPath = tests::makeTempPath("gba_parser", ".gba");
    tests::ScopedPath cleanupRom(romPath);
    const auto image = buildGbaTestRomImage("PHASE1DEMO", "ABCD", "01");
    T_REQUIRE(tests::writeBinaryFile(romPath, image));

    gb::gba::System gba;
    T_REQUIRE(gba.loadRomFromFile(romPath.string()));
    T_REQUIRE(gba.loaded());
    T_EQ(gba.metadata().title, std::string("PHASE1DEMO"));
    T_EQ(gba.metadata().gameCode, std::string("ABCD"));
    T_EQ(gba.metadata().makerCode, std::string("01"));
    T_REQUIRE(gba.metadata().validNintendoLogo);
    T_REQUIRE(gba.metadata().validFixedByte);
    T_REQUIRE(gba.metadata().validHeaderChecksum);
}

TEST_CASE("state", "gba_rom_parser_rejects_too_small_file") {
    const auto romPath = tests::makeTempPath("gba_small", ".gba");
    tests::ScopedPath cleanupRom(romPath);
    T_REQUIRE(tests::writeBinaryFile(romPath, {0x00, 0x01, 0x02, 0x03}));

    gb::gba::System gba;
    T_REQUIRE(!gba.loadRomFromFile(romPath.string()));
    T_REQUIRE(!gba.loaded());
}
