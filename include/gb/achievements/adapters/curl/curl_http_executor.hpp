#pragma once

#include <atomic>
#include <string_view>

#include "gb/achievements/protocol/http_transport.hpp"

namespace gb::achievements::adapters::curl {

[[nodiscard]] protocol::HttpExecutor makeCurlHttpExecutor(std::atomic<bool>& stopRequested);
[[nodiscard]] std::string_view curlHttpUserAgent() noexcept;

} // namespace gb::achievements::adapters::curl
