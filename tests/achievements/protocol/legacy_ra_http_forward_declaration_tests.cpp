namespace gb::frontend {
class RaHttpTransport;
}

#include <type_traits>

#include "gb/app/frontend/realtime/retroachievements_http.hpp"

static_assert(std::is_class_v<gb::frontend::RaHttpTransport>);
