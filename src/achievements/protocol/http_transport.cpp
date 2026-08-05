#include "gb/achievements/protocol/http_transport.hpp"

#include "gb/achievements/security/secret_string.hpp"

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
        try {
            observer(body.data(), body.size());
        } catch (...) {
        }
    }
    std::vector<std::uint8_t>{}.swap(body);
}

void secureErasePostData(std::string& postData, const HttpWipeObserver& observer) {
    bool observerNotified = false;
    (void)secureEraseStringStorage(postData, [&observer, &observerNotified](const char* bytes, std::size_t size) {
        observerNotified = true;
        if (observer) {
            try {
                observer(reinterpret_cast<const std::uint8_t*>(bytes), size);
            } catch (...) {
            }
        }
    });
    if (observer && !observerNotified) {
        try {
            observer(nullptr, 0U);
        } catch (...) {
        }
    }
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

HttpExecutor makeUnavailableHttpExecutor() {
    return [](const HttpRequest& request) {
        return makeErrorResponse(request, requestFailedError);
    };
}

void subtractOutstanding(std::size_t& outstanding, std::size_t count) {
    outstanding = count > outstanding ? 0U : outstanding - count;
}

void transferRequest(HttpRequest& destination, HttpRequest& source) noexcept {
    using std::swap;
    swap(destination.id, source.id);
    swap(destination.channel, source.channel);
    destination.url.swap(source.url);
    destination.postData.swap(source.postData);
}

void transferResponse(HttpResponse& destination, HttpResponse& source) noexcept {
    using std::swap;
    swap(destination.id, source.id);
    swap(destination.channel, source.channel);
    swap(destination.statusCode, source.statusCode);
    destination.body.swap(source.body);
    destination.error.swap(source.error);
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

HttpCancellationFlag makeHttpCancellationFlag() {
    return std::make_shared<std::atomic<bool>>(false);
}

class HttpTransport::Impl {
public:
    Impl(
        HttpExecutor executor,
        HttpCancellationFlag cancellationFlag,
        HttpWipeObserver wipeObserver
    )
        : executor_(executor ? std::move(executor) : makeUnavailableHttpExecutor()),
          wipeObserver_(std::move(wipeObserver)),
          stopRequested_(cancellationFlag ? std::move(cancellationFlag) : makeHttpCancellationFlag()) {
        workers_[0] = std::thread([this] { run(HttpChannel::Api); });
        workers_[1] = std::thread([this] { run(HttpChannel::Image); });
    }

    ~Impl() {
        shutdown();
    }

    bool submit(HttpRequest& request) {
        bool rejected = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            const std::size_t index = channelIndex(request.channel);
            if (stopping_ || outstandingByChannel_[index] >= maximumOutstandingPerChannel) {
                rejected = true;
            } else {
                try {
                    pendingByChannel_[index].emplace_back();
                    transferRequest(pendingByChannel_[index].back(), request);
                    ++outstandingByChannel_[index];
                } catch (...) {
                    rejected = true;
                }
            }
        }
        secureErasePostData(request.postData, wipeObserver_);
        if (rejected) {
            return false;
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
        if (runsOnWorkerThread()) {
            requestWorkerShutdown();
            return;
        }
        {
            std::unique_lock<std::mutex> shutdownLock(shutdownMutex_);
            if (shutdownCompleted_) {
                return;
            }
            if (shutdownInProgress_) {
                if (shutdownThread_ == std::this_thread::get_id()) {
                    return;
                }
                shutdownCondition_.wait(shutdownLock, [this] { return shutdownCompleted_; });
                return;
            }
            shutdownInProgress_ = true;
            shutdownThread_ = std::this_thread::get_id();
        }
        auto pendingToWipe = takePendingForShutdown();
        wipePendingRequests(pendingToWipe);
        pendingCondition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        std::array<std::deque<HttpResponse>, 2> completedToWipe;
        {
            std::lock_guard<std::mutex> queueLock(queueMutex_);
            for (std::size_t index = 0; index < completedByChannel_.size(); ++index) {
                subtractOutstanding(outstandingByChannel_[index], completedByChannel_[index].size());
                completedToWipe[index].swap(completedByChannel_[index]);
            }
        }
        for (auto& completed : completedToWipe) {
            for (auto& response : completed) {
                secureEraseBody(response.body, wipeObserver_);
            }
        }
        {
            std::lock_guard<std::mutex> shutdownLock(shutdownMutex_);
            shutdownInProgress_ = false;
            shutdownCompleted_ = true;
            shutdownThread_ = {};
        }
        shutdownCondition_.notify_all();
    }

private:
    [[nodiscard]] bool runsOnWorkerThread() const {
        const std::thread::id currentThread = std::this_thread::get_id();
        for (const auto& worker : workers_) {
            if (worker.get_id() == currentThread) {
                return true;
            }
        }
        return false;
    }

    std::array<std::deque<HttpRequest>, 2> takePendingForShutdown() {
        std::array<std::deque<HttpRequest>, 2> pendingToWipe;
        std::lock_guard<std::mutex> queueLock(queueMutex_);
        stopping_ = true;
        stopRequested_->store(true, std::memory_order_relaxed);
        for (std::size_t index = 0; index < pendingByChannel_.size(); ++index) {
            subtractOutstanding(outstandingByChannel_[index], pendingByChannel_[index].size());
            pendingToWipe[index].swap(pendingByChannel_[index]);
        }
        return pendingToWipe;
    }

    void wipePendingRequests(std::array<std::deque<HttpRequest>, 2>& pendingToWipe) {
        for (auto& pending : pendingToWipe) {
            for (auto& request : pending) {
                secureErasePostData(request.postData, wipeObserver_);
            }
        }
    }

    void requestWorkerShutdown() {
        auto pendingToWipe = takePendingForShutdown();
        wipePendingRequests(pendingToWipe);
        pendingCondition_.notify_all();
    }

    void run(HttpChannel channel) {
        const std::size_t index = channelIndex(channel);
        for (;;) {
            HttpRequest request;
            std::string transferredPostData;
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
                transferRequest(request, pendingByChannel_[index].front());
                transferredPostData.swap(pendingByChannel_[index].front().postData);
                pendingByChannel_[index].pop_front();
            }
            secureErasePostData(transferredPostData, wipeObserver_);

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
            completedByChannel_[channelIndex(response.channel)].emplace_back();
            transferResponse(completedByChannel_[channelIndex(response.channel)].back(), response);
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
    HttpCancellationFlag stopRequested_;
    std::mutex shutdownMutex_;
    std::condition_variable shutdownCondition_;
    bool shutdownInProgress_ = false;
    bool shutdownCompleted_ = false;
    std::thread::id shutdownThread_{};
    std::array<std::thread, 2> workers_;
};

HttpTransport::HttpTransport()
    : impl_(std::make_unique<Impl>(
        HttpExecutor{}, makeHttpCancellationFlag(), HttpWipeObserver{}
    )) {}

HttpTransport::HttpTransport(HttpExecutor executor)
    : impl_(std::make_unique<Impl>(
        std::move(executor), makeHttpCancellationFlag(), HttpWipeObserver{}
    )) {}

HttpTransport::HttpTransport(HttpExecutor executor, HttpWipeObserver wipeObserver)
    : impl_(std::make_unique<Impl>(
        std::move(executor), makeHttpCancellationFlag(), std::move(wipeObserver)
    )) {}

HttpTransport::HttpTransport(
    HttpExecutor executor,
    HttpCancellationFlag cancellationFlag,
    HttpWipeObserver wipeObserver
)
    : impl_(std::make_unique<Impl>(
        std::move(executor), std::move(cancellationFlag), std::move(wipeObserver)
    )) {}

HttpTransport::~HttpTransport() = default;

bool HttpTransport::submit(HttpRequest request) {
    return impl_->submit(request);
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
