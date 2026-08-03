#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gb::frontend {

inline constexpr std::chrono::milliseconds kRaHttpRequestTimeout{15000};

enum class RaHttpChannel {
    Api,
    Image,
};

struct RaHttpRequest {
    std::uint64_t id = 0;
    RaHttpChannel channel = RaHttpChannel::Api;
    std::string url;
    std::string postData;
};

struct RaHttpResponse {
    std::uint64_t id = 0;
    RaHttpChannel channel = RaHttpChannel::Api;
    long statusCode = 0;
    std::vector<std::uint8_t> body;
    std::string error;
};

using RaHttpExecutor = std::function<RaHttpResponse(const RaHttpRequest&)>;

enum class RaHttpMethod {
    Get,
    Post,
};

enum class RaHttpRedirectProtocols {
    HttpAndHttps,
    HttpsOnly,
};

struct RaHttpRequestPolicy {
    RaHttpMethod method = RaHttpMethod::Get;
    long followLocation = 1L;
    long maxRedirects = 3L;
    RaHttpRedirectProtocols redirectProtocols = RaHttpRedirectProtocols::HttpAndHttps;
};

[[nodiscard]] RaHttpRequestPolicy makeRaHttpRequestPolicy(const RaHttpRequest& request);

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
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gb::frontend
