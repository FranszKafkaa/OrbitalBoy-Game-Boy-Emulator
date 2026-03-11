#include "gb/app/frontend/realtime/audio_ring_buffer.hpp"

#include <algorithm>
#include <chrono>

namespace gb::frontend {

AudioRingBuffer::AudioRingBuffer(std::size_t capacitySamples)
    : data_(std::max<std::size_t>(capacitySamples, 2048), 0) {}

std::size_t AudioRingBuffer::push(const int16_t* samples, std::size_t count) {
    if (!samples || count == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return 0;
    }
    std::size_t written = 0;
    for (; written < count; ++written) {
        if (size_ >= data_.size()) {
            dropped_ += (count - written);
            break;
        }
        data_[writePos_] = samples[written];
        writePos_ = (writePos_ + 1) % data_.size();
        ++size_;
    }
    if (written > 0) {
        cv_.notify_one();
    }
    return written;
}

std::size_t AudioRingBuffer::pop(int16_t* out, std::size_t maxCount, int timeoutMs) {
    if (!out || maxCount == 0) {
        return 0;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    if (size_ == 0 && !closed_) {
        cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
            return closed_ || size_ > 0;
        });
    }
    const std::size_t count = std::min(maxCount, size_);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = data_[readPos_];
        readPos_ = (readPos_ + 1) % data_.size();
    }
    size_ -= count;
    return count;
}

void AudioRingBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_ = 0;
    readPos_ = writePos_;
}

void AudioRingBuffer::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_.notify_all();
}

std::size_t AudioRingBuffer::droppedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

} // namespace gb::frontend
