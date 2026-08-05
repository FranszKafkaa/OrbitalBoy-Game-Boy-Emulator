#include <type_traits>

#include "gb/achievements/runtime/models.hpp"
#include "gb/app/frontend/realtime/retroachievements_models.hpp"

#include "../../test_framework.hpp"

static_assert(std::is_same_v<gb::frontend::RaConnectionState, gb::achievements::ConnectionState>);
static_assert(std::is_same_v<gb::frontend::RaUiEventType, gb::achievements::UiEventType>);
static_assert(std::is_same_v<gb::frontend::RaUserSummary, gb::achievements::UserSummary>);
static_assert(std::is_same_v<gb::frontend::RaAchievementSummary, gb::achievements::AchievementSummary>);
static_assert(std::is_same_v<gb::frontend::RaGameSummary, gb::achievements::GameSummary>);
static_assert(std::is_same_v<gb::frontend::RaGameProgressSummary, gb::achievements::GameProgressSummary>);
static_assert(std::is_same_v<gb::frontend::RaProfileSummary, gb::achievements::ProfileSummary>);
static_assert(std::is_same_v<gb::frontend::RaSessionSnapshot, gb::achievements::SessionSnapshot>);
static_assert(std::is_same_v<gb::frontend::RaUiEvent, gb::achievements::UiEvent>);

TEST_CASE("achievement_models", "owned_models_preserve_default_session_and_event_values") {
    const gb::achievements::UserSummary user{};
    const gb::achievements::AchievementSummary achievement{};
    const gb::achievements::GameSummary game{};
    const gb::achievements::GameProgressSummary progress{};
    const gb::achievements::ProfileSummary profile{};
    const gb::achievements::SessionSnapshot snapshot{};
    const gb::achievements::UiEvent event{};

    T_REQUIRE(user.username.empty());
    T_REQUIRE(user.displayName.empty());
    T_REQUIRE(user.avatarUrl.empty());
    T_REQUIRE(user.avatarPath.empty());
    T_EQ(user.scoreHardcore, 0U);
    T_EQ(user.scoreCasual, 0U);
    T_EQ(user.unreadMessages, 0U);
    T_EQ(achievement.id, 0U);
    T_REQUIRE(achievement.title.empty());
    T_REQUIRE(achievement.description.empty());
    T_REQUIRE(achievement.badgeUrl.empty());
    T_REQUIRE(achievement.badgePath.empty());
    T_EQ(achievement.points, 0U);
    T_REQUIRE(!achievement.unlocked);
    T_REQUIRE(achievement.measuredProgress.empty());
    T_EQ(game.gameId, 0U);
    T_REQUIRE(game.title.empty());
    T_REQUIRE(game.badgeUrl.empty());
    T_REQUIRE(game.badgePath.empty());
    T_EQ(game.total, 0U);
    T_EQ(game.unlockedCasual, 0U);
    T_EQ(game.unlockedHardcore, 0U);
    T_EQ(progress.gameId, 0U);
    T_REQUIRE(progress.title.empty());
    T_REQUIRE(progress.badgeUrl.empty());
    T_REQUIRE(progress.badgePath.empty());
    T_EQ(progress.total, 0U);
    T_EQ(progress.unlockedCasual, 0U);
    T_EQ(progress.unlockedHardcore, 0U);
    T_REQUIRE(profile.user.username.empty());
    T_REQUIRE(profile.library.empty());
    T_REQUIRE(snapshot.connectionState == gb::achievements::ConnectionState::Disabled);
    T_REQUIRE(snapshot.statusText.empty());
    T_REQUIRE(snapshot.errorText.empty());
    T_REQUIRE(snapshot.profile.library.empty());
    T_EQ(snapshot.currentGame.gameId, 0U);
    T_EQ(snapshot.currentGame.total, 0U);
    T_REQUIRE(snapshot.currentAchievements.empty());
    T_REQUIRE(snapshot.romHash.empty());
    T_EQ(snapshot.connectionGeneration, 0U);
    T_REQUIRE(!snapshot.gameLoaded);
    T_REQUIRE(event.type == gb::achievements::UiEventType::Offline);
    T_REQUIRE(event.title.empty());
    T_REQUIRE(event.detail.empty());
    T_EQ(event.points, 0U);
    T_REQUIRE(event.imagePath.empty());
}

TEST_CASE("achievement_models", "legacy_aliases_support_nested_aggregate_session_data") {
    gb::frontend::RaSessionSnapshot snapshot{
        gb::frontend::RaConnectionState::Online,
        "Connected",
        {},
        {{"player", "Player", "avatar-url", "avatar-path", 120U, 75U, 2U},
         {{7U, "Game Seven", "game-url", "game-path", 9U, 4U, 3U}}},
        {7U, "Game Seven", "game-url", "game-path", 9U, 4U, 3U},
        {{101U, "First", "Description", "badge-url", "badge-path", 5U, true, "3/5"}},
        "rom-hash",
        11U,
        true,
    };
    const gb::achievements::SessionSnapshot& ownedSnapshot = snapshot;

    T_REQUIRE(ownedSnapshot.connectionState == gb::achievements::ConnectionState::Online);
    T_EQ(ownedSnapshot.profile.library.size(), 1U);
    T_EQ(ownedSnapshot.profile.library.front().gameId, 7U);
    T_EQ(ownedSnapshot.currentAchievements.size(), 1U);
    T_REQUIRE(ownedSnapshot.currentAchievements.front().unlocked);
    T_REQUIRE(ownedSnapshot.currentAchievements.front().measuredProgress == "3/5");
    T_REQUIRE(ownedSnapshot.romHash == "rom-hash");
    T_EQ(ownedSnapshot.connectionGeneration, 11U);
    T_REQUIRE(ownedSnapshot.gameLoaded);
}
