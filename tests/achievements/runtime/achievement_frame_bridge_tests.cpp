#include <array>

#include "gb/achievements/runtime/achievement_frame_bridge.hpp"

#include "../../test_framework.hpp"

namespace {

using gb::achievements::memory::MemoryReader;
using gb::achievements::parser::Condition;
using gb::achievements::parser::ConditionTrigger;
using gb::achievements::parser::MemorySize;
using gb::achievements::parser::OperandKind;
using gb::achievements::parser::Operator;
using gb::achievements::runtime::AchievementFrameBridge;
using gb::achievements::runtime::FrameEventKind;
using gb::achievements::runtime::ConditionEvaluationStatus;

Condition equalByte(std::uint32_t address, std::uint32_t value) {
    Condition condition;
    condition.left.kind = OperandKind::Memory;
    condition.left.memory.size = MemorySize::EightBit;
    condition.left.memory.address = address;
    condition.op = Operator::Equal;
    condition.right = gb::achievements::parser::Operand{};
    condition.right->kind = OperandKind::Constant;
    condition.right->constant = value;
    return condition;
}

ConditionTrigger triggerFor(std::uint32_t address, std::uint32_t value) {
    ConditionTrigger trigger;
    trigger.core.conditions.push_back(equalByte(address, value));
    return trigger;
}

TEST_CASE("achievements_frame_bridge", "emits_one_unlock_and_suppresses_duplicates") {
    std::array<std::uint8_t, 1U> memory{7U};
    MemoryReader reader = [&memory](std::uint32_t address, std::uint8_t* out, std::size_t count) {
        if (address != 0U || count != 1U) return std::size_t{0U};
        *out = memory[0];
        return std::size_t{1U};
    };
    AchievementFrameBridge bridge(std::move(reader));
    std::vector<FrameEventKind> events;
    bridge.setEventCallback([&events](const auto& event) { events.push_back(event.kind); });
    T_REQUIRE(bridge.addAchievement("one", triggerFor(0U, 7U)));

    T_EQ(bridge.evaluateFrame().unlocked, 1U);
    T_EQ(bridge.evaluateFrame().unlocked, 0U);
    T_EQ(events.size(), 1U);
    T_REQUIRE(events.front() == FrameEventKind::Unlocked);
}

TEST_CASE("achievements_frame_bridge", "tracks_locked_later_unlocks_and_independent_entries") {
    std::array<std::uint8_t, 2U> memory{0U, 2U};
    MemoryReader reader = [&memory](std::uint32_t address, std::uint8_t* out, std::size_t count) {
        if (address >= memory.size() || count != 1U) return std::size_t{0U};
        *out = memory[address];
        return std::size_t{1U};
    };
    AchievementFrameBridge bridge(std::move(reader));
    std::vector<std::string> unlocked;
    bridge.setEventCallback([&unlocked](const auto& event) { if (event.kind == FrameEventKind::Unlocked) unlocked.push_back(event.key); });
    T_REQUIRE(bridge.addAchievement("first", triggerFor(0U, 1U)));
    T_REQUIRE(bridge.addAchievement("second", triggerFor(1U, 2U)));

    T_EQ(bridge.evaluateFrame().unlocked, 1U);
    memory[0] = 1U;
    T_EQ(bridge.evaluateFrame().unlocked, 1U);
    T_EQ(unlocked.size(), 2U);
}

TEST_CASE("achievements_frame_bridge", "reports_errors_without_unlocking_and_reset_rearms") {
    std::array<std::uint8_t, 1U> memory{0U};
    AchievementFrameBridge bridge([&memory](std::uint32_t, std::uint8_t* out, std::size_t) { *out = memory[0]; return std::size_t{1U}; });
    std::vector<FrameEventKind> events;
    bridge.setEventCallback([&events](const auto& event) { events.push_back(event.kind); });
    auto unsupported = triggerFor(0U, 1U);
    unsupported.core.conditions.front().left.memory.size = MemorySize::Float;
    T_REQUIRE(bridge.addAchievement("bad", unsupported));
    T_REQUIRE(bridge.addAchievement("good", triggerFor(0U, 0U)));
    const auto summary = bridge.evaluateFrame();
    T_EQ(summary.errors, 1U);
    T_EQ(summary.unlocked, 1U);
    T_REQUIRE(events.size() == 2U);
    T_REQUIRE(events.at(0U) == FrameEventKind::Error);
    bridge.reset();
    T_EQ(bridge.evaluateFrame().unlocked, 1U);
}

} // namespace
