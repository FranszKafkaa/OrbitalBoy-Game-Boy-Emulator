#include "gb/app/frontend/realtime/retroachievements_lifecycle.hpp"

#include <algorithm>
#include <utility>

namespace gb::frontend {
namespace {

void callIfSet(const std::function<void()>& action) {
    if (action) {
        action();
    }
}

} // namespace

RaRuntimeCommand::RaRuntimeCommand(
    RaRuntimeCommandType typeValue,
    std::string usernameValue,
    std::string passwordValue,
    int saveSlotValue
)
    : type(typeValue),
      username(std::move(usernameValue)),
      password(std::move(passwordValue)),
      saveSlot(saveSlotValue) {
}

RaRuntimeCommand::~RaRuntimeCommand() {
    wipeSecret();
}

RaRuntimeCommand::RaRuntimeCommand(RaRuntimeCommand&& other) noexcept
    : type(other.type),
      saveSlot(other.saveSlot),
      repeatCount(other.repeatCount) {
    username.swap(other.username);
    password.swap(other.password);
}

RaRuntimeCommand& RaRuntimeCommand::operator=(RaRuntimeCommand&& other) noexcept {
    if (this != &other) {
        wipeSecret();
        type = other.type;
        saveSlot = other.saveSlot;
        repeatCount = other.repeatCount;
        username.clear();
        password.clear();
        username.swap(other.username);
        password.swap(other.password);
    }
    return *this;
}

void RaRuntimeCommand::wipeSecret(const RaSecretWipeObserver& observer) {
    const std::size_t logicalSize = password.size();
    if (logicalSize == 0U) {
        return;
    }
    volatile char* bytes = password.data();
    for (std::size_t index = 0; index < logicalSize; ++index) {
        bytes[index] = '\0';
    }
    if (observer) {
        observer(password.data(), logicalSize);
    }
    password.clear();
}

RaRuntimeCommandQueue::RaRuntimeCommandQueue(
    std::size_t capacity,
    RaSecretWipeObserver wipeObserver
)
    : capacity_(std::max<std::size_t>(1U, capacity)),
      wipeObserver_(std::move(wipeObserver)) {
    commands_.reserve(capacity_);
}

bool RaRuntimeCommandQueue::enqueue(RaRuntimeCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_) {
        command.wipeSecret(wipeObserver_);
        return false;
    }
    if ((command.type == RaRuntimeCommandType::TimelineBack
         || command.type == RaRuntimeCommandType::TimelineForward)
        && !commands_.empty() && commands_.back().type == command.type) {
        commands_.back().repeatCount =
            std::min<std::size_t>(8U, commands_.back().repeatCount + 1U);
        return true;
    }
    if (command.type == RaRuntimeCommandType::SaveState
        || command.type == RaRuntimeCommandType::LoadState) {
        if (!commands_.empty()
            && commands_.back().type == command.type
            && commands_.back().saveSlot == command.saveSlot) {
            return true;
        }
    }
    if (commands_.size() >= capacity_) {
        command.wipeSecret(wipeObserver_);
        return false;
    }
    commands_.push_back(std::move(command));
    return true;
}

std::vector<RaRuntimeCommand> RaRuntimeCommandQueue::takeAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RaRuntimeCommand> result;
    result.swap(commands_);
    commands_.reserve(capacity_);
    return result;
}

void RaRuntimeCommandQueue::stopAccepting() {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
}

std::size_t RaRuntimeCommandQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.size();
}

void RaDeferredProgressRestore::stage(
    std::optional<RaStoredProgress> progress
) {
    progress_ = std::move(progress);
    resetPending_ = !progress_.has_value();
}

bool RaDeferredProgressRestore::pending() const {
    return progress_.has_value() || resetPending_;
}

RaDeferredRestoreResult RaDeferredProgressRestore::applyIfReady(
    const RaSessionSnapshot& snapshot,
    const Deserialize& deserialize,
    const Reset& reset
) {
    if (!pending()) {
        return RaDeferredRestoreResult::NotPending;
    }
    if (!snapshot.gameLoaded) {
        return RaDeferredRestoreResult::Waiting;
    }
    const bool restored = progress_.has_value()
        && progress_->romHash == snapshot.romHash
        && deserialize
        && deserialize(progress_->romHash, progress_->payload);
    progress_.reset();
    resetPending_ = false;
    if (restored) {
        return RaDeferredRestoreResult::Restored;
    }
    if (reset) {
        (void)reset();
    }
    return RaDeferredRestoreResult::Reset;
}

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
        callIfSet(actions.processPending);
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
