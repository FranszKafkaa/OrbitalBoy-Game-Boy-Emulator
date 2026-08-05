#pragma once

#include "gb/achievements/runtime/achievement_runtime.hpp"
#include "gb/app/frontend/realtime/retroachievements_session.hpp"

namespace gb::achievements {

class RcheevosAchievementRuntime final : public AchievementRuntime {
public:
    RcheevosAchievementRuntime(
        gb::GameBoy& gameBoy,
        gb::frontend::RaHttpTransport& transport,
        gb::frontend::RaConfig config = {},
        gb::frontend::RaConfigPersistence persistConfig = {},
        gb::frontend::RaClientApi* clientApi = nullptr
    );
    RcheevosAchievementRuntime(
        gb::frontend::RaMemoryReader memoryReader,
        std::uint32_t defaultConsoleId,
        gb::frontend::RaHttpTransport& transport,
        gb::frontend::RaConfig config = {},
        gb::frontend::RaConfigPersistence persistConfig = {},
        gb::frontend::RaClientApi* clientApi = nullptr
    );

    void enqueueLogin(std::string username, RaSecretString password) override;
    void enqueueTokenLogin(std::string username, RaSecretString token) override;
    void enqueueLogout() override;
    void enqueueLoadGame(std::uint32_t consoleId, std::string romPath) override;
    void processPending() override;
    void doFrame() override;
    void idle() override;
    [[nodiscard]] RaSessionSnapshot snapshot() const override;
    [[nodiscard]] std::vector<RaUiEvent> takeEvents() override;
    [[nodiscard]] std::vector<std::uint8_t> serializeProgress() const override;
    [[nodiscard]] bool deserializeProgress(
        std::string_view romHash,
        const std::vector<std::uint8_t>& payload
    ) override;
    [[nodiscard]] bool resetProgress() override;
    bool shutdown() override;

private:
    gb::frontend::RetroAchievementsSession session_;
};

} // namespace gb::achievements
