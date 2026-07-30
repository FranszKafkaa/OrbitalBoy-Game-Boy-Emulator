#include "gb/app/frontend/realtime/frame_timeline.hpp"

#include <cstdio>
#include <utility>

namespace gb::frontend {

FrameTimeline::FrameTimeline(const gb::GameBoy& gb)
    : FrameTimeline(gb, {}, {}) {
}

FrameTimeline::FrameTimeline(
    const gb::GameBoy& gb,
    ProgressCapture progressCapture,
    ProgressRestore progressRestore
)
    : progressCapture_(std::move(progressCapture)),
      progressRestore_(std::move(progressRestore)) {
    reset(gb);
}

void FrameTimeline::reset(const gb::GameBoy& gb) {
    history_.clear();
    history_.push_back({
        gb.saveState(),
        progressCapture_ ? progressCapture_() : std::vector<std::uint8_t>{}
    });
    cursor_ = 0;
}

bool FrameTimeline::stepBack(gb::GameBoy& gb) {
    if (cursor_ == 0) {
        return false;
    }
    --cursor_;
    const auto& entry = history_[cursor_];
    gb.loadState(entry.gameBoy);
    if (progressRestore_) {
        progressRestore_(entry.achievementProgress);
    }
    return true;
}

bool FrameTimeline::stepForward(gb::GameBoy& gb) {
    if (cursor_ + 1 >= history_.size()) {
        return false;
    }
    ++cursor_;
    const auto& entry = history_[cursor_];
    gb.loadState(entry.gameBoy);
    if (progressRestore_) {
        progressRestore_(entry.achievementProgress);
    }
    return true;
}

void FrameTimeline::captureCurrent(const gb::GameBoy& gb) {
    truncateFuture();
    history_.push_back({
        gb.saveState(),
        progressCapture_ ? progressCapture_() : std::vector<std::uint8_t>{}
    });
    if (history_.size() > MaxHistory) {
        history_.pop_front();
        if (cursor_ > 0) {
            --cursor_;
        }
    }
    cursor_ = history_.size() - 1;
}

std::size_t FrameTimeline::position() const {
    return cursor_ + 1;
}

std::size_t FrameTimeline::size() const {
    return history_.size();
}

void FrameTimeline::truncateFuture() {
    if (cursor_ + 1 < history_.size()) {
        while (history_.size() > cursor_ + 1) {
            history_.pop_back();
        }
    }
}

std::string frameTimelineLabel(const FrameTimeline& timeline) {
    char msg[48];
    std::snprintf(msg, sizeof(msg), "FRAME %zu/%zu", timeline.position(), timeline.size());
    return msg;
}

} // namespace gb::frontend
