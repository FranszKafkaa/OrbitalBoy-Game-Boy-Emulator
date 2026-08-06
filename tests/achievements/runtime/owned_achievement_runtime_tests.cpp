#include <array>

#include "gb/achievements/runtime/owned_achievement_runtime.hpp"

#include "../../test_framework.hpp"

namespace {
using namespace gb::achievements;

parser::ConditionTrigger passingTrigger() {
    parser::ConditionTrigger trigger;
    parser::Condition condition;
    condition.left.kind = parser::OperandKind::Constant;
    condition.left.constant = 1U;
    condition.op = parser::Operator::Equal;
    condition.right = parser::Operand{};
    condition.right->kind = parser::OperandKind::Constant;
    condition.right->constant = 1U;
    trigger.core.conditions.push_back(condition);
    return trigger;
}

TEST_CASE("owned_achievement_runtime", "registers_unlocks_once_and_serializes_progress") {
    OwnedAchievementRuntime runtime([](std::uint32_t, std::uint8_t*, std::size_t) { return std::size_t{0U}; });
    T_REQUIRE(runtime.registerAchievement("key", "Title", "Desc", 5U, passingTrigger()));
    runtime.doFrame();
    auto snapshot = runtime.snapshot();
    T_REQUIRE(snapshot.currentAchievements.front().unlocked);
    auto events = runtime.takeEvents();
    T_EQ(events.size(), 1U);
    runtime.doFrame();
    T_REQUIRE(runtime.takeEvents().empty());
    const auto payload = runtime.serializeProgress();
    OwnedAchievementRuntime restored([](std::uint32_t, std::uint8_t*, std::size_t) { return std::size_t{0U}; });
    T_REQUIRE(restored.registerAchievement("key", "Title", "Desc", 5U, passingTrigger()));
    T_REQUIRE(restored.deserializeProgress("", payload));
    T_REQUIRE(restored.snapshot().currentAchievements.front().unlocked);
}

TEST_CASE("owned_achievement_runtime", "malformed_progress_and_reset_are_safe") {
    OwnedAchievementRuntime runtime([](std::uint32_t, std::uint8_t*, std::size_t) { return std::size_t{0U}; });
    T_REQUIRE(runtime.registerAchievement("key", "Title", "Desc", 5U, passingTrigger()));
    T_REQUIRE(!runtime.deserializeProgress("", std::vector<std::uint8_t>{'O', 'A', 'R', 1U, 1U}));
    runtime.doFrame();
    T_REQUIRE(runtime.resetProgress());
    T_REQUIRE(!runtime.snapshot().currentAchievements.front().unlocked);
}

TEST_CASE("owned_achievement_runtime", "login_logout_and_load_game_preserve_session") {
    OwnedAchievementRuntime runtime([](std::uint32_t, std::uint8_t*, std::size_t) { return std::size_t{0U}; });
    SecretString secret;
    secret.assign("token");
    runtime.enqueueTokenLogin("user", std::move(secret));
    runtime.processPending();
    T_REQUIRE(runtime.snapshot().connectionState == ConnectionState::Online);
    runtime.enqueueLoadGame(5U, "demo.gba");
    runtime.processPending();
    T_REQUIRE(runtime.snapshot().gameLoaded);
    T_REQUIRE(runtime.snapshot().connectionState == ConnectionState::Online);
    runtime.enqueueLogout();
    runtime.processPending();
    T_REQUIRE(runtime.snapshot().connectionState == ConnectionState::LoggedOut);
    T_REQUIRE(runtime.shutdown());
}

} // namespace
