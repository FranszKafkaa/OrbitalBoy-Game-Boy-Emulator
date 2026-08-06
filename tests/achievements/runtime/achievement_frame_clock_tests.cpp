#include <array>

#include "gb/achievements/runtime/achievement_frame_bridge.hpp"
#include "gb/achievements/runtime/achievement_frame_clock.hpp"

#include "../../test_framework.hpp"

namespace {

using gb::achievements::memory::MemoryReader;
using gb::achievements::parser::Condition;
using gb::achievements::parser::ConditionTrigger;
using gb::achievements::parser::MemorySize;
using gb::achievements::parser::OperandKind;
using gb::achievements::parser::Operator;
using gb::achievements::runtime::AchievementFrameBridge;
using gb::achievements::runtime::AchievementFrameClock;

ConditionTrigger trigger() {
    ConditionTrigger result;
    Condition condition;
    condition.left.kind = OperandKind::Memory;
    condition.left.memory.size = MemorySize::EightBit;
    condition.left.memory.address = 0U;
    condition.op = Operator::Equal;
    condition.right = gb::achievements::parser::Operand{};
    condition.right->kind = OperandKind::Constant;
    condition.right->constant = 7U;
    result.core.conditions.push_back(condition);
    return result;
}

TEST_CASE("achievements_frame_clock", "evaluates_once_and_forwards_summary") {
    std::array<std::uint8_t, 1U> memory{7U};
    std::size_t reads = 0U;
    MemoryReader reader = [&memory, &reads](std::uint32_t, std::uint8_t* out, std::size_t count) {
        ++reads;
        if (count != 1U) return std::size_t{0U};
        *out = memory[0];
        return std::size_t{1U};
    };
    AchievementFrameBridge bridge(std::move(reader));
    T_REQUIRE(bridge.addAchievement("clocked", trigger()));
    AchievementFrameClock clock(bridge);
    std::size_t callbacks = 0U;
    clock.setFrameCallback([&callbacks](const auto& forwarded) {
        ++callbacks;
        T_EQ(forwarded.unlocked, 1U);
    });
    const auto summary = clock.onFrame();
    T_EQ(summary.evaluated, 1U);
    T_EQ(summary.unlocked, 1U);
    T_EQ(reads, 1U);
    T_EQ(callbacks, 1U);
}

} // namespace
