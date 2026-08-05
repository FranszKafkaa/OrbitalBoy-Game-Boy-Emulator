#include "gb/app/frontend/realtime/retroachievements_http.hpp"

#include "gb/achievements/adapters/curl/curl_http_executor.hpp"

namespace gb::frontend {

std::string_view retroAchievementsUserAgent() noexcept {
    return gb::achievements::adapters::curl::curlHttpUserAgent();
}

} // namespace gb::frontend
