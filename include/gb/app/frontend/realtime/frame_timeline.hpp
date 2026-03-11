#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "gb/core/gameboy.hpp"

namespace gb::frontend {

class FrameTimeline {
public:
    static constexpr std::size_t MaxHistory = 900;

    explicit FrameTimeline(const gb::GameBoy& gb);

    void reset(const gb::GameBoy& gb);
    bool stepBack(gb::GameBoy& gb);
    bool stepForward(gb::GameBoy& gb);
    void captureCurrent(const gb::GameBoy& gb);

    [[nodiscard]] std::size_t position() const;
    [[nodiscard]] std::size_t size() const;

private:
    void truncateFuture();

    std::vector<gb::GameBoy::SaveState> history_{};
    std::size_t cursor_ = 0;
};

std::string frameTimelineLabel(const FrameTimeline& timeline);

} // namespace gb::frontend
