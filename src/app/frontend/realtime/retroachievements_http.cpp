#include "gb/app/frontend/realtime/retroachievements_http.hpp"

#include "gb/achievements/adapters/curl/curl_http_executor.hpp"
#include "gb/achievements/security/secret_string.hpp"

#include <utility>

namespace gb::frontend {

std::string_view retroAchievementsUserAgent() noexcept {
    return gb::achievements::adapters::curl::curlHttpUserAgent();
}

RaHttpTransport::RaHttpTransport()
    : cancellationFlag_(gb::achievements::protocol::makeHttpCancellationFlag()),
      transport_(
          gb::achievements::adapters::curl::makeCurlHttpExecutor(*cancellationFlag_),
          cancellationFlag_
      ) {}

RaHttpTransport::RaHttpTransport(RaHttpExecutor executor)
    : transport_(std::move(executor)) {}

RaHttpTransport::~RaHttpTransport() = default;

bool RaHttpTransport::submit(RaHttpRequest request) {
    const bool accepted = transport_.submit(std::move(request));
    (void)gb::achievements::secureEraseStringStorage(request.postData);
    return accepted;
}

std::vector<RaHttpResponse> RaHttpTransport::takeCompleted(RaHttpChannel channel) {
    return transport_.takeCompleted(channel);
}

bool RaHttpTransport::acceptingRequests() const {
    return transport_.acceptingRequests();
}

void RaHttpTransport::shutdown() {
    transport_.shutdown();
}

} // namespace gb::frontend
