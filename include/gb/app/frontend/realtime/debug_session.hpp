#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "gb/app/frontend/realtime/session_models.hpp"

namespace gb::frontend {

enum class BreakpointToggleResult {
    Added,
    Removed,
    LimitReached,
};

class DebugSession {
public:
    explicit DebugSession(std::size_t breakpointLimit = 16);

    BreakpointToggleResult toggleBreakpoint(gb::u16 address);
    [[nodiscard]] bool hasBreakpoint(gb::u16 address) const;
    bool removeBreakpointAt(std::size_t index);
    [[nodiscard]] const std::vector<gb::u16>& breakpoints() const;

    bool queueMemoryWrite(gb::u16 address, gb::u8 value, const char* source);
    std::optional<QueuedMemoryWrite> takeQueuedWrite();
    [[nodiscard]] const QueuedMemoryWrite& pendingWrite() const;

    MemoryWatch& memoryWatch();
    [[nodiscard]] const MemoryWatch& memoryWatch() const;
    MemorySearchState& memorySearch();
    [[nodiscard]] const MemorySearchState& memorySearch() const;

private:
    std::size_t breakpointLimit_;
    std::vector<gb::u16> breakpoints_;
    QueuedMemoryWrite queuedWrite_;
    MemoryWatch memoryWatch_;
    MemorySearchState memorySearch_;
};

} // namespace gb::frontend
