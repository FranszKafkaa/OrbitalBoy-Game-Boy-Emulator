#include "gb/app/frontend/realtime/retroachievements_lifecycle.hpp"

#include "gb/app/frontend/realtime/secure_string.hpp"

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
    std::string_view passwordValue,
    int saveSlotValue
)
    : type(typeValue),
      username(std::move(usernameValue)),
      saveSlot(saveSlotValue) {
    password.assign(passwordValue);
}

RaRuntimeCommand::~RaRuntimeCommand() {
    wipeSecret();
}

RaRuntimeCommand::RaRuntimeCommand(RaRuntimeCommand&& other)
    : type(other.type),
      password(std::move(other.password)),
      saveSlot(other.saveSlot),
      repeatCount(other.repeatCount) {
    username.swap(other.username);
}

RaRuntimeCommand& RaRuntimeCommand::operator=(RaRuntimeCommand&& other) {
    if (this != &other) {
        wipeSecret();
        type = other.type;
        saveSlot = other.saveSlot;
        repeatCount = other.repeatCount;
        username.clear();
        username.swap(other.username);
        password = std::move(other.password);
    }
    return *this;
}

void RaRuntimeCommand::wipeSecret(const RaSecretWipeObserver& observer) {
    password.clear(observer);
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

void processRaRuntimeCommandBatch(
    std::vector<RaRuntimeCommand> commands,
    const RaRuntimeCommandExecutor& execute,
    const std::function<void()>& processPending
) {
    for (auto& command : commands) {
        const std::size_t repeatCount =
            std::max<std::size_t>(1U, command.repeatCount);
        for (std::size_t repeat = 0; repeat < repeatCount; ++repeat) {
            if (execute) {
                execute(command);
            }
            if ((command.type == RaRuntimeCommandType::Login
                 || command.type == RaRuntimeCommandType::Logout)
                && processPending) {
                processPending();
            }
        }
    }
}

void RaDeferredProgressRestore::stage(
    std::optional<RaStoredProgress> progress
) {
    progress_ = std::move(progress);
    resetPending_ = !progress_.has_value();
    waitingSince_.reset();
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
    waitingSince_.reset();
    if (restored) {
        return RaDeferredRestoreResult::Restored;
    }
    if (reset) {
        (void)reset();
    }
    return RaDeferredRestoreResult::Reset;
}

RaDeferredRestoreResult RaDeferredProgressRestore::prepareCommittedFrame(
    const RaSessionSnapshot& snapshot,
    std::chrono::milliseconds now,
    const Deserialize& deserialize,
    const Reset& reset,
    std::chrono::milliseconds timeout
) {
    const auto result = applyIfReady(snapshot, deserialize, reset);
    if (result != RaDeferredRestoreResult::Waiting) {
        return result;
    }
    if (!waitingSince_.has_value() || now < *waitingSince_) {
        waitingSince_ = now;
        return result;
    }
    if (now - *waitingSince_ <= timeout) {
        return result;
    }
    progress_.reset();
    resetPending_ = false;
    waitingSince_.reset();
    if (reset) {
        (void)reset();
    }
    return RaDeferredRestoreResult::TimedOutReset;
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
    if (!connectionGeneration_.has_value()
        || *connectionGeneration_ != snapshot.connectionGeneration) {
        if (connectionGeneration_.has_value()) {
            gameLoadRequested_ = false;
        }
        connectionGeneration_ = snapshot.connectionGeneration;
    }
    if (snapshot.connectionState == RaConnectionState::LoggedOut
        || snapshot.connectionState == RaConnectionState::LoggingIn
        || snapshot.connectionState == RaConnectionState::Offline) {
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
        callIfSet(actions.applyPendingProgress);
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
