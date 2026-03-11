#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gb/app/app_options.hpp"
#include "gb/app/rom_suite_runner.hpp"
#include "gb/core/gameboy.hpp"

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

TEST_CASE("options", "defaults_when_no_args") {
    gb::AppOptions options;
    std::string error;

    T_REQUIRE(parseArgs({"gbemu"}, options, error));
    T_EQ(options.romPath, std::string(""));
    T_REQUIRE(!options.headless);
    T_REQUIRE(!options.chooseRom);
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

TEST_CASE("options", "invalid_numeric_argument_throws_exception") {
    gb::AppOptions options;
    std::string error;

    bool threw = false;
    try {
        (void)parseArgs({"gbemu", "--scale", "abc"}, options, error);
    } catch (const std::exception&) {
        threw = true;
    }

    T_REQUIRE(threw);
}
