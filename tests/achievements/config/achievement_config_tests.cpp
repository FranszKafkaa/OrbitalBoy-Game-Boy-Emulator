#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gb/achievements/config/achievement_config.hpp"
#include "gb/achievements/storage/private_file_io.hpp"

#include "../../test_framework.hpp"

namespace {

std::filesystem::path temporaryPath(const std::string& name) {
    static std::uint64_t counter = 0;
    const auto now = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / (name + "_" + std::to_string(now) + "_"
           + std::to_string(++counter));
}

class ScopedPath {
public:
    explicit ScopedPath(std::filesystem::path path)
        : path_(std::move(path)) {}

    ~ScopedPath() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

} // namespace

TEST_CASE("achievement_config", "canonical_config_serializes_escaped_v1_values") {
    const auto path = temporaryPath("achievement_config_round_trip");
    ScopedPath cleanup(path);
    const gb::achievements::AchievementConfig expected{
        1, "Ada=Lov\\elace", "token=with\\slashes", false, true
    };

    T_REQUIRE(gb::achievements::saveAchievementConfig(path.string(), expected));
    T_EQ(
        readText(path),
        std::string(
            "version=1\nusername=Ada\\=Lov\\\\elace\ntoken=token\\=with"
            "\\\\slashes\nauto_login=false\nshow_notifications=true\n"
        )
    );

    const auto actual = gb::achievements::loadAchievementConfig(path.string());
    T_EQ(actual.username, expected.username);
    T_EQ(actual.token, expected.token);
    T_REQUIRE(!actual.autoLogin);
    T_REQUIRE(actual.showNotifications);
}

TEST_CASE("achievement_config", "canonical_config_rejects_invalid_and_oversized_sensitive_values") {
    const auto path = temporaryPath("achievement_config_invalid");
    ScopedPath cleanup(path);

    gb::achievements::AchievementConfig controlCharacter{};
    controlCharacter.username = "Ada\nLovelace";
    T_REQUIRE(!gb::achievements::saveAchievementConfig(
        path.string(),
        controlCharacter
    ));

    gb::achievements::AchievementConfig oversized{};
    oversized.token.assign(4097U, 'x');
    T_REQUIRE(!gb::achievements::saveAchievementConfig(path.string(), oversized));
}

TEST_CASE("achievement_config", "canonical_config_invalidates_credentials_durably") {
    const auto path = temporaryPath("achievement_config_invalidate");
    ScopedPath cleanup(path);
    T_REQUIRE(gb::achievements::saveAchievementConfig(
        path.string(),
        {1, "Ada", "old-token", true, true}
    ));

    T_REQUIRE(gb::achievements::invalidateAchievementConfig(path.string()));
    T_REQUIRE(!std::filesystem::exists(path));
}
