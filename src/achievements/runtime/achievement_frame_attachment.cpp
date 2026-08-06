#include "gb/achievements/runtime/achievement_frame_attachment.hpp"

#include "gb/core/gameboy.hpp"
#include "gb/core/gba/system.hpp"
#include "gb/core/gba/mgba_core.hpp"

namespace gb::achievements::runtime {

AchievementFrameAttachment::~AchievementFrameAttachment() {
    detach();
}

void AchievementFrameAttachment::onFrame() {
    lastSummary_ = clock_.onFrame();
}

bool AchievementFrameAttachment::attach(gb::GameBoy& gameBoy) {
    detach();
    gameBoy_ = &gameBoy;
    coreKind_ = CoreKind::GameBoy;
    attached_ = true;
    gameBoy_->setFrameObserver([this] { onFrame(); });
    return true;
}

bool AchievementFrameAttachment::attach(gb::gba::System& system) {
    detach();
    gbaSystem_ = &system;
    coreKind_ = CoreKind::GbaSystem;
    attached_ = true;
    gbaSystem_->setFrameObserver([this] { onFrame(); });
    return true;
}

bool AchievementFrameAttachment::attach(gb::gba::MgbaCore& core) {
    detach();
    mgbaCore_ = &core;
    coreKind_ = CoreKind::MgbaCore;
    attached_ = true;
    mgbaCore_->setFrameObserver([this] { onFrame(); });
    return true;
}

bool AchievementFrameAttachment::detach() {
    if (!attached_) return false;
    // Ownership rule: callers must not replace a core observer while attached;
    // setFrameObserver({}) is therefore safe for this attachment's callback.
    if (coreKind_ == CoreKind::GameBoy && gameBoy_ != nullptr) {
        gameBoy_->setFrameObserver({});
    } else if (coreKind_ == CoreKind::GbaSystem && gbaSystem_ != nullptr) {
        gbaSystem_->setFrameObserver({});
    } else if (coreKind_ == CoreKind::MgbaCore && mgbaCore_ != nullptr) {
        mgbaCore_->setFrameObserver({});
    }
    gameBoy_ = nullptr;
    gbaSystem_ = nullptr;
    mgbaCore_ = nullptr;
    coreKind_ = CoreKind::None;
    attached_ = false;
    return true;
}

} // namespace gb::achievements::runtime
