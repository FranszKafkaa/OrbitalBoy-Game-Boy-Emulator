#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>

#include "gb/app/frontend/realtime/retroachievements_models.hpp"

namespace gb::frontend {

struct RaLifecycleActions {
    std::function<void()> loadConfig;
    std::function<void()> tokenLogin;
    std::function<void()> loadGame;
    std::function<void()> processPending;
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
    bool ownerStopped_ = false;
    bool uiStopped_ = false;
    std::optional<std::chrono::milliseconds> lastIdle_{};
};

} // namespace gb::frontend
