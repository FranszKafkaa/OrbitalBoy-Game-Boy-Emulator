#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gb/core/types.hpp"

namespace gb::frontend {

struct MemoryWatch {
    gb::u16 address = 0xC000;
    std::array<gb::u8, 96> history{};
    std::size_t count = 0;
    std::size_t head = 0;
    bool freeze = false;
    gb::u8 freezeValue = 0;
};

struct MemoryEditState {
    bool active = false;
    bool editAddress = true;
    std::string addressHex;
    std::string valueHex;
};

struct MemoryWriteUiState {
    bool pending = false;
    gb::u16 pendingAddress = 0;
    gb::u8 pendingValue = 0;
    bool hasLast = false;
    bool lastOk = false;
    gb::u16 lastAddress = 0;
    gb::u8 lastValue = 0;
    std::uint64_t lastFrame = 0;
    std::string lastTag;
};

enum class MemorySearchMode {
    Exact = 0,
    Greater = 1,
    Less = 2,
    Changed = 3,
    Unchanged = 4,
};

struct MemorySearchUiState {
    bool visible = false;
    bool editingValue = false;
    MemorySearchMode mode = MemorySearchMode::Exact;
    std::string valueHex = "00";
    bool hasSnapshot = false;
    std::size_t totalMatches = 0;
    int scroll = 0;
    std::vector<gb::u16> matches{};
};

} // namespace gb::frontend
