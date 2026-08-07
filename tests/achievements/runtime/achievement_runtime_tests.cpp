#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gb/achievements/runtime/achievement_runtime.hpp"
#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS
#include "gb/achievements/adapters/rcheevos/rcheevos_achievement_runtime.hpp"
#include "gb/achievements/runtime/achievement_runtime_factory.hpp"
#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/core/gameboy.hpp"
#endif

#include "../../test_framework.hpp"
#include "../../test_utils.hpp"

namespace {

#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS
gb::frontend::RaHttpTransport makeTransport() {
    return gb::frontend::RaHttpTransport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
}
#endif

class RecordingAchievementRuntime final : public gb::achievements::AchievementRuntime {
public:
    void enqueueLogin(std::string, gb::achievements::SecretString) override {
        ++loginCount;
    }
    void enqueueTokenLogin(std::string, gb::achievements::SecretString) override {
        ++tokenLoginCount;
    }
    void enqueueLogout() override {
        ++logoutCount;
    }
    void enqueueLoadGame(std::uint32_t, std::string) override {
        ++loadGameCount;
    }
    void processPending() override {
        ++processPendingCount;
    }
    void doFrame() override {
        ++frameCount;
    }
    void idle() override {
        ++idleCount;
    }
    [[nodiscard]] gb::achievements::SessionSnapshot snapshot() const override {
        return {};
    }
    [[nodiscard]] std::vector<gb::achievements::UiEvent> takeEvents() override {
        return {};
    }
    [[nodiscard]] std::vector<std::uint8_t> serializeProgress() const override {
        return {1U, 2U};
    }
    [[nodiscard]] bool deserializeProgress(
        std::string_view romHash,
        const std::vector<std::uint8_t>& payload
    ) override {
        return romHash == "hash" && payload == std::vector<std::uint8_t>{1U, 2U};
    }
    [[nodiscard]] bool resetProgress() override {
        ++resetCount;
        return true;
    }
    bool shutdown() override {
        ++shutdownCount;
        return true;
    }

    int loginCount = 0;
    int tokenLoginCount = 0;
    int logoutCount = 0;
    int loadGameCount = 0;
    int processPendingCount = 0;
    int frameCount = 0;
    int idleCount = 0;
    int resetCount = 0;
    int shutdownCount = 0;
};

} // namespace

TEST_CASE("achievements_runtime", "public_runtime_contract_supports_polymorphic_session_calls") {
    auto concrete = std::make_unique<RecordingAchievementRuntime>();
    RecordingAchievementRuntime* observed = concrete.get();
    std::unique_ptr<gb::achievements::AchievementRuntime> runtime = std::move(concrete);
    gb::achievements::SecretString secret;
    secret.assign("secret");

    runtime->enqueueLogin("user", std::move(secret));
    runtime->enqueueTokenLogin("user", gb::achievements::SecretString{});
    runtime->enqueueLogout();
    runtime->enqueueLoadGame(5U, "game.gba");
    runtime->processPending();
    runtime->doFrame();
    runtime->idle();
    T_REQUIRE(runtime->snapshot().connectionState == gb::achievements::ConnectionState::Disabled);
    T_REQUIRE(runtime->takeEvents().empty());
    T_REQUIRE((runtime->serializeProgress() == std::vector<std::uint8_t>{1U, 2U}));
    T_REQUIRE(runtime->deserializeProgress("hash", {1U, 2U}));
    T_REQUIRE(runtime->resetProgress());
    T_REQUIRE(runtime->shutdown());

    T_EQ(observed->loginCount, 1);
    T_EQ(observed->tokenLoginCount, 1);
    T_EQ(observed->logoutCount, 1);
    T_EQ(observed->loadGameCount, 1);
    T_EQ(observed->processPendingCount, 1);
    T_EQ(observed->frameCount, 1);
    T_EQ(observed->idleCount, 1);
    T_EQ(observed->resetCount, 1);
    T_EQ(observed->shutdownCount, 1);
}

#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS
TEST_CASE("achievements_runtime", "default_factory_returns_runtime_for_gameboy") {
    gb::GameBoy gameBoy;
    auto transport = makeTransport();

    std::unique_ptr<gb::achievements::AchievementRuntime> runtime =
        gb::achievements::makeDefaultAchievementRuntime(gameBoy, transport);

    T_REQUIRE(runtime != nullptr);
    T_REQUIRE(runtime->snapshot().connectionState == gb::achievements::ConnectionState::LoggedOut);

    runtime->enqueueLogout();
    runtime->processPending();
    runtime->idle();
    T_REQUIRE(runtime->takeEvents().empty());
    T_REQUIRE(runtime->shutdown());
    transport.shutdown();
}

TEST_CASE("achievements_runtime", "factory_uses_legacy_runtime_when_requested") {
    tests::ScopedEnvironmentVariable legacy("GBEMU_USE_LEGACY_RETROACHIEVEMENTS", "1");
    tests::ScopedEnvironmentVariable endpoint("GBEMU_OWNED_ACHIEVEMENTS_ENDPOINT", nullptr);
    gb::GameBoy gameBoy;
    auto transport = makeTransport();

    auto runtime = gb::achievements::makeDefaultAchievementRuntime(gameBoy, transport);

    T_REQUIRE(runtime != nullptr);
    T_REQUIRE(dynamic_cast<gb::achievements::RcheevosAchievementRuntime*>(runtime.get()) != nullptr);
    T_REQUIRE(runtime->snapshot().connectionState == gb::achievements::ConnectionState::LoggedOut);
    T_REQUIRE(runtime->shutdown());
    transport.shutdown();
}

TEST_CASE("achievements_runtime", "default_factory_accepts_generic_memory_reader_for_gba") {
    auto transport = makeTransport();

    std::unique_ptr<gb::achievements::AchievementRuntime> runtime =
        gb::achievements::makeDefaultAchievementRuntime(
            [](std::uint32_t, std::uint8_t*, std::uint32_t) { return 0U; },
            5U,
            transport
        );

    T_REQUIRE(runtime != nullptr);
    T_REQUIRE(runtime->snapshot().connectionState == gb::achievements::ConnectionState::LoggedOut);
    T_REQUIRE(runtime->serializeProgress().empty());
    T_REQUIRE(runtime->shutdown());
    transport.shutdown();
}

TEST_CASE("achievements_runtime", "default_factory_uses_owned_api_when_endpoint_configured") {
    tests::ScopedEnvironmentVariable endpoint("GBEMU_OWNED_ACHIEVEMENTS_ENDPOINT", "https://example.test");
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        const std::vector<std::uint8_t> body{'{','"','u','s','e','r','n','a','m','e','"',':','"','a','p','i','"','}'};
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200L, body, {}};
    });
    auto runtime = gb::achievements::makeDefaultAchievementRuntime(
        [](std::uint32_t, std::uint8_t*, std::uint32_t) { return 0U; }, 5U, transport);
    gb::achievements::SecretString token;
    token.assign("token");
    runtime->enqueueTokenLogin("api", std::move(token));
    runtime->processPending();
    T_REQUIRE(runtime->snapshot().connectionState == gb::achievements::ConnectionState::Online);
    T_EQ(runtime->snapshot().profile.user.username, std::string("api"));
    T_REQUIRE(runtime->shutdown());
    transport.shutdown();
}
#endif
