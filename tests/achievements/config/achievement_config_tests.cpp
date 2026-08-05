#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "gb/achievements/config/achievement_config.hpp"
#include "gb/achievements/storage/private_file_io.hpp"

#include "../achievement_test_utils.hpp"
#include "../../test_framework.hpp"

namespace {
using achievement_tests::ScopedPath;
using achievement_tests::readText;
using achievement_tests::temporaryPath;
using achievement_tests::writeText;

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

TEST_CASE("achievement_config", "canonical_config_wipes_tokens_during_copy_move_assignment_and_destruction") {
    std::vector<std::size_t> wipedSizes;
    bool allZeroes = true;
    const gb::achievements::AchievementConfigWipeObserver observer =
        [&](const char* bytes, std::size_t size) {
            wipedSizes.push_back(size);
            allZeroes = allZeroes && bytes != nullptr
                && std::all_of(bytes, bytes + size, [](char byte) {
                    return byte == '\0';
                });
        };

    std::size_t wipesBeforeDestruction = 0U;
    {
        gb::achievements::AchievementConfig original{
            1, "Ada", "original-token", true, true, observer
        };
        gb::achievements::AchievementConfig assigned{
            1, "Ada", "old-token", true, true, observer
        };
        assigned = original;
        T_EQ(assigned.token, std::string("original-token"));

        gb::achievements::AchievementConfig copied = assigned;
        T_EQ(copied.token, std::string("original-token"));

        gb::achievements::AchievementConfig moved(std::move(assigned));
        T_REQUIRE(assigned.token.empty());
        T_EQ(moved.token, std::string("original-token"));

        gb::achievements::AchievementConfig target{
            1, "Ada", "replace-token", true, true, observer
        };
        target = std::move(moved);
        T_REQUIRE(moved.token.empty());
        T_EQ(target.token, std::string("original-token"));
        wipesBeforeDestruction = wipedSizes.size();
    }

    T_REQUIRE(wipedSizes.size() >= wipesBeforeDestruction + 3U);
    T_REQUIRE(allZeroes);
}

TEST_CASE("achievement_config", "canonical_config_malformed_version_wipes_loaded_token_and_returns_defaults") {
    const auto path = temporaryPath("achievement_config_malformed_wipe");
    ScopedPath cleanup(path);
    writeText(path, "token=short-token\nversion=not-one\n");

    std::size_t wipedBuffers = 0U;
    bool allZeroes = true;
    const auto loaded = gb::achievements::loadAchievementConfig(
        path.string(),
        [&](const char* bytes, std::size_t size) {
            ++wipedBuffers;
            allZeroes = allZeroes && bytes != nullptr
                && std::all_of(bytes, bytes + size, [](char byte) {
                    return byte == '\0';
                });
        }
    );

    T_EQ(loaded.version, 1);
    T_REQUIRE(loaded.username.empty());
    T_REQUIRE(loaded.token.empty());
    T_REQUIRE(loaded.autoLogin);
    T_REQUIRE(loaded.showNotifications);
    T_REQUIRE(wipedBuffers >= 2U);
    T_REQUIRE(allZeroes);
}

TEST_CASE("achievement_config", "canonical_config_rejects_oversized_files_before_parsing") {
    const auto path = temporaryPath("achievement_config_oversized_file");
    ScopedPath cleanup(path);
    std::string contents = "version=1\nusername=Ada\ntoken=must-not-load\n";
    while (contents.size() <= 4096U) {
        contents += "ignored=x\n";
    }
    writeText(path, contents);

    const auto loaded = gb::achievements::loadAchievementConfig(path.string());
    T_REQUIRE(loaded.username.empty());
    T_REQUIRE(loaded.token.empty());
}

TEST_CASE("achievement_config", "canonical_config_ignores_malformed_escapes_and_booleans") {
    const auto path = temporaryPath("achievement_config_malformed_values");
    ScopedPath cleanup(path);
    writeText(
        path,
        "version=1\nusername=Ada\\q\ntoken=token\\q\n"
        "auto_login=perhaps\nshow_notifications=nope\n"
    );

    const auto loaded = gb::achievements::loadAchievementConfig(path.string());
    T_EQ(loaded.version, 1);
    T_REQUIRE(loaded.username.empty());
    T_REQUIRE(loaded.token.empty());
    T_REQUIRE(loaded.autoLogin);
    T_REQUIRE(loaded.showNotifications);
}

#if !defined(_WIN32)
TEST_CASE("achievement_config", "canonical_invalidation_publishes_empty_config_then_durably_removes_it") {
    const auto path = temporaryPath("achievement_config_safe_invalidate");
    ScopedPath cleanup(path);
    T_REQUIRE(gb::achievements::saveAchievementConfig(
        path.string(),
        {1, "Ada", "old-token", true, true}
    ));
    std::vector<gb::achievements::storage::PrivateFileIoEvent> trace;
    gb::achievements::storage::PrivateFileIoHooks hooks{};
    hooks.trace = [&](const auto event, const auto&) { trace.push_back(event); };
    hooks.syncDirectory = [](const auto&) { return true; };

    T_REQUIRE(gb::achievements::invalidateAchievementConfig(
        path.string(),
        nullptr,
        &hooks
    ));
    T_REQUIRE(trace == std::vector<gb::achievements::storage::PrivateFileIoEvent>({
        gb::achievements::storage::PrivateFileIoEvent::TemporaryCreated,
        gb::achievements::storage::PrivateFileIoEvent::TemporarySynced,
        gb::achievements::storage::PrivateFileIoEvent::Replaced,
        gb::achievements::storage::PrivateFileIoEvent::DirectorySynced,
        gb::achievements::storage::PrivateFileIoEvent::Removed,
        gb::achievements::storage::PrivateFileIoEvent::DirectorySynced,
    }));
}

TEST_CASE("achievement_config", "canonical_invalidation_quarantines_sensitive_file_when_empty_publish_fails") {
    const auto directory = temporaryPath("achievement_config_quarantine");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto path = directory / "config";
    T_REQUIRE(gb::achievements::saveAchievementConfig(
        path.string(),
        {1, "Ada", "old-token", true, true}
    ));
    gb::achievements::storage::PrivateFileIoHooks hooks{};
    hooks.chooseTemporaryPath = [&](const auto&, int) {
        return directory.parent_path() / "not-a-sibling";
    };
    bool quarantined = false;

    T_REQUIRE(gb::achievements::invalidateAchievementConfig(
        path.string(),
        &quarantined,
        &hooks
    ));
    T_REQUIRE(quarantined);
    T_REQUIRE(!std::filesystem::exists(path));
    bool foundQuarantine = false;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        foundQuarantine = foundQuarantine
            || entry.path().filename().string().find("config.invalid.") == 0U;
    }
    T_REQUIRE(foundQuarantine);
}
#endif
