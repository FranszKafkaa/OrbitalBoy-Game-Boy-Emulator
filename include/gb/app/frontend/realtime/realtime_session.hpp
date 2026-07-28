#pragma once

#include "gb/app/frontend/realtime_options.hpp"
#include "gb/core/gameboy.hpp"

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

class RealtimeSession {
public:
    RealtimeSession(gb::GameBoy& gameBoy, const RealtimeOptions& options);
    int run();

private:
    gb::GameBoy& gameBoy_;
    const RealtimeOptions& options_;
};

} // namespace gb::frontend
#endif
