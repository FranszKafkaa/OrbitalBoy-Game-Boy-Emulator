#include "gb/app/frontend/realtime/debug_session.hpp"

#include <algorithm>

namespace gb::frontend {

namespace {

bool isWritableAddress(gb::u16 address) {
    return (address >= 0xA000 && address <= 0xBFFF)
        || (address >= 0xC000 && address <= 0xFDFF)
        || (address >= 0xFE00 && address <= 0xFE9F)
        || (address >= 0xFF00 && address <= 0xFFFF);
}

} // namespace

DebugSession::DebugSession(std::size_t breakpointLimit)
    : breakpointLimit_(breakpointLimit) {
    breakpoints_.reserve(breakpointLimit_);
}

BreakpointToggleResult DebugSession::toggleBreakpoint(gb::u16 address) {
    const auto it = std::find(breakpoints_.begin(), breakpoints_.end(), address);
    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        return BreakpointToggleResult::Removed;
    }
    if (breakpoints_.size() >= breakpointLimit_) {
        return BreakpointToggleResult::LimitReached;
    }
    breakpoints_.push_back(address);
    std::sort(breakpoints_.begin(), breakpoints_.end());
    return BreakpointToggleResult::Added;
}

bool DebugSession::hasBreakpoint(gb::u16 address) const {
    return std::binary_search(breakpoints_.begin(), breakpoints_.end(), address);
}

bool DebugSession::removeBreakpointAt(std::size_t index) {
    if (index >= breakpoints_.size()) {
        return false;
    }
    breakpoints_.erase(breakpoints_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

const std::vector<gb::u16>& DebugSession::breakpoints() const {
    return breakpoints_;
}

bool DebugSession::queueMemoryWrite(gb::u16 address, gb::u8 value, const char* source) {
    if (!isWritableAddress(address)) {
        return false;
    }
    queuedWrite_.active = true;
    queuedWrite_.address = address;
    queuedWrite_.value = value;
    queuedWrite_.source = source;
    return true;
}

std::optional<QueuedMemoryWrite> DebugSession::takeQueuedWrite() {
    if (!queuedWrite_.active) {
        return std::nullopt;
    }
    QueuedMemoryWrite write = queuedWrite_;
    queuedWrite_.active = false;
    return write;
}

const QueuedMemoryWrite& DebugSession::pendingWrite() const {
    return queuedWrite_;
}

MemoryWatch& DebugSession::memoryWatch() {
    return memoryWatch_;
}

const MemoryWatch& DebugSession::memoryWatch() const {
    return memoryWatch_;
}

MemorySearchState& DebugSession::memorySearch() {
    return memorySearch_;
}

const MemorySearchState& DebugSession::memorySearch() const {
    return memorySearch_;
}

} // namespace gb::frontend
