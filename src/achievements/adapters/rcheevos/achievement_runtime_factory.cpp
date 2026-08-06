#include "gb/achievements/runtime/achievement_runtime_factory.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

#include "gb/achievements/adapters/gameboy_memory_reader.hpp"
#include "gb/achievements/runtime/owned_achievement_runtime.hpp"
#include "gb/achievements/runtime/owned_achievement_api.hpp"
#include "gb/app/frontend/realtime/retroachievements_config.hpp"

namespace gb::achievements {

namespace {
std::string ownedApiEndpoint() {
    const char* value = std::getenv("GBEMU_OWNED_ACHIEVEMENTS_ENDPOINT");
    return value == nullptr ? std::string{} : std::string(value);
}
}

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport
) {
    (void)transport;
    const auto endpoint = ownedApiEndpoint();
    std::unique_ptr<OwnedAchievementRuntime> runtime;
    if (endpoint.empty()) runtime = std::make_unique<OwnedAchievementRuntime>(gb::achievements::adapters::makeGameBoyMemoryReader(gameBoy));
    else runtime = std::make_unique<OwnedAchievementRuntime>(gb::achievements::adapters::makeGameBoyMemoryReader(gameBoy), std::make_unique<OwnedAchievementApi>(transport.ownedTransport(), endpoint));
    runtime->attach(gameBoy);
    return runtime;
}

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    RaConfigPersistence persistConfig
) {
    (void)transport;
    (void)config;
    (void)persistConfig;
    const auto endpoint = ownedApiEndpoint();
    std::unique_ptr<OwnedAchievementRuntime> runtime;
    if (endpoint.empty()) runtime = std::make_unique<OwnedAchievementRuntime>(gb::achievements::adapters::makeGameBoyMemoryReader(gameBoy));
    else runtime = std::make_unique<OwnedAchievementRuntime>(gb::achievements::adapters::makeGameBoyMemoryReader(gameBoy), std::make_unique<OwnedAchievementApi>(transport.ownedTransport(), endpoint));
    runtime->attach(gameBoy);
    return runtime;
}

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::frontend::RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    gb::frontend::RaHttpTransport& transport
) {
    (void)defaultConsoleId;
    (void)transport;
    memory::MemoryReader reader = [memoryReader = std::move(memoryReader)](std::uint32_t address, std::uint8_t* buffer, std::size_t count) {
        if (!memoryReader || buffer == nullptr || count == 0U) return std::size_t{0U};
        const auto requested = std::min<std::size_t>(count, std::numeric_limits<std::uint32_t>::max());
        return static_cast<std::size_t>(memoryReader(address, buffer, static_cast<std::uint32_t>(requested)));
    };
    const auto endpoint = ownedApiEndpoint();
    if (endpoint.empty()) return std::make_unique<OwnedAchievementRuntime>(std::move(reader));
    return std::make_unique<OwnedAchievementRuntime>(std::move(reader), std::make_unique<OwnedAchievementApi>(transport.ownedTransport(), endpoint));
}

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::frontend::RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    RaConfigPersistence persistConfig
) {
    (void)config;
    (void)persistConfig;
    return makeDefaultAchievementRuntime(std::move(memoryReader), defaultConsoleId, transport);
}

} // namespace gb::achievements
