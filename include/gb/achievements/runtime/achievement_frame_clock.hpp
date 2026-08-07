#pragma once

#include <functional>
#include <utility>

#include "gb/achievements/runtime/achievement_frame_bridge.hpp"

namespace gb::achievements::runtime {

class AchievementFrameClock {
public:
    using FrameCallback = std::function<void(const FrameEvaluationSummary&)>;

    explicit AchievementFrameClock(AchievementFrameBridge& bridge) : bridge_(bridge) {}

    void setFrameCallback(FrameCallback callback) { callback_ = std::move(callback); }

    [[nodiscard]] FrameEvaluationSummary onFrame() {
        const auto summary = bridge_.evaluateFrame();
        if (callback_) callback_(summary);
        return summary;
    }
    [[nodiscard]] FrameEvaluationSummary evaluateFrame() { return onFrame(); }

private:
    AchievementFrameBridge& bridge_;
    FrameCallback callback_;
};

using FrameHook = AchievementFrameClock;

} // namespace gb::achievements::runtime
