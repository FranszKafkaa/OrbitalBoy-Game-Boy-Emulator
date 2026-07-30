#include "rc_client.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "gb/app/frontend/realtime/retroachievements_config.hpp"
#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/app/frontend/realtime/retroachievements_memory.hpp"
#include "gb/app/frontend/realtime/retroachievements_models.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/app/runtime_paths.hpp"

#include "test_framework.hpp"
#include "test_utils.hpp"

#ifndef GBEMU_ENABLE_RETROACHIEVEMENTS
#error "RetroAchievements tests require GBEMU_ENABLE_RETROACHIEVEMENTS"
#endif

namespace {

uint32_t readMemory(uint32_t, uint8_t*, uint32_t, rc_client_t*) {
    return 0;
}

void callServer(const rc_api_request_t*, rc_client_server_callback_t, void*, rc_client_t*) {
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<gb::frontend::RaHttpResponse> waitAndDrain(
    gb::frontend::RaHttpTransport& transport,
    gb::frontend::RaHttpChannel channel,
    std::size_t expectedCount = 1
) {
    std::vector<gb::frontend::RaHttpResponse> responses;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (responses.size() < expectedCount && std::chrono::steady_clock::now() < deadline) {
        auto completed = transport.takeCompleted(channel);
        responses.insert(
            responses.end(),
            std::make_move_iterator(completed.begin()),
            std::make_move_iterator(completed.end())
        );
        if (responses.size() < expectedCount) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return responses;
}

} // namespace

TEST_CASE("retroachievements", "client_can_remain_in_casual_mode") {
    rc_client_t* client = rc_client_create(readMemory, callServer);
    T_REQUIRE(client != nullptr);
    rc_client_set_hardcore_enabled(client, 0);
    T_REQUIRE(!rc_client_get_hardcore_enabled(client));
    rc_client_destroy(client);
}

TEST_CASE("retroachievements", "memory_reader_rejects_invalid_ranges") {
    gb::GameBoy gameBoy;
    std::array<std::uint8_t, 4> out{};

    T_EQ(gb::frontend::readRetroAchievementsMemory(gameBoy.bus(), 0xFFFF, out.data(), 2), 0U);
    T_EQ(gb::frontend::readRetroAchievementsMemory(gameBoy.bus(), 0, nullptr, 1), 0U);
    T_EQ(gb::frontend::readRetroAchievementsMemory(gameBoy.bus(), 0, out.data(), 0), 0U);
}

TEST_CASE("retroachievements", "memory_reader_reads_wram_in_address_order") {
    gb::GameBoy gameBoy;
    gameBoy.bus().write(0xC000, 0x3A);
    gameBoy.bus().write(0xC001, 0x7F);
    gameBoy.bus().write(0xC002, 0x05);
    std::array<std::uint8_t, 3> out{};

    T_EQ(gb::frontend::readRetroAchievementsMemory(gameBoy.bus(), 0xC000, out.data(), out.size()), 3U);
    T_EQ(out[0], 0x3A);
    T_EQ(out[1], 0x7F);
    T_EQ(out[2], 0x05);
}

TEST_CASE("retroachievements", "config_round_trips_token_without_password") {
    const auto path = tests::makeTempPath("ra_config", ".cfg");
    tests::ScopedPath cleanup(path);

    const gb::frontend::RaConfig expected{1, "Marcelo=Janke\\V", "token=value\\with\\slashes", true, true};
    T_REQUIRE(gb::frontend::saveRetroAchievementsConfig(path.string(), expected));

    const auto actual = gb::frontend::loadRetroAchievementsConfig(path.string());
    T_EQ(actual.username, expected.username);
    T_EQ(actual.token, expected.token);
    T_REQUIRE(actual.autoLogin);
    T_REQUIRE(actual.showNotifications);
    T_REQUIRE(readTextFile(path).find("password") == std::string::npos);
}

TEST_CASE("retroachievements", "config_replaces_existing_content") {
    const auto path = tests::makeTempPath("ra_config_replace", ".cfg");
    tests::ScopedPath cleanup(path);

    T_REQUIRE(gb::frontend::saveRetroAchievementsConfig(path.string(), {1, "first", "first-token", true, true}));
    T_REQUIRE(gb::frontend::saveRetroAchievementsConfig(path.string(), {1, "second", "second-token", false, false}));

    const auto actual = gb::frontend::loadRetroAchievementsConfig(path.string());
    T_EQ(actual.username, std::string("second"));
    T_EQ(actual.token, std::string("second-token"));
    T_REQUIRE(!actual.autoLogin);
    T_REQUIRE(!actual.showNotifications);
}

TEST_CASE("retroachievements", "config_uses_unique_temporary_file_without_clobbering_sibling") {
    const auto directory = tests::makeTempPath("ra_config_temp", "");
    tests::ScopedPath cleanup(directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / "settings.cfg";
    const auto predictableTemporary = directory / "settings.cfg.tmp";
    {
        std::ofstream out(predictableTemporary);
        out << "keep this sibling untouched";
    }

    T_REQUIRE(gb::frontend::saveRetroAchievementsConfig(path.string(), {1, "Marcelo", "token-value", true, true}));
    T_EQ(readTextFile(predictableTemporary), std::string("keep this sibling untouched"));
}

TEST_CASE("retroachievements", "config_missing_or_malformed_uses_safe_defaults") {
    const auto missingPath = tests::makeTempPath("ra_config_missing", ".cfg");
    tests::ScopedPath missingCleanup(missingPath);

    const auto missing = gb::frontend::loadRetroAchievementsConfig(missingPath.string());
    T_EQ(missing.version, 1);
    T_REQUIRE(missing.username.empty());
    T_REQUIRE(missing.token.empty());
    T_REQUIRE(missing.autoLogin);
    T_REQUIRE(missing.showNotifications);

    const auto malformedPath = tests::makeTempPath("ra_config_malformed", ".cfg");
    tests::ScopedPath malformedCleanup(malformedPath);
    {
        std::ofstream out(malformedPath);
        out << "version=one\n";
        out << "username=Marcelo\n";
        out << "token=token-value\n";
        out << "auto_login=perhaps\n";
        out << "show_notifications=not-a-boolean\n";
    }

    const auto malformed = gb::frontend::loadRetroAchievementsConfig(malformedPath.string());
    T_EQ(malformed.version, 1);
    T_REQUIRE(malformed.username.empty());
    T_REQUIRE(malformed.token.empty());
    T_REQUIRE(malformed.autoLogin);
    T_REQUIRE(malformed.showNotifications);

    const auto invalidBooleanPath = tests::makeTempPath("ra_config_invalid_boolean", ".cfg");
    tests::ScopedPath invalidBooleanCleanup(invalidBooleanPath);
    {
        std::ofstream out(invalidBooleanPath);
        out << "version=1\n";
        out << "username=Marcelo\n";
        out << "token=token-value\n";
        out << "auto_login=perhaps\n";
        out << "show_notifications=not-a-boolean\n";
    }

    const auto invalidBoolean = gb::frontend::loadRetroAchievementsConfig(invalidBooleanPath.string());
    T_EQ(invalidBoolean.username, std::string("Marcelo"));
    T_EQ(invalidBoolean.token, std::string("token-value"));
    T_REQUIRE(invalidBoolean.autoLogin);
    T_REQUIRE(invalidBoolean.showNotifications);

    const auto oversizedPath = tests::makeTempPath("ra_config_oversized_load", ".cfg");
    tests::ScopedPath oversizedCleanup(oversizedPath);
    {
        std::ofstream out(oversizedPath);
        out << "version=1\n";
        out << "username=" << std::string(4097, 'u') << '\n';
        out << "token=" << std::string(4097, 't') << '\n';
    }

    const auto oversized = gb::frontend::loadRetroAchievementsConfig(oversizedPath.string());
    T_REQUIRE(oversized.username.empty());
    T_REQUIRE(oversized.token.empty());
}

TEST_CASE("retroachievements", "config_rejects_control_characters_and_oversized_secrets") {
    const auto path = tests::makeTempPath("ra_config_invalid", ".cfg");
    tests::ScopedPath cleanup(path);

    gb::frontend::RaConfig controlCharacter{};
    controlCharacter.username = "Marcelo\nJanke";
    T_REQUIRE(!gb::frontend::saveRetroAchievementsConfig(path.string(), controlCharacter));

    gb::frontend::RaConfig oversized{};
    oversized.token.assign(4097, 'x');
    T_REQUIRE(!gb::frontend::saveRetroAchievementsConfig(path.string(), oversized));
}

TEST_CASE("retroachievements", "ui_models_own_session_values") {
    gb::frontend::RaSessionSnapshot snapshot{};
    snapshot.connectionState = gb::frontend::RaConnectionState::Online;
    snapshot.statusText = "Connected";
    snapshot.profile.user.username = "Marcelo";
    snapshot.profile.library.push_back({42, "Orbital Boy", "https://example.invalid/game.png", "/tmp/game.png", 12, 3, 1});
    snapshot.currentGame = {42, "Orbital Boy", "https://example.invalid/game.png", "/tmp/game.png", 12, 3, 1};
    snapshot.currentAchievements.push_back(
        {7, "First orbit", "Complete orbit one", "https://example.invalid/badge.png", "/tmp/badge.png", 5, true, "1/1"}
    );
    snapshot.gameLoaded = true;

    const auto copied = snapshot;
    T_EQ(copied.profile.user.username, std::string("Marcelo"));
    T_EQ(copied.profile.library.at(0).title, std::string("Orbital Boy"));
    T_EQ(copied.currentAchievements.at(0).title, std::string("First orbit"));
    T_REQUIRE(copied.gameLoaded);
}

TEST_CASE("retroachievements", "global_paths_use_runtime_states_directory") {
    const std::filesystem::path configPath(gb::retroAchievementsConfigPath());
    const std::filesystem::path cacheDirectory(gb::retroAchievementsCacheDirectory());

    T_EQ(configPath.filename().string(), std::string("global.retroachievements"));
    T_EQ(configPath.parent_path().filename().string(), std::string("states"));
    T_EQ(cacheDirectory.filename().string(), std::string("retroachievements-cache"));
    T_EQ(cacheDirectory.parent_path().filename().string(), std::string("states"));
    T_REQUIRE(std::filesystem::is_directory(cacheDirectory));
}

TEST_CASE("retroachievements", "http_returns_completions_only_when_drained") {
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 200, {'o', 'k'}, {}
        };
    });

    T_REQUIRE(transport.submit({42, gb::frontend::RaHttpChannel::Api, "https://example.invalid", {}}));
    const auto responses = waitAndDrain(transport, gb::frontend::RaHttpChannel::Api);

    T_EQ(responses.size(), 1U);
    T_EQ(responses.front().id, 42U);
    T_EQ(responses.front().statusCode, 200L);
    T_EQ(responses.front().body.size(), 2U);
    T_REQUIRE(responses.front().error.empty());
    T_REQUIRE(transport.takeCompleted(gb::frontend::RaHttpChannel::Api).empty());
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_redirect_policy_follows_only_get_requests") {
    const gb::frontend::RaHttpRequest getRequest{
        1, gb::frontend::RaHttpChannel::Api, "https://example.invalid/get", {}
    };
    const gb::frontend::RaHttpRequest postRequest{
        2, gb::frontend::RaHttpChannel::Api, "https://example.invalid/post", "password=secret"
    };

    const auto getPolicy = gb::frontend::makeRaHttpRequestPolicy(getRequest);
    const auto postPolicy = gb::frontend::makeRaHttpRequestPolicy(postRequest);

    T_REQUIRE(getPolicy.method == gb::frontend::RaHttpMethod::Get);
    T_EQ(getPolicy.followLocation, 1L);
    T_EQ(getPolicy.maxRedirects, 3L);
    T_REQUIRE(postPolicy.method == gb::frontend::RaHttpMethod::Post);
    T_EQ(postPolicy.followLocation, 0L);
    T_EQ(postPolicy.maxRedirects, 3L);
}

TEST_CASE("retroachievements", "http_draining_image_preserves_api_completions") {
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 200, {static_cast<std::uint8_t>(request.id)}, {}
        };
    });

    T_REQUIRE(transport.submit({11, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}));
    T_REQUIRE(transport.submit({22, gb::frontend::RaHttpChannel::Image, "https://example.invalid/image", {}}));

    const auto imageResponses = waitAndDrain(transport, gb::frontend::RaHttpChannel::Image);
    T_EQ(imageResponses.size(), 1U);
    T_EQ(imageResponses.front().id, 22U);

    const auto apiResponses = waitAndDrain(transport, gb::frontend::RaHttpChannel::Api);
    T_EQ(apiResponses.size(), 1U);
    T_EQ(apiResponses.front().id, 11U);
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_submit_executes_only_on_its_worker") {
    std::atomic<bool> executorStarted{false};
    std::atomic<bool> releaseExecutor{false};
    std::thread::id submitterThread;
    std::thread::id executorThread;
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        executorThread = std::this_thread::get_id();
        executorStarted.store(true);
        while (!releaseExecutor.load()) {
            std::this_thread::yield();
        }
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 200, {'o', 'k'}, {}
        };
    });

    auto submitFuture = std::async(std::launch::async, [&] {
        submitterThread = std::this_thread::get_id();
        return transport.submit(
            {7, gb::frontend::RaHttpChannel::Api, "https://example.invalid", {}}
        );
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!executorStarted.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool submitReturnedWhileExecutorBlocked =
        submitFuture.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
    releaseExecutor.store(true);
    const bool accepted = submitFuture.get();

    T_REQUIRE(executorStarted.load());
    T_REQUIRE(submitReturnedWhileExecutorBlocked);
    T_REQUIRE(accepted);
    T_REQUIRE(executorThread != submitterThread);
    T_EQ(waitAndDrain(transport, gb::frontend::RaHttpChannel::Api).size(), 1U);
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_uses_exactly_one_worker") {
    std::atomic<int> activeExecutors{0};
    std::atomic<int> maximumActiveExecutors{0};
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        const int active = activeExecutors.fetch_add(1) + 1;
        int observedMaximum = maximumActiveExecutors.load();
        while (active > observedMaximum
               && !maximumActiveExecutors.compare_exchange_weak(observedMaximum, active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        activeExecutors.fetch_sub(1);
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 200, {'o', 'k'}, {}
        };
    });

    for (std::uint64_t id = 1; id <= 8; ++id) {
        T_REQUIRE(transport.submit({id, gb::frontend::RaHttpChannel::Api, "https://example.invalid", {}}));
    }

    T_EQ(waitAndDrain(transport, gb::frontend::RaHttpChannel::Api, 8).size(), 8U);
    T_EQ(maximumActiveExecutors.load(), 1);
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_limits_outstanding_requests_per_channel") {
    std::atomic<bool> executorStarted{false};
    std::atomic<bool> releaseExecutor{false};
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        if (request.id == 0) {
            executorStarted.store(true);
            while (!releaseExecutor.load()) {
                std::this_thread::yield();
            }
        }
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 200, {'o', 'k'}, {}
        };
    });

    bool allAccepted = transport.submit(
        {0, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}
    );
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!executorStarted.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool startedBeforeDeadline = executorStarted.load();

    for (std::uint64_t id = 1; id < 64; ++id) {
        const bool accepted = transport.submit(
            {id, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}
        );
        allAccepted = allAccepted && accepted;
    }
    const bool overflowRejected = !transport.submit(
        {64, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}
    );
    releaseExecutor.store(true);
    transport.shutdown();

    T_REQUIRE(startedBeforeDeadline);
    T_REQUIRE(allAccepted);
    T_REQUIRE(overflowRejected);
}

TEST_CASE("retroachievements", "http_completed_responses_hold_capacity_until_channel_drain") {
    std::atomic<bool> imageExecutorStarted{false};
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        if (request.channel == gb::frontend::RaHttpChannel::Image) {
            imageExecutorStarted.store(true);
        }
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 200, {'o', 'k'}, {}
        };
    });

    for (std::uint64_t id = 0; id < 64; ++id) {
        T_REQUIRE(transport.submit(
            {id, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}
        ));
    }
    T_REQUIRE(!transport.submit(
        {64, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}
    ));
    T_REQUIRE(transport.submit(
        {65, gb::frontend::RaHttpChannel::Image, "https://example.invalid/image", {}}
    ));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!imageExecutorStarted.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    T_REQUIRE(imageExecutorStarted.load());
    T_REQUIRE(!transport.submit(
        {66, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}
    ));

    T_EQ(transport.takeCompleted(gb::frontend::RaHttpChannel::Api).size(), 64U);
    T_REQUIRE(transport.submit(
        {67, gb::frontend::RaHttpChannel::Api, "https://example.invalid/api", {}}
    ));
    T_EQ(waitAndDrain(transport, gb::frontend::RaHttpChannel::Api).size(), 1U);
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_rejects_response_bodies_above_four_mib") {
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{
            request.id,
            request.channel,
            200,
            std::vector<std::uint8_t>(4U * 1024U * 1024U + 1U, 0x5A),
            {}
        };
    });

    T_REQUIRE(transport.submit(
        {99, gb::frontend::RaHttpChannel::Image, "https://example.invalid/large", {}}
    ));
    const auto responses = waitAndDrain(transport, gb::frontend::RaHttpChannel::Image);

    T_EQ(responses.size(), 1U);
    T_EQ(responses.front().id, 99U);
    T_REQUIRE(!responses.front().error.empty());
    T_REQUIRE(responses.front().body.empty());
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_accepts_response_bodies_at_four_mib") {
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{
            request.id,
            request.channel,
            200,
            std::vector<std::uint8_t>(4U * 1024U * 1024U, 0x5A),
            {}
        };
    });

    T_REQUIRE(transport.submit(
        {100, gb::frontend::RaHttpChannel::Image, "https://example.invalid/boundary", {}}
    ));
    const auto responses = waitAndDrain(transport, gb::frontend::RaHttpChannel::Image);

    T_EQ(responses.size(), 1U);
    T_REQUIRE(responses.front().error.empty());
    T_EQ(responses.front().body.size(), 4U * 1024U * 1024U);
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_rejects_unsafe_urls_and_non_success_statuses") {
    std::atomic<int> calls{0};
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        calls.fetch_add(1);
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 503, {'n', 'o'}, {}
        };
    });

    T_REQUIRE(transport.submit({1, gb::frontend::RaHttpChannel::Api, "file:///tmp/private", {}}));
    T_REQUIRE(transport.submit(
        {2, gb::frontend::RaHttpChannel::Api, "https://example.invalid/unavailable", {}}
    ));
    const auto responses = waitAndDrain(transport, gb::frontend::RaHttpChannel::Api, 2);

    T_EQ(responses.size(), 2U);
    T_EQ(calls.load(), 1);
    T_REQUIRE(!responses.at(0).error.empty());
    T_REQUIRE(responses.at(0).body.empty());
    T_EQ(responses.at(1).statusCode, 503L);
    T_REQUIRE(!responses.at(1).error.empty());
    T_REQUIRE(responses.at(1).body.empty());
    transport.shutdown();
}

TEST_CASE("retroachievements", "http_shutdown_is_idempotent_and_cancels_pending_requests") {
    std::atomic<int> calls{0};
    std::atomic<bool> executorStarted{false};
    std::atomic<bool> releaseExecutor{false};
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        calls.fetch_add(1);
        executorStarted.store(true);
        while (!releaseExecutor.load()) {
            std::this_thread::yield();
        }
        return gb::frontend::RaHttpResponse{
            request.id, request.channel, 200, {'o', 'k'}, {}
        };
    });

    const bool firstAccepted =
        transport.submit({1, gb::frontend::RaHttpChannel::Api, "https://example.invalid/one", {}});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!executorStarted.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool startedBeforeDeadline = executorStarted.load();
    if (!startedBeforeDeadline) {
        releaseExecutor.store(true);
    }

    const bool secondAccepted =
        transport.submit({2, gb::frontend::RaHttpChannel::Api, "https://example.invalid/two", {}});
    const bool thirdAccepted =
        transport.submit({3, gb::frontend::RaHttpChannel::Api, "https://example.invalid/three", {}});

    std::thread shutdownThread([&] {
        transport.shutdown();
    });
    const auto shutdownDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (transport.acceptingRequests()
           && std::chrono::steady_clock::now() < shutdownDeadline) {
        std::this_thread::yield();
    }
    const bool stoppingObserved = !transport.acceptingRequests();
    releaseExecutor.store(true);
    shutdownThread.join();
    transport.shutdown();

    T_REQUIRE(firstAccepted);
    T_REQUIRE(startedBeforeDeadline);
    T_REQUIRE(secondAccepted);
    T_REQUIRE(thirdAccepted);
    T_REQUIRE(stoppingObserved);
    T_EQ(calls.load(), 1);
    const auto completed = transport.takeCompleted(gb::frontend::RaHttpChannel::Api);
    T_EQ(completed.size(), 1U);
    T_EQ(completed.front().id, 1U);

    T_REQUIRE(!transport.submit({4, gb::frontend::RaHttpChannel::Api, "https://example.invalid/four", {}}));
    const auto afterShutdown = transport.takeCompleted(gb::frontend::RaHttpChannel::Api);
    T_EQ(calls.load(), 1);
    T_REQUIRE(afterShutdown.empty());
}
