#include <array>

#include "gb/achievements/runtime/owned_achievement_runtime.hpp"
#include "gb/achievements/runtime/owned_achievement_api.hpp"
#include "gb/achievements/protocol/http_transport.hpp"

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

TEST_CASE("owned_achievement_runtime", "owned_api_parses_tolerant_login_response") {
    bool sawSecretWipe = false;
    protocol::HttpTransport transport(
        [](const auto& request) {
            return protocol::HttpResponse{request.id, request.channel, 200L,
                std::vector<std::uint8_t>{'{','"','u','s','e','r','n','a','m','e','"',':','"','u','"',',','"','d','i','s','p','l','a','y','_','n','a','m','e','"',':','"','U','s','e','r','"',',','"','s','c','o','r','e','H','a','r','d','c','o','r','e','"',':','4','2','}'}, {}};
        },
        [&sawSecretWipe](const std::uint8_t*, std::size_t) { sawSecretWipe = true; }
    );
    OwnedAchievementApi api(transport, "https://example.test");
    SecretString token;
    token.assign("secret");
    const auto result = api.loginToken("u", std::move(token));
    T_REQUIRE(result.ok);
    T_EQ(result.user.displayName, std::string("User"));
    T_EQ(result.user.scoreHardcore, 42U);
    T_REQUIRE(sawSecretWipe);
    transport.shutdown();
}

TEST_CASE("owned_achievement_runtime", "owned_api_rejects_malformed_and_http_error") {
    protocol::HttpTransport transport([](const auto& request) {
        return protocol::HttpResponse{request.id, request.channel, 500L, {}, "server"};
    });
    OwnedAchievementApi api(transport, "https://example.test");
    SecretString token;
    token.assign("secret");
    T_REQUIRE(!api.loginToken("u", std::move(token)).ok);
    transport.shutdown();
}

TEST_CASE("owned_achievement_runtime", "hardcore_unlocks_and_mutation_invalidation") {
    OwnedAchievementRuntime runtime([](std::uint32_t, std::uint8_t*, std::size_t) { return std::size_t{0U}; });
    T_REQUIRE(runtime.registerAchievement("key", "Title", "Desc", 5U, passingTrigger()));
    runtime.setHardcoreEnabled(true);
    runtime.doFrame();
    T_EQ(runtime.snapshot().currentGame.unlockedHardcore, 1U);
    T_REQUIRE(runtime.takeEvents().size() == 1U);
    runtime.notifyStateMutation("rewind");
    T_REQUIRE(runtime.hardcoreInvalidated());
    T_EQ(runtime.takeEvents().size(), 1U);
    runtime.notifyStateMutation("cheat");
    T_REQUIRE(runtime.takeEvents().empty());
}

TEST_CASE("owned_achievement_runtime", "casual_and_load_reset_hardcore_state") {
    OwnedAchievementRuntime runtime([](std::uint32_t, std::uint8_t*, std::size_t) { return std::size_t{0U}; });
    T_REQUIRE(runtime.registerAchievement("key", "Title", "Desc", 5U, passingTrigger()));
    runtime.setHardcoreEnabled(false);
    runtime.doFrame();
    T_EQ(runtime.snapshot().currentGame.unlockedCasual, 1U);
    T_EQ(runtime.snapshot().currentGame.unlockedHardcore, 0U);
    runtime.setHardcoreEnabled(true);
    runtime.notifyStateMutation("save state");
    runtime.enqueueLoadGame(1U, "next.gb");
    runtime.processPending();
    T_REQUIRE(!runtime.hardcoreInvalidated());
    T_REQUIRE(runtime.hardcoreEnabled());
}

} // namespace
