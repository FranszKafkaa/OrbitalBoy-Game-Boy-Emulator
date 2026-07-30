#include "gb/app/frontend/realtime/retroachievements_lifecycle.hpp"

namespace gb::frontend {
namespace {

void callIfSet(const std::function<void()>& action) {
    if (action) {
        action();
    }
}

} // namespace

void RaRealtimeLifecycleCoordinator::start(
    RaLifecycleActions& actions,
    bool shouldTokenLogin
) {
    callIfSet(actions.loadConfig);
    if (shouldTokenLogin) {
        callIfSet(actions.tokenLogin);
    }
}

void RaRealtimeLifecycleCoordinator::observeSnapshot(
    const RaSessionSnapshot& snapshot,
    RaLifecycleActions& actions
) {
    if (snapshot.connectionState == RaConnectionState::LoggedOut
        || snapshot.connectionState == RaConnectionState::LoggingIn) {
        gameLoadRequested_ = false;
    }
    if (snapshot.connectionState == RaConnectionState::Online
        && !snapshot.gameLoaded && !gameLoadRequested_) {
        gameLoadRequested_ = true;
        callIfSet(actions.loadGame);
    }
}

void RaRealtimeLifecycleCoordinator::committedFrames(
    std::size_t count,
    RaLifecycleActions& actions
) {
    for (std::size_t index = 0; index < count; ++index) {
        callIfSet(actions.processPending);
        callIfSet(actions.doFrame);
        callIfSet(actions.captureTimeline);
    }
}

void RaRealtimeLifecycleCoordinator::rollbackFrames(
    std::size_t count,
    RaLifecycleActions& actions
) {
    for (std::size_t index = 0; index < count; ++index) {
        callIfSet(actions.emulateRollbackFrame);
    }
}

void RaRealtimeLifecycleCoordinator::pausedPoll(
    std::chrono::milliseconds now,
    RaLifecycleActions& actions
) {
    callIfSet(actions.processPending);
    if (!lastIdle_.has_value()
        || now < *lastIdle_
        || now - *lastIdle_ >= std::chrono::milliseconds(100)) {
        callIfSet(actions.idle);
        lastIdle_ = now;
    }
}

void RaRealtimeLifecycleCoordinator::saveState(RaLifecycleActions& actions) {
    callIfSet(actions.serializeProgress);
    callIfSet(actions.saveProgressSidecar);
}

void RaRealtimeLifecycleCoordinator::loadState(RaLifecycleActions& actions) {
    callIfSet(actions.loadGameState);
    callIfSet(actions.deserializeProgress);
}

void RaRealtimeLifecycleCoordinator::shutdownOwner(RaLifecycleActions& actions) {
    if (!ownerStopped_) {
        callIfSet(actions.shutdownSession);
        ownerStopped_ = true;
    }
}

void RaRealtimeLifecycleCoordinator::shutdownUi(RaLifecycleActions& actions) {
    if (!uiStopped_) {
        callIfSet(actions.shutdownImageCache);
        callIfSet(actions.joinHttpWorker);
        uiStopped_ = true;
    }
}

} // namespace gb::frontend
