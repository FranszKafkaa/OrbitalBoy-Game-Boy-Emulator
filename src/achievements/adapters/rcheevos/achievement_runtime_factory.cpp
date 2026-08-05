#include "gb/achievements/runtime/achievement_runtime_factory.hpp"

#include <utility>

#include "gb/achievements/adapters/rcheevos/rcheevos_achievement_runtime.hpp"

namespace gb::achievements {

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport
) {
    return std::make_unique<RcheevosAchievementRuntime>(gameBoy, transport);
}

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    RaConfigPersistence persistConfig
) {
    return std::make_unique<RcheevosAchievementRuntime>(
        gameBoy,
        transport,
        std::move(config),
        std::move(persistConfig)
    );
}

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::frontend::RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    gb::frontend::RaHttpTransport& transport
) {
    return std::make_unique<RcheevosAchievementRuntime>(
        std::move(memoryReader),
        defaultConsoleId,
        transport
    );
}

std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::frontend::RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    RaConfigPersistence persistConfig
) {
    return std::make_unique<RcheevosAchievementRuntime>(
        std::move(memoryReader),
        defaultConsoleId,
        transport,
        std::move(config),
        std::move(persistConfig)
    );
}

} // namespace gb::achievements
