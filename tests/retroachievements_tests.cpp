#include "rc_client.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "gb/app/frontend/realtime/retroachievements_config.hpp"
#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/app/frontend/realtime/retroachievements_memory.hpp"
#include "gb/app/frontend/realtime/retroachievements_models.hpp"
#include "gb/app/frontend/realtime/retroachievements_progress.hpp"
#include "gb/app/frontend/realtime/retroachievements_session.hpp"
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

std::string tempFilePath(std::string_view name) {
    return tests::makeTempPath(std::string(name), "").string();
}

std::vector<std::uint8_t> validProgressSidecar() {
    const std::string hash = "0123456789abcdef0123456789abcdef";
    std::vector<std::uint8_t> bytes{'O', 'B', 'R', 'A', 1};
    bytes.insert(bytes.end(), hash.begin(), hash.end());
    bytes.insert(bytes.end(), {4, 0, 0, 0, 1, 2, 3, 4});
    return bytes;
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

TEST_CASE("retroachievements", "production_session_initializes_without_network") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    gb::frontend::RetroAchievementsSession session(gameBoy, transport);

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::LoggedOut);
    T_REQUIRE(session.snapshot().profile.user.username.empty());
    session.shutdown();
    transport.shutdown();
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

TEST_CASE("retroachievements", "progress_sidecar_round_trips_exact_versioned_format") {
    const std::string statePath = tempFilePath("slot.state");
    const std::string path = gb::frontend::retroAchievementsProgressPathForState(statePath);
    tests::ScopedPath cleanup(path);
    tests::ScopedPath temporaryCleanup(path + ".tmp");
    const std::string hash = "0123456789abcdef0123456789abcdef";
    const std::vector<std::uint8_t> payload{1, 2, 3, 4};

    T_EQ(path, statePath + ".ra-progress");
    T_REQUIRE(gb::frontend::saveRetroAchievementsProgress(path, hash, payload));
    const auto raw = tests::readBinaryFile(path);
    const auto expectedRaw = validProgressSidecar();
    T_REQUIRE(raw == expectedRaw);

    const auto loaded = gb::frontend::loadRetroAchievementsProgress(path, hash);
    T_REQUIRE(loaded.has_value());
    T_EQ(loaded->romHash, hash);
    T_REQUIRE(loaded->payload == payload);
}

TEST_CASE("retroachievements", "progress_sidecar_replaces_existing_content") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    tests::ScopedPath temporaryCleanup(path + ".tmp");
    const std::string firstHash = "0123456789abcdef0123456789abcdef";
    const std::string secondHash = "fedcba9876543210fedcba9876543210";

    T_REQUIRE(gb::frontend::saveRetroAchievementsProgress(path, firstHash, {1, 2, 3, 4}));
    T_REQUIRE(gb::frontend::saveRetroAchievementsProgress(path, secondHash, {5, 6}));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(path, firstHash).has_value());
    const auto loaded = gb::frontend::loadRetroAchievementsProgress(path, secondHash);
    T_REQUIRE(loaded.has_value());
    T_EQ(loaded->romHash, secondHash);
    T_REQUIRE(loaded->payload == std::vector<std::uint8_t>({5, 6}));

    std::vector<std::uint8_t> expected{'O', 'B', 'R', 'A', 1};
    expected.insert(expected.end(), secondHash.begin(), secondHash.end());
    expected.insert(expected.end(), {2, 0, 0, 0, 5, 6});
    T_REQUIRE(tests::readBinaryFile(path) == expected);
}

TEST_CASE("retroachievements", "progress_sidecar_accepts_payload_at_one_mib_limit") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    tests::ScopedPath temporaryCleanup(path + ".tmp");
    const std::string hash = "0123456789abcdef0123456789abcdef";
    const std::vector<std::uint8_t> payload(1024U * 1024U, 0xA5);

    T_REQUIRE(gb::frontend::saveRetroAchievementsProgress(path, hash, payload));
    const auto loaded = gb::frontend::loadRetroAchievementsProgress(path, hash);
    T_REQUIRE(loaded.has_value());
    T_EQ(loaded->payload.size(), payload.size());
    T_EQ(loaded->payload.front(), 0xA5);
    T_EQ(loaded->payload.back(), 0xA5);
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_wrong_magic") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    auto bytes = validProgressSidecar();
    bytes[0] = 'X';
    T_REQUIRE(tests::writeBinaryFile(path, bytes));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef"
    ).has_value());
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_unknown_version") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    auto bytes = validProgressSidecar();
    bytes[4] = 2;
    T_REQUIRE(tests::writeBinaryFile(path, bytes));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef"
    ).has_value());
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_other_rom_hash") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    T_REQUIRE(tests::writeBinaryFile(path, validProgressSidecar()));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(
        path,
        "fedcba9876543210fedcba9876543210"
    ).has_value());
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_truncated_header") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    auto bytes = validProgressSidecar();
    bytes.resize(40);
    T_REQUIRE(tests::writeBinaryFile(path, bytes));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef"
    ).has_value());
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_truncated_payload") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    auto bytes = validProgressSidecar();
    bytes.pop_back();
    T_REQUIRE(tests::writeBinaryFile(path, bytes));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef"
    ).has_value());
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_bytes_after_declared_payload") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    auto bytes = validProgressSidecar();
    bytes.push_back(5);
    T_REQUIRE(tests::writeBinaryFile(path, bytes));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef"
    ).has_value());
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_declared_payload_above_one_mib") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    auto bytes = validProgressSidecar();
    bytes[37] = 1;
    bytes[38] = 0;
    bytes[39] = 16;
    bytes[40] = 0;
    bytes.resize(41U + (1024U * 1024U) + 1U);
    T_REQUIRE(tests::writeBinaryFile(path, bytes));

    T_REQUIRE(!gb::frontend::loadRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef"
    ).has_value());
}

TEST_CASE("retroachievements", "progress_sidecar_rejects_invalid_save_inputs_without_touching_files") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    tests::ScopedPath temporaryCleanup(path + ".tmp");
    const std::vector<std::uint8_t> original{9, 8, 7};
    T_REQUIRE(tests::writeBinaryFile(path, original));

    T_REQUIRE(!gb::frontend::saveRetroAchievementsProgress(
        path,
        "0123456789ABCDEF0123456789ABCDEF",
        {1}
    ));
    T_REQUIRE(tests::readBinaryFile(path) == original);
    T_REQUIRE(!std::filesystem::exists(path + ".tmp"));

    const std::vector<std::uint8_t> oversized((1024U * 1024U) + 1U, 0);
    T_REQUIRE(!gb::frontend::saveRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef",
        oversized
    ));
    T_REQUIRE(tests::readBinaryFile(path) == original);
    T_REQUIRE(!std::filesystem::exists(path + ".tmp"));
}

TEST_CASE("retroachievements", "progress_sidecar_removes_temporary_when_replace_fails") {
    const std::string path = tempFilePath("slot.state.ra-progress");
    tests::ScopedPath cleanup(path);
    tests::ScopedPath temporaryCleanup(path + ".tmp");
    T_REQUIRE(std::filesystem::create_directories(path));

    T_REQUIRE(!gb::frontend::saveRetroAchievementsProgress(
        path,
        "0123456789abcdef0123456789abcdef",
        {1, 2, 3, 4}
    ));
    T_REQUIRE(std::filesystem::is_directory(path));
    T_REQUIRE(!std::filesystem::exists(path + ".tmp"));
}

#if !defined(_WIN32)
TEST_CASE("retroachievements", "progress_sidecar_validates_size_from_open_stream") {
    const auto directory = tests::makeTempPath("ra_progress_stream_identity", "");
    tests::ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto fifoPath = directory / "opened-sidecar";
    const auto replacementPath = directory / "replacement-sidecar";
    const auto sidecarPath = directory / "slot.state.ra-progress";
    const auto nextLinkPath = directory / "next-link";
    const auto bytes = validProgressSidecar();

    T_EQ(::mkfifo(fifoPath.c_str(), 0600), 0);
    T_REQUIRE(tests::writeBinaryFile(replacementPath, bytes));
    std::filesystem::create_symlink(fifoPath, sidecarPath);

    auto load = std::async(std::launch::async, [&] {
        return gb::frontend::loadRetroAchievementsProgress(
            sidecarPath.string(),
            "0123456789abcdef0123456789abcdef"
        );
    });

    std::ofstream openedStreamWriter(fifoPath, std::ios::binary);
    T_REQUIRE(openedStreamWriter.is_open());
    std::filesystem::create_symlink(replacementPath, nextLinkPath);
    std::filesystem::rename(nextLinkPath, sidecarPath);
    openedStreamWriter.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    openedStreamWriter.close();

    T_REQUIRE(load.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    T_REQUIRE(!load.get().has_value());
}
#endif

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

namespace {

class FakeRaClientApi final : public gb::frontend::RaClientApi {
public:
    FakeRaClientApi()
        : ownerThread(std::this_thread::get_id()) {
        handle = reinterpret_cast<rc_client_t*>(this);
        asyncHandle = reinterpret_cast<rc_client_async_handle_t*>(this);
        game.title = "";
        game.hash = "";
        game.badge_url = "";
        setUser("Marcelo", "Marcelo", "token-1");
    }

    rc_client_t* create(
        rc_client_read_memory_func_t readMemory,
        rc_client_server_call_t serverCall
    ) override {
        recordCall();
        readMemoryFunction = readMemory;
        serverCallFunction = serverCall;
        return handle;
    }

    void destroy(rc_client_t*) override {
        recordCall();
        destroyed = true;
    }

    void setUserdata(rc_client_t*, void* value) override {
        recordCall();
        userdata = value;
    }

    void setEventHandler(rc_client_t*, rc_client_event_handler_t handlerValue) override {
        recordCall();
        eventHandler = handlerValue;
    }

    void setHardcoreEnabled(rc_client_t*, int enabled) override {
        recordCall();
        hardcoreEnabled = enabled;
    }

    rc_client_async_handle_t* beginLoginWithPassword(
        rc_client_t*,
        const char* username,
        const char* password,
        rc_client_callback_t callback,
        void* callbackUserdata
    ) override {
        recordCall();
        ++passwordLoginCalls;
        lastLoginUsername = username ? username : "";
        lastPassword = password ? password : "";
        loginCallback = callback;
        loginCallbackUserdata = callbackUserdata;
        return asyncHandle;
    }

    rc_client_async_handle_t* beginLoginWithToken(
        rc_client_t*,
        const char* username,
        const char* token,
        rc_client_callback_t callback,
        void* callbackUserdata
    ) override {
        recordCall();
        ++tokenLoginCalls;
        lastLoginUsername = username ? username : "";
        lastToken = token ? token : "";
        loginCallback = callback;
        loginCallbackUserdata = callbackUserdata;
        return asyncHandle;
    }

    void logout(rc_client_t*) override {
        recordCall();
        ++logoutCalls;
    }

    const rc_client_user_t* getUserInfo(const rc_client_t*) const override {
        return userAvailable ? &user : nullptr;
    }

    int userGetImageUrl(
        const rc_client_user_t*,
        char* buffer,
        std::size_t bufferSize
    ) const override {
        ++const_cast<FakeRaClientApi*>(this)->userImageCalls;
        return copyUrl(userAvatarUrl, buffer, bufferSize);
    }

    rc_client_async_handle_t* beginFetchAllUserProgress(
        rc_client_t*,
        std::uint32_t consoleId,
        rc_client_fetch_all_user_progress_callback_t callback,
        void* callbackUserdata
    ) override {
        recordCall();
        progressConsoles.push_back(consoleId);
        if (!autoCompleteProgress) {
            progressCallbacks[consoleId] = {callback, callbackUserdata};
            return asyncHandle;
        }

        auto& entries = progressEntries[consoleId];
        rc_client_all_user_progress_t list{
            entries.empty() ? nullptr : entries.data(),
            static_cast<std::uint32_t>(entries.size()),
        };
        const int result = progressResults.count(consoleId) != 0
            ? progressResults.at(consoleId)
            : RC_OK;
        callback(
            result,
            result == RC_OK ? nullptr : "profile failed",
            result == RC_OK ? &list : nullptr,
            handle,
            callbackUserdata
        );
        return asyncHandle;
    }

    void destroyAllUserProgress(rc_client_all_user_progress_t*) override {
        recordCall();
    }

    rc_client_async_handle_t* beginFetchGameTitles(
        rc_client_t*,
        const std::uint32_t* gameIds,
        std::uint32_t numGameIds,
        rc_client_fetch_game_titles_callback_t callback,
        void* callbackUserdata
    ) override {
        recordCall();
        titleBatches.emplace_back(gameIds, gameIds + numGameIds);
        if (!autoCompleteTitles) {
            titleCallbacks.push_back({callback, callbackUserdata});
            return asyncHandle;
        }

        std::vector<rc_client_game_title_entry_t> entries;
        entries.reserve(numGameIds);
        for (std::uint32_t index = 0; index < numGameIds; ++index) {
            const auto gameId = gameIds[index];
            auto& title = titles[gameId];
            if (title.empty()) {
                title = "Game " + std::to_string(gameId);
            }
            auto& badgeUrl = titleBadgeUrls[gameId];
            if (badgeUrl.empty()) {
                badgeUrl = "https://example.invalid/game-" + std::to_string(gameId) + ".png";
            }
            rc_client_game_title_entry_t entry{};
            entry.game_id = gameId;
            entry.title = title.c_str();
            entry.badge_url = badgeUrl.c_str();
            entries.push_back(entry);
        }
        rc_client_game_title_list_t list{
            entries.empty() ? nullptr : entries.data(),
            static_cast<std::uint32_t>(entries.size()),
        };
        callback(
            titleResult,
            titleResult == RC_OK ? nullptr : "titles failed",
            titleResult == RC_OK ? &list : nullptr,
            handle,
            callbackUserdata
        );
        return asyncHandle;
    }

    void destroyGameTitleList(rc_client_game_title_list_t*) override {
        recordCall();
    }

    rc_client_async_handle_t* beginIdentifyAndLoadGame(
        rc_client_t*,
        std::uint32_t consoleId,
        const char* filePath,
        const std::uint8_t*,
        std::size_t,
        rc_client_callback_t callback,
        void* callbackUserdata
    ) override {
        recordCall();
        lastGameConsole = consoleId;
        lastGamePath = filePath ? filePath : "";
        gameCallback = callback;
        gameCallbackUserdata = callbackUserdata;
        if (autoCompleteGame) {
            callback(gameResult, gameResult == RC_OK ? nullptr : "game failed", handle, callbackUserdata);
        }
        return asyncHandle;
    }

    bool isGameLoaded(const rc_client_t*) const override {
        return gameAvailable;
    }

    const rc_client_game_t* getGameInfo(const rc_client_t*) const override {
        return gameAvailable ? &game : nullptr;
    }

    int gameGetImageUrl(
        const rc_client_game_t* gameInfo,
        char* buffer,
        std::size_t bufferSize
    ) const override {
        return copyUrl(gameInfo && gameInfo->badge_url ? gameInfo->badge_url : "", buffer, bufferSize);
    }

    void getUserGameSummary(
        const rc_client_t*,
        rc_client_user_game_summary_t* outSummary
    ) const override {
        if (outSummary) {
            *outSummary = gameSummary;
        }
    }

    rc_client_achievement_list_t* createAchievementList(
        rc_client_t*,
        int category,
        int grouping
    ) override {
        recordCall();
        achievementCategory = category;
        achievementGrouping = grouping;
        achievementPointers.clear();
        for (auto& achievement : achievements) {
            achievementPointers.push_back(&achievement);
        }
        achievementBucket = {
            achievementPointers.empty() ? nullptr : achievementPointers.data(),
            static_cast<std::uint32_t>(achievementPointers.size()),
            "Progress",
            0,
            RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED,
        };
        achievementList = {&achievementBucket, 1};
        return &achievementList;
    }

    void destroyAchievementList(rc_client_achievement_list_t*) override {
        recordCall();
    }

    int achievementGetImageUrl(
        const rc_client_achievement_t* achievement,
        int,
        char* buffer,
        std::size_t bufferSize
    ) const override {
        return copyUrl(
            achievement && achievement->badge_url ? achievement->badge_url : "",
            buffer,
            bufferSize
        );
    }

    void doFrame(rc_client_t*) override {
        recordCall();
        ++doFrameCalls;
        if (eventOnFrame.has_value() && eventHandler) {
            eventHandler(&*eventOnFrame, handle);
            eventOnFrame.reset();
        }
    }

    void idle(rc_client_t*) override {
        recordCall();
        ++idleCalls;
    }

    std::size_t progressSize(rc_client_t*) const override {
        return serializedProgress.size();
    }

    int serializeProgressSized(
        rc_client_t*,
        std::uint8_t* buffer,
        std::size_t bufferSize
    ) const override {
        if (serializeResult != RC_OK) {
            return serializeResult;
        }
        if (bufferSize != serializedProgress.size()) {
            return RC_INSUFFICIENT_BUFFER;
        }
        std::copy(serializedProgress.begin(), serializedProgress.end(), buffer);
        return RC_OK;
    }

    int deserializeProgressSized(
        rc_client_t*,
        const std::uint8_t* buffer,
        std::size_t bufferSize
    ) override {
        recordCall();
        ++deserializeCalls;
        deserializedProgress.assign(buffer, buffer + bufferSize);
        return deserializeResult;
    }

    void completeLogin(int result, const char* errorMessage = nullptr) {
        T_REQUIRE(loginCallback != nullptr);
        auto callback = loginCallback;
        loginCallback = nullptr;
        callback(result, errorMessage, handle, loginCallbackUserdata);
    }

    void completeGame(int result, const char* errorMessage = nullptr) {
        T_REQUIRE(gameCallback != nullptr);
        auto callback = gameCallback;
        gameCallback = nullptr;
        callback(result, errorMessage, handle, gameCallbackUserdata);
    }

    void emitAchievement(std::size_t index) {
        T_REQUIRE(eventHandler != nullptr);
        T_REQUIRE(index < achievements.size());
        rc_client_event_t event{};
        event.type = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED;
        event.achievement = &achievements[index];
        eventHandler(&event, handle);
    }

    void emitIgnoredEvent() {
        T_REQUIRE(eventHandler != nullptr);
        rc_client_event_t event{};
        event.type = RC_CLIENT_EVENT_LEADERBOARD_STARTED;
        eventHandler(&event, handle);
    }

    void issueServerRequest(std::string url) {
        T_REQUIRE(serverCallFunction != nullptr);
        serverUrl = std::move(url);
        rc_api_request_t request{};
        request.url = serverUrl.c_str();
        serverCallFunction(
            &request,
            [](const rc_api_server_response_t* response, void* callbackData) {
                auto& fake = *static_cast<FakeRaClientApi*>(callbackData);
                fake.serverCallbackThread = std::this_thread::get_id();
                fake.serverResponseStatus = response ? response->http_status_code : 0;
            },
            this,
            handle
        );
    }

    std::uint32_t readThroughClient(
        std::uint32_t address,
        std::uint8_t* buffer,
        std::uint32_t numBytes
    ) {
        T_REQUIRE(readMemoryFunction != nullptr);
        return readMemoryFunction(address, buffer, numBytes, handle);
    }

    void setUser(std::string username, std::string displayName, std::string token) {
        userUsername = std::move(username);
        userDisplayName = std::move(displayName);
        userToken = std::move(token);
        user.username = userUsername.c_str();
        user.display_name = userDisplayName.c_str();
        user.token = userToken.c_str();
        user.avatar_url = userAvatarUrl.c_str();
    }

    void setGame(
        std::uint32_t id,
        std::string title,
        std::string hash,
        std::string badgeUrl
    ) {
        gameTitle = std::move(title);
        gameHash = std::move(hash);
        gameBadgeUrl = std::move(badgeUrl);
        game.id = id;
        game.console_id = 4;
        game.title = gameTitle.c_str();
        game.hash = gameHash.c_str();
        game.badge_url = gameBadgeUrl.c_str();
        gameAvailable = true;
    }

    rc_client_achievement_t makeAchievement(
        std::uint32_t id,
        const char* title,
        std::uint32_t points,
        bool unlocked
    ) {
        achievementTitles.emplace_back(title);
        achievementDescriptions.emplace_back("Description " + std::to_string(id));
        achievementBadgeUrls.emplace_back(
            "https://example.invalid/achievement-" + std::to_string(id) + ".png"
        );
        rc_client_achievement_t achievement{};
        achievement.id = id;
        achievement.title = achievementTitles.back().c_str();
        achievement.description = achievementDescriptions.back().c_str();
        achievement.badge_url = achievementBadgeUrls.back().c_str();
        achievement.points = points;
        achievement.state = unlocked
            ? RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED
            : RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE;
        achievement.unlocked = unlocked
            ? RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE
            : RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
        std::snprintf(
            achievement.measured_progress,
            sizeof(achievement.measured_progress),
            "%u/%u",
            unlocked ? 1U : 0U,
            1U
        );
        return achievement;
    }

    struct ProgressCallback {
        rc_client_fetch_all_user_progress_callback_t callback = nullptr;
        void* userdata = nullptr;
    };

    struct TitleCallback {
        rc_client_fetch_game_titles_callback_t callback = nullptr;
        void* userdata = nullptr;
    };

    std::thread::id ownerThread;
    bool calledOffOwner = false;
    bool destroyed = false;
    int hardcoreEnabled = 1;
    int passwordLoginCalls = 0;
    int tokenLoginCalls = 0;
    int logoutCalls = 0;
    int doFrameCalls = 0;
    int idleCalls = 0;
    int userImageCalls = 0;
    int deserializeCalls = 0;
    int achievementCategory = 0;
    int achievementGrouping = 0;
    std::string lastLoginUsername;
    std::string lastPassword;
    std::string lastToken;
    std::uint32_t lastGameConsole = 0;
    std::string lastGamePath;
    std::vector<std::uint32_t> progressConsoles;
    std::vector<std::vector<std::uint32_t>> titleBatches;
    std::unordered_map<std::uint32_t, std::vector<rc_client_all_user_progress_entry_t>>
        progressEntries;
    std::unordered_map<std::uint32_t, int> progressResults;
    std::map<std::uint32_t, std::string> titles;
    std::map<std::uint32_t, std::string> titleBadgeUrls;
    bool autoCompleteProgress = false;
    bool autoCompleteTitles = true;
    int titleResult = RC_OK;
    bool autoCompleteGame = false;
    int gameResult = RC_OK;
    bool userAvailable = true;
    bool gameAvailable = false;
    rc_client_user_t user{};
    rc_client_game_t game{};
    rc_client_user_game_summary_t gameSummary{};
    std::deque<std::string> achievementTitles;
    std::deque<std::string> achievementDescriptions;
    std::deque<std::string> achievementBadgeUrls;
    std::vector<rc_client_achievement_t> achievements;
    std::vector<const rc_client_achievement_t*> achievementPointers;
    rc_client_achievement_bucket_t achievementBucket{};
    rc_client_achievement_list_t achievementList{};
    std::vector<std::uint8_t> serializedProgress;
    std::vector<std::uint8_t> deserializedProgress;
    int serializeResult = RC_OK;
    int deserializeResult = RC_OK;
    std::optional<rc_client_event_t> eventOnFrame;
    std::thread::id serverCallbackThread;
    int serverResponseStatus = 0;

private:
    static int copyUrl(
        std::string_view value,
        char* buffer,
        std::size_t bufferSize
    ) {
        if (!buffer || value.size() >= bufferSize) {
            return RC_INSUFFICIENT_BUFFER;
        }
        std::memcpy(buffer, value.data(), value.size());
        buffer[value.size()] = '\0';
        return RC_OK;
    }

    void recordCall() const {
        if (std::this_thread::get_id() != ownerThread) {
            const_cast<FakeRaClientApi*>(this)->calledOffOwner = true;
        }
    }

    rc_client_t* handle = nullptr;
    rc_client_async_handle_t* asyncHandle = nullptr;
    void* userdata = nullptr;
    rc_client_read_memory_func_t readMemoryFunction = nullptr;
    rc_client_server_call_t serverCallFunction = nullptr;
    rc_client_event_handler_t eventHandler = nullptr;
    rc_client_callback_t loginCallback = nullptr;
    void* loginCallbackUserdata = nullptr;
    rc_client_callback_t gameCallback = nullptr;
    void* gameCallbackUserdata = nullptr;
    std::unordered_map<std::uint32_t, ProgressCallback> progressCallbacks;
    std::vector<TitleCallback> titleCallbacks;
    std::string userUsername;
    std::string userDisplayName;
    std::string userToken;
    std::string userAvatarUrl = "https://example.invalid/avatar.png";
    std::string gameTitle;
    std::string gameHash;
    std::string gameBadgeUrl;
    std::string serverUrl;
};

gb::frontend::RetroAchievementsSession makeSession(
    gb::GameBoy& gameBoy,
    gb::frontend::RaHttpTransport& transport,
    FakeRaClientApi& api,
    gb::frontend::RaConfig config = {},
    gb::frontend::RaConfigPersistence persist = {}
) {
    return gb::frontend::RetroAchievementsSession(
        gameBoy,
        transport,
        std::move(config),
        std::move(persist),
        &api
    );
}

void processUntil(
    gb::frontend::RetroAchievementsSession& session,
    const std::function<bool()>& condition
) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        session.processPending();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.processPending();
    T_REQUIRE(condition());
}

} // namespace

TEST_CASE("retroachievements", "session_password_login_transitions_online_without_exposing_secret") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    std::vector<gb::frontend::RaConfig> persisted;
    auto session = makeSession(
        gameBoy,
        transport,
        api,
        {},
        [&](const auto& config) { persisted.push_back(config); }
    );

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::LoggedOut);
    session.enqueueLogin("Marcelo", "segredo-super-secreto");
    session.processPending();

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::LoggingIn);
    T_EQ(api.passwordLoginCalls, 1);
    T_EQ(api.lastPassword, std::string("segredo-super-secreto"));
    session.processPending();
    T_EQ(api.passwordLoginCalls, 1);

    api.setUser("Marcelo", "Marcelo Janke", "token-retornado");
    api.user.score = 1234;
    api.user.score_softcore = 56;
    api.user.num_unread_messages = 7;
    api.completeLogin(RC_OK);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::Online);
    T_EQ(snapshot.profile.user.username, std::string("Marcelo"));
    T_EQ(snapshot.profile.user.displayName, std::string("Marcelo Janke"));
    T_EQ(snapshot.profile.user.scoreHardcore, 1234U);
    T_EQ(snapshot.profile.user.scoreCasual, 56U);
    T_EQ(snapshot.profile.user.unreadMessages, 7U);
    T_EQ(api.userImageCalls, 1);
    T_REQUIRE(snapshot.statusText.find("segredo-super-secreto") == std::string::npos);
    T_REQUIRE(snapshot.errorText.find("segredo-super-secreto") == std::string::npos);
    T_EQ(persisted.size(), 1U);
    T_EQ(persisted.front().token, std::string("token-retornado"));
    T_REQUIRE(api.hardcoreEnabled == 0);
    T_REQUIRE(!api.calledOffOwner);

    const auto events = session.takeEvents();
    T_EQ(events.size(), 1U);
    T_REQUIRE(events.front().type == gb::frontend::RaUiEventType::LoginSucceeded);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_memory_thunk_reads_only_on_owner_thread") {
    gb::GameBoy gameBoy;
    gameBoy.bus().write(0xC000, 0x5A);
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    auto session = makeSession(gameBoy, transport, api);
    std::array<std::uint8_t, 1> ownerBytes{};

    T_EQ(api.readThroughClient(0xC000, ownerBytes.data(), 1), 1U);
    T_EQ(ownerBytes.front(), 0x5A);
    auto backgroundRead = std::async(std::launch::async, [&] {
        std::array<std::uint8_t, 1> bytes{};
        return api.readThroughClient(0xC000, bytes.data(), 1);
    });
    T_EQ(backgroundRead.get(), 0U);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_login_failure_clears_only_non_transient_credentials") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    gb::frontend::RaConfig config{};
    config.username = "Marcelo";
    config.token = "token-anterior";
    std::vector<gb::frontend::RaConfig> persisted;
    auto session = makeSession(
        gameBoy,
        transport,
        api,
        config,
        [&](const auto& next) { persisted.push_back(next); }
    );

    session.enqueueTokenLogin(config.username, config.token);
    session.processPending();
    api.completeLogin(RC_NO_RESPONSE, "network detail");

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::Error);
    T_REQUIRE(session.snapshot().errorText.find("network detail") == std::string::npos);
    T_REQUIRE(persisted.empty());

    session.enqueueTokenLogin(config.username, config.token);
    session.processPending();
    api.completeLogin(RC_INVALID_CREDENTIALS, "bad password");

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::Error);
    T_EQ(persisted.size(), 1U);
    T_REQUIRE(persisted.back().token.empty());
    const auto events = session.takeEvents();
    T_EQ(events.size(), 2U);
    T_REQUIRE(events.back().type == gb::frontend::RaUiEventType::LoginFailed);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_transport_failure_goes_offline_on_owner_thread_and_keeps_snapshot") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{
            request.id,
            request.channel,
            0,
            {},
            "sem rede"
        };
    });
    FakeRaClientApi api;
    auto session = makeSession(gameBoy, transport, api);
    session.enqueueTokenLogin("Marcelo", "token");
    session.processPending();
    api.completeLogin(RC_OK);
    const auto before = session.snapshot();

    api.issueServerRequest("https://example.invalid/profile");
    processUntil(session, [&] { return api.serverResponseStatus != 0; });

    const auto after = session.snapshot();
    T_REQUIRE(after.connectionState == gb::frontend::RaConnectionState::Offline);
    T_EQ(after.profile.user.username, before.profile.user.username);
    T_EQ(after.profile.user.avatarUrl, before.profile.user.avatarUrl);
    T_REQUIRE(api.serverCallbackThread == api.ownerThread);
    const auto events = session.takeEvents();
    T_REQUIRE(!events.empty());
    T_REQUIRE(events.back().type == gb::frontend::RaUiEventType::Offline);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_transport_backpressure_completes_rejection_without_pending_callback") {
    std::atomic<bool> executorStarted{false};
    std::atomic<bool> releaseExecutor{false};
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        executorStarted.store(true);
        while (!releaseExecutor.load()) {
            std::this_thread::yield();
        }
        return gb::frontend::RaHttpResponse{
            request.id,
            request.channel,
            200,
            {'o', 'k'},
            {}
        };
    });
    T_REQUIRE(transport.submit({
        1000,
        gb::frontend::RaHttpChannel::Api,
        "https://example.invalid/blocked",
        {},
    }));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!executorStarted.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    T_REQUIRE(executorStarted.load());
    for (std::uint64_t id = 1001; id < 1064; ++id) {
        T_REQUIRE(transport.submit({
            id,
            gb::frontend::RaHttpChannel::Api,
            "https://example.invalid/full",
            {},
        }));
    }

    FakeRaClientApi api;
    auto session = makeSession(gameBoy, transport, api);
    session.enqueueTokenLogin("Marcelo", "token");
    session.processPending();
    api.completeLogin(RC_OK);
    api.issueServerRequest("https://example.invalid/rejected");
    session.processPending();

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::Offline);
    T_EQ(
        api.serverResponseStatus,
        RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR
    );
    T_REQUIRE(api.serverCallbackThread == api.ownerThread);

    releaseExecutor.store(true);
    transport.shutdown();
    session.processPending();
    T_EQ(
        api.serverResponseStatus,
        RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR
    );
    session.shutdown();
}

TEST_CASE("retroachievements", "production_session_handles_transport_backpressure_without_network") {
    std::atomic<bool> executorStarted{false};
    std::atomic<bool> releaseExecutor{false};
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([&](const auto& request) {
        executorStarted.store(true);
        while (!releaseExecutor.load()) {
            std::this_thread::yield();
        }
        return gb::frontend::RaHttpResponse{
            request.id,
            request.channel,
            200,
            {'o', 'k'},
            {}
        };
    });
    T_REQUIRE(transport.submit({
        2000,
        gb::frontend::RaHttpChannel::Api,
        "https://example.invalid/blocked",
        {},
    }));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!executorStarted.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    T_REQUIRE(executorStarted.load());
    for (std::uint64_t id = 2001; id < 2064; ++id) {
        T_REQUIRE(transport.submit({
            id,
            gb::frontend::RaHttpChannel::Api,
            "https://example.invalid/full",
            {},
        }));
    }

    gb::frontend::RetroAchievementsSession session(gameBoy, transport);
    session.enqueueTokenLogin("Marcelo", "token");
    session.processPending();

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::Error);
    T_REQUIRE(!session.snapshot().errorText.empty());
    session.shutdown();
    releaseExecutor.store(true);
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_process_pending_does_not_drain_image_channel") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{
            request.id,
            request.channel,
            200,
            {'p', 'n', 'g'},
            {}
        };
    });
    FakeRaClientApi api;
    auto session = makeSession(gameBoy, transport, api);

    T_REQUIRE(transport.submit({
        71,
        gb::frontend::RaHttpChannel::Image,
        "https://example.invalid/image.png",
        {},
    }));
    std::vector<gb::frontend::RaHttpResponse> imageResponses;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (imageResponses.empty() && std::chrono::steady_clock::now() < deadline) {
        session.processPending();
        imageResponses =
            transport.takeCompleted(gb::frontend::RaHttpChannel::Image);
        std::this_thread::yield();
    }
    session.processPending();

    T_EQ(imageResponses.size(), 1U);
    T_EQ(imageResponses.front().id, 71U);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_logout_clears_token_profile_and_current_game") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    std::vector<gb::frontend::RaConfig> persisted;
    auto session = makeSession(
        gameBoy,
        transport,
        api,
        {},
        [&](const auto& config) { persisted.push_back(config); }
    );
    session.enqueueLogin("Marcelo", "senha");
    session.processPending();
    api.completeLogin(RC_OK);
    api.setGame(42, "Orbital Boy", "0123456789abcdef0123456789abcdef", "game.png");
    api.gameSummary.num_core_achievements = 12;
    api.gameSummary.num_unlocked_achievements = 3;
    session.enqueueLoadGame(4, "/roms/orbital.gb");
    session.processPending();
    api.completeGame(RC_OK);
    T_REQUIRE(session.snapshot().gameLoaded);

    session.enqueueLogout();
    session.processPending();

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::LoggedOut);
    T_REQUIRE(snapshot.profile.user.username.empty());
    T_REQUIRE(snapshot.profile.library.empty());
    T_REQUIRE(snapshot.currentGame.title.empty());
    T_REQUIRE(snapshot.currentAchievements.empty());
    T_REQUIRE(!snapshot.gameLoaded);
    T_REQUIRE(!persisted.empty());
    T_REQUIRE(persisted.back().token.empty());
    T_EQ(api.logoutCalls, 1);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_ignores_login_callback_that_arrives_after_logout") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    auto session = makeSession(gameBoy, transport, api);
    session.enqueueLogin("Marcelo", "senha");
    session.processPending();
    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::LoggingIn);

    session.enqueueLogout();
    session.processPending();
    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::LoggedOut);
    api.completeLogin(RC_ABORTED, "Login aborted");

    T_REQUIRE(session.snapshot().connectionState == gb::frontend::RaConnectionState::LoggedOut);
    T_REQUIRE(session.snapshot().errorText.empty());
    const auto events = session.takeEvents();
    T_REQUIRE(events.empty());
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_ignores_game_callback_that_arrives_after_logout") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.setGame(42, "Orbital Boy", "0123456789abcdef0123456789abcdef", "game.png");
    auto session = makeSession(gameBoy, transport, api);
    session.enqueueLoadGame(4, "/roms/orbital.gb");
    session.processPending();

    session.enqueueLogout();
    session.processPending();
    api.completeGame(RC_OK);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::LoggedOut);
    T_REQUIRE(!snapshot.gameLoaded);
    T_REQUIRE(snapshot.currentGame.title.empty());
    T_REQUIRE(session.takeEvents().empty());
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_ignores_achievement_event_that_arrives_after_logout") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.setGame(42, "Orbital Boy", "0123456789abcdef0123456789abcdef", "game.png");
    api.achievements.push_back(api.makeAchievement(7, "First Orbit", 5, true));
    auto session = makeSession(gameBoy, transport, api);
    session.enqueueLoadGame(4, "/roms/orbital.gb");
    session.processPending();
    api.completeGame(RC_OK);
    T_EQ(session.takeEvents().size(), 1U);

    session.enqueueLogout();
    session.processPending();
    api.emitAchievement(0);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::LoggedOut);
    T_REQUIRE(!snapshot.gameLoaded);
    T_REQUIRE(snapshot.currentAchievements.empty());
    T_REQUIRE(session.takeEvents().empty());
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_do_frame_processes_game_load_before_one_runtime_frame") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.setGame(91, "Color Orbit", "0123456789abcdef0123456789abcdef", "game.png");
    api.autoCompleteGame = true;
    auto session = makeSession(gameBoy, transport, api);

    session.enqueueLoadGame(0, "/roms/color-orbit.gbc");
    gameBoy.setHardwareMode(true);
    session.doFrame();

    T_EQ(api.lastGameConsole, 6U);
    T_EQ(api.doFrameCalls, 1);
    T_REQUIRE(session.snapshot().gameLoaded);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_loads_game_in_casual_mode_and_serializes_matching_progress") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.setGame(42, "Orbital Boy", "0123456789abcdef0123456789abcdef", "game.png");
    api.gameSummary.num_core_achievements = 2;
    api.gameSummary.num_unlocked_achievements = 1;
    api.achievements.push_back(api.makeAchievement(7, "First Orbit", 5, true));
    api.achievements.push_back(api.makeAchievement(8, "Second Orbit", 10, false));
    api.serializedProgress = {1, 2, 3, 4};
    auto session = makeSession(gameBoy, transport, api);

    session.enqueueLoadGame(4, "/roms/orbital.gb");
    session.processPending();
    api.completeGame(RC_OK);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.gameLoaded);
    T_EQ(snapshot.currentGame.gameId, 42U);
    T_EQ(snapshot.currentGame.total, 2U);
    T_EQ(snapshot.currentGame.unlockedCasual, 1U);
    T_EQ(snapshot.currentAchievements.size(), 2U);
    T_REQUIRE(snapshot.currentAchievements.front().unlocked);
    T_EQ(snapshot.currentAchievements.front().measuredProgress, std::string("1/1"));
    T_EQ(api.achievementCategory, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE);
    T_EQ(api.achievementGrouping, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);

    T_REQUIRE(session.serializeProgress() == std::vector<std::uint8_t>({1, 2, 3, 4}));
    api.serializeResult = RC_API_FAILURE;
    T_REQUIRE(session.serializeProgress().empty());
    api.serializeResult = RC_OK;
    T_REQUIRE(!session.deserializeProgress(
        "fedcba9876543210fedcba9876543210",
        {9, 8}
    ));
    T_EQ(api.deserializeCalls, 0);
    T_REQUIRE(session.deserializeProgress(
        "0123456789abcdef0123456789abcdef",
        {9, 8}
    ));
    T_REQUIRE(api.deserializedProgress == std::vector<std::uint8_t>({9, 8}));
    api.deserializeResult = RC_API_FAILURE;
    T_REQUIRE(!session.deserializeProgress(
        "0123456789abcdef0123456789abcdef",
        {7, 6}
    ));

    session.doFrame();
    session.idle();
    T_EQ(api.doFrameCalls, 1);
    T_EQ(api.idleCalls, 1);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_merges_gb_gbc_profile_batches_titles_and_sorts_case_insensitively") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.autoCompleteProgress = true;
    for (std::uint32_t id = 1; id <= 101; ++id) {
        api.progressEntries[4].push_back({id, 10, id % 4, id % 3});
    }
    for (std::uint32_t id = 100; id <= 205; ++id) {
        api.progressEntries[6].push_back({id, 20, id % 5, id % 2});
    }
    api.titles[1] = "beta";
    api.titles[2] = "Alpha";
    api.titles[3] = "alpha";
    auto session = makeSession(gameBoy, transport, api);

    session.enqueueLogin("Marcelo", "senha");
    session.processPending();
    api.completeLogin(RC_OK);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::Online);
    T_EQ(api.progressConsoles.size(), 2U);
    T_EQ(api.progressConsoles.at(0), 4U);
    T_EQ(api.progressConsoles.at(1), 6U);
    T_EQ(snapshot.profile.library.size(), 205U);
    T_EQ(api.titleBatches.size(), 3U);
    T_EQ(api.titleBatches.at(0).size(), 100U);
    T_EQ(api.titleBatches.at(1).size(), 100U);
    T_EQ(api.titleBatches.at(2).size(), 5U);
    for (const auto& batch : api.titleBatches) {
        T_REQUIRE(batch.size() <= 100U);
    }
    T_EQ(snapshot.profile.library.at(0).gameId, 2U);
    T_EQ(snapshot.profile.library.at(1).gameId, 3U);
    T_EQ(snapshot.profile.library.at(2).gameId, 1U);
    const auto duplicate = std::find_if(
        snapshot.profile.library.begin(),
        snapshot.profile.library.end(),
        [](const auto& game) { return game.gameId == 100; }
    );
    T_REQUIRE(duplicate != snapshot.profile.library.end());
    T_EQ(duplicate->total, 20U);
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_keeps_successful_console_profile_when_other_console_fails") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.autoCompleteProgress = true;
    api.progressEntries[4].push_back({7, 9, 4, 2});
    api.progressResults[6] = RC_API_FAILURE;
    api.titles[7] = "Orbital Boy";
    auto session = makeSession(gameBoy, transport, api);

    session.enqueueTokenLogin("Marcelo", "token");
    session.processPending();
    api.completeLogin(RC_OK);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::Online);
    T_EQ(snapshot.profile.library.size(), 1U);
    T_EQ(snapshot.profile.library.front().gameId, 7U);
    T_REQUIRE(!snapshot.errorText.empty());
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_publishes_empty_library_for_empty_console_results") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.autoCompleteProgress = true;
    auto session = makeSession(gameBoy, transport, api);

    session.enqueueTokenLogin("Marcelo", "token");
    session.processPending();
    api.completeLogin(RC_OK);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::Online);
    T_REQUIRE(snapshot.profile.library.empty());
    T_REQUIRE(snapshot.errorText.empty());
    T_REQUIRE(api.titleBatches.empty());
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_publishes_profile_progress_when_title_batch_fails") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.autoCompleteProgress = true;
    api.progressEntries[4].push_back({17, 12, 5, 3});
    api.titleResult = RC_API_FAILURE;
    auto session = makeSession(gameBoy, transport, api);

    session.enqueueTokenLogin("Marcelo", "token");
    session.processPending();
    api.completeLogin(RC_OK);

    const auto snapshot = session.snapshot();
    T_REQUIRE(snapshot.connectionState == gb::frontend::RaConnectionState::Online);
    T_EQ(snapshot.profile.library.size(), 1U);
    T_EQ(snapshot.profile.library.front().gameId, 17U);
    T_EQ(snapshot.profile.library.front().total, 12U);
    T_REQUIRE(snapshot.profile.library.front().title.empty());
    T_REQUIRE(!snapshot.errorText.empty());
    session.shutdown();
    transport.shutdown();
}

TEST_CASE("retroachievements", "session_bounds_achievement_events_and_publishes_newest_snapshot") {
    gb::GameBoy gameBoy;
    gb::frontend::RaHttpTransport transport([](const auto& request) {
        return gb::frontend::RaHttpResponse{request.id, request.channel, 200, {}, {}};
    });
    FakeRaClientApi api;
    api.setGame(42, "Orbital Boy", "0123456789abcdef0123456789abcdef", "game.png");
    api.gameSummary.num_core_achievements = 34;
    for (std::uint32_t id = 0; id < 34; ++id) {
        api.achievements.push_back(
            api.makeAchievement(id, ("Achievement " + std::to_string(id)).c_str(), id, true)
        );
    }
    auto session = makeSession(gameBoy, transport, api);
    session.enqueueLoadGame(6, "/roms/orbital.gbc");
    session.processPending();
    api.completeGame(RC_OK);
    const auto gameLoadedEvents = session.takeEvents();
    T_EQ(gameLoadedEvents.size(), 1U);

    api.emitIgnoredEvent();
    api.user.score = 4321;
    api.user.score_softcore = 123;
    for (std::size_t index = 0; index < api.achievements.size(); ++index) {
        api.emitAchievement(index);
    }

    const auto events = session.takeEvents();
    T_EQ(events.size(), 32U);
    T_EQ(events.front().title, std::string("Achievement 2"));
    T_EQ(events.back().title, std::string("Achievement 33"));
    T_EQ(session.snapshot().currentAchievements.size(), 34U);
    T_EQ(session.snapshot().profile.user.scoreHardcore, 4321U);
    T_EQ(session.snapshot().profile.user.scoreCasual, 123U);
    session.shutdown();
    transport.shutdown();
}
