#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gb/achievements/parser/condition_parser.hpp"
#include "gb/achievements/runtime/achievement_frame_attachment.hpp"

namespace gb { class GameBoy; namespace gba { class MgbaCore; } }

namespace gb::achievements::runtime {

class FrontendAchievementBridge {
public:
    using EventCallback = std::function<void(const FrameEvent&)>;

    explicit FrontendAchievementBridge(EventCallback callback = {});
    ~FrontendAchievementBridge();

    bool addAchievement(std::string key, parser::ConditionTrigger trigger);
    bool attach(gb::GameBoy& gameBoy);
    bool attach(gb::gba::MgbaCore& core);
    bool detach();
    void setEventCallback(EventCallback callback);
    [[nodiscard]] bool attached() const { return attachment_ != nullptr && attachment_->attached(); }

private:
    struct Definition { std::string key; parser::ConditionTrigger trigger; };
    EventCallback callback_;
    std::vector<Definition> definitions_;
    std::unique_ptr<AchievementFrameBridge> bridge_;
    std::unique_ptr<AchievementFrameClock> clock_;
    std::unique_ptr<AchievementFrameAttachment> attachment_;
};

} // namespace gb::achievements::runtime
