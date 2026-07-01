#include <filesystem>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

#include "gb/app/frontend/realtime/cheat_engine.hpp"
#include "gb/app/frontend/realtime/control_bindings.hpp"
#include "gb/app/frontend/realtime/frame_timeline.hpp"
#include "gb/app/frontend/realtime/link_transport.hpp"
#include "gb/app/frontend/realtime/network_config.hpp"
#include "gb/app/frontend/realtime/replay_io.hpp"
#include "gb/app/frontend/realtime/runlab_control_queue.hpp"
#include "gb/app/frontend/realtime/save_slots.hpp"
#include "gb/app/frontend/realtime/timing_policy.hpp"
#include "gb/app/frontend/realtime/top_menu.hpp"
#include "gb/app/frontend/runlab.hpp"
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

TEST_CASE("frontend", "control_bindings_fallback_and_mirror") {
    gb::frontend::ControlBindings bindings = gb::frontend::defaultControlBindings();
    bindings.keys[2] = 2222;
    bindings.padButtons[6] = 4444;

    const auto primary = tests::makeTempPath("controls_primary", ".cfg");
    const auto fallback = tests::makeTempPath("controls_fallback", ".cfg");
    tests::ScopedPath cleanupPrimary(primary);
    tests::ScopedPath cleanupFallback(fallback);

    T_REQUIRE(gb::frontend::saveControlBindings(fallback.string(), bindings));

    gb::frontend::ControlBindings loaded{};
    T_REQUIRE(gb::frontend::loadControlBindingsWithFallback(primary.string(), fallback.string(), loaded));
    T_EQ(loaded.keys[2], 2222);
    T_EQ(loaded.padButtons[6], 4444);

    bindings.keys[3] = 3333;
    bindings.padButtons[1] = 1111;
    T_REQUIRE(gb::frontend::saveControlBindingsWithMirror(primary.string(), fallback.string(), bindings));

    gb::frontend::ControlBindings fromPrimary{};
    gb::frontend::ControlBindings fromMirror{};
    T_REQUIRE(gb::frontend::loadControlBindings(primary.string(), fromPrimary));
    T_REQUIRE(gb::frontend::loadControlBindings(fallback.string(), fromMirror));
    T_EQ(fromPrimary.keys[3], 3333);
    T_EQ(fromMirror.keys[3], 3333);
    T_EQ(fromPrimary.padButtons[1], 1111);
    T_EQ(fromMirror.padButtons[1], 1111);
}

TEST_CASE("frontend", "runlab_control_command_parser_and_tick") {
    std::string error;
    const auto command = gb::frontend::parseRunLabControlCommandLine(
        "{\"command\":\"hold\",\"buttons\":[\"right\",\"a\"],\"frames\":3}",
        &error
    );
    T_REQUIRE(command.has_value());
    T_EQ(command->buttonMask, static_cast<std::uint8_t>((1u << 0) | (1u << 4)));
    T_EQ(command->frames, 3);

    const auto bad = gb::frontend::parseRunLabControlCommandLine(
        "{\"command\":\"hold\",\"buttons\":[\"banana\"],\"frames\":3}",
        &error
    );
    T_REQUIRE(!bad.has_value());

    const auto step = gb::frontend::parseRunLabControlCommandLine(
        "{\"command\":\"step_frame\",\"frames\":4}",
        &error
    );
    T_REQUIRE(step.has_value());
    T_EQ(static_cast<int>(step->type), static_cast<int>(gb::frontend::RunLabControlCommandType::StepFrame));
    T_EQ(step->frames, 4);

    const auto pause = gb::frontend::parseRunLabControlCommandLine("{\"command\":\"pause\"}", &error);
    T_REQUIRE(pause.has_value());
    T_EQ(static_cast<int>(pause->type), static_cast<int>(gb::frontend::RunLabControlCommandType::Pause));

    const auto heartbeat = gb::frontend::parseRunLabControlCommandLine("{\"command\":\"heartbeat\"}", &error);
    T_REQUIRE(heartbeat.has_value());

    const auto annotation = gb::frontend::parseRunLabControlCommandLine(
        "{\"command\":\"annotation\",\"label\":\"PLAYER\",\"type\":\"player\",\"x\":12,\"y\":34,\"w\":16,\"h\":24,\"frames\":5}",
        &error
    );
    T_REQUIRE(annotation.has_value());
    T_EQ(static_cast<int>(annotation->type), static_cast<int>(gb::frontend::RunLabControlCommandType::Annotation));
    T_EQ(annotation->annotation.label, std::string("PLAYER"));
    T_EQ(annotation->annotation.type, std::string("player"));
    T_EQ(annotation->annotation.x, 12);
    T_EQ(annotation->annotation.y, 34);
    T_EQ(annotation->annotation.w, 16);
    T_EQ(annotation->annotation.h, 24);
    T_EQ(annotation->annotation.frames, 5);

    const auto path = tests::makeTempPath("runlab_control", ".jsonl");
    tests::ScopedPath cleanup(path);
    {
        std::ofstream out(path, std::ios::trunc);
        out << gb::frontend::makeRunLabControlCommandJson("hold", {"right", "a"}, 2) << "\n";
        out << gb::frontend::makeRunLabControlCommandJson("release_all", {}, 1) << "\n";
    }

    gb::frontend::RunLabControlQueue queue;
    queue.configure(path, true);
    queue.poll(); // first poll seeks to end so stale commands are ignored
    {
        std::ofstream out(path, std::ios::app);
        out << "{\"command\":\"heartbeat\",\"frames\":1}\n";
        out << "{\"command\":\"annotation\",\"label\":\"ENEMY\",\"type\":\"enemy\",\"x\":30,\"y\":40,\"w\":8,\"h\":8,\"frames\":2}\n";
        out << gb::frontend::makeRunLabControlCommandJson("tap", {"left"}, 2) << "\n";
    }
    queue.poll();
    T_REQUIRE(queue.clientRecentlySeen());
    T_EQ(queue.pendingCount(), static_cast<std::size_t>(2));
    const auto first = queue.tick();
    T_REQUIRE(!first.active);
    T_EQ(first.commandName, std::string("annotation"));
    T_EQ(queue.annotations().size(), static_cast<std::size_t>(1));
    T_EQ(queue.annotations().front().label, std::string("ENEMY"));
    const auto second = queue.tick();
    T_REQUIRE(second.active);
    T_EQ(second.buttonMask, static_cast<std::uint8_t>(1u << 1));
    const auto third = queue.tick();
    T_REQUIRE(third.active);
    T_EQ(third.buttonMask, static_cast<std::uint8_t>(1u << 1));
    const auto fourth = queue.tick();
    T_REQUIRE(!fourth.active);
    T_REQUIRE(queue.annotations().empty());
}

TEST_CASE("frontend", "runlab_control_annotations_clear") {
    const auto path = tests::makeTempPath("runlab_control_annotations", ".jsonl");
    tests::ScopedPath cleanup(path);
    {
        std::ofstream out(path, std::ios::trunc);
    }

    gb::frontend::RunLabControlQueue queue;
    queue.configure(path, true);
    queue.poll();
    {
        std::ofstream out(path, std::ios::app);
        out << "{\"command\":\"annotation\",\"label\":\"SCENARIO\",\"type\":\"scenario\",\"x\":0,\"y\":80,\"w\":160,\"h\":64,\"frames\":30}\n";
    }
    queue.poll();
    const auto first = queue.tick();
    T_REQUIRE(!first.active);
    T_EQ(queue.annotations().size(), static_cast<std::size_t>(1));
    queue.clear();
    T_REQUIRE(queue.annotations().empty());
    T_EQ(queue.pendingCount(), static_cast<std::size_t>(0));
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

TEST_CASE("frontend", "link_transport_closed_mode_behaviour") {
    gb::frontend::UdpLinkTransport transport{};
    T_REQUIRE(!transport.isOpen());

    std::uint8_t remote = 0x00;
    bool predicted = true;
    T_REQUIRE(!transport.exchangeSerialByte(0x12, remote));
    T_EQ(remote, static_cast<std::uint8_t>(0xFF));
    T_REQUIRE(!transport.exchangeNetplayInput(1, 0x34, remote, predicted));
    T_EQ(remote, static_cast<std::uint8_t>(0x00));
    T_REQUIRE(!predicted);

    transport.pump();

    std::uint8_t v = 0;
    std::uint32_t checksum = 0;
    T_REQUIRE(!transport.takeNetplayInput(1, v));
    T_REQUIRE(!transport.takeNetplayChecksum(1, checksum));
    T_REQUIRE(!transport.sendNetplayChecksum(1, 0x12345678u));
    const auto all = transport.takeAllNetplayInputs();
    T_REQUIRE(all.empty());
}

TEST_CASE("frontend", "network_config_roundtrip") {
    const auto path = tests::makeTempPath("network_cfg", ".cfg");
    tests::ScopedPath cleanup(path);

    gb::frontend::NetworkFrontendConfig cfg{};
    cfg.netplayDelayFrames = 7;
    cfg.linkMode = 3;
    T_REQUIRE(gb::frontend::saveNetworkFrontendConfig(path.string(), cfg));

    const auto loaded = gb::frontend::loadNetworkFrontendConfig(path.string());
    T_REQUIRE(loaded.has_value());
    T_EQ(loaded->netplayDelayFrames, 7);
    T_EQ(loaded->linkMode, 3);
}

TEST_CASE("frontend", "runlab_memory_label_formatting") {
    const auto label = gb::frontend::runlab::makeMemoryLabel(
        "player.x",
        0xC202,
        gb::frontend::runlab::MemoryValueType::U16Le,
        "player",
        "x"
    );

    T_EQ(label.label, std::string("player.x"));
    T_EQ(gb::frontend::runlab::formatAddress(label.address), std::string("0xC202"));
    T_EQ(std::string(gb::frontend::runlab::memoryValueTypeName(label.type)), std::string("u16_le"));
    T_EQ(label.entity, std::string("player"));
    T_EQ(label.field, std::string("x"));
}

TEST_CASE("frontend", "runlab_event_timeline_detects_labeled_change") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup);

    gb::frontend::runlab::State state{};
    state.memoryLabels.push_back(gb::frontend::runlab::makeMemoryLabel("lives", 0xC000, gb::frontend::runlab::MemoryValueType::U8));

    gb.bus().write(0xC000, 3);
    gb::frontend::runlab::updateTimeline(state, gb.bus(), 10);
    T_REQUIRE(state.events.empty());

    gb.bus().write(0xC000, 2);
    gb::frontend::runlab::updateTimeline(state, gb.bus(), 11);
    T_REQUIRE(state.events.size() >= static_cast<std::size_t>(3));
    T_EQ(state.events[0].frame, static_cast<std::uint64_t>(11));
    T_EQ(state.events[0].label, std::string("lives"));
    T_EQ(state.events[0].previous, 3);
    T_EQ(state.events[0].current, 2);
    T_EQ(static_cast<int>(state.events[0].type), static_cast<int>(gb::frontend::runlab::RunLabEventType::MemoryChanged));
}

TEST_CASE("frontend", "runlab_entity_candidate_serializes_to_profile") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup);
    gb.bus().write(0xFF40, 0x00);

    gb::frontend::runlab::State state{};
    const gb::frontend::runlab::OamSpriteRef sprite{0xFE0C, 112, 80, 4, 0};
    const auto idx = gb::frontend::runlab::createOrSelectEntityFromSprite(state, sprite, gb.bus());
    T_EQ(idx, static_cast<std::size_t>(0));
    state.memoryLabels.push_back(gb::frontend::runlab::makeMemoryLabel(
        "player.x",
        0xC202,
        gb::frontend::runlab::MemoryValueType::U16Le,
        "player",
        "x"
    ));

    const std::string json = gb::frontend::runlab::exportProfileJson(state, "TEST GAME");
    T_REQUIRE(json.find("\"label\": \"player\"") != std::string::npos);
    T_REQUIRE(json.find("\"type\": \"Player\"") != std::string::npos);
    T_REQUIRE(json.find("\"oam_indices\": [3]") != std::string::npos);
    T_REQUIRE(json.find("\"address\": \"0xC202\"") != std::string::npos);
}

TEST_CASE("frontend", "runlab_ram_diff_promotes_address") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup);

    gb::frontend::runlab::State state{};
    gb.bus().write(0xC123, 0x10);
    gb::frontend::runlab::captureRamSnapshot(state, gb.bus());
    gb.bus().write(0xC123, 0x11);

    const std::size_t total = gb::frontend::runlab::buildRamDiff(state, gb.bus());
    T_REQUIRE(total >= 1);
    T_REQUIRE(!state.lastDiff.empty());
    T_EQ(state.lastDiff.front().address, static_cast<gb::u16>(0xC123));

    const std::size_t labelIdx = gb::frontend::runlab::promoteDiffAddress(state, 0);
    T_EQ(labelIdx, static_cast<std::size_t>(0));
    T_EQ(state.memoryLabels[0].address, static_cast<gb::u16>(0xC123));
}

TEST_CASE("frontend", "runlab_correlation_ranks_unchanged_low_and_matching_high") {
    std::deque<gb::frontend::runlab::CorrelationSample> samples;
    for (int i = 0; i < 12; ++i) {
        gb::frontend::runlab::CorrelationSample sample{};
        sample.frame = static_cast<std::uint64_t>(i);
        sample.entityX = 24 + i;
        sample.entityY = 80;
        sample.wram.assign(0x2000, 0);
        sample.wram[0x0123] = static_cast<gb::u8>(sample.entityX);
        sample.wram[0x0124] = 0x44; // unchanged address should not rank.
        sample.wram[0x0200] = static_cast<gb::u8>((1000 + sample.entityX) & 0xFF);
        sample.wram[0x0201] = static_cast<gb::u8>(((1000 + sample.entityX) >> 8) & 0xFF);
        samples.push_back(std::move(sample));
    }

    const auto result = gb::frontend::runlab::analyzeCorrelationSamples(samples, 4);
    T_REQUIRE(!result.entityX.empty());
    T_EQ(result.entityX.front().address, static_cast<gb::u16>(0xC123));
    T_REQUIRE(result.entityX.front().score > 0.80);

    bool unchangedRanked = false;
    for (const auto& candidate : result.entityX) {
        unchangedRanked = unchangedRanked || candidate.address == 0xC124;
    }
    T_REQUIRE(!unchangedRanked);
}

TEST_CASE("frontend", "runlab_correlation_supports_u16_and_promotion") {
    gb::frontend::runlab::State state{};
    state.entities.push_back(gb::frontend::runlab::EntityCandidate{"player", gb::frontend::runlab::EntityType::Player, {0}, {}});
    state.selectedEntity = 0;

    for (int i = 0; i < 12; ++i) {
        gb::frontend::runlab::CorrelationSample sample{};
        sample.frame = static_cast<std::uint64_t>(i);
        sample.entityX = 16 + i;
        sample.entityY = 40 + (i / 2);
        sample.wram.assign(0x2000, 0);
        const int logicalX = 300 + sample.entityX;
        sample.wram[0x0300] = static_cast<gb::u8>(logicalX & 0xFF);
        sample.wram[0x0301] = static_cast<gb::u8>((logicalX >> 8) & 0xFF);
        state.correlationSamples.push_back(std::move(sample));
    }

    T_REQUIRE(gb::frontend::runlab::runCorrelationScan(state, 5));
    bool sawU16 = false;
    for (const auto& candidate : state.correlationResult.entityX) {
        if (candidate.address == 0xC300 && candidate.type == gb::frontend::runlab::MemoryValueType::U16Le) {
            sawU16 = true;
        }
    }
    T_REQUIRE(sawU16);

    const auto labelIdx = gb::frontend::runlab::promoteCorrelationCandidate(state, gb::frontend::runlab::CorrelationTarget::EntityX, 0);
    T_REQUIRE(labelIdx < state.memoryLabels.size());
    T_EQ(state.memoryLabels[labelIdx].label, std::string("player.x"));
    T_EQ(state.memoryLabels[labelIdx].entity, std::string("player"));
    T_EQ(state.memoryLabels[labelIdx].field, std::string("x"));

    const std::string json = gb::frontend::runlab::exportProfileJson(state, "TEST GAME");
    T_REQUIRE(json.find("\"label\": \"player.x\"") != std::string::npos);
    T_REQUIRE(json.find("\"entity\": \"player\"") != std::string::npos);
}

TEST_CASE("frontend", "runlab_event_detection_generates_damage_death_and_level_change") {
    const auto livesDrop = gb::frontend::runlab::detectEventsForChange(
        gb::frontend::runlab::makeMemoryLabel("lives", 0xC000, gb::frontend::runlab::MemoryValueType::U8),
        3,
        2,
        100
    );
    bool hasDamage = false;
    for (const auto& event : livesDrop) {
        hasDamage = hasDamage || event.type == gb::frontend::runlab::RunLabEventType::DamageCandidate;
    }
    T_REQUIRE(hasDamage);

    const auto death = gb::frontend::runlab::detectEventsForChange(
        gb::frontend::runlab::makeMemoryLabel("player.hp", 0xC001, gb::frontend::runlab::MemoryValueType::U8, "player", "hp"),
        1,
        0,
        101
    );
    bool hasDeath = false;
    for (const auto& event : death) {
        hasDeath = hasDeath || event.type == gb::frontend::runlab::RunLabEventType::DeathCandidate;
    }
    T_REQUIRE(hasDeath);

    const auto level = gb::frontend::runlab::detectEventsForChange(
        gb::frontend::runlab::makeMemoryLabel("level_id", 0xC002, gb::frontend::runlab::MemoryValueType::U8),
        1,
        2,
        102
    );
    bool hasLevel = false;
    bool hasSplit = false;
    for (const auto& event : level) {
        hasLevel = hasLevel || event.type == gb::frontend::runlab::RunLabEventType::LevelChangeCandidate;
        hasSplit = hasSplit || event.type == gb::frontend::runlab::RunLabEventType::SplitCandidate;
    }
    T_REQUIRE(hasLevel);
    T_REQUIRE(hasSplit);
}

TEST_CASE("frontend", "runlab_goal_conditions_compare_against_baseline_and_values") {
    std::vector<gb::frontend::runlab::MemoryLabel> baseline;
    baseline.push_back(gb::frontend::runlab::makeMemoryLabel("level_id", 0xC000, gb::frontend::runlab::MemoryValueType::U8));
    baseline.back().lastValue = 1;
    baseline.back().hasLastValue = true;

    std::vector<gb::frontend::runlab::MemoryLabel> current = baseline;
    current[0].lastValue = 2;

    T_REQUIRE(gb::frontend::runlab::evaluateCondition(
        gb::frontend::runlab::GoalCondition{"level_id", gb::frontend::runlab::ConditionOperator::ChangedFromInitial, 0},
        current,
        baseline
    ));
    T_REQUIRE(gb::frontend::runlab::evaluateCondition(
        gb::frontend::runlab::GoalCondition{"level_id", gb::frontend::runlab::ConditionOperator::GreaterEqual, 2},
        current,
        baseline
    ));
}

TEST_CASE("frontend", "runlab_split_triggers_once_only") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup);

    gb::frontend::runlab::State state{};
    state.memoryLabels.push_back(gb::frontend::runlab::makeMemoryLabel("level_id", 0xC000, gb::frontend::runlab::MemoryValueType::U8));
    gb.bus().write(0xC000, 1);
    (void)gb::frontend::runlab::createDefaultGoalFromLabels(state);
    (void)gb::frontend::runlab::createDefaultSplitFromLabels(state);
    T_REQUIRE(gb::frontend::runlab::startGoal(state, 0, gb.bus(), 10));

    gb.bus().write(0xC000, 2);
    gb::frontend::runlab::evaluateGoalsAndSplits(state, gb.bus(), 20);
    T_REQUIRE(state.splits[0].triggered);
    T_EQ(state.splits[0].triggeredFrame, static_cast<std::uint64_t>(20));
    const auto eventCount = state.events.size();

    gb::frontend::runlab::evaluateGoalsAndSplits(state, gb.bus(), 21);
    T_EQ(state.events.size(), eventCount);
}

TEST_CASE("frontend", "runlab_profile_export_includes_goals_and_splits") {
    gb::frontend::runlab::State state{};
    state.memoryLabels.push_back(gb::frontend::runlab::makeMemoryLabel("level_id", 0xC000, gb::frontend::runlab::MemoryValueType::U8));
    (void)gb::frontend::runlab::createDefaultGoalFromLabels(state);
    (void)gb::frontend::runlab::createDefaultSplitFromLabels(state);

    const std::string json = gb::frontend::runlab::exportProfileJson(state, "TEST GAME");
    T_REQUIRE(json.find("\"goals\"") != std::string::npos);
    T_REQUIRE(json.find("\"splits\"") != std::string::npos);
    T_REQUIRE(json.find("\"operator\": \"changed_from_initial\"") != std::string::npos);
    T_REQUIRE(json.find("\"event_detection_rules\"") != std::string::npos);
}

TEST_CASE("frontend", "runlab_current_state_export_includes_control_status") {
    gb::frontend::runlab::State state{};
    const std::string json = gb::frontend::runlab::exportCurrentStateJson(
        state,
        "TEST GAME",
        12,
        true,
        true,
        {},
        {},
        "{ \"enabled\": true, \"pending_count\": 2, \"current_command\": \"step_frame\" }"
    );
    T_REQUIRE(json.find("\"control\"") != std::string::npos);
    T_REQUIRE(json.find("\"pending_count\": 2") != std::string::npos);
    T_REQUIRE(json.find("\"current_command\": \"step_frame\"") != std::string::npos);
}

TEST_CASE("frontend", "fast_forward_timing_policy_is_paced") {
    const auto normalBudget = gb::frontend::emulationFrameBudget(false);
    const auto ffBudget = gb::frontend::emulationFrameBudget(true);

    T_EQ(normalBudget.count(), static_cast<long long>(16742));
    T_REQUIRE(ffBudget.count() > 0);
    T_REQUIRE(ffBudget < normalBudget);
    T_EQ(ffBudget.count(), normalBudget.count() / gb::frontend::kFastForwardMultiplier);
    T_EQ(gb::frontend::emulationFramesPerTick(false), 1);
    T_EQ(gb::frontend::emulationFramesPerTick(true), 1);
}

TEST_CASE("frontend", "frame_timeline_keeps_fixed_size_and_navigation") {
    gb::GameBoy gb;
    tests::ScopedPath cleanup;
    loadBlankRom(gb, cleanup);

    gb::frontend::FrameTimeline timeline(gb);
    for (std::size_t i = 0; i < gb::frontend::FrameTimeline::MaxHistory + 64; ++i) {
        timeline.captureCurrent(gb);
    }

    T_EQ(timeline.size(), gb::frontend::FrameTimeline::MaxHistory);
    T_EQ(timeline.position(), timeline.size());

    T_REQUIRE(timeline.stepBack(gb));
    T_EQ(timeline.position(), timeline.size() - 1);
    T_REQUIRE(timeline.stepForward(gb));
    T_EQ(timeline.position(), timeline.size());
}

TEST_CASE("frontend", "top_menu_section_and_action_hit_test") {
    const int outputW = 960;
    const int yInBar = gb::frontend::topMenuBarHeight() / 2;

    const auto section = gb::frontend::hitTestTopMenuSection(outputW, 16, yInBar);
    T_REQUIRE(section.has_value());
    T_EQ(static_cast<int>(section.value()), static_cast<int>(gb::frontend::TopMenuSection::Session));

    const auto secRect = gb::frontend::topMenuSectionRect(outputW, gb::frontend::TopMenuSection::Image);
    const auto imageHit = gb::frontend::hitTestTopMenuSection(outputW, secRect.x + 2, secRect.y + 2);
    T_REQUIRE(imageHit.has_value());
    T_EQ(static_cast<int>(imageHit.value()), static_cast<int>(gb::frontend::TopMenuSection::Image));

    const auto drop = gb::frontend::topMenuDropdownRect(outputW, gb::frontend::TopMenuSection::Session);
    const auto firstAction = gb::frontend::hitTestTopMenuAction(outputW, gb::frontend::TopMenuSection::Session, drop.x + 6, drop.y + 6);
    T_REQUIRE(firstAction.has_value());
    T_EQ(static_cast<int>(firstAction.value()), static_cast<int>(gb::frontend::TopMenuAction::TogglePause));

    const auto outside = gb::frontend::hitTestTopMenuAction(outputW, gb::frontend::TopMenuSection::Session, drop.x - 2, drop.y - 2);
    T_REQUIRE(!outside.has_value());

    const auto netRect = gb::frontend::topMenuSectionRect(outputW, gb::frontend::TopMenuSection::Network);
    const auto netHit = gb::frontend::hitTestTopMenuSection(outputW, netRect.x + 2, netRect.y + 2);
    T_REQUIRE(netHit.has_value());
    T_EQ(static_cast<int>(netHit.value()), static_cast<int>(gb::frontend::TopMenuSection::Network));

    const auto netDrop = gb::frontend::topMenuDropdownRect(outputW, gb::frontend::TopMenuSection::Network);
    const auto netAction = gb::frontend::hitTestTopMenuAction(
        outputW,
        gb::frontend::TopMenuSection::Network,
        netDrop.x + 6,
        netDrop.y + 6
    );
    T_REQUIRE(netAction.has_value());
    T_EQ(static_cast<int>(netAction.value()), static_cast<int>(gb::frontend::TopMenuAction::CycleLinkMode));
}
