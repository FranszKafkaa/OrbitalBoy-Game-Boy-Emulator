#pragma once

#include <cstdint>

#include "gb/achievements/runtime/achievement_frame_clock.hpp"

namespace gb { class GameBoy; namespace gba { class System; class MgbaCore; } }

namespace gb::achievements::runtime {

class AchievementFrameAttachment {
public:
    explicit AchievementFrameAttachment(AchievementFrameClock& clock) : clock_(clock) {}
    ~AchievementFrameAttachment();

    AchievementFrameAttachment(const AchievementFrameAttachment&) = delete;
    AchievementFrameAttachment& operator=(const AchievementFrameAttachment&) = delete;

    bool attach(gb::GameBoy& gameBoy);
    bool attach(gb::gba::System& system);
    bool attach(gb::gba::MgbaCore& core);
    bool detach();

    [[nodiscard]] bool attached() const { return attached_; }
    [[nodiscard]] const FrameEvaluationSummary& lastSummary() const { return lastSummary_; }

private:
    enum class CoreKind { None, GameBoy, GbaSystem, MgbaCore };
    void onFrame();

    AchievementFrameClock& clock_;
    gb::GameBoy* gameBoy_ = nullptr;
    gb::gba::System* gbaSystem_ = nullptr;
    gb::gba::MgbaCore* mgbaCore_ = nullptr;
    CoreKind coreKind_ = CoreKind::None;
    bool attached_ = false;
    FrameEvaluationSummary lastSummary_{};
};

} // namespace gb::achievements::runtime
