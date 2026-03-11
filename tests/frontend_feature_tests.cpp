#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gb/app/frontend/realtime/cheat_engine.hpp"
#include "gb/app/frontend/realtime/control_bindings.hpp"
#include "gb/app/frontend/realtime/link_transport.hpp"
#include "gb/app/frontend/realtime/replay_io.hpp"
#include "gb/app/frontend/realtime/save_slots.hpp"
#include "gb/core/gameboy.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

namespace {

void loadBlankRom(gb::GameBoy& gb, tests::ScopedPath& cleanup) {
    tests::RomSpec spec{};
    spec.name = "frontend_feature_blank";
    spec.program = {0x76};
    const auto path = tests::writeTempRom(spec);
    cleanup = tests::ScopedPath(path);
    T_REQUIRE(gb.loadRom(path.string()));
}

} // namespace

TEST_CASE("frontend", "save_slot_paths_and_meta_roundtrip") {
    const std::string base = "states/game.state";
    T_EQ(gb::frontend::saveSlotStatePath(base, 2), std::string("states/game.slot2.state"));
    T_EQ(gb::frontend::saveSlotMetaPath(base, 2), std::string("states/game.slot2.meta"));
    T_EQ(gb::frontend::saveSlotThumbnailPath(base, 2), std::string("states/game.slot2.ppm"));

    const auto metaPath = tests::makeTempPath("slot_meta", ".meta");
    tests::ScopedPath cleanup(metaPath);

    gb::frontend::SaveSlotMeta meta{};
    meta.slot = 3;
    meta.title = "TEST";
    meta.timestamp = gb::frontend::nowIso8601Local();
    meta.frame = 1234;

    T_REQUIRE(gb::frontend::writeSaveSlotMeta(metaPath.string(), meta));
    const auto loaded = gb::frontend::readSaveSlotMeta(metaPath.string());
    T_REQUIRE(loaded.has_value());
    T_EQ(loaded->slot, 3);
    T_EQ(loaded->title, std::string("TEST"));
    T_EQ(loaded->frame, static_cast<std::uint64_t>(1234));
}

TEST_CASE("frontend", "cheat_parser_supports_gs_and_write") {
    {
        std::string error;
        const auto c = gb::frontend::parseCheatLine("01AA C000", &error);
        T_REQUIRE(!c.has_value());
    }

    {
        std::string error;
        const auto c = gb::frontend::parseCheatLine("01AACC00", &error);
        T_REQUIRE(c.has_value());
        T_EQ(static_cast<int>(c->kind), static_cast<int>(gb::frontend::CheatKind::GameShark));
        T_EQ(c->value, 0xAA);
        T_EQ(c->address, 0xCC00);
    }

    {
        std::string error;
        const auto c = gb::frontend::parseCheatLine("C123=7F", &error);
        T_REQUIRE(c.has_value());
        T_EQ(static_cast<int>(c->kind), static_cast<int>(gb::frontend::CheatKind::Write));
        T_EQ(c->address, 0xC123);
        T_EQ(c->value, 0x7F);
    }

    {
        std::string error;
        const auto c = gb::frontend::parseCheatLine("GG C321:44", &error);
        T_REQUIRE(c.has_value());
        T_EQ(static_cast<int>(c->kind), static_cast<int>(gb::frontend::CheatKind::GameGenie));
        T_EQ(c->address, 0xC321);
        T_EQ(c->value, 0x44);
    }
}

TEST_CASE("frontend", "cheat_apply_writes_to_bus") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup);

    std::vector<gb::frontend::CheatCode> cheats;
    cheats.push_back(gb::frontend::CheatCode{gb::frontend::CheatKind::Write, 0xC000, 0x3A, std::nullopt, "C000=3A"});
    cheats.push_back(gb::frontend::CheatCode{gb::frontend::CheatKind::Write, 0x0001, 0x99, std::nullopt, "0001=99"}); // ignored ROM

    const std::size_t applied = gb::frontend::applyCheats(cheats, gb.bus());
    T_EQ(applied, static_cast<std::size_t>(1));
    T_EQ(gb.bus().peek(0xC000), 0x3A);
}

TEST_CASE("frontend", "replay_roundtrip_binary") {
    gb::frontend::ReplayData data{};
    data.version = 1;
    data.seed = 42;
    data.frameInputs = {0x01, 0x12, 0x34, 0xFF};

    const auto path = tests::makeTempPath("replay", ".bin");
    tests::ScopedPath cleanup(path);

    T_REQUIRE(gb::frontend::saveReplayFile(path.string(), data));
    const auto loaded = gb::frontend::loadReplayFile(path.string());
    T_REQUIRE(loaded.has_value());
    T_EQ(loaded->version, static_cast<std::uint32_t>(1));
    T_EQ(loaded->seed, static_cast<std::uint32_t>(42));
    T_EQ(loaded->frameInputs.size(), static_cast<std::size_t>(4));
    T_EQ(loaded->frameInputs[2], static_cast<std::uint8_t>(0x34));
}

TEST_CASE("frontend", "pack_buttons_bit_layout") {
    const auto mask = gb::frontend::packButtons(true, false, true, false, true, false, true, false);
    T_EQ(mask, static_cast<std::uint8_t>(0x55));
}

TEST_CASE("frontend", "control_bindings_save_load_apply") {
    gb::frontend::ControlBindings bindings = gb::frontend::defaultControlBindings();
    bindings.keys[0] = 1001;
    bindings.padButtons[4] = 3001;

    const auto path = tests::makeTempPath("controls", ".cfg");
    tests::ScopedPath cleanup(path);

    T_REQUIRE(gb::frontend::saveControlBindings(path.string(), bindings));

    gb::frontend::ControlBindings loaded{};
    T_REQUIRE(gb::frontend::loadControlBindings(path.string(), loaded));
    T_EQ(loaded.keys[0], 1001);
    T_EQ(loaded.padButtons[4], 3001);

    gb::GameBoy gb;
    tests::ScopedPath cleanupRom;
    loadBlankRom(gb, cleanupRom);

    T_REQUIRE(gb::frontend::applyKeyboardBinding(gb, loaded, 1001, true));
    T_REQUIRE(gb.joypad().state().pressed[0]);

    T_REQUIRE(gb::frontend::applyGamepadBinding(gb, loaded, 3001, true));
    T_REQUIRE(gb.joypad().state().pressed[4]);
}

TEST_CASE("frontend", "link_endpoint_parser") {
    {
        const auto ep = gb::frontend::parseLinkEndpoint("127.0.0.1:7777");
        T_REQUIRE(ep.has_value());
        T_EQ(ep->host, std::string("127.0.0.1"));
        T_EQ(ep->port, static_cast<std::uint16_t>(7777));
    }
    {
        const auto ep = gb::frontend::parseLinkEndpoint("localhost");
        T_REQUIRE(!ep.has_value());
    }
    {
        const auto ep = gb::frontend::parseLinkEndpoint("1.2.3.4:70000");
        T_REQUIRE(!ep.has_value());
    }
}
