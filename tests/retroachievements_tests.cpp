#include "rc_client.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "gb/app/frontend/realtime/retroachievements_config.hpp"
#include "gb/app/frontend/realtime/retroachievements_models.hpp"
#include "gb/app/runtime_paths.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

#ifndef GBEMU_ENABLE_RETROACHIEVEMENTS
#error "RetroAchievements tests require GBEMU_ENABLE_RETROACHIEVEMENTS"
#endif

namespace {

uint32_t readMemory(uint32_t, uint8_t*, uint32_t, rc_client_t*) {
    return 0;
}

void callServer(const rc_api_request_t*, rc_client_server_callback_t, void*, rc_client_t*) {
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("retroachievements", "client_can_remain_in_casual_mode") {
    rc_client_t* client = rc_client_create(readMemory, callServer);
    T_REQUIRE(client != nullptr);
    rc_client_set_hardcore_enabled(client, 0);
    T_REQUIRE(!rc_client_get_hardcore_enabled(client));
    rc_client_destroy(client);
}

TEST_CASE("retroachievements", "config_round_trips_token_without_password") {
    const auto path = tests::makeTempPath("ra_config", ".cfg");
    tests::ScopedPath cleanup(path);

    const gb::frontend::RaConfig expected{1, "Marcelo=Janke\\V", "token=value\\with\\slashes", true, true};
    T_REQUIRE(gb::frontend::saveRetroAchievementsConfig(path.string(), expected));

    const auto actual = gb::frontend::loadRetroAchievementsConfig(path.string());
    T_EQ(actual.username, expected.username);
    T_EQ(actual.token, expected.token);
    T_REQUIRE(actual.autoLogin);
    T_REQUIRE(actual.showNotifications);
    T_REQUIRE(readTextFile(path).find("password") == std::string::npos);
}

TEST_CASE("retroachievements", "config_missing_or_malformed_uses_safe_defaults") {
    const auto missingPath = tests::makeTempPath("ra_config_missing", ".cfg");
    tests::ScopedPath missingCleanup(missingPath);

    const auto missing = gb::frontend::loadRetroAchievementsConfig(missingPath.string());
    T_EQ(missing.version, 1);
    T_REQUIRE(missing.username.empty());
    T_REQUIRE(missing.token.empty());
    T_REQUIRE(missing.autoLogin);
    T_REQUIRE(missing.showNotifications);

    const auto malformedPath = tests::makeTempPath("ra_config_malformed", ".cfg");
    tests::ScopedPath malformedCleanup(malformedPath);
    {
        std::ofstream out(malformedPath);
        out << "version=one\n";
        out << "username=Marcelo\n";
        out << "token=token-value\n";
        out << "auto_login=perhaps\n";
        out << "show_notifications=not-a-boolean\n";
    }

    const auto malformed = gb::frontend::loadRetroAchievementsConfig(malformedPath.string());
    T_EQ(malformed.version, 1);
    T_REQUIRE(malformed.username.empty());
    T_REQUIRE(malformed.token.empty());
    T_REQUIRE(malformed.autoLogin);
    T_REQUIRE(malformed.showNotifications);

    const auto invalidBooleanPath = tests::makeTempPath("ra_config_invalid_boolean", ".cfg");
    tests::ScopedPath invalidBooleanCleanup(invalidBooleanPath);
    {
        std::ofstream out(invalidBooleanPath);
        out << "version=1\n";
        out << "username=Marcelo\n";
        out << "token=token-value\n";
        out << "auto_login=perhaps\n";
        out << "show_notifications=not-a-boolean\n";
    }

    const auto invalidBoolean = gb::frontend::loadRetroAchievementsConfig(invalidBooleanPath.string());
    T_EQ(invalidBoolean.username, std::string("Marcelo"));
    T_EQ(invalidBoolean.token, std::string("token-value"));
    T_REQUIRE(invalidBoolean.autoLogin);
    T_REQUIRE(invalidBoolean.showNotifications);
}

TEST_CASE("retroachievements", "config_rejects_control_characters_and_oversized_secrets") {
    const auto path = tests::makeTempPath("ra_config_invalid", ".cfg");
    tests::ScopedPath cleanup(path);

    gb::frontend::RaConfig controlCharacter{};
    controlCharacter.username = "Marcelo\nJanke";
    T_REQUIRE(!gb::frontend::saveRetroAchievementsConfig(path.string(), controlCharacter));

    gb::frontend::RaConfig oversized{};
    oversized.token.assign(4097, 'x');
    T_REQUIRE(!gb::frontend::saveRetroAchievementsConfig(path.string(), oversized));
}

TEST_CASE("retroachievements", "ui_models_own_session_values") {
    gb::frontend::RaSessionSnapshot snapshot{};
    snapshot.connectionState = gb::frontend::RaConnectionState::Online;
    snapshot.statusText = "Connected";
    snapshot.profile.user.username = "Marcelo";
    snapshot.profile.library.push_back({42, "Orbital Boy", "https://example.invalid/game.png", "/tmp/game.png", 12, 3, 1});
    snapshot.currentGame = {42, "Orbital Boy", "https://example.invalid/game.png", "/tmp/game.png", 12, 3, 1};
    snapshot.currentAchievements.push_back(
        {7, "First orbit", "Complete orbit one", "https://example.invalid/badge.png", "/tmp/badge.png", 5, true, "1/1"}
    );
    snapshot.gameLoaded = true;

    const auto copied = snapshot;
    T_EQ(copied.profile.user.username, std::string("Marcelo"));
    T_EQ(copied.profile.library.at(0).title, std::string("Orbital Boy"));
    T_EQ(copied.currentAchievements.at(0).title, std::string("First orbit"));
    T_REQUIRE(copied.gameLoaded);
}

TEST_CASE("retroachievements", "global_paths_use_runtime_states_directory") {
    const std::filesystem::path configPath(gb::retroAchievementsConfigPath());
    const std::filesystem::path cacheDirectory(gb::retroAchievementsCacheDirectory());

    T_EQ(configPath.filename().string(), std::string("global.retroachievements"));
    T_EQ(configPath.parent_path().filename().string(), std::string("states"));
    T_EQ(cacheDirectory.filename().string(), std::string("retroachievements-cache"));
    T_EQ(cacheDirectory.parent_path().filename().string(), std::string("states"));
    T_REQUIRE(std::filesystem::is_directory(cacheDirectory));
}
