#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gb/achievements/runtime/models.hpp"
#include "gb/achievements/security/secret_string.hpp"

namespace gb::achievements {

class AchievementRuntime {
public:
    virtual ~AchievementRuntime();

    virtual void enqueueLogin(std::string username, SecretString password) = 0;
    virtual void enqueueTokenLogin(std::string username, SecretString token) = 0;
    virtual void enqueueLogout() = 0;
    virtual void enqueueLoadGame(std::uint32_t consoleId, std::string romPath) = 0;

    virtual void processPending() = 0;
    virtual void doFrame() = 0;
    virtual void idle() = 0;
    [[nodiscard]] virtual SessionSnapshot snapshot() const = 0;
    [[nodiscard]] virtual std::vector<UiEvent> takeEvents() = 0;

    [[nodiscard]] virtual std::vector<std::uint8_t> serializeProgress() const = 0;
    [[nodiscard]] virtual bool deserializeProgress(
        std::string_view romHash,
        const std::vector<std::uint8_t>& payload
    ) = 0;
    [[nodiscard]] virtual bool resetProgress() = 0;
    virtual bool shutdown() = 0;
};

} // namespace gb::achievements
