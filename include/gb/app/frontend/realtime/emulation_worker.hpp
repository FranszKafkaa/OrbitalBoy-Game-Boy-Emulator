#pragma once

#include <functional>
#include <thread>
#include <utility>
#include <vector>

namespace gb::frontend {

class EmulationWorker {
public:
    explicit EmulationWorker(std::function<void()> stopAction = {});
    ~EmulationWorker();

    EmulationWorker(const EmulationWorker&) = delete;
    EmulationWorker& operator=(const EmulationWorker&) = delete;

    template <typename Function>
    void start(Function&& function) {
        threads_.emplace_back(std::forward<Function>(function));
    }

    void stop();
    [[nodiscard]] bool running() const;

private:
    std::function<void()> stopAction_;
    std::vector<std::thread> threads_;
    bool stopRequested_ = false;
};

} // namespace gb::frontend
