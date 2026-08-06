#include "gb/achievements/runtime/achievement_frame_bridge.hpp"

#include <utility>

namespace gb::achievements::runtime {

AchievementFrameBridge::AchievementFrameBridge(memory::MemoryReader reader)
    : evaluator_(std::move(reader)) {}

bool AchievementFrameBridge::addAchievement(std::string key, parser::ConditionTrigger trigger) {
    if (key.empty()) return false;
    for (const auto& entry : entries_) if (entry.key == key) return false;
    entries_.push_back({std::move(key), std::move(trigger), false});
    return true;
}

FrameEvaluationSummary AchievementFrameBridge::evaluateFrame() {
    FrameEvaluationSummary summary;
    for (auto& entry : entries_) {
        ++summary.evaluated;
        const auto result = evaluator_.evaluate(entry.trigger);
        if (result.status == ConditionEvaluationStatus::Triggered && !entry.unlocked) {
            entry.unlocked = true;
            ++summary.unlocked;
            if (callback_) callback_(FrameEvent{FrameEventKind::Unlocked, entry.key, result.status, result.reason});
        } else if (result.status == ConditionEvaluationStatus::Unsupported || result.status == ConditionEvaluationStatus::MemoryError) {
            ++summary.errors;
            if (callback_) callback_(FrameEvent{FrameEventKind::Error, entry.key, result.status, result.reason});
        }
    }
    return summary;
}
void AchievementFrameBridge::reset() { evaluator_.reset(); for (auto& entry : entries_) entry.unlocked = false; }
void AchievementFrameBridge::clear() { entries_.clear(); evaluator_.reset(); }
void AchievementFrameBridge::setEventCallback(EventCallback callback) { callback_ = std::move(callback); }

} // namespace gb::achievements::runtime
