#include "gb/app/frontend/realtime/netplay_session.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace gb::frontend {

NetplaySession::NetplaySession(int delayFrames, std::size_t historyLimit)
    : historyLimit_(std::max<std::size_t>(1, historyLimit)) {
    const int delay = std::clamp(delayFrames, 0, 10);
    for (int i = 0; i < delay; ++i) {
        localInputDelayQueue_.push_back(0);
    }
}

std::uint8_t NetplaySession::delayLocalInput(std::uint8_t input, int desiredDelay) {
    const int delay = std::clamp(desiredDelay, 0, 10);
    while (static_cast<int>(localInputDelayQueue_.size()) < delay) {
        localInputDelayQueue_.push_back(0);
    }
    while (static_cast<int>(localInputDelayQueue_.size()) > delay) {
        localInputDelayQueue_.pop_front();
    }
    if (delay == 0) {
        return input;
    }
    localInputDelayQueue_.push_back(input);
    const std::uint8_t applied = localInputDelayQueue_.front();
    localInputDelayQueue_.pop_front();
    return applied;
}

void NetplaySession::recordFrame(NetplayFrameRecord record) {
    history_.push_back(std::move(record));
    trimHistory();
}

std::optional<std::uint64_t> NetplaySession::acceptAuthoritativeInput(
    std::uint64_t frame,
    std::uint8_t input
) {
    authoritativeInputs_[frame] = input;
    for (const auto& record : history_) {
        if (record.frame == frame) {
            if (record.predicted && record.remoteInput != input) {
                return frame;
            }
            break;
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> NetplaySession::takeAuthoritativeInput(std::uint64_t frame) {
    const auto it = authoritativeInputs_.find(frame);
    if (it == authoritativeInputs_.end()) {
        return std::nullopt;
    }
    const std::uint8_t input = it->second;
    authoritativeInputs_.erase(it);
    return input;
}

bool NetplaySession::resimulateFrom(
    std::uint64_t frame,
    gb::GameBoy& gameBoy,
    const std::function<void(std::uint8_t)>& applyInput,
    const std::function<void()>& runFrame,
    const std::function<void(std::uint64_t)>& afterFrame
) {
    auto start = history_.end();
    for (auto it = history_.begin(); it != history_.end(); ++it) {
        if (it->frame == frame) {
            start = it;
            break;
        }
    }
    if (start == history_.end()) {
        return false;
    }

    gameBoy.loadState(start->preState);
    for (auto it = start; it != history_.end(); ++it) {
        it->preState = gameBoy.saveState();
        const auto authoritative = authoritativeInputs_.find(it->frame);
        if (authoritative != authoritativeInputs_.end()) {
            it->remoteInput = authoritative->second;
            it->predicted = false;
        }
        applyInput(static_cast<std::uint8_t>(it->localInput | it->remoteInput));
        runFrame();
        afterFrame(it->frame);
    }
    return true;
}

void NetplaySession::recordChecksum(std::uint64_t frame, std::uint32_t checksumValue) {
    checksums_[frame] = checksumValue;
    trimChecksums();
}

std::optional<std::uint32_t> NetplaySession::checksum(std::uint64_t frame) const {
    const auto it = checksums_.find(frame);
    if (it == checksums_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void NetplaySession::notePrediction() {
    ++predictedCount_;
}

void NetplaySession::noteRollback() {
    ++rollbackCount_;
}

void NetplaySession::noteDesync() {
    ++desyncCount_;
}

std::uint64_t NetplaySession::predictedCount() const {
    return predictedCount_;
}

std::uint64_t NetplaySession::rollbackCount() const {
    return rollbackCount_;
}

std::uint64_t NetplaySession::desyncCount() const {
    return desyncCount_;
}

std::deque<NetplayFrameRecord>& NetplaySession::history() {
    return history_;
}

const std::deque<NetplayFrameRecord>& NetplaySession::history() const {
    return history_;
}

const std::unordered_map<std::uint64_t, std::uint8_t>& NetplaySession::authoritativeInputs() const {
    return authoritativeInputs_;
}

const std::unordered_map<std::uint64_t, std::uint32_t>& NetplaySession::checksums() const {
    return checksums_;
}

void NetplaySession::trimHistory() {
    while (history_.size() > historyLimit_) {
        const std::uint64_t oldFrame = history_.front().frame;
        history_.pop_front();
        authoritativeInputs_.erase(oldFrame);
    }
}

void NetplaySession::trimChecksums() {
    while (checksums_.size() > historyLimit_) {
        auto oldest = checksums_.end();
        for (auto it = checksums_.begin(); it != checksums_.end(); ++it) {
            if (oldest == checksums_.end() || it->first < oldest->first) {
                oldest = it;
            }
        }
        if (oldest == checksums_.end()) {
            break;
        }
        checksums_.erase(oldest);
    }
}

} // namespace gb::frontend
