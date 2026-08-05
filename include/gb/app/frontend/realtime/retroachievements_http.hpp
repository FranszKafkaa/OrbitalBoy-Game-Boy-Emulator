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
using RaHttpTransport = gb::achievements::protocol::HttpTransport;

inline constexpr auto kRaHttpRequestTimeout = gb::achievements::protocol::kHttpRequestTimeout;

[[nodiscard]] inline RaHttpRequestPolicy makeRaHttpRequestPolicy(const RaHttpRequest& request) {
    return gb::achievements::protocol::makeHttpRequestPolicy(request);
}

[[nodiscard]] std::string_view retroAchievementsUserAgent() noexcept;

} // namespace gb::frontend
