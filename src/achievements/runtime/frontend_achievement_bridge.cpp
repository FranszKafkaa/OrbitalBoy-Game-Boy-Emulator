#include "gb/achievements/runtime/frontend_achievement_bridge.hpp"

#include "gb/achievements/adapters/gameboy_memory_reader.hpp"
#include "gb/achievements/adapters/gba_memory_reader.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/core/gba/mgba_core.hpp"

namespace gb::achievements::runtime {

FrontendAchievementBridge::FrontendAchievementBridge(EventCallback callback)
    : callback_(std::move(callback)) {}

FrontendAchievementBridge::~FrontendAchievementBridge() {
    detach();
}

bool FrontendAchievementBridge::addAchievement(std::string key, parser::ConditionTrigger trigger) {
    definitions_.push_back(Definition{std::move(key), std::move(trigger)});
    if (bridge_) {
        return bridge_->addAchievement(definitions_.back().key, definitions_.back().trigger);
    }
    return true;
}

bool FrontendAchievementBridge::attach(gb::GameBoy& gameBoy) {
    detach();
    bridge_ = std::make_unique<AchievementFrameBridge>(adapters::makeGameBoyMemoryReader(gameBoy));
    bridge_->setEventCallback(callback_);
    for (const auto& definition : definitions_) {
        if (!bridge_->addAchievement(definition.key, definition.trigger)) return false;
    }
    clock_ = std::make_unique<AchievementFrameClock>(*bridge_);
    attachment_ = std::make_unique<AchievementFrameAttachment>(*clock_);
    return attachment_->attach(gameBoy);
}

bool FrontendAchievementBridge::attach(gb::gba::MgbaCore& core) {
    detach();
    bridge_ = std::make_unique<AchievementFrameBridge>(adapters::makeGbaMemoryReader(core));
    bridge_->setEventCallback(callback_);
    for (const auto& definition : definitions_) {
        if (!bridge_->addAchievement(definition.key, definition.trigger)) return false;
    }
    clock_ = std::make_unique<AchievementFrameClock>(*bridge_);
    attachment_ = std::make_unique<AchievementFrameAttachment>(*clock_);
    return attachment_->attach(core);
}

bool FrontendAchievementBridge::detach() {
    if (!attachment_) return false;
    const bool detached = attachment_->detach();
    attachment_.reset();
    clock_.reset();
    bridge_.reset();
    return detached;
}

void FrontendAchievementBridge::setEventCallback(EventCallback callback) {
    callback_ = std::move(callback);
    if (bridge_) bridge_->setEventCallback(callback_);
}

} // namespace gb::achievements::runtime
