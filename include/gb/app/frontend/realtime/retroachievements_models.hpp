#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gb::frontend {

enum class RaConnectionState {
    Disabled,
    LoggedOut,
    LoggingIn,
    Online,
    Offline,
    Error,
};

enum class RaUiEventType {
    LoginSucceeded,
    LoginFailed,
    AchievementUnlocked,
    GameLoaded,
    Offline,
};

struct RaUserSummary {
    std::string username;
    std::string displayName;
    std::string avatarUrl;
    std::string avatarPath;
    std::uint32_t scoreHardcore = 0;
    std::uint32_t scoreCasual = 0;
    std::uint32_t unreadMessages = 0;
};

struct RaAchievementSummary {
    std::uint32_t id = 0;
    std::string title;
    std::string description;
    std::string badgeUrl;
    std::string badgePath;
    std::uint32_t points = 0;
    bool unlocked = false;
    std::string measuredProgress;
};

struct RaGameSummary {
    std::uint32_t gameId = 0;
    std::string title;
    std::string badgeUrl;
    std::string badgePath;
    std::uint32_t total = 0;
    std::uint32_t unlockedCasual = 0;
    std::uint32_t unlockedHardcore = 0;
};

struct RaGameProgressSummary {
    std::uint32_t gameId = 0;
    std::string title;
    std::string badgeUrl;
    std::string badgePath;
    std::uint32_t total = 0;
    std::uint32_t unlockedCasual = 0;
    std::uint32_t unlockedHardcore = 0;
};

struct RaProfileSummary {
    RaUserSummary user;
    std::vector<RaGameProgressSummary> library;
};

struct RaSessionSnapshot {
    RaConnectionState connectionState = RaConnectionState::Disabled;
    std::string statusText;
    std::string errorText;
    RaProfileSummary profile;
    RaGameSummary currentGame;
    std::vector<RaAchievementSummary> currentAchievements;
    bool gameLoaded = false;
};

struct RaUiEvent {
    RaUiEventType type = RaUiEventType::Offline;
    std::string title;
    std::string detail;
    std::uint32_t points = 0;
    std::string imagePath;
};

} // namespace gb::frontend
