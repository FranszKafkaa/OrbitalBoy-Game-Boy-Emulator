#include "gb/app/frontend/realtime/ui_message_queue.hpp"

#include <utility>

namespace gb::frontend {

void UiMessageQueue::post(std::string text, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = UiMessage{std::move(text), frames};
}

std::optional<UiMessage> UiMessageQueue::takeLatest() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<UiMessage> message = std::move(pending_);
    pending_.reset();
    return message;
}

} // namespace gb::frontend
