#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
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

class WipeRecorder {
public:
    void observe(const std::uint8_t* bytes, std::size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        allBytesWereZero_ = allBytesWereZero_ && (bytes != nullptr
            && std::all_of(bytes, bytes + size, [](std::uint8_t byte) {
                return byte == 0U;
            }));
        sizes_.push_back(size);
        condition_.notify_all();
    }

    [[nodiscard]] bool waitForSize(std::size_t size) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(2), [&] {
            return std::find(sizes_.begin(), sizes_.end(), size) != sizes_.end();
        });
    }

    [[nodiscard]] bool waitForWipe() {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(2), [&] {
            return !sizes_.empty();
        });
    }

    [[nodiscard]] bool waitForCount(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(2), [&] {
            return sizes_.size() >= count;
        });
    }

    [[nodiscard]] bool waitForNonZeroCount(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(2), [&] {
            return static_cast<std::size_t>(std::count_if(
                sizes_.begin(), sizes_.end(), [](std::size_t size) { return size != 0U; }
            )) >= count;
        });
    }

    [[nodiscard]] bool sawSize(std::size_t size) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::find(sizes_.begin(), sizes_.end(), size) != sizes_.end();
    }

    [[nodiscard]] bool allBytesWereZero() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return allBytesWereZero_;
    }

    [[nodiscard]] std::size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sizes_.size();
    }

    [[nodiscard]] std::size_t nonZeroCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<std::size_t>(std::count_if(
            sizes_.begin(), sizes_.end(), [](std::size_t size) { return size != 0U; }));
    }

    [[nodiscard]] std::size_t zeroCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<std::size_t>(std::count(sizes_.begin(), sizes_.end(), 0U));
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::size_t> sizes_;
    bool allBytesWereZero_ = true;
};

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

TEST_CASE("achievements_http_transport", "wipes_active_post_data_after_executor_returns") {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    WipeRecorder wipes;
    HttpTransport transport(
        [&](const HttpRequest& request) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            return HttpResponse{request.id, request.channel, 200L, {}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) { wipes.observe(bytes, size); }
    );

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/active",
                                std::string(96U, 'a')}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
        release = true;
    }
    condition.notify_all();
    T_REQUIRE(wipes.waitForNonZeroCount(1U));
    T_REQUIRE(wipes.allBytesWereZero());
    T_EQ(wipes.zeroCount(), 0U);
    transport.shutdown();
}

TEST_CASE("achievements_http_transport", "keeps_post_data_in_a_stable_node_until_active_cleanup") {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    WipeRecorder wipes;
    HttpTransport transport(
        [&](const HttpRequest& request) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            return HttpResponse{request.id, request.channel, 200L, {}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) { wipes.observe(bytes, size); }
    );

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/handoff",
                                std::string(96U, 'h')}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
    }
    const auto wipesBeforeExecutorReturns = wipes.count();
    const auto nonZeroWipesBeforeExecutorReturns = wipes.nonZeroCount();
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    T_REQUIRE(wipes.waitForNonZeroCount(nonZeroWipesBeforeExecutorReturns + 1U));
    transport.shutdown();

    T_REQUIRE(wipesBeforeExecutorReturns <= 1U);
    T_EQ(wipesBeforeExecutorReturns, nonZeroWipesBeforeExecutorReturns);
    T_EQ(wipes.zeroCount(), 0U);
    T_REQUIRE(wipes.allBytesWereZero());
}

TEST_CASE("achievements_http_transport", "wipes_pending_post_data_during_shutdown") {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    WipeRecorder wipes;
    HttpTransport transport(
        [&](const HttpRequest& request) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            return HttpResponse{request.id, request.channel, 200L, {}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) { wipes.observe(bytes, size); }
    );

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/gate", {}}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
    }
    T_REQUIRE(transport.submit({2U, HttpChannel::Api, "https://example.invalid/pending",
                                std::string(96U, 'p')}));
    const auto nonZeroWipesBeforeShutdown = wipes.nonZeroCount();
    auto shutdown = std::async(std::launch::async, [&] { transport.shutdown(); });
    T_REQUIRE(wipes.waitForNonZeroCount(nonZeroWipesBeforeShutdown + 1U));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    T_REQUIRE(shutdown.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    shutdown.get();
    T_REQUIRE(wipes.allBytesWereZero());
    T_EQ(wipes.zeroCount(), 0U);
}

TEST_CASE("achievements_http_transport", "wipes_successful_completed_response_during_shutdown") {
    WipeRecorder wipes;
    std::promise<void> executed;
    const auto executedFuture = executed.get_future();
    HttpTransport transport(
        [&](const HttpRequest& request) {
            executed.set_value();
            return HttpResponse{request.id, request.channel, 200L, {'o', 'k'}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) { wipes.observe(bytes, size); }
    );

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/completed", {}}));
    T_REQUIRE(executedFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    transport.shutdown();

    T_REQUIRE(wipes.sawSize(2U));
    T_REQUIRE(wipes.allBytesWereZero());
    T_EQ(wipes.zeroCount(), 0U);
}

TEST_CASE("achievements_http_transport", "wipes_non_success_and_oversized_response_bodies") {
    WipeRecorder wipes;
    std::atomic<int> calls{0};
    HttpTransport transport(
        [&](const HttpRequest& request) {
            if (calls.fetch_add(1) == 0) {
                return HttpResponse{request.id, request.channel, 500L, {'f', 'a', 'i', 'l'}, {}};
            }
            return HttpResponse{request.id, request.channel, 200L,
                                std::vector<std::uint8_t>(4U * 1024U * 1024U + 1U, 0x5AU), {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) { wipes.observe(bytes, size); }
    );

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/failure", {}}));
    T_REQUIRE(transport.submit({2U, HttpChannel::Api, "https://example.invalid/overflow", {}}));
    const auto responses = waitAndDrain(transport, HttpChannel::Api, 2U);

    T_REQUIRE(!responses.at(0).error.empty());
    T_REQUIRE(!responses.at(1).error.empty());
    T_REQUIRE(wipes.sawSize(4U));
    T_REQUIRE(wipes.sawSize(4U * 1024U * 1024U + 1U));
    T_REQUIRE(wipes.allBytesWereZero());
    T_EQ(wipes.zeroCount(), 0U);
    transport.shutdown();
}

TEST_CASE("achievements_http_transport", "wipes_rejected_post_data_after_shutdown") {
    WipeRecorder wipes;
    HttpTransport transport(
        [](const HttpRequest& request) {
            return HttpResponse{request.id, request.channel, 200L, {}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) { wipes.observe(bytes, size); }
    );
    transport.shutdown();

    T_REQUIRE(!transport.submit({1U, HttpChannel::Api, "https://example.invalid/rejected", "shutdown-post"}));
    T_REQUIRE(wipes.waitForNonZeroCount(1U));
    T_REQUIRE(wipes.allBytesWereZero());
    T_EQ(wipes.zeroCount(), 0U);
}

TEST_CASE("achievements_http_transport", "wipes_rejected_post_data_at_channel_limit") {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    WipeRecorder wipes;
    HttpTransport transport(
        [&](const HttpRequest& request) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            return HttpResponse{request.id, request.channel, 200L, {}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) { wipes.observe(bytes, size); }
    );

    for (std::uint64_t id = 0; id < 64U; ++id) {
        T_REQUIRE(transport.submit({id, HttpChannel::Api, "https://example.invalid/full", {}}));
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
    }
    T_REQUIRE(!transport.submit({65U, HttpChannel::Api, "https://example.invalid/rejected", "limit-post"}));
    T_REQUIRE(wipes.waitForNonZeroCount(1U));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    transport.shutdown();
    T_REQUIRE(wipes.allBytesWereZero());
    T_EQ(wipes.zeroCount(), 0U);
}

TEST_CASE("achievements_http_transport", "contains_reentrant_and_throwing_wipe_observers") {
    HttpTransport* transportPointer = nullptr;
    std::atomic<int> calls{0};
    std::atomic<bool> armed{false};
    std::atomic<bool> sawTransport{false};
    std::atomic<bool> reenteredShutdown{false};
    std::promise<void> executed;
    const auto executedFuture = executed.get_future();
    HttpTransport transport(
        [&](const HttpRequest& request) {
            executed.set_value();
            return HttpResponse{request.id, request.channel, 200L, {'o', 'k'}, {}};
        },
        [&](const std::uint8_t*, std::size_t) {
            if (!armed.load()) {
                return;
            }
            ++calls;
            if (transportPointer != nullptr) {
                sawTransport.store(true);
                transportPointer->shutdown();
                reenteredShutdown.store(true);
            }
            throw std::runtime_error("observer failure");
        }
    );
    transportPointer = &transport;

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/reenter", {}}));
    T_REQUIRE(executedFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    armed.store(true);
    transport.shutdown();

    T_REQUIRE(calls.load() >= 1);
    T_REQUIRE(sawTransport.load());
    T_REQUIRE(reenteredShutdown.load());
}

TEST_CASE("achievements_http_transport", "allows_worker_wipe_observer_to_request_shutdown") {
    HttpTransport* transportPointer = nullptr;
    std::atomic<bool> shutdownReturned{false};
    HttpTransport transport(
        [&](const HttpRequest& request) {
            return HttpResponse{request.id, request.channel, 500L, {'b', 'a', 'd'}, {}};
        },
        [&](const std::uint8_t*, std::size_t size) {
            if (size == 3U && transportPointer != nullptr) {
                transportPointer->shutdown();
                shutdownReturned.store(true);
            }
        }
    );
    transportPointer = &transport;

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/worker", {}}));
    T_REQUIRE(waitUntil([&] { return shutdownReturned.load(); }));
    transport.shutdown();

    T_REQUIRE(shutdownReturned.load());
    T_REQUIRE(!transport.acceptingRequests());
}

TEST_CASE("achievements_http_transport", "allows_worker_shutdown_observer_to_overlap_external_shutdown") {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    HttpTransport* transportPointer = nullptr;
    std::atomic<bool> workerShutdownReturned{false};
    std::atomic<bool> externalStarted{false};
    WipeRecorder wipes;
    HttpTransport transport(
        [&](const HttpRequest& request) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            return HttpResponse{request.id, request.channel, 500L, {'b', 'a', 'd'}, {}};
        },
        [&](const std::uint8_t* bytes, std::size_t size) {
            wipes.observe(bytes, size);
            if (size == 3U && transportPointer != nullptr) {
                transportPointer->shutdown();
                workerShutdownReturned.store(true);
            }
        }
    );
    transportPointer = &transport;

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/overlap", {}}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
    }
    auto externalShutdown = std::async(std::launch::async, [&] {
        externalStarted.store(true);
        transport.shutdown();
    });
    T_REQUIRE(waitUntil([&] { return externalStarted.load() && !transport.acceptingRequests(); }));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();

    T_REQUIRE(externalShutdown.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    externalShutdown.get();
    T_REQUIRE(workerShutdownReturned.load());
    T_REQUIRE(wipes.sawSize(3U));
    T_REQUIRE(wipes.allBytesWereZero());
    T_EQ(wipes.zeroCount(), 0U);
}

TEST_CASE("achievements_http_transport", "serializes_simultaneous_external_shutdown_callers") {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool release = false;
    std::atomic<int> throwingCleanupCalls{0};
    HttpTransport transport(
        [&](const HttpRequest& request) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            return HttpResponse{request.id, request.channel, 200L, {'o', 'k'}, {}};
        },
        [&](const std::uint8_t*, std::size_t size) {
            if (size == 2U) {
                ++throwingCleanupCalls;
                throw std::runtime_error("observer cleanup failure");
            }
        }
    );

    T_REQUIRE(transport.submit({1U, HttpChannel::Api, "https://example.invalid/two-callers", {}}));
    {
        std::unique_lock<std::mutex> lock(mutex);
        T_REQUIRE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
    }
    auto first = std::async(std::launch::async, [&] { transport.shutdown(); });
    auto second = std::async(std::launch::async, [&] { transport.shutdown(); });
    T_REQUIRE(waitUntil([&] { return !transport.acceptingRequests(); }));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();

    T_REQUIRE(first.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    T_REQUIRE(second.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    first.get();
    second.get();
    T_REQUIRE(!transport.acceptingRequests());
    T_EQ(throwingCleanupCalls.load(), 1);
}

} // namespace
