#include <filesystem>
#include <string>
#include <type_traits>

#include "gb/achievements/config/achievement_config.hpp"
#include "gb/achievements/storage/private_file_io.hpp"
#include "gb/app/frontend/realtime/retroachievements_config.hpp"

#include "../../test_framework.hpp"
#include "../../test_utils.hpp"

static_assert(std::is_same_v<
    gb::frontend::RaConfig,
    gb::achievements::AchievementConfig
>);
static_assert(std::is_same_v<
    gb::frontend::RaConfigWipeObserver,
    gb::achievements::AchievementConfigWipeObserver
>);
static_assert(std::is_same_v<
    gb::frontend::detail::PrivateFileIoHooks,
    gb::achievements::storage::PrivateFileIoHooks
>);

TEST_CASE("achievement_config_compatibility", "legacy_config_facade_forwards_to_canonical_storage") {
    const auto path = tests::makeTempPath("achievement_config_legacy", ".cfg");
    tests::ScopedPath cleanup(path);
    const gb::frontend::RaConfig legacy{1, "Ada", "token", true, false};

    T_REQUIRE(gb::frontend::saveRetroAchievementsConfig(path.string(), legacy));
    const auto canonical = gb::achievements::loadAchievementConfig(path.string());
    T_EQ(canonical.username, std::string("Ada"));
    T_EQ(canonical.token, std::string("token"));
    T_REQUIRE(!canonical.showNotifications);
}
