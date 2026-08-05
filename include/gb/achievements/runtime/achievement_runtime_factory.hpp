#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "gb/achievements/runtime/achievement_runtime.hpp"

#ifdef GBEMU_ENABLE_RETROACHIEVEMENTS
namespace gb {
class GameBoy;
}

namespace gb::achievements {
struct AchievementConfig;
}

namespace gb::frontend {
class RaHttpTransport;
using RaMemoryReader = std::function<std::uint32_t(
    std::uint32_t,
    std::uint8_t*,
    std::uint32_t
)>;
}

namespace gb::achievements {

using RaConfigPersistence = std::function<bool(const AchievementConfig&)>;

[[nodiscard]] std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport
);
[[nodiscard]] std::unique_ptr<AchievementRuntime> makeDefaultAchievementRuntime(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport,
    AchievementConfig config,
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
    AchievementConfig config,
    RaConfigPersistence persistConfig
);

} // namespace gb::achievements
#endif
