#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gb/achievements/protocol/http_transport.hpp"

#include "../../test_framework.hpp"

namespace {

using gb::achievements::protocol::HttpChannel;
using gb::achievements::protocol::HttpRequest;
using gb::achievements::protocol::HttpResponse;
using gb::achievements::protocol::HttpTransport;

bool waitUntil(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    return predicate();
}

std::vector<HttpResponse> waitAndDrain(HttpTransport& transport, HttpChannel channel,
                                       std::size_t expected = 1U) {
    std::vector<HttpResponse> results;
    T_REQUIRE(waitUntil([&] {
        auto completed = transport.takeCompleted(channel);
        results.insert(results.end(), std::make_move_iterator(completed.begin()),
                       std::make_move_iterator(completed.end()));
        return results.size() >= expected;
    }));
    return results;
}

TEST_CASE("achievements_http_transport", "normalizes_executor_responses_and_rejects_unsafe_urls") {
    std::atomic<int> calls{0};
    HttpTransport transport([&](const HttpRequest&) {
        ++calls;
        return HttpResponse{999U, HttpChannel::Image, 503L, {'n', 'o'}, {}};
    });

    T_REQUIRE(transport.submit({7U, HttpChannel::Api, "HTTPS://example.invalid/status", {}}));
    T_REQUIRE(transport.submit({8U, HttpChannel::Api, "file:///private", {}}));
    const auto results = waitAndDrain(transport, HttpChannel::Api, 2U);

    T_EQ(calls.load(), 1);
    T_EQ(results.at(0).id, 7U);
    T_REQUIRE(results.at(0).channel == HttpChannel::Api);
    T_EQ(results.at(0).statusCode, 503L);
    T_REQUIRE(!results.at(0).error.empty());
    T_REQUIRE(results.at(0).body.empty());
    T_EQ(results.at(1).id, 8U);
    T_REQUIRE(!results.at(1).error.empty());
    transport.shutdown();
}

TEST_CASE("achievements_http_transport", "uses_post_policy_without_redirects_and_secure_image_redirects") {
    using namespace gb::achievements::protocol;
    const auto post = makeHttpRequestPolicy({1U, HttpChannel::Api, "https://example.invalid", "x=1"});
    const auto image = makeHttpRequestPolicy({2U, HttpChannel::Image, "https://example.invalid", {}});

    T_REQUIRE(post.method == HttpMethod::Post);
    T_EQ(post.followLocation, 0L);
    T_EQ(post.maxRedirects, 3L);
    T_REQUIRE(image.method == HttpMethod::Get);
    T_REQUIRE(image.redirectProtocols == HttpRedirectProtocols::HttpsOnly);
}

TEST_CASE("achievements_http_transport", "accepts_four_mib_and_wipes_larger_response_bodies") {
    std::atomic<int> calls{0};
    HttpTransport transport([&](const HttpRequest& request) {
        const int call = calls.fetch_add(1);
        const std::size_t size = call == 0
            ? 4U * 1024U * 1024U
            : 4U * 1024U * 1024U + 1U;
        return HttpResponse{request.id, request.channel, 200L,
                            std::vector<std::uint8_t>(size, 0x5AU), {}};
    });

    T_REQUIRE(transport.submit({1U, HttpChannel::Image, "https://example.invalid/exact", {}}));
    T_REQUIRE(transport.submit({2U, HttpChannel::Image, "https://example.invalid/overflow", {}}));
    const auto results = waitAndDrain(transport, HttpChannel::Image, 2U);

    T_REQUIRE(results.at(0).error.empty());
    T_EQ(results.at(0).body.size(), 4U * 1024U * 1024U);
    T_REQUIRE(!results.at(1).error.empty());
    T_REQUIRE(results.at(1).body.empty());
    transport.shutdown();
}

TEST_CASE("achievements_http_transport", "preserves_submission_order_within_a_channel") {
    HttpTransport transport([](const HttpRequest& request) {
        return HttpResponse{request.id, request.channel, 200L,
                            {static_cast<std::uint8_t>(request.id)}, {}};
    });

    T_REQUIRE(transport.submit({3U, HttpChannel::Api, "https://example.invalid/three", {}}));
    T_REQUIRE(transport.submit({4U, HttpChannel::Api, "https://example.invalid/four", {}}));
    T_REQUIRE(transport.submit({5U, HttpChannel::Api, "https://example.invalid/five", {}}));
    const auto results = waitAndDrain(transport, HttpChannel::Api, 3U);

    T_EQ(results.at(0).id, 3U);
    T_EQ(results.at(1).id, 4U);
    T_EQ(results.at(2).id, 5U);
    transport.shutdown();
}

TEST_CASE("achievements_http_transport", "keeps_api_work_available_while_image_worker_is_blocked") {
    std::mutex mutex;
    std::condition_variable condition;
    bool imageStarted = false;
    bool releaseImage = false;
    HttpTransport transport([&](const HttpRequest& request) {
        if (request.channel == HttpChannel::Image) {
            std::unique_lock<std::mutex> lock(mutex);
            imageStarted = true;
            condition.notify_all();
            condition.wait(lock, [&] { return releaseImage; });
        }
        return HttpResponse{request.id, request.channel, 200L, {'o', 'k'}, {}};
    });

    T_REQUIRE(transport.submit({1U, HttpChannel::Image, "https://example.invalid/image", {}}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return imageStarted; }));
    }
    T_REQUIRE(transport.submit({2U, HttpChannel::Api, "https://example.invalid/api", {}}));
    const auto api = waitAndDrain(transport, HttpChannel::Api);

    T_EQ(api.front().id, 2U);
    {
        std::lock_guard<std::mutex> lock(mutex);
        releaseImage = true;
    }
    condition.notify_all();
    transport.shutdown();
}

TEST_CASE("achievements_http_transport", "limits_each_channel_to_sixty_four_outstanding_requests") {
    std::mutex mutex;
    std::condition_variable condition;
    bool firstStarted = false;
    bool release = false;
    HttpTransport transport([&](const HttpRequest& request) {
        std::unique_lock<std::mutex> lock(mutex);
        firstStarted = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release; });
        return HttpResponse{request.id, request.channel, 200L, {}, {}};
    });

    for (std::uint64_t id = 0; id < 64U; ++id) {
        T_REQUIRE(transport.submit({id, HttpChannel::Api, "https://example.invalid/api", {}}));
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return firstStarted; }));
    }
    T_REQUIRE(!transport.submit({64U, HttpChannel::Api, "https://example.invalid/api", {}}));
    T_REQUIRE(transport.submit({65U, HttpChannel::Image, "https://example.invalid/image", {}}));

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    transport.shutdown();
}

TEST_CASE("achievements_http_transport", "wipes_failed_response_and_pending_post_data_during_shutdown") {
    std::mutex mutex;
    std::condition_variable condition;
    bool started = false;
    bool release = false;
    std::vector<std::size_t> wipedSizes;
    bool allWipedBytesWereZero = true;
    HttpTransport transport(
        [&](const HttpRequest& request) {
            std::unique_lock<std::mutex> lock(mutex);
            started = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            return HttpResponse{request.id, request.channel, 500L, {'b', 'o', 'd', 'y'}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) {
            wipedSizes.push_back(size);
            allWipedBytesWereZero = allWipedBytesWereZero && bytes != nullptr;
            for (std::size_t index = 0; index < size; ++index) {
                allWipedBytesWereZero = allWipedBytesWereZero && bytes[index] == 0U;
            }
        }
    );

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/active", "active-secret"}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return started; }));
    }
    T_REQUIRE(transport.submit({2U, HttpChannel::Api, "https://example.invalid/pending", "pending-secret"}));
    std::thread shutdownThread([&] { transport.shutdown(); });
    T_REQUIRE(waitUntil([&] { return !transport.acceptingRequests(); }));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    shutdownThread.join();
    transport.shutdown();

    T_REQUIRE(!transport.submit({3U, HttpChannel::Api, "https://example.invalid/rejected", {}}));
    T_REQUIRE(transport.takeCompleted(HttpChannel::Api).empty());
    T_REQUIRE(!wipedSizes.empty());
    T_REQUIRE(allWipedBytesWereZero);
}

TEST_CASE("achievements_http_transport", "converts_executor_exceptions_to_failed_responses") {
    HttpTransport transport([](const HttpRequest&) -> HttpResponse {
        throw std::runtime_error("network failure");
    });
    T_REQUIRE(transport.submit({11U, HttpChannel::Api, "http://example.invalid", {}}));
    const auto results = waitAndDrain(transport, HttpChannel::Api);
    T_EQ(results.front().id, 11U);
    T_REQUIRE(!results.front().error.empty());
    T_REQUIRE(results.front().body.empty());
    transport.shutdown();
}

} // namespace
