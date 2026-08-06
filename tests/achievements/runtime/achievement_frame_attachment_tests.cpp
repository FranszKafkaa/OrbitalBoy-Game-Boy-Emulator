#include "gb/achievements/adapters/gameboy_memory_reader.hpp"
#include "gb/achievements/runtime/achievement_frame_attachment.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/core/gba/system.hpp"

#include "../../test_framework.hpp"

namespace {

using namespace gb::achievements;

parser::ConditionTrigger trigger() {
    parser::ConditionTrigger result;
    parser::Condition condition;
    condition.left.kind = parser::OperandKind::Memory;
    condition.left.memory.size = parser::MemorySize::EightBit;
    condition.left.memory.address = 0xFFFFU;
    condition.op = parser::Operator::Equal;
    condition.right = parser::Operand{};
    condition.right->kind = parser::OperandKind::Constant;
    condition.right->constant = 7U;
    result.core.conditions.push_back(condition);
    return result;
}

TEST_CASE("achievements_frame_attachment", "attaches_gameboy_and_evaluates_once") {
    gb::GameBoy gameBoy;
    gameBoy.bus().write(0xFFFFU, 7U);
    runtime::AchievementFrameBridge bridge(adapters::makeGameBoyMemoryReader(gameBoy));
    T_REQUIRE(bridge.addAchievement("attached", trigger()));
    runtime::AchievementFrameClock clock(bridge);
    runtime::AchievementFrameAttachment attachment(clock);
    T_REQUIRE(attachment.attach(gameBoy));
    gameBoy.runFrame();
    T_EQ(attachment.lastSummary().evaluated, 1U);
    T_EQ(attachment.lastSummary().unlocked, 1U);
    T_REQUIRE(attachment.detach());
    gameBoy.runFrame();
    T_EQ(attachment.lastSummary().unlocked, 1U);
}

TEST_CASE("achievements_frame_attachment", "reattach_replaces_and_detach_stops_callback") {
    gb::GameBoy first;
    gb::GameBoy second;
    first.bus().write(0xFFFFU, 7U);
    second.bus().write(0xFFFFU, 7U);
    runtime::AchievementFrameBridge bridge(adapters::makeGameBoyMemoryReader(first));
    T_REQUIRE(bridge.addAchievement("attached", trigger()));
    runtime::AchievementFrameClock clock(bridge);
    runtime::AchievementFrameAttachment attachment(clock);
    T_REQUIRE(attachment.attach(first));
    first.runFrame();
    const auto firstSummary = attachment.lastSummary();
    T_REQUIRE(attachment.attach(second));
    second.runFrame();
    T_EQ(attachment.lastSummary().evaluated, firstSummary.evaluated);
    T_REQUIRE(attachment.detach());
    second.runFrame();
    T_REQUIRE(!attachment.attached());
}

TEST_CASE("achievements_frame_attachment", "gba_unloaded_attach_is_safe_and_detachable") {
    gb::gba::System system;
    runtime::AchievementFrameBridge bridge([](std::uint32_t, std::uint8_t*, std::size_t) { return std::size_t{0U}; });
    runtime::AchievementFrameClock clock(bridge);
    runtime::AchievementFrameAttachment attachment(clock);
    T_REQUIRE(attachment.attach(system));
    system.runFrame(false);
    T_REQUIRE(attachment.detach());
}

} // namespace
