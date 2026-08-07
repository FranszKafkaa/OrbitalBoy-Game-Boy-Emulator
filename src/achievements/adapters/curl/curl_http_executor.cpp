#include "gb/achievements/adapters/curl/curl_http_executor.hpp"

#include <curl/curl.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace gb::achievements::adapters::curl {
namespace {

constexpr const char* requestFailedError = "Unable to complete the HTTP request.";
constexpr const char* responseTooLargeError = "HTTP response exceeded the 4 MiB limit.";

struct CurlResponseBuffer {
    std::vector<std::uint8_t> body;
    bool exceededLimit = false;
    bool writeFailed = false;
};

void secureEraseBody(std::vector<std::uint8_t>& body) {
    volatile std::uint8_t* bytes = body.empty() ? nullptr : body.data();
    for (std::size_t index = 0; index < body.size(); ++index) {
        bytes[index] = 0U;
    }
    std::vector<std::uint8_t>{}.swap(body);
}

std::size_t appendCurlBody(char* data, std::size_t size, std::size_t count,
                           void* userData) noexcept {
    auto& buffer = *static_cast<CurlResponseBuffer*>(userData);
    if (size != 0U && count > static_cast<std::size_t>(-1) / size) {
        buffer.exceededLimit = true;
        return 0U;
    }
    const std::size_t byteCount = size * count;
    if (byteCount > protocol::kHttpMaximumResponseBodySize - buffer.body.size()) {
        buffer.exceededLimit = true;
        return 0U;
    }
    const auto* first = reinterpret_cast<const std::uint8_t*>(data);
    try {
        buffer.body.insert(buffer.body.end(), first, first + byteCount);
    } catch (...) {
        buffer.writeFailed = true;
        return 0U;
    }
    return byteCount;
}

bool initializeCurl() {
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return result == CURLE_OK;
}

int curlProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    return static_cast<std::atomic<bool>*>(userData)->load(std::memory_order_relaxed) ? 1 : 0;
}

const char* redirectProtocols(const protocol::HttpRequestPolicy& policy) {
    return policy.redirectProtocols == protocol::HttpRedirectProtocols::HttpsOnly
        ? "https"
        : "http,https";
}

#if LIBCURL_VERSION_NUM < 0x075500
long redirectProtocolMask(const protocol::HttpRequestPolicy& policy) {
    return policy.redirectProtocols == protocol::HttpRedirectProtocols::HttpsOnly
        ? CURLPROTO_HTTPS
        : CURLPROTO_HTTP | CURLPROTO_HTTPS;
}
#endif

protocol::HttpResponse executeWithCurl(const protocol::HttpRequest& request,
                                       std::atomic<bool>* stopping) {
    protocol::HttpResponse response{request.id, request.channel, 0L, {}, {}};
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
    const auto policy = protocol::makeHttpRequestPolicy(request);
    const bool configured =
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str()) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                            static_cast<long>(protocol::kHttpRequestTimeout.count())) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, policy.followLocation) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_MAXREDIRS, policy.maxRedirects) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_USERAGENT, curlHttpUserAgent().data()) == CURLE_OK
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
    if (requestConfigured && policy.method == protocol::HttpMethod::Get) {
        requestConfigured = curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L) == CURLE_OK;
    } else if (requestConfigured) {
        requestConfigured = curl_easy_setopt(curl, CURLOPT_POST, 1L) == CURLE_OK
            && curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.postData.data()) == CURLE_OK
            && curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                                static_cast<curl_off_t>(request.postData.size())) == CURLE_OK;
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

protocol::HttpExecutor makeCurlHttpExecutor(std::atomic<bool>& stopRequested) {
    return [&stopRequested](const protocol::HttpRequest& request) {
        return executeWithCurl(request, &stopRequested);
    };
}

std::string_view curlHttpUserAgent() noexcept {
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

} // namespace gb::achievements::adapters::curl
