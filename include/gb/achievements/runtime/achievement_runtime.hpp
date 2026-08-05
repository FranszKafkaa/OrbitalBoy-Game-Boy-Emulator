#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gb/app/frontend/realtime/retroachievements_models.hpp"
#include "gb/app/frontend/realtime/secure_string.hpp"

namespace gb::achievements {

using RaSecretString = gb::frontend::RaSecretString;
using RaSessionSnapshot = gb::frontend::RaSessionSnapshot;
using RaUiEvent = gb::frontend::RaUiEvent;

class AchievementRuntime {
public:
    virtual ~AchievementRuntime();

    virtual void enqueueLogin(std::string username, RaSecretString password) = 0;
    virtual void enqueueTokenLogin(std::string username, RaSecretString token) = 0;
    virtual void enqueueLogout() = 0;
    virtual void enqueueLoadGame(std::uint32_t consoleId, std::string romPath) = 0;

    virtual void processPending() = 0;
    virtual void doFrame() = 0;
    virtual void idle() = 0;
    [[nodiscard]] virtual RaSessionSnapshot snapshot() const = 0;
    [[nodiscard]] virtual std::vector<RaUiEvent> takeEvents() = 0;

    [[nodiscard]] virtual std::vector<std::uint8_t> serializeProgress() const = 0;
    [[nodiscard]] virtual bool deserializeProgress(
        std::string_view romHash,
        const std::vector<std::uint8_t>& payload
    ) = 0;
    [[nodiscard]] virtual bool resetProgress() = 0;
    virtual bool shutdown() = 0;
};

} // namespace gb::achievements
