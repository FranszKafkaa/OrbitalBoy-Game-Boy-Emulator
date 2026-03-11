#include "gb/app/frontend/realtime/frame_timeline.hpp"

#include <cstdio>

namespace gb::frontend {

FrameTimeline::FrameTimeline(const gb::GameBoy& gb) {
    reset(gb);
}

void FrameTimeline::reset(const gb::GameBoy& gb) {
    history_.clear();
    history_.reserve(MaxHistory + 8);
    history_.push_back(gb.saveState());
    cursor_ = 0;
}

bool FrameTimeline::stepBack(gb::GameBoy& gb) {
    if (cursor_ == 0) {
        return false;
    }
    --cursor_;
    gb.loadState(history_[cursor_]);
    return true;
}

bool FrameTimeline::stepForward(gb::GameBoy& gb) {
    if (cursor_ + 1 >= history_.size()) {
        return false;
    }
    ++cursor_;
    gb.loadState(history_[cursor_]);
    return true;
}

void FrameTimeline::captureCurrent(const gb::GameBoy& gb) {
    truncateFuture();
    history_.push_back(gb.saveState());
    if (history_.size() > MaxHistory) {
        history_.erase(history_.begin());
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
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1), history_.end());
    }
}

std::string frameTimelineLabel(const FrameTimeline& timeline) {
    char msg[48];
    std::snprintf(msg, sizeof(msg), "FRAME %zu/%zu", timeline.position(), timeline.size());
    return msg;
}

} // namespace gb::frontend
