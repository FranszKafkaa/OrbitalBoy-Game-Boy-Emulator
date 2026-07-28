#include "gb/app/frontend/realtime/emulation_worker.hpp"

#include <utility>

namespace gb::frontend {

EmulationWorker::EmulationWorker(std::function<void()> stopAction)
    : stopAction_(std::move(stopAction)) {}

EmulationWorker::~EmulationWorker() {
    stop();
}

void EmulationWorker::stop() {
    if (!stopRequested_) {
        stopRequested_ = true;
        if (stopAction_) {
            stopAction_();
        }
    }
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

bool EmulationWorker::running() const {
    return !threads_.empty();
}

} // namespace gb::frontend
