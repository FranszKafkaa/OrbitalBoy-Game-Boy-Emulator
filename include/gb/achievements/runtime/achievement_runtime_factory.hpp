#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "gb/achievements/runtime/achievement_runtime.hpp"

#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS
namespace gb {
class GameBoy;
}

namespace gb::frontend {
class RaHttpTransport;
struct RaConfig;
using RaMemoryReader = std::function<std::uint32_t(
    std::uint32_t,
    std::uint8_t*,
    std::uint32_t
)>;
}

namespace gb::achievements {

using RaConfigPersistence = std::function<bool(const gb::frontend::RaConfig&)>;

[[nodiscard]] std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport
);
[[nodiscard]] std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    RaConfigPersistence persistConfig
);
[[nodiscard]] std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::frontend::RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    gb::frontend::RaHttpTransport& transport
);
[[nodiscard]] std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::frontend::RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaConfig config,
    RaConfigPersistence persistConfig
);

} // namespace gb::achievements
#endif
