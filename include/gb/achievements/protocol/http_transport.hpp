#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gb::achievements::protocol {

inline constexpr std::chrono::milliseconds kHttpRequestTimeout{15000};
inline constexpr std::size_t kHttpMaximumResponseBodySize = 4U * 1024U * 1024U;

enum class HttpChannel {
    Api,
    Image,
};

struct HttpRequest {
    std::uint64_t id = 0;
    HttpChannel channel = HttpChannel::Api;
    std::string url;
    std::string postData;
};

struct HttpResponse {
    std::uint64_t id = 0;
    HttpChannel channel = HttpChannel::Api;
    long statusCode = 0;
    std::vector<std::uint8_t> body;
    std::string error;
};

using HttpExecutor = std::function<HttpResponse(const HttpRequest&)>;
using HttpWipeObserver = std::function<void(const std::uint8_t* bytes, std::size_t size)>;
using HttpCancellationFlag = std::shared_ptr<std::atomic<bool>>;

enum class HttpMethod {
    Get,
    Post,
};

enum class HttpRedirectProtocols {
    HttpAndHttps,
    HttpsOnly,
};

struct HttpRequestPolicy {
    HttpMethod method = HttpMethod::Get;
    long followLocation = 1L;
    long maxRedirects = 3L;
    HttpRedirectProtocols redirectProtocols = HttpRedirectProtocols::HttpAndHttps;
};

[[nodiscard]] HttpRequestPolicy makeHttpRequestPolicy(const HttpRequest& request);
[[nodiscard]] HttpCancellationFlag makeHttpCancellationFlag();

class HttpTransport {
public:
    HttpTransport();
    explicit HttpTransport(HttpExecutor executor);
    HttpTransport(HttpExecutor executor, HttpWipeObserver wipeObserver);
    HttpTransport(
        HttpExecutor executor,
        HttpCancellationFlag cancellationFlag,
        HttpWipeObserver wipeObserver = {}
    );
    ~HttpTransport();

    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;
    HttpTransport(HttpTransport&&) = delete;
    HttpTransport& operator=(HttpTransport&&) = delete;

    [[nodiscard]] bool submit(HttpRequest request);
    [[nodiscard]] std::vector<HttpResponse> takeCompleted(HttpChannel channel);
    [[nodiscard]] bool acceptingRequests() const;
    void shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gb::achievements::protocol
