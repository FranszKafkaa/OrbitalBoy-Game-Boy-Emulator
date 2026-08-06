#include "gb/achievements/runtime/owned_achievement_runtime.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "gb/core/gameboy.hpp"
#include "gb/core/gba/mgba_core.hpp"
#include "gb/core/gba/system.hpp"

namespace gb::achievements {

OwnedAchievementRuntime::OwnedAchievementRuntime(memory::MemoryReader reader)
    : bridge_(std::move(reader)), clock_(bridge_), attachment_(clock_) {
    snapshot_.connectionState = ConnectionState::LoggedOut;
    bridge_.setEventCallback([this](const auto& event) { handleFrameEvent(event); });
}

OwnedAchievementRuntime::~OwnedAchievementRuntime() { shutdown(); }

bool OwnedAchievementRuntime::registerAchievement(std::string key, std::string title, std::string description,
                                                  std::uint32_t points, parser::ConditionTrigger trigger) {
    if (shutdown_ || key.empty() || !bridge_.addAchievement(key, trigger)) return false;
    definitions_.push_back({std::move(key), AchievementSummary{0U, std::move(title), std::move(description), {}, {}, points, false, {}}, std::move(trigger)});
    definitions_.back().summary.id = static_cast<std::uint32_t>(definitions_.size());
    refreshAchievements();
    return true;
}

bool OwnedAchievementRuntime::attach(gb::GameBoy& gameBoy) { return attachment_.attach(gameBoy); }
bool OwnedAchievementRuntime::attach(gb::gba::System& system) { return attachment_.attach(system); }
bool OwnedAchievementRuntime::attach(gb::gba::MgbaCore& core) { return attachment_.attach(core); }
bool OwnedAchievementRuntime::detach() { return attachment_.detach(); }

void OwnedAchievementRuntime::enqueueLogin(std::string username, SecretString password) {
    pending_.push_back({CommandKind::Login, std::move(username), std::move(password), 0U, {}});
}
void OwnedAchievementRuntime::enqueueTokenLogin(std::string username, SecretString token) {
    pending_.push_back({CommandKind::TokenLogin, std::move(username), std::move(token), 0U, {}});
}
void OwnedAchievementRuntime::enqueueLogout() { pending_.push_back(Command{CommandKind::Logout, {}, SecretString{}, 0U, {}}); }
void OwnedAchievementRuntime::enqueueLoadGame(std::uint32_t consoleId, std::string romPath) {
    pending_.push_back(Command{CommandKind::LoadGame, {}, SecretString{}, consoleId, std::move(romPath)});
}

void OwnedAchievementRuntime::processPending() {
    while (!pending_.empty()) {
        auto command = std::move(pending_.front());
        pending_.pop_front();
        if (command.kind == CommandKind::Logout) {
            snapshot_.connectionState = ConnectionState::LoggedOut;
            snapshot_.profile.user = {};
            continue;
        }
        if (command.kind == CommandKind::Login || command.kind == CommandKind::TokenLogin) {
            if (command.username.empty() || command.secret.empty()) {
                snapshot_.connectionState = ConnectionState::Error;
                snapshot_.errorText = "credentials missing";
            } else {
                snapshot_.connectionState = ConnectionState::Online;
                snapshot_.profile.user.username = command.username;
                snapshot_.profile.user.displayName = command.username;
                snapshot_.errorText.clear();
            }
            continue;
        }
        snapshot_.gameLoaded = true;
        snapshot_.currentGame.gameId = command.consoleId;
        snapshot_.romHash = command.path;
        bridge_.reset();
        for (auto& definition : definitions_) definition.summary.unlocked = false;
        refreshAchievements();
        events_.push_back({UiEventType::GameLoaded, "Game loaded", command.path, 0U, {}});
    }
}

void OwnedAchievementRuntime::doFrame() { if (!shutdown_ && attachment_.attached()) return; if (!shutdown_) bridge_.evaluateFrame(); }
void OwnedAchievementRuntime::idle() {}

SessionSnapshot OwnedAchievementRuntime::snapshot() const { return snapshot_; }
std::vector<UiEvent> OwnedAchievementRuntime::takeEvents() { auto result = std::move(events_); events_.clear(); return result; }

std::vector<std::uint8_t> OwnedAchievementRuntime::serializeProgress() const {
    std::vector<std::uint8_t> payload{'O', 'A', 'R', 1U, 0U, 0U};
    std::size_t count = 0U;
    for (const auto& definition : definitions_) if (definition.summary.unlocked) ++count;
    if (count > std::numeric_limits<std::uint16_t>::max()) return {};
    payload[4] = static_cast<std::uint8_t>(count & 0xFFU);
    payload[5] = static_cast<std::uint8_t>((count >> 8U) & 0xFFU);
    for (const auto& definition : definitions_) {
        if (!definition.summary.unlocked || definition.summary.title.size() > std::numeric_limits<std::uint16_t>::max()) continue;
        const auto& key = definition.key;
        payload.push_back(static_cast<std::uint8_t>(key.size() & 0xFFU));
        payload.push_back(static_cast<std::uint8_t>((key.size() >> 8U) & 0xFFU));
        payload.insert(payload.end(), key.begin(), key.end());
    }
    return payload.size() <= 65536U ? payload : std::vector<std::uint8_t>{};
}

bool OwnedAchievementRuntime::deserializeProgress(std::string_view, const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 6U || payload[0] != 'O' || payload[1] != 'A' || payload[2] != 'R' || payload[3] != 1U) return false;
    const std::size_t count = payload[4] | (static_cast<std::size_t>(payload[5]) << 8U);
    std::size_t offset = 6U;
    for (std::size_t index = 0U; index < count; ++index) {
        if (offset + 2U > payload.size()) return false;
        const std::size_t length = payload[offset] | (static_cast<std::size_t>(payload[offset + 1U]) << 8U);
        offset += 2U;
        if (offset + length > payload.size()) return false;
        const std::string title(reinterpret_cast<const char*>(payload.data() + offset), length);
        offset += length;
        for (auto& definition : definitions_) if (definition.key == title) definition.summary.unlocked = true;
    }
    if (offset != payload.size()) return false;
    refreshAchievements();
    return true;
}

bool OwnedAchievementRuntime::resetProgress() {
    bridge_.reset();
    for (auto& definition : definitions_) definition.summary.unlocked = false;
    refreshAchievements();
    return true;
}

bool OwnedAchievementRuntime::shutdown() {
    if (shutdown_) return true;
    attachment_.detach();
    pending_.clear();
    shutdown_ = true;
    return true;
}

void OwnedAchievementRuntime::handleFrameEvent(const runtime::FrameEvent& event) {
    if (event.kind != runtime::FrameEventKind::Unlocked) return;
    for (auto& definition : definitions_) {
        if (definition.key == event.key) {
            if (definition.summary.unlocked) return;
            definition.summary.unlocked = true;
            events_.push_back({UiEventType::AchievementUnlocked, definition.summary.title, definition.summary.description, definition.summary.points, {}});
        }
    }
    refreshAchievements();
}

void OwnedAchievementRuntime::refreshAchievements() {
    snapshot_.currentAchievements.clear();
    for (const auto& definition : definitions_) snapshot_.currentAchievements.push_back(definition.summary);
}

} // namespace gb::achievements
