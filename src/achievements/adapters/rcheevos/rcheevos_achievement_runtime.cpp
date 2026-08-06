#include "gb/achievements/adapters/rcheevos/rcheevos_achievement_runtime.hpp"

// Compatibility-only legacy adapter. The default factory uses owned runtime.

#include <utility>

namespace gb::achievements {

RcheevosAchievementRuntime::RcheevosAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    gb::frontend::RaConfigPersistence persistConfig,
    gb::frontend::RaClientApi* clientApi
)
    : session_(
          gameBoy,
          transport,
          std::move(config),
          std::move(persistConfig),
          clientApi
      ) {
}

RcheevosAchievementRuntime::RcheevosAchievementRuntime(
    gb::frontend::RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    gb::frontend::RaConfigPersistence persistConfig,
    gb::frontend::RaClientApi* clientApi
)
    : session_(
          std::move(memoryReader),
          defaultConsoleId,
          transport,
          std::move(config),
          std::move(persistConfig),
          clientApi
      ) {
}

void RcheevosAchievementRuntime::enqueueLogin(
    std::string username,
    SecretString password
) {
    session_.enqueueLogin(std::move(username), std::move(password));
}

void RcheevosAchievementRuntime::enqueueTokenLogin(
    std::string username,
    SecretString token
) {
    session_.enqueueTokenLogin(std::move(username), std::move(token));
}

void RcheevosAchievementRuntime::enqueueLogout() {
    session_.enqueueLogout();
}

void RcheevosAchievementRuntime::enqueueLoadGame(
    std::uint32_t consoleId,
    std::string romPath
) {
    session_.enqueueLoadGame(consoleId, std::move(romPath));
}

void RcheevosAchievementRuntime::processPending() {
    session_.processPending();
}

void RcheevosAchievementRuntime::doFrame() {
    session_.doFrame();
}

void RcheevosAchievementRuntime::idle() {
    session_.idle();
}

SessionSnapshot RcheevosAchievementRuntime::snapshot() const {
    return session_.snapshot();
}

std::vector<UiEvent> RcheevosAchievementRuntime::takeEvents() {
    return session_.takeEvents();
}

std::vector<std::uint8_t> RcheevosAchievementRuntime::serializeProgress() const {
    return session_.serializeProgress();
}

bool RcheevosAchievementRuntime::deserializeProgress(
    std::string_view romHash,
    const std::vector<std::uint8_t>& payload
) {
    return session_.deserializeProgress(romHash, payload);
}

bool RcheevosAchievementRuntime::resetProgress() {
    return session_.resetProgress();
}

bool RcheevosAchievementRuntime::shutdown() {
    return session_.shutdown();
}

} // namespace gb::achievements
