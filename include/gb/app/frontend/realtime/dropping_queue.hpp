#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace gb::frontend {

template <typename T, std::size_t MaxDepth>
class DroppingQueue {
public:
    bool push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        if (queue_.size() >= MaxDepth) {
            queue_.pop_front();
            ++dropped_;
        }
        queue_.push_back(std::move(item));
        cv_.notify_one();
        return true;
    }

    bool waitPop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    bool tryPopLatest(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.back());
        queue_.clear();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    [[nodiscard]] std::size_t droppedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::deque<T> queue_{};
    bool closed_ = false;
    std::size_t dropped_ = 0;
};

} // namespace gb::frontend
