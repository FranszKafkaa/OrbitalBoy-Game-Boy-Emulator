#include "gb/app/frontend/realtime/realtime_session.hpp"

#ifdef GBEMU_USE_SDL2
namespace gb::frontend {

RealtimeSession::RealtimeSession(gb::GameBoy& gameBoy, const RealtimeOptions& options)
    : gameBoy_(gameBoy),
      options_(options) {}

} // namespace gb::frontend
#endif
