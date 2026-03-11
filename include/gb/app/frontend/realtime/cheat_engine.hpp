#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gb/core/bus.hpp"

namespace gb::frontend {

enum class CheatKind {
    GameShark,
    GameGenie,
    Write,
};

struct CheatCode {
    CheatKind kind = CheatKind::Write;
    gb::u16 address = 0;
    gb::u8 value = 0;
    std::optional<gb::u8> compare{};
    std::string source{};
};

struct CheatFileResult {
    std::vector<CheatCode> cheats{};
    std::vector<std::string> errors{};
};

std::optional<CheatCode> parseCheatLine(const std::string& line, std::string* error = nullptr);
CheatFileResult loadCheatsFromFile(const std::string& path);
std::size_t applyCheats(const std::vector<CheatCode>& cheats, gb::Bus& bus);

} // namespace gb::frontend
