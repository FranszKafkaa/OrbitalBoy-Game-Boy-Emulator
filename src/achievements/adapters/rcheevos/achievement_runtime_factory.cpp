#include "gb/achievements/runtime/achievement_runtime_factory.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "gb/achievements/adapters/gameboy_memory_reader.hpp"
#include "gb/achievements/runtime/owned_achievement_runtime.hpp"
#include "gb/app/frontend/realtime/retroachievements_config.hpp"

namespace gb::achievements {

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport
) {
    (void)transport;
    auto runtime = std::make_unique<OwnedAchievementRuntime>(gb::achievements::adapters::makeGameBoyMemoryReader(gameBoy));
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
    auto runtime = std::make_unique<OwnedAchievementRuntime>(gb::achievements::adapters::makeGameBoyMemoryReader(gameBoy));
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
    return std::make_unique<OwnedAchievementRuntime>(std::move(reader));
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
