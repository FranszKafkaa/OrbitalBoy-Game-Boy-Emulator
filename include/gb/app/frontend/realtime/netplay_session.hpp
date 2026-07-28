#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>

#include "gb/core/gameboy.hpp"

namespace gb::frontend {

struct NetplayFrameRecord {
    std::uint64_t frame = 0;
    gb::GameBoy::SaveState preState{};
    std::uint8_t localInput = 0;
    std::uint8_t remoteInput = 0;
    bool predicted = false;
};

class NetplaySession {
public:
    explicit NetplaySession(int delayFrames = 0, std::size_t historyLimit = 180);

    std::uint8_t delayLocalInput(std::uint8_t input, int desiredDelay);
    void recordFrame(NetplayFrameRecord record);
    std::optional<std::uint64_t> acceptAuthoritativeInput(
        std::uint64_t frame,
        std::uint8_t input
    );
    std::optional<std::uint8_t> takeAuthoritativeInput(std::uint64_t frame);
    bool resimulateFrom(
        std::uint64_t frame,
        gb::GameBoy& gameBoy,
        const std::function<void(std::uint8_t)>& applyInput,
        const std::function<void()>& runFrame,
        const std::function<void(std::uint64_t)>& afterFrame
    );

    void recordChecksum(std::uint64_t frame, std::uint32_t checksum);
    [[nodiscard]] std::optional<std::uint32_t> checksum(std::uint64_t frame) const;

    void notePrediction();
    void noteRollback();
    void noteDesync();

    [[nodiscard]] std::uint64_t predictedCount() const;
    [[nodiscard]] std::uint64_t rollbackCount() const;
    [[nodiscard]] std::uint64_t desyncCount() const;

    std::deque<NetplayFrameRecord>& history();
    [[nodiscard]] const std::deque<NetplayFrameRecord>& history() const;
    [[nodiscard]] const std::unordered_map<std::uint64_t, std::uint8_t>& authoritativeInputs() const;
    [[nodiscard]] const std::unordered_map<std::uint64_t, std::uint32_t>& checksums() const;

private:
    void trimHistory();
    void trimChecksums();

    std::size_t historyLimit_;
    std::deque<std::uint8_t> localInputDelayQueue_;
    std::deque<NetplayFrameRecord> history_;
    std::unordered_map<std::uint64_t, std::uint8_t> authoritativeInputs_;
    std::unordered_map<std::uint64_t, std::uint32_t> checksums_;
    std::uint64_t rollbackCount_ = 0;
    std::uint64_t desyncCount_ = 0;
    std::uint64_t predictedCount_ = 0;
};

} // namespace gb::frontend
