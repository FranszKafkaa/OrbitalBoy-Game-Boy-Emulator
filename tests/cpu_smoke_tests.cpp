#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "gb/core/gameboy.hpp"

namespace {

std::string createTempRomPath(const std::string& name) {
    return "/tmp/" + name + ".gb";
}

bool createTestRom(const std::string& path, const std::vector<gb::u8>& program, gb::u16 origin = 0x0100) {
    std::vector<gb::u8> rom(0x8000, 0x00);

    const char title[] = "GBTEST";
    for (std::size_t i = 0; i < sizeof(title) - 1; ++i) {
        rom[0x134 + i] = static_cast<gb::u8>(title[i]);
    }
    rom[0x147] = 0x00;

    for (std::size_t i = 0; i < program.size(); ++i) {
        const auto addr = static_cast<std::size_t>(origin + i);
        if (addr >= rom.size()) {
            return false;
        }
        rom[addr] = program[i];
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }

    const auto written = std::fwrite(rom.data(), 1, rom.size(), f);
    std::fclose(f);
    return written == rom.size();
}

void runUntilHalt(gb::GameBoy& gb, int maxSteps = 2000) {
    for (int i = 0; i < maxSteps; ++i) {
        gb.step();
        if (gb.cpu().isHalted()) {
            return;
        }
    }
    assert(false && "CPU did not halt within max steps");
}

void testDaa() {
    const std::string path = createTempRomPath("gbemu_test_daa");
    const std::vector<gb::u8> code = {
        0x3E, 0x15,
        0xC6, 0x27,
        0x27,
        0x76,
    };

    assert(createTestRom(path, code));

    gb::GameBoy gb;
    assert(gb.loadRom(path));
    runUntilHalt(gb);

    const auto& r = gb.cpu().regs();
    assert(r.a == 0x42);
    assert((r.f & 0x80) == 0);
    assert((r.f & 0x40) == 0);
    assert((r.f & 0x20) == 0);
    assert((r.f & 0x10) == 0);

    std::remove(path.c_str());
}

void testCbOps() {
    const std::string path = createTempRomPath("gbemu_test_cb");
    const std::vector<gb::u8> code = {
        0x06, 0x01,
        0xCB, 0x00,
        0xCB, 0x78,
        0xCB, 0xC0,
        0xCB, 0x80,
        0x76,
    };

    assert(createTestRom(path, code));

    gb::GameBoy gb;
    assert(gb.loadRom(path));
    runUntilHalt(gb);

    const auto& r = gb.cpu().regs();
    assert(r.b == 0x02);
    assert((r.f & 0x80) != 0);

    std::remove(path.c_str());
}

void testDmaCopy() {
    const std::string path = createTempRomPath("gbemu_test_dma");
    const std::vector<gb::u8> code = {
        0x3E, 0x12,
        0xEA, 0x00, 0xC0,
        0x3E, 0x34,
        0xEA, 0x01, 0xC0,
        0x3E, 0xC0,
        0xE0, 0x46,
        0xFA, 0x00, 0xFE,
        0x47,
        0xFA, 0x01, 0xFE,
        0x4F,
        0x76,
    };

    assert(createTestRom(path, code));

    gb::GameBoy gb;
    assert(gb.loadRom(path));
    runUntilHalt(gb);

    const auto& r = gb.cpu().regs();
    assert(r.b == 0x12);
    assert(r.c == 0x34);

    std::remove(path.c_str());
}

void testCallRet() {
    const std::string path = createTempRomPath("gbemu_test_call_ret");
    std::vector<gb::u8> code(0x40, 0x00);

    code[0x00] = 0xCD;
    code[0x01] = 0x10;
    code[0x02] = 0x01;
    code[0x03] = 0x76;

    code[0x10] = 0x3E;
    code[0x11] = 0x99;
    code[0x12] = 0xC9;

    assert(createTestRom(path, code));

    gb::GameBoy gb;
    assert(gb.loadRom(path));
    runUntilHalt(gb);

    const auto& r = gb.cpu().regs();
    assert(r.a == 0x99);

    std::remove(path.c_str());
}

} // namespace

int main() {
    testDaa();
    testCbOps();
    testDmaCopy();
    testCallRet();

    return 0;
}
