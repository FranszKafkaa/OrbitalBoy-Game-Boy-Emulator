#include "gb/app/frontend/realtime/cheat_engine.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace gb::frontend {

namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string toUpper(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool parseHexU16(const std::string& text, gb::u16& out) {
    if (text.empty() || text.size() > 4) {
        return false;
    }
    unsigned value = 0;
    for (char ch : text) {
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= static_cast<unsigned>(ch - '0');
        } else if (ch >= 'A' && ch <= 'F') {
            value |= static_cast<unsigned>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    out = static_cast<gb::u16>(value & 0xFFFF);
    return true;
}

bool parseHexU8(const std::string& text, gb::u8& out) {
    if (text.empty() || text.size() > 2) {
        return false;
    }
    unsigned value = 0;
    for (char ch : text) {
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= static_cast<unsigned>(ch - '0');
        } else if (ch >= 'A' && ch <= 'F') {
            value |= static_cast<unsigned>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    out = static_cast<gb::u8>(value & 0xFF);
    return true;
}

std::optional<CheatCode> parseGameShark(const std::string& code, std::string* error) {
    // Formato classico simplificado: 01VVAAAA
    if (code.size() != 8 || code.rfind("01", 0) != 0) {
        return std::nullopt;
    }
    gb::u8 value = 0;
    gb::u16 address = 0;
    if (!parseHexU8(code.substr(2, 2), value) || !parseHexU16(code.substr(4, 4), address)) {
        if (error) {
            *error = "GameShark invalido";
        }
        return std::nullopt;
    }
    return CheatCode{CheatKind::GameShark, address, value, std::nullopt, code};
}

std::optional<CheatCode> parseWriteCode(const std::string& code, CheatKind kind, std::string* error) {
    // Formatos aceitos: C000=7F | C000:7F
    const auto eqPos = code.find('=');
    const auto clPos = code.find(':');
    const auto pos = (eqPos != std::string::npos) ? eqPos : clPos;
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    gb::u16 address = 0;
    gb::u8 value = 0;
    if (!parseHexU16(trim(code.substr(0, pos)), address)) {
        if (error) {
            *error = "endereco invalido";
        }
        return std::nullopt;
    }
    if (!parseHexU8(trim(code.substr(pos + 1)), value)) {
        if (error) {
            *error = "valor invalido";
        }
        return std::nullopt;
    }
    return CheatCode{kind, address, value, std::nullopt, code};
}

} // namespace

std::optional<CheatCode> parseCheatLine(const std::string& rawLine, std::string* error) {
    std::string line = trim(rawLine);
    if (line.empty() || line[0] == '#') {
        return std::nullopt;
    }

    const auto hashPos = line.find('#');
    if (hashPos != std::string::npos) {
        line = trim(line.substr(0, hashPos));
    }
    if (line.empty()) {
        return std::nullopt;
    }

    line = toUpper(line);

    if (auto gs = parseGameShark(line, error); gs.has_value()) {
        return gs;
    }

    if (line.rfind("GS ", 0) == 0) {
        return parseGameShark(trim(line.substr(3)), error);
    }

    if (line.rfind("WRITE ", 0) == 0) {
        return parseWriteCode(trim(line.substr(6)), CheatKind::Write, error);
    }

    if (line.rfind("GG ", 0) == 0 || line.rfind("GG:", 0) == 0) {
        const std::string payload = line.rfind("GG ", 0) == 0 ? trim(line.substr(3)) : trim(line.substr(3));
        return parseWriteCode(payload, CheatKind::GameGenie, error);
    }

    if (auto plain = parseWriteCode(line, CheatKind::Write, error); plain.has_value()) {
        return plain;
    }

    if (error) {
        *error = "formato nao suportado";
    }
    return std::nullopt;
}

CheatFileResult loadCheatsFromFile(const std::string& path) {
    CheatFileResult result{};
    std::ifstream in(path);
    if (!in) {
        return result;
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string parseError;
        const auto parsed = parseCheatLine(line, &parseError);
        if (!parsed.has_value()) {
            const std::string trimmed = trim(line);
            if (!trimmed.empty() && trimmed[0] != '#') {
                if (!parseError.empty()) {
                    std::ostringstream oss;
                    oss << "linha " << lineNo << ": " << parseError;
                    result.errors.push_back(oss.str());
                }
            }
            continue;
        }
        result.cheats.push_back(parsed.value());
    }

    return result;
}

std::size_t applyCheats(const std::vector<CheatCode>& cheats, gb::Bus& bus) {
    std::size_t applied = 0;
    for (const auto& cheat : cheats) {
        if (cheat.address <= 0x7FFF) {
            continue;
        }
        if (cheat.compare.has_value() && bus.peek(cheat.address) != cheat.compare.value()) {
            continue;
        }
        bus.write(cheat.address, cheat.value);
        ++applied;
    }
    return applied;
}

} // namespace gb::frontend
