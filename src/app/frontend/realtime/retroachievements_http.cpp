#include "gb/app/frontend/realtime/retroachievements_http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace gb::frontend {

namespace {

constexpr std::size_t maximumResponseSize = 4U * 1024U * 1024U;
constexpr const char* unsupportedUrlError = "Unsupported HTTP request URL.";
constexpr const char* requestFailedError = "Unable to complete the HTTP request.";
constexpr const char* responseTooLargeError = "HTTP response exceeded the 4 MiB limit.";
constexpr const char* statusError = "HTTP request returned a non-success status.";
constexpr const char* shutdownError = "HTTP transport is shut down.";

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
        response.body.clear();
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

RaHttpResponse executeWithCurl(const RaHttpRequest& request) {
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
    const bool configured =
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str()) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK
        && curl_easy_setopt(
            curl,
            CURLOPT_POSTREDIR,
            static_cast<long>(CURL_REDIR_POST_ALL)
        ) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_USERAGENT, "OrbitalBoy/RetroAchievements-MVP") == CURLE_OK
#if LIBCURL_VERSION_NUM >= 0x075500
        && curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") == CURLE_OK
#else
        && curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS) == CURLE_OK
#endif
        && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendCurlBody) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer) == CURLE_OK;

    bool requestConfigured = configured;
    if (requestConfigured && request.postData.empty()) {
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

    curl_easy_cleanup(curl);
    return response;
}

RaHttpExecutor resolveExecutor(RaHttpExecutor executor) {
    if (executor) {
        return executor;
    }
    initializeCurl();
    return executeWithCurl;
}

} // namespace

class RaHttpTransport::Impl {
public:
    explicit Impl(RaHttpExecutor executor)
        : executor_(std::move(executor))
        , worker_([this] { run(); }) {
    }

    ~Impl() {
        shutdown();
    }

    void submit(RaHttpRequest request) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopping_) {
                completed_.push_back(makeErrorResponse(request, shutdownError));
                return;
            }
            pending_.push_back(std::move(request));
        }
        pendingCondition_.notify_one();
    }

    std::vector<RaHttpResponse> takeCompleted(RaHttpChannel channel) {
        std::vector<RaHttpResponse> matching;
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto it = completed_.begin(); it != completed_.end();) {
            if (it->channel == channel) {
                matching.push_back(std::move(*it));
                it = completed_.erase(it);
            } else {
                ++it;
            }
        }
        return matching;
    }

    void shutdown() {
        std::lock_guard<std::mutex> shutdownLock(shutdownMutex_);
        {
            std::lock_guard<std::mutex> queueLock(queueMutex_);
            stopping_ = true;
        }
        pendingCondition_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void run() {
        for (;;) {
            RaHttpRequest request;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                pendingCondition_.wait(lock, [this] {
                    return stopping_ || !pending_.empty();
                });
                if (pending_.empty()) {
                    if (stopping_) {
                        return;
                    }
                    continue;
                }
                request = std::move(pending_.front());
                pending_.pop_front();
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

            std::lock_guard<std::mutex> lock(queueMutex_);
            completed_.push_back(std::move(response));
        }
    }

    RaHttpExecutor executor_;
    std::mutex queueMutex_;
    std::condition_variable pendingCondition_;
    std::deque<RaHttpRequest> pending_;
    std::deque<RaHttpResponse> completed_;
    bool stopping_ = false;
    std::mutex shutdownMutex_;
    std::thread worker_;
};

RaHttpTransport::RaHttpTransport()
    : impl_(std::make_unique<Impl>(resolveExecutor(RaHttpExecutor{}))) {
}

RaHttpTransport::RaHttpTransport(RaHttpExecutor executor)
    : impl_(std::make_unique<Impl>(resolveExecutor(std::move(executor)))) {
}

RaHttpTransport::~RaHttpTransport() = default;

void RaHttpTransport::submit(RaHttpRequest request) {
    impl_->submit(std::move(request));
}

std::vector<RaHttpResponse> RaHttpTransport::takeCompleted(RaHttpChannel channel) {
    return impl_->takeCompleted(channel);
}

void RaHttpTransport::shutdown() {
    impl_->shutdown();
}

} // namespace gb::frontend
