#include "gb/achievements/protocol/http_transport.hpp"

#include "gb/achievements/security/secret_string.hpp"

#if defined(GBEMU_ENABLE_RETROACHIEVEMENTS)
#include "gb/achievements/adapters/curl/curl_http_executor.hpp"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace gb::achievements::protocol {
namespace {

constexpr std::size_t maximumOutstandingPerChannel = 64U;
constexpr const char* unsupportedUrlError = "Unsupported HTTP request URL.";
constexpr const char* requestFailedError = "Unable to complete the HTTP request.";
constexpr const char* responseTooLargeError = "HTTP response exceeded the 4 MiB limit.";
constexpr const char* statusError = "HTTP request returned a non-success status.";

bool startsWithIgnoringCase(const std::string& value, const char* prefix) {
    const std::size_t prefixLength = std::char_traits<char>::length(prefix);
    if (value.size() < prefixLength) {
        return false;
    }
    return std::equal(prefix, prefix + prefixLength, value.begin(), [](char lhs, char rhs) {
        if (lhs >= 'A' && lhs <= 'Z') {
            lhs = static_cast<char>(lhs - 'A' + 'a');
        }
        if (rhs >= 'A' && rhs <= 'Z') {
            rhs = static_cast<char>(rhs - 'A' + 'a');
        }
        return lhs == rhs;
    });
}

bool isSupportedUrl(const std::string& url) {
    return startsWithIgnoringCase(url, "http://") || startsWithIgnoringCase(url, "https://");
}

std::size_t channelIndex(HttpChannel channel) {
    return channel == HttpChannel::Image ? 1U : 0U;
}

void secureEraseBody(std::vector<std::uint8_t>& body, const HttpWipeObserver& observer) {
    volatile std::uint8_t* bytes = body.empty() ? nullptr : body.data();
    for (std::size_t index = 0; index < body.size(); ++index) {
        bytes[index] = 0U;
    }
    if (observer && !body.empty()) {
        observer(body.data(), body.size());
    }
    std::vector<std::uint8_t>{}.swap(body);
}

void secureErasePostData(std::string& postData, const HttpWipeObserver& observer) {
    (void)secureEraseStringStorage(postData, [&observer](const char* bytes, std::size_t size) {
        if (observer) {
            observer(reinterpret_cast<const std::uint8_t*>(bytes), size);
        }
    });
}

HttpResponse makeErrorResponse(const HttpRequest& request, const char* error) {
    return HttpResponse{request.id, request.channel, 0L, {}, error};
}

HttpResponse normalizeResponse(const HttpRequest& request, HttpResponse response,
                               const HttpWipeObserver& observer) {
    response.id = request.id;
    response.channel = request.channel;
    if (response.body.size() > kHttpMaximumResponseBodySize) {
        response.error = responseTooLargeError;
    } else if (response.error.empty()
               && (response.statusCode < 200L || response.statusCode >= 300L)) {
        response.error = statusError;
    }
    if (!response.error.empty()) {
        secureEraseBody(response.body, observer);
    }
    return response;
}

HttpExecutor makeDefaultHttpExecutor(std::atomic<bool>& stopRequested) {
#if defined(GBEMU_ENABLE_RETROACHIEVEMENTS)
    return adapters::curl::makeCurlHttpExecutor(stopRequested);
#else
    (void)stopRequested;
    return [](const HttpRequest& request) {
        return makeErrorResponse(request, requestFailedError);
    };
#endif
}

void subtractOutstanding(std::size_t& outstanding, std::size_t count) {
    outstanding = count > outstanding ? 0U : outstanding - count;
}

} // namespace

HttpRequestPolicy makeHttpRequestPolicy(const HttpRequest& request) {
    if (request.postData.empty()) {
        const auto redirectProtocols = request.channel == HttpChannel::Image
            ? HttpRedirectProtocols::HttpsOnly
            : HttpRedirectProtocols::HttpAndHttps;
        return {HttpMethod::Get, 1L, 3L, redirectProtocols};
    }
    return {HttpMethod::Post, 0L, 3L, HttpRedirectProtocols::HttpAndHttps};
}

class HttpTransport::Impl {
public:
    Impl(HttpExecutor executor, HttpWipeObserver wipeObserver)
        : executor_(executor ? std::move(executor) : makeDefaultHttpExecutor(stopRequested_)),
          wipeObserver_(std::move(wipeObserver)) {
        workers_[0] = std::thread([this] { run(HttpChannel::Api); });
        workers_[1] = std::thread([this] { run(HttpChannel::Image); });
    }

    ~Impl() {
        shutdown();
    }

    bool submit(HttpRequest request) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            const std::size_t index = channelIndex(request.channel);
            if (stopping_ || outstandingByChannel_[index] >= maximumOutstandingPerChannel) {
                return false;
            }
            pendingByChannel_[index].push_back(std::move(request));
            ++outstandingByChannel_[index];
        }
        pendingCondition_.notify_all();
        return true;
    }

    std::vector<HttpResponse> takeCompleted(HttpChannel channel) {
        std::vector<HttpResponse> matching;
        std::lock_guard<std::mutex> lock(queueMutex_);
        const std::size_t index = channelIndex(channel);
        auto& completed = completedByChannel_[index];
        matching.reserve(completed.size());
        while (!completed.empty()) {
            matching.push_back(std::move(completed.front()));
            completed.pop_front();
        }
        subtractOutstanding(outstandingByChannel_[index], matching.size());
        return matching;
    }

    bool acceptingRequests() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return !stopping_;
    }

    void shutdown() {
        std::lock_guard<std::mutex> shutdownLock(shutdownMutex_);
        {
            std::lock_guard<std::mutex> queueLock(queueMutex_);
            stopping_ = true;
            stopRequested_.store(true, std::memory_order_relaxed);
            for (std::size_t index = 0; index < pendingByChannel_.size(); ++index) {
                for (auto& request : pendingByChannel_[index]) {
                    secureErasePostData(request.postData, wipeObserver_);
                }
                subtractOutstanding(outstandingByChannel_[index], pendingByChannel_[index].size());
                pendingByChannel_[index].clear();
            }
        }
        pendingCondition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        for (std::size_t index = 0; index < completedByChannel_.size(); ++index) {
            for (auto& response : completedByChannel_[index]) {
                secureEraseBody(response.body, wipeObserver_);
            }
            subtractOutstanding(outstandingByChannel_[index], completedByChannel_[index].size());
            completedByChannel_[index].clear();
        }
    }

private:
    void run(HttpChannel channel) {
        const std::size_t index = channelIndex(channel);
        for (;;) {
            HttpRequest request;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                pendingCondition_.wait(lock, [this, index] {
                    return stopping_ || !pendingByChannel_[index].empty();
                });
                if (pendingByChannel_[index].empty()) {
                    if (stopping_) {
                        return;
                    }
                    continue;
                }
                request = std::move(pendingByChannel_[index].front());
                pendingByChannel_[index].pop_front();
            }

            HttpResponse response;
            if (!isSupportedUrl(request.url)) {
                response = makeErrorResponse(request, unsupportedUrlError);
            } else {
                try {
                    response = normalizeResponse(request, executor_(request), wipeObserver_);
                } catch (...) {
                    response = makeErrorResponse(request, requestFailedError);
                }
            }
            secureErasePostData(request.postData, wipeObserver_);

            std::lock_guard<std::mutex> lock(queueMutex_);
            completedByChannel_[channelIndex(response.channel)].push_back(std::move(response));
        }
    }

    HttpExecutor executor_;
    HttpWipeObserver wipeObserver_;
    mutable std::mutex queueMutex_;
    std::condition_variable pendingCondition_;
    std::array<std::deque<HttpRequest>, 2> pendingByChannel_;
    std::array<std::deque<HttpResponse>, 2> completedByChannel_;
    std::array<std::size_t, 2> outstandingByChannel_{};
    bool stopping_ = false;
    std::atomic<bool> stopRequested_{false};
    std::mutex shutdownMutex_;
    std::array<std::thread, 2> workers_;
};

HttpTransport::HttpTransport()
    : impl_(std::make_unique<Impl>(HttpExecutor{}, HttpWipeObserver{})) {}

HttpTransport::HttpTransport(HttpExecutor executor)
    : impl_(std::make_unique<Impl>(std::move(executor), HttpWipeObserver{})) {}

HttpTransport::HttpTransport(HttpExecutor executor, HttpWipeObserver wipeObserver)
    : impl_(std::make_unique<Impl>(std::move(executor), std::move(wipeObserver))) {}

HttpTransport::~HttpTransport() = default;

bool HttpTransport::submit(HttpRequest request) {
    return impl_->submit(std::move(request));
}

std::vector<HttpResponse> HttpTransport::takeCompleted(HttpChannel channel) {
    return impl_->takeCompleted(channel);
}

bool HttpTransport::acceptingRequests() const {
    return impl_->acceptingRequests();
}

void HttpTransport::shutdown() {
    impl_->shutdown();
}

} // namespace gb::achievements::protocol
