#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "gb/achievements/runtime/achievement_frame_attachment.hpp"
#include "gb/achievements/runtime/achievement_runtime.hpp"

namespace gb { class GameBoy; namespace gba { class System; class MgbaCore; } }

namespace gb::achievements {

class OwnedAchievementApi;

class OwnedAchievementRuntime final : public AchievementRuntime {
public:
    explicit OwnedAchievementRuntime(memory::MemoryReader reader);
    OwnedAchievementRuntime(memory::MemoryReader reader, OwnedAchievementApi& api);
    ~OwnedAchievementRuntime() override;

    bool registerAchievement(std::string key, std::string title, std::string description,
                             std::uint32_t points, parser::ConditionTrigger trigger);
    bool attach(gb::GameBoy& gameBoy);
    bool attach(gb::gba::System& system);
    bool attach(gb::gba::MgbaCore& core);
    bool detach();

    void enqueueLogin(std::string username, SecretString password) override;
    void enqueueTokenLogin(std::string username, SecretString token) override;
    void enqueueLogout() override;
    void enqueueLoadGame(std::uint32_t consoleId, std::string romPath) override;
    void processPending() override;
    void doFrame() override;
    void idle() override;
    [[nodiscard]] SessionSnapshot snapshot() const override;
    [[nodiscard]] std::vector<UiEvent> takeEvents() override;
    [[nodiscard]] std::vector<std::uint8_t> serializeProgress() const override;
    [[nodiscard]] bool deserializeProgress(std::string_view romHash, const std::vector<std::uint8_t>& payload) override;
    [[nodiscard]] bool resetProgress() override;
    bool shutdown() override;

private:
    struct Definition { std::string key; AchievementSummary summary; parser::ConditionTrigger trigger; };
    enum class CommandKind { Login, TokenLogin, Logout, LoadGame };
    struct Command { CommandKind kind; std::string username; SecretString secret; std::uint32_t consoleId = 0; std::string path; };
    void handleFrameEvent(const runtime::FrameEvent& event);
    void refreshAchievements();

    runtime::AchievementFrameBridge bridge_;
    runtime::AchievementFrameClock clock_;
    runtime::AchievementFrameAttachment attachment_;
    std::vector<Definition> definitions_;
    std::deque<Command> pending_;
    std::vector<UiEvent> events_;
    SessionSnapshot snapshot_{};
    bool shutdown_ = false;
    OwnedAchievementApi* api_ = nullptr;
};

} // namespace gb::achievements
