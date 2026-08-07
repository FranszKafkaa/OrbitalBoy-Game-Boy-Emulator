#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gb::achievements {

enum class ConnectionState {
    Disabled,
    LoggedOut,
    LoggingIn,
    Online,
    Offline,
    Error,
};

enum class UiEventType {
    LoginSucceeded,
    LoginFailed,
    LoginRequired,
    AchievementUnlocked,
    HardcoreInvalidated,
    GameLoaded,
    Offline,
    Reconnected,
};

struct UserSummary {
    std::string username;
    std::string displayName;
    std::string avatarUrl;
    std::string avatarPath;
    std::uint32_t scoreHardcore = 0;
    std::uint32_t scoreCasual = 0;
    std::uint32_t unreadMessages = 0;
};

struct AchievementSummary {
    std::uint32_t id = 0;
    std::string title;
    std::string description;
    std::string badgeUrl;
    std::string badgePath;
    std::uint32_t points = 0;
    bool unlocked = false;
    std::string measuredProgress;
};

struct GameSummary {
    std::uint32_t gameId = 0;
    std::string title;
    std::string badgeUrl;
    std::string badgePath;
    std::uint32_t total = 0;
    std::uint32_t unlockedCasual = 0;
    std::uint32_t unlockedHardcore = 0;
};

struct GameProgressSummary {
    std::uint32_t gameId = 0;
    std::string title;
    std::string badgeUrl;
    std::string badgePath;
    std::uint32_t total = 0;
    std::uint32_t unlockedCasual = 0;
    std::uint32_t unlockedHardcore = 0;
};

struct ProfileSummary {
    UserSummary user;
    std::vector<GameProgressSummary> library;
};

struct SessionSnapshot {
    ConnectionState connectionState = ConnectionState::Disabled;
    std::string statusText;
    std::string errorText;
    ProfileSummary profile;
    GameSummary currentGame;
    std::vector<AchievementSummary> currentAchievements;
    std::string romHash;
    std::uint64_t connectionGeneration = 0;
    bool gameLoaded = false;
};

struct UiEvent {
    UiEventType type = UiEventType::Offline;
    std::string title;
    std::string detail;
    std::uint32_t points = 0;
    std::string imagePath;
};

} // namespace gb::achievements
