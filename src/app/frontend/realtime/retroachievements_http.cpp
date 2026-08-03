#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/app/frontend/realtime/secure_string.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace gb::frontend {

std::string_view retroAchievementsUserAgent() noexcept {
#if defined(_WIN32)
    return "OrbitalBoy/1.0 (Windows) rcheevos/12.4.0";
#elif defined(__APPLE__)
    return "OrbitalBoy/1.0 (macOS) rcheevos/12.4.0";
#elif defined(__linux__)
    return "OrbitalBoy/1.0 (Linux) rcheevos/12.4.0";
#else
    return "OrbitalBoy/1.0 (Unknown) rcheevos/12.4.0";
#endif
}

namespace {

constexpr std::size_t maximumResponseSize = 4U * 1024U * 1024U;
constexpr std::size_t maximumOutstandingPerChannel = 64;
constexpr const char* unsupportedUrlError = "Unsupported HTTP request URL.";
constexpr const char* requestFailedError = "Unable to complete the HTTP request.";
constexpr const char* responseTooLargeError = "HTTP response exceeded the 4 MiB limit.";
constexpr const char* statusError = "HTTP request returned a non-success status.";

const char* redirectProtocols(const RaHttpRequestPolicy& policy) {
    return policy.redirectProtocols == RaHttpRedirectProtocols::HttpsOnly ? "https" : "http,https";
}

#if LIBCURL_VERSION_NUM < 0x075500
long redirectProtocolMask(const RaHttpRequestPolicy& policy) {
    return policy.redirectProtocols == RaHttpRedirectProtocols::HttpsOnly
        ? CURLPROTO_HTTPS
        : CURLPROTO_HTTP | CURLPROTO_HTTPS;
}
#endif

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

std::size_t channelIndex(RaHttpChannel channel) {
    return channel == RaHttpChannel::Image ? 1U : 0U;
}

void secureEraseBody(std::vector<std::uint8_t>& body) {
    volatile std::uint8_t* bytes = body.empty() ? nullptr : body.data();
    for (std::size_t index = 0; index < body.size(); ++index) {
        bytes[index] = 0;
    }
    std::vector<std::uint8_t>{}.swap(body);
}

RaHttpResponse makeErrorResponse(const RaHttpRequest& request, const char* error) {
    return RaHttpResponse{request.id, request.channel, 0, {}, error};
}

RaHttpResponse normalizeResponse(const RaHttpRequest& request, RaHttpResponse response) {
    response.id = request.id;
    response.channel = request.channel;

    if (response.body.size() > maximumResponseSize) {
        response.error = responseTooLargeError;
    } else if (response.error.empty()
               && (response.statusCode < 200L || response.statusCode >= 300L)) {
        response.error = statusError;
    }

    if (!response.error.empty()) {
        secureEraseBody(response.body);
    }
    return response;
}

struct CurlResponseBuffer {
    std::vector<std::uint8_t> body;
    bool exceededLimit = false;
    bool writeFailed = false;
};

std::size_t appendCurlBody(
    char* data,
    std::size_t size,
    std::size_t count,
    void* userData
) noexcept {
    auto& buffer = *static_cast<CurlResponseBuffer*>(userData);
    if (size != 0 && count > static_cast<std::size_t>(-1) / size) {
        buffer.exceededLimit = true;
        return 0;
    }

    const std::size_t byteCount = size * count;
    if (byteCount > maximumResponseSize - buffer.body.size()) {
        buffer.exceededLimit = true;
        return 0;
    }

    const auto* first = reinterpret_cast<const std::uint8_t*>(data);
    try {
        buffer.body.insert(buffer.body.end(), first, first + byteCount);
    } catch (...) {
        buffer.writeFailed = true;
        return 0;
    }
    return byteCount;
}

bool initializeCurl() {
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, [] {
        result = curl_global_init(CURL_GLOBAL_DEFAULT);
    });
    return result == CURLE_OK;
}

int curlProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    return static_cast<std::atomic<bool>*>(userData)->load(std::memory_order_relaxed)
        ? 1
        : 0;
}

RaHttpResponse executeWithCurl(
    const RaHttpRequest& request,
    std::atomic<bool>* stopping
) {
    RaHttpResponse response{request.id, request.channel, 0, {}, {}};
    if (!initializeCurl()) {
        response.error = requestFailedError;
        return response;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = requestFailedError;
        return response;
    }

    CurlResponseBuffer buffer;
    const RaHttpRequestPolicy policy = makeRaHttpRequestPolicy(request);
    const bool configured =
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str()) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L) == CURLE_OK
        && curl_easy_setopt(
            curl, CURLOPT_TIMEOUT_MS, static_cast<long>(kRaHttpRequestTimeout.count())
        ) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, policy.followLocation) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_MAXREDIRS, policy.maxRedirects) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_USERAGENT, retroAchievementsUserAgent().data()) == CURLE_OK
#if LIBCURL_VERSION_NUM >= 0x075500
        && curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, redirectProtocols(policy)) == CURLE_OK
#else
        && curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, redirectProtocolMask(policy)) == CURLE_OK
#endif
        && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgress) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_XFERINFODATA, stopping) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendCurlBody) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer) == CURLE_OK;

    bool requestConfigured = configured;
    if (requestConfigured && policy.method == RaHttpMethod::Get) {
        requestConfigured = curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L) == CURLE_OK;
    } else if (requestConfigured) {
        requestConfigured =
            curl_easy_setopt(curl, CURLOPT_POST, 1L) == CURLE_OK
            && curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.postData.data()) == CURLE_OK
            && curl_easy_setopt(
                curl,
                CURLOPT_POSTFIELDSIZE_LARGE,
                static_cast<curl_off_t>(request.postData.size())
            ) == CURLE_OK;
    }

    CURLcode performResult = CURLE_FAILED_INIT;
    if (requestConfigured) {
        performResult = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    }

    if (buffer.exceededLimit) {
        response.error = responseTooLargeError;
    } else if (buffer.writeFailed || !requestConfigured || performResult != CURLE_OK) {
        response.error = requestFailedError;
    } else {
        response.body = std::move(buffer.body);
    }

    if (!response.error.empty()) {
        secureEraseBody(buffer.body);
    }

    curl_easy_cleanup(curl);
    return response;
}

} // namespace

RaHttpRequestPolicy makeRaHttpRequestPolicy(const RaHttpRequest& request) {
    if (request.postData.empty()) {
        const auto protocols = request.channel == RaHttpChannel::Image
            ? RaHttpRedirectProtocols::HttpsOnly
            : RaHttpRedirectProtocols::HttpAndHttps;
        return {RaHttpMethod::Get, 1L, 3L, protocols};
    }
    return {RaHttpMethod::Post, 0L, 3L, RaHttpRedirectProtocols::HttpAndHttps};
}

class RaHttpTransport::Impl {
public:
    explicit Impl(RaHttpExecutor executor)
        : executor_(std::move(executor)) {
        if (!executor_) {
            initializeCurl();
            executor_ = [this](const RaHttpRequest& request) {
                return executeWithCurl(request, &stopRequested_);
            };
        }
        workers_[0] = std::thread([this] { run(RaHttpChannel::Api); });
        workers_[1] = std::thread([this] { run(RaHttpChannel::Image); });
    }

    ~Impl() {
        shutdown();
    }

    bool submit(RaHttpRequest request) {
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

    std::vector<RaHttpResponse> takeCompleted(RaHttpChannel channel) {
        std::vector<RaHttpResponse> matching;
        std::lock_guard<std::mutex> lock(queueMutex_);
        const std::size_t index = channelIndex(channel);
        auto& completed = completedByChannel_[index];
        matching.reserve(completed.size());
        while (!completed.empty()) {
            matching.push_back(std::move(completed.front()));
            completed.pop_front();
        }
        outstandingByChannel_[index] -= matching.size();
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
                    (void)secureEraseStringStorage(request.postData);
                }
                outstandingByChannel_[index] -= pendingByChannel_[index].size();
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
                secureEraseBody(response.body);
            }
            outstandingByChannel_[index] -= completedByChannel_[index].size();
            completedByChannel_[index].clear();
        }
    }

private:
    void run(RaHttpChannel channel) {
        const std::size_t index = channelIndex(channel);
        for (;;) {
            RaHttpRequest request;
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

            RaHttpResponse response;
            if (!isSupportedUrl(request.url)) {
                response = makeErrorResponse(request, unsupportedUrlError);
            } else {
                try {
                    response = normalizeResponse(request, executor_(request));
                } catch (...) {
                    response = makeErrorResponse(request, requestFailedError);
                }
            }
            (void)secureEraseStringStorage(request.postData);

            std::lock_guard<std::mutex> lock(queueMutex_);
            completedByChannel_[channelIndex(response.channel)].push_back(std::move(response));
        }
    }

    RaHttpExecutor executor_;
    mutable std::mutex queueMutex_;
    std::condition_variable pendingCondition_;
    std::array<std::deque<RaHttpRequest>, 2> pendingByChannel_;
    std::array<std::deque<RaHttpResponse>, 2> completedByChannel_;
    std::array<std::size_t, 2> outstandingByChannel_{};
    bool stopping_ = false;
    std::atomic<bool> stopRequested_{false};
    std::mutex shutdownMutex_;
    std::array<std::thread, 2> workers_;
};

RaHttpTransport::RaHttpTransport()
    : impl_(std::make_unique<Impl>(RaHttpExecutor{})) {
}

RaHttpTransport::RaHttpTransport(RaHttpExecutor executor)
    : impl_(std::make_unique<Impl>(std::move(executor))) {
}

RaHttpTransport::~RaHttpTransport() = default;

bool RaHttpTransport::submit(RaHttpRequest request) {
    return impl_->submit(std::move(request));
}

std::vector<RaHttpResponse> RaHttpTransport::takeCompleted(RaHttpChannel channel) {
    return impl_->takeCompleted(channel);
}

bool RaHttpTransport::acceptingRequests() const {
    return impl_->acceptingRequests();
}

void RaHttpTransport::shutdown() {
    impl_->shutdown();
}

} // namespace gb::frontend
