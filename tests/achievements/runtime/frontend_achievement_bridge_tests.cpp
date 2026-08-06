#include "gb/achievements/runtime/frontend_achievement_bridge.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/core/gba/mgba_core.hpp"

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

TEST_CASE("frontend_achievement_bridge", "gb_forwards_single_unlock_and_detach_stops") {
    gb::GameBoy gameBoy;
    gameBoy.bus().write(0xFFFFU, 7U);
    int unlocks = 0;
    runtime::FrontendAchievementBridge helper([&unlocks](const auto& event) {
        if (event.kind == runtime::FrameEventKind::Unlocked) ++unlocks;
    });
    T_REQUIRE(helper.addAchievement("synthetic", trigger()));
    T_REQUIRE(helper.attach(gameBoy));
    gameBoy.runFrame();
    gameBoy.runFrame();
    T_EQ(unlocks, 1);
    T_REQUIRE(helper.detach());
    gameBoy.runFrame();
    T_EQ(unlocks, 1);
}

TEST_CASE("frontend_achievement_bridge", "gba_unloaded_attach_is_safe") {
    gb::gba::MgbaCore core;
    runtime::FrontendAchievementBridge helper;
    T_REQUIRE(helper.attach(core));
    T_REQUIRE(helper.detach());
}

} // namespace
