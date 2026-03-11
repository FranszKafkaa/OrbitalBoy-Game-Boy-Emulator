#pragma once

#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace gb::frontend {

class AudioRingBuffer {
public:
    explicit AudioRingBuffer(std::size_t capacitySamples);

    std::size_t push(const int16_t* samples, std::size_t count);
    std::size_t pop(int16_t* out, std::size_t maxCount, int timeoutMs);
    void clear();
    void close();

    [[nodiscard]] std::size_t droppedCount() const;

private:
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::vector<int16_t> data_{};
    std::size_t readPos_ = 0;
    std::size_t writePos_ = 0;
    std::size_t size_ = 0;
    std::size_t dropped_ = 0;
    bool closed_ = false;
};

} // namespace gb::frontend
