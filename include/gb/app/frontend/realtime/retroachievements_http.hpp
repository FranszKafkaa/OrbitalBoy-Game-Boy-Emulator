#pragma once

#include <string_view>

#include "gb/achievements/protocol/http_transport.hpp"

namespace gb::frontend {

using RaHttpChannel = gb::achievements::protocol::HttpChannel;
using RaHttpRequest = gb::achievements::protocol::HttpRequest;
using RaHttpResponse = gb::achievements::protocol::HttpResponse;
using RaHttpExecutor = gb::achievements::protocol::HttpExecutor;
using RaHttpMethod = gb::achievements::protocol::HttpMethod;
using RaHttpRedirectProtocols = gb::achievements::protocol::HttpRedirectProtocols;
using RaHttpRequestPolicy = gb::achievements::protocol::HttpRequestPolicy;

inline constexpr auto kRaHttpRequestTimeout = gb::achievements::protocol::kHttpRequestTimeout;

[[nodiscard]] inline RaHttpRequestPolicy makeRaHttpRequestPolicy(const RaHttpRequest& request) {
    return gb::achievements::protocol::makeHttpRequestPolicy(request);
}

[[nodiscard]] std::string_view retroAchievementsUserAgent() noexcept;

class RaHttpTransport {
public:
    RaHttpTransport();
    explicit RaHttpTransport(RaHttpExecutor executor);
    ~RaHttpTransport();

    RaHttpTransport(const RaHttpTransport&) = delete;
    RaHttpTransport& operator=(const RaHttpTransport&) = delete;
    RaHttpTransport(RaHttpTransport&&) = delete;
    RaHttpTransport& operator=(RaHttpTransport&&) = delete;

    [[nodiscard]] bool submit(RaHttpRequest request);
    [[nodiscard]] std::vector<RaHttpResponse> takeCompleted(RaHttpChannel channel);
    [[nodiscard]] bool acceptingRequests() const;
    void shutdown();

private:
    gb::achievements::protocol::HttpCancellationFlag cancellationFlag_;
    gb::achievements::protocol::HttpTransport transport_;
};

} // namespace gb::frontend
