#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gb/achievements/runtime/achievement_runtime.hpp"
#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS
#include "gb/achievements/runtime/achievement_runtime_factory.hpp"
#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/core/gameboy.hpp"
#endif

#include "../../test_framework.hpp"

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
    void enqueueLogin(std::string, gb::achievements::RaSecretString) override {
        ++loginCount;
    }
    void enqueueTokenLogin(std::string, gb::achievements::RaSecretString) override {
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
    gb::achievements::RaSecretString secret;
    secret.assign("secret");

    runtime->enqueueLogin("user", std::move(secret));
    runtime->enqueueTokenLogin("user", gb::achievements::RaSecretString{});
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
#endif
