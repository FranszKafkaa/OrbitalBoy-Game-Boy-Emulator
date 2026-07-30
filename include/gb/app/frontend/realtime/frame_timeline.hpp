#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "gb/core/gameboy.hpp"

namespace gb::frontend {

struct FrameTimelineEntry {
    gb::GameBoy::SaveState gameBoy;
    std::vector<std::uint8_t> achievementProgress;
};

class FrameTimeline {
public:
    static constexpr std::size_t MaxHistory = 900;
    using ProgressCapture = std::function<std::vector<std::uint8_t>()>;
    using ProgressRestore = std::function<void(const std::vector<std::uint8_t>&)>;

    explicit FrameTimeline(const gb::GameBoy& gb);
    FrameTimeline(
        const gb::GameBoy& gb,
        ProgressCapture progressCapture,
        ProgressRestore progressRestore
    );

    void reset(const gb::GameBoy& gb);
    bool stepBack(gb::GameBoy& gb);
    bool stepForward(gb::GameBoy& gb);
    void captureCurrent(const gb::GameBoy& gb);

    [[nodiscard]] std::size_t position() const;
    [[nodiscard]] std::size_t size() const;

private:
    void truncateFuture();

    ProgressCapture progressCapture_{};
    ProgressRestore progressRestore_{};
    std::deque<FrameTimelineEntry> history_{};
    std::size_t cursor_ = 0;
};

std::string frameTimelineLabel(const FrameTimeline& timeline);

} // namespace gb::frontend
