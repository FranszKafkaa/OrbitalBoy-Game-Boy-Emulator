#include "gb/achievements/runtime/hardcore_frontend_bridge.hpp"

#include "gb/achievements/runtime/owned_achievement_runtime.hpp"

namespace gb::achievements {

void notifyOwnedHardcoreMutation(AchievementRuntime* runtime, std::string_view reason) {
    if (runtime == nullptr) return;
    if (auto* owned = dynamic_cast<OwnedAchievementRuntime*>(runtime)) {
        owned->notifyStateMutation(std::string(reason));
    }
}

} // namespace gb::achievements
