#pragma once

#include <string_view>

namespace gb::achievements {
class AchievementRuntime;

void notifyOwnedHardcoreMutation(AchievementRuntime* runtime, std::string_view reason);

} // namespace gb::achievements
