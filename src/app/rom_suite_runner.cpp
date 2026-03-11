#include "gb/app/rom_suite_runner.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "gb/core/gameboy.hpp"

namespace gb {
namespace {

struct Expectations {
    std::optional<std::string> title;
    std::optional<u16> pc;
    std::optional<u16> sp;
    std::optional<u8> a;
    std::optional<u8> b;
    std::optional<u8> c;
    std::optional<u8> d;
    std::optional<u8> e;
    std::optional<u8> h;
    std::optional<u8> l;
    std::optional<u8> f;
    std::optional<u8> ie;
    std::optional<u8> iflag;
    std::optional<bool> halted;
};

struct SuiteCase {
    std::string name;
    std::filesystem::path romPath;
    int frames = 180;
    Expectations expect{};
};

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

std::vector<std::string> split(const std::string& line, char delimiter) {
    std::vector<std::string> out;
    std::string current;
    std::istringstream iss(line);
    while (std::getline(iss, current, delimiter)) {
        out.push_back(trim(current));
    }
    return out;
}

bool parseNumber(const std::string& text, unsigned long long& out) {
    try {
        std::size_t idx = 0;
        out = std::stoull(text, &idx, 0);
        return idx == text.size();
    } catch (...) {
        return false;
    }
}

bool parseBool(const std::string& text, bool& out) {
    if (text == "1" || text == "true" || text == "TRUE" || text == "on") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "FALSE" || text == "off") {
        out = false;
        return true;
    }
    return false;
}

bool applyExpectationToken(Expectations& expect, const std::string& key, const std::string& value, std::string& error) {
    unsigned long long number = 0;

    const auto parseU16 = [&](std::optional<u16>& target) {
        if (!parseNumber(value, number) || number > 0xFFFF) {
            return false;
        }
        target = static_cast<u16>(number);
        return true;
    };

    const auto parseU8 = [&](std::optional<u8>& target) {
        if (!parseNumber(value, number) || number > 0xFF) {
            return false;
        }
        target = static_cast<u8>(number);
        return true;
    };

    if (key == "title") {
        expect.title = value;
        return true;
    }
    if (key == "pc") return parseU16(expect.pc);
    if (key == "sp") return parseU16(expect.sp);
    if (key == "a") return parseU8(expect.a);
    if (key == "b") return parseU8(expect.b);
    if (key == "c") return parseU8(expect.c);
    if (key == "d") return parseU8(expect.d);
    if (key == "e") return parseU8(expect.e);
    if (key == "h") return parseU8(expect.h);
    if (key == "l") return parseU8(expect.l);
    if (key == "f") return parseU8(expect.f);
    if (key == "ie") return parseU8(expect.ie);
    if (key == "if") return parseU8(expect.iflag);
    if (key == "halted") {
        bool parsed = false;
        if (!parseBool(value, parsed)) {
            return false;
        }
        expect.halted = parsed;
        return true;
    }

    error = "chave de expectativa desconhecida: " + key;
    return false;
}

bool parseCaseLine(
    const std::filesystem::path& manifestDir,
    const std::string& line,
    int lineNumber,
    SuiteCase& outCase,
    std::string& error
) {
    const auto parts = split(line, '|');
    if (parts.size() < 3) {
        error = "linha " + std::to_string(lineNumber) + ": esperado formato name|rom|frames|...";
        return false;
    }

    outCase = SuiteCase{};
    outCase.name = parts[0];
    if (outCase.name.empty()) {
        error = "linha " + std::to_string(lineNumber) + ": nome do caso vazio";
        return false;
    }

    outCase.romPath = manifestDir / parts[1];

    unsigned long long frames = 0;
    if (!parseNumber(parts[2], frames) || frames < 1 || frames > 30000) {
        error = "linha " + std::to_string(lineNumber) + ": valor de frames invalido";
        return false;
    }
    outCase.frames = static_cast<int>(frames);

    for (std::size_t i = 3; i < parts.size(); ++i) {
        if (parts[i].empty()) {
            continue;
        }
        const auto pos = parts[i].find('=');
        if (pos == std::string::npos) {
            error = "linha " + std::to_string(lineNumber) + ": expectativa invalida (sem '='): " + parts[i];
            return false;
        }

        const std::string key = trim(parts[i].substr(0, pos));
        const std::string value = trim(parts[i].substr(pos + 1));
        if (key.empty()) {
            error = "linha " + std::to_string(lineNumber) + ": chave vazia em expectativa";
            return false;
        }
        if (!applyExpectationToken(outCase.expect, key, value, error)) {
            if (error.empty()) {
                error = "linha " + std::to_string(lineNumber) + ": valor invalido para " + key;
            } else {
                error = "linha " + std::to_string(lineNumber) + ": " + error;
            }
            return false;
        }
    }

    return true;
}

std::optional<std::string> evaluateExpectations(const SuiteCase& testCase, const GameBoy& gb) {
    const auto& regs = gb.cpu().regs();

    const auto checkU8 = [](const char* field, std::optional<u8> expected, u8 actual) -> std::optional<std::string> {
        if (!expected.has_value()) {
            return std::nullopt;
        }
        if (actual == *expected) {
            return std::nullopt;
        }
        std::ostringstream oss;
        oss << field << " esperado=0x" << std::hex << static_cast<int>(*expected)
            << " atual=0x" << static_cast<int>(actual);
        return oss.str();
    };

    const auto checkU16 = [](const char* field, std::optional<u16> expected, u16 actual) -> std::optional<std::string> {
        if (!expected.has_value()) {
            return std::nullopt;
        }
        if (actual == *expected) {
            return std::nullopt;
        }
        std::ostringstream oss;
        oss << field << " esperado=0x" << std::hex << *expected
            << " atual=0x" << actual;
        return oss.str();
    };

    if (testCase.expect.title.has_value() && gb.cartridge().title() != *testCase.expect.title) {
        return "title esperado='" + *testCase.expect.title + "' atual='" + gb.cartridge().title() + "'";
    }

    if (auto err = checkU16("PC", testCase.expect.pc, regs.pc)) return err;
    if (auto err = checkU16("SP", testCase.expect.sp, regs.sp)) return err;
    if (auto err = checkU8("A", testCase.expect.a, regs.a)) return err;
    if (auto err = checkU8("B", testCase.expect.b, regs.b)) return err;
    if (auto err = checkU8("C", testCase.expect.c, regs.c)) return err;
    if (auto err = checkU8("D", testCase.expect.d, regs.d)) return err;
    if (auto err = checkU8("E", testCase.expect.e, regs.e)) return err;
    if (auto err = checkU8("H", testCase.expect.h, regs.h)) return err;
    if (auto err = checkU8("L", testCase.expect.l, regs.l)) return err;
    if (auto err = checkU8("F", testCase.expect.f, regs.f)) return err;
    if (auto err = checkU8("IE", testCase.expect.ie, gb.bus().interruptEnable())) return err;
    if (auto err = checkU8("IF", testCase.expect.iflag, gb.bus().interruptFlags())) return err;

    if (testCase.expect.halted.has_value() && gb.cpu().isHalted() != *testCase.expect.halted) {
        return std::string("halted esperado=") + (*testCase.expect.halted ? "true" : "false")
            + " atual=" + (gb.cpu().isHalted() ? "true" : "false");
    }

    return std::nullopt;
}

} // namespace

int runRomCompatibilitySuite(const std::string& manifestPath) {
    const std::filesystem::path manifest = manifestPath;
    std::ifstream in(manifest);
    if (!in) {
        std::cerr << "falha ao abrir manifesto de suite: " << manifestPath << "\n";
        return 1;
    }

    std::vector<SuiteCase> cases;
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        SuiteCase parsed;
        std::string error;
        if (!parseCaseLine(manifest.parent_path(), trimmed, lineNumber, parsed, error)) {
            std::cerr << "manifesto invalido: " << error << "\n";
            return 1;
        }
        cases.push_back(parsed);
    }

    if (cases.empty()) {
        std::cerr << "manifesto sem casos: " << manifestPath << "\n";
        return 1;
    }

    int passed = 0;
    int failed = 0;

    for (const auto& testCase : cases) {
        GameBoy gb;
        if (!gb.loadRom(testCase.romPath.string())) {
            ++failed;
            std::cerr << "[FAIL] " << testCase.name << " -> falha ao carregar ROM: " << testCase.romPath << "\n";
            continue;
        }

        for (int f = 0; f < testCase.frames; ++f) {
            gb.runFrame();
        }

        const auto result = evaluateExpectations(testCase, gb);
        if (result.has_value()) {
            ++failed;
            std::cerr << "[FAIL] " << testCase.name << " -> " << *result << "\n";
        } else {
            ++passed;
            std::cout << "[PASS] " << testCase.name << "\n";
        }
    }

    std::cout << "[ROM-SUITE] total=" << cases.size() << " pass=" << passed << " fail=" << failed << "\n";
    return failed == 0 ? 0 : 1;
}

} // namespace gb
