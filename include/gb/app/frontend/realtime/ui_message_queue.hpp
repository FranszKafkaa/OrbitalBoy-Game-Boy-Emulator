#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace gb::frontend {

struct UiMessage {
    std::string text;
    int frames = 0;
};

class UiMessageQueue {
public:
    void post(std::string text, int frames);
    std::optional<UiMessage> takeLatest();

private:
    std::mutex mutex_;
    std::optional<UiMessage> pending_;
};

} // namespace gb::frontend
