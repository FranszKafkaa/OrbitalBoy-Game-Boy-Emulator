#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gb::frontend {

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

class RaHttpTransport {
public:
    RaHttpTransport();
    explicit RaHttpTransport(RaHttpExecutor executor);
    ~RaHttpTransport();

    RaHttpTransport(const RaHttpTransport&) = delete;
    RaHttpTransport& operator=(const RaHttpTransport&) = delete;
    RaHttpTransport(RaHttpTransport&&) = delete;
    RaHttpTransport& operator=(RaHttpTransport&&) = delete;

    void submit(RaHttpRequest request);
    [[nodiscard]] std::vector<RaHttpResponse> takeCompleted(RaHttpChannel channel);
    void shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gb::frontend
