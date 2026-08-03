#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "gb/app/frontend/realtime/retroachievements_models.hpp"
#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/app/frontend/realtime/retroachievements_progress.hpp"
#include "gb/app/frontend/realtime/secure_string.hpp"

namespace gb::frontend {

enum class RaRuntimeCommandType {
    Login,
    Logout,
    SaveState,
    LoadState,
    TimelineBack,
    TimelineForward,
};

using RaSecretWipeObserver =
    std::function<void(const char* bytes, std::size_t size)>;

struct RaRuntimeCommand {
    RaRuntimeCommand(
        RaRuntimeCommandType typeValue = RaRuntimeCommandType::Logout,
        std::string usernameValue = {},
        std::string_view passwordValue = {},
        int saveSlotValue = 0
    );
    ~RaRuntimeCommand();
    RaRuntimeCommand(const RaRuntimeCommand&) = delete;
    RaRuntimeCommand& operator=(const RaRuntimeCommand&) = delete;
    RaRuntimeCommand(RaRuntimeCommand&& other);
    RaRuntimeCommand& operator=(RaRuntimeCommand&& other);

    void wipeSecret(const RaSecretWipeObserver& observer = {});

    RaRuntimeCommandType type = RaRuntimeCommandType::Logout;
    std::string username{};
    RaSecretString password{};
    int saveSlot = 0;
    std::size_t repeatCount = 1;
};

class RaRuntimeCommandQueue {
public:
    explicit RaRuntimeCommandQueue(
        std::size_t capacity = 32,
        RaSecretWipeObserver wipeObserver = {}
    );

    bool enqueue(RaRuntimeCommand command);
    [[nodiscard]] std::vector<RaRuntimeCommand> takeAll();
    void stopAccepting();
    [[nodiscard]] std::size_t size() const;

private:
    std::size_t capacity_ = 0;
    RaSecretWipeObserver wipeObserver_{};
    mutable std::mutex mutex_{};
    std::vector<RaRuntimeCommand> commands_{};
    bool accepting_ = true;
};

using RaRuntimeCommandExecutor = std::function<void(RaRuntimeCommand&)>;
void processRaRuntimeCommandBatch(
    std::vector<RaRuntimeCommand> commands,
    const RaRuntimeCommandExecutor& execute,
    const std::function<void()>& processPending
);

enum class RaDeferredRestoreResult {
    NotPending,
    Waiting,
    Restored,
    Reset,
    TimedOutReset,
};

class RaDeferredProgressRestore {
public:
    using Deserialize = std::function<bool(
        std::string_view,
        const std::vector<std::uint8_t>&
    )>;
    using Reset = std::function<bool()>;

    void stage(std::optional<RaStoredProgress> progress);
    [[nodiscard]] bool pending() const;
    RaDeferredRestoreResult applyIfReady(
        const RaSessionSnapshot& snapshot,
        const Deserialize& deserialize,
        const Reset& reset
    );
    RaDeferredRestoreResult prepareCommittedFrame(
        const RaSessionSnapshot& snapshot,
        std::chrono::milliseconds now,
        const Deserialize& deserialize,
        const Reset& reset,
        std::chrono::milliseconds timeout =
            kRaHttpRequestTimeout + std::chrono::seconds(1)
    );

private:
    std::optional<RaStoredProgress> progress_{};
    bool resetPending_ = false;
    std::optional<std::chrono::milliseconds> waitingSince_{};
};

struct RaLifecycleActions {
    std::function<void()> loadConfig;
    std::function<void()> tokenLogin;
    std::function<void()> loadGame;
    std::function<void()> processPending;
    std::function<void()> applyPendingProgress;
    std::function<void()> doFrame;
    std::function<void()> captureTimeline;
    std::function<void()> emulateRollbackFrame;
    std::function<void()> idle;
    std::function<void()> serializeProgress;
    std::function<void()> saveProgressSidecar;
    std::function<void()> loadGameState;
    std::function<void()> deserializeProgress;
    std::function<void()> shutdownSession;
    std::function<void()> shutdownImageCache;
    std::function<void()> joinHttpWorker;
};

class RaRealtimeLifecycleCoordinator {
public:
    void start(RaLifecycleActions& actions, bool shouldTokenLogin);
    void observeSnapshot(
        const RaSessionSnapshot& snapshot,
        RaLifecycleActions& actions
    );
    void committedFrames(std::size_t count, RaLifecycleActions& actions);
    void rollbackFrames(std::size_t count, RaLifecycleActions& actions);
    void pausedPoll(
        std::chrono::milliseconds now,
        RaLifecycleActions& actions
    );
    void saveState(RaLifecycleActions& actions);
    void loadState(RaLifecycleActions& actions);
    void shutdownOwner(RaLifecycleActions& actions);
    void shutdownUi(RaLifecycleActions& actions);

private:
    bool gameLoadRequested_ = false;
    std::optional<std::uint64_t> connectionGeneration_{};
    bool ownerStopped_ = false;
    bool uiStopped_ = false;
    std::optional<std::chrono::milliseconds> lastIdle_{};
};

} // namespace gb::frontend
