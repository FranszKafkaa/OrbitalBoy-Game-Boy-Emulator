#include "gb/core/gba/system.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace gb::gba {

namespace {

constexpr std::size_t kGbaHeaderMinSize = 0xC0;
constexpr std::size_t kNintendoLogoOffset = 0x04;
constexpr std::size_t kNintendoLogoSize = 156;
constexpr std::size_t kTitleOffset = 0xA0;
constexpr std::size_t kTitleSize = 12;
constexpr std::size_t kGameCodeOffset = 0xAC;
constexpr std::size_t kGameCodeSize = 4;
constexpr std::size_t kMakerCodeOffset = 0xB0;
constexpr std::size_t kMakerCodeSize = 2;
constexpr std::size_t kFixedValueOffset = 0xB2;
constexpr std::size_t kUnitCodeOffset = 0xB3;
constexpr std::size_t kDeviceTypeOffset = 0xB4;
constexpr std::size_t kSoftwareVersionOffset = 0xBC;
constexpr std::size_t kComplementOffset = 0xBD;

constexpr std::array<u8, kNintendoLogoSize> kNintendoLogo = {
    0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A,
    0x84, 0xE4, 0x09, 0xAD, 0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21,
    0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20, 0x10, 0x46, 0x4A, 0x4A,
    0xF8, 0x27, 0x31, 0xEC, 0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF,
    0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0xC1, 0x94, 0x56, 0x8A, 0xC0,
    0x13, 0x72, 0xA7, 0xFC, 0x9F, 0x84, 0x4D, 0x73, 0xA3, 0xCA, 0x9A, 0x61,
    0x58, 0x97, 0xA3, 0x27, 0xFC, 0x03, 0x98, 0x76, 0x23, 0x1D, 0xC7, 0x61,
    0x03, 0x04, 0xAE, 0x56, 0xBF, 0x38, 0x84, 0x00, 0x40, 0xA7, 0x0E, 0xFD,
    0xFF, 0x52, 0xFE, 0x03, 0x6F, 0x95, 0x30, 0xF1, 0x97, 0xFB, 0xC0, 0x85,
    0x60, 0xD6, 0x80, 0x25, 0xA9, 0x63, 0xBE, 0x03, 0x01, 0x4E, 0x38, 0xE2,
    0xF9, 0xA2, 0x34, 0xFF, 0xBB, 0x3E, 0x03, 0x44, 0x78, 0x00, 0x90, 0xCB,
    0x88, 0x11, 0x3A, 0x94, 0x65, 0xC0, 0x7C, 0x63, 0x87, 0xF0, 0x3C, 0xAF,
    0xD6, 0x25, 0xE4, 0x8B, 0x38, 0x0A, 0xAC, 0x72, 0x21, 0xD4, 0xF8, 0x07,
};

std::string decodeAsciiField(const std::vector<u8>& data, std::size_t offset, std::size_t len) {
    std::string out;
    out.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
        if (offset + i >= data.size()) {
            break;
        }
        const char ch = static_cast<char>(data[offset + i]);
        if (ch == '\0') {
            break;
        }
        out.push_back(ch);
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

u16 rgbTo565(u8 r, u8 g, u8 b) {
    const u16 r5 = static_cast<u16>(r >> 3);
    const u16 g6 = static_cast<u16>(g >> 2);
    const u16 b5 = static_cast<u16>(b >> 3);
    return static_cast<u16>((r5 << 11) | (g6 << 5) | b5);
}

} // namespace

bool System::loadRomFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    const auto begin = std::istreambuf_iterator<char>(in);
    const auto end = std::istreambuf_iterator<char>();
    std::vector<u8> data(begin, end);
    if (data.size() < kGbaHeaderMinSize) {
        return false;
    }

    romPath_ = path;
    romData_ = std::move(data);
    frameCounter_ = 0;
    refreshMetadata();
    if (!memory_.loadRom(romData_)) {
        return false;
    }
    ppu_.connectMemory(&memory_);
    ppu_.reset();
    cpu_.connectMemory(&memory_);
    cpu_.reset();
    setInputState(InputState{});
    renderBootstrapFrame();
    return true;
}

const std::string& System::loadedRomPath() const {
    return romPath_;
}

const std::vector<u8>& System::romData() const {
    return romData_;
}

const RomMetadata& System::metadata() const {
    return metadata_;
}

bool System::loaded() const {
    return !romData_.empty();
}

const std::array<u16, System::FramebufferSize>& System::framebuffer() const {
    return framebuffer_;
}

std::array<u16, System::FramebufferSize>& System::framebuffer() {
    return framebuffer_;
}

const Memory& System::memory() const {
    return memory_;
}

Memory& System::memory() {
    return memory_;
}

const CpuArm7tdmi& System::cpu() const {
    return cpu_;
}

CpuArm7tdmi& System::cpu() {
    return cpu_;
}

void System::reset() {
    frameCounter_ = 0;
    memory_.reset();
    ppu_.connectMemory(&memory_);
    ppu_.reset();
    cpu_.reset();
    setInputState(InputState{});
    renderBootstrapFrame();
}

void System::runFrame() {
    if (romData_.empty()) {
        framebuffer_.fill(0);
        return;
    }

    ++frameCounter_;
    constexpr int kInstructionsPerFrame = 70000;
    runInstructions(kInstructionsPerFrame);
    renderExecutionFrame();
}

void System::runInstructions(int instructionCount) {
    if (instructionCount <= 0) {
        return;
    }
    for (int i = 0; i < instructionCount; ++i) {
        const int cpuCycles = cpu_.step();
        if (cpuCycles <= 0) {
            break;
        }
        const int busCycles = cpuCycles * 4;
        memory_.step(busCycles);
        ppu_.step(busCycles);
    }
}

void System::setInputState(const InputState& input) {
    u16 keys = Memory::DefaultKeyInput;
    if (input.a) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 0U));
    }
    if (input.b) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 1U));
    }
    if (input.select) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 2U));
    }
    if (input.start) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 3U));
    }
    if (input.right) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 4U));
    }
    if (input.left) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 5U));
    }
    if (input.up) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 6U));
    }
    if (input.down) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 7U));
    }
    if (input.r) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 8U));
    }
    if (input.l) {
        keys = static_cast<u16>(keys & ~static_cast<u16>(1U << 9U));
    }
    memory_.setKeyInputRaw(keys);
}

void System::refreshMetadata() {
    metadata_ = RomMetadata{};
    metadata_.title = decodeAsciiField(romData_, kTitleOffset, kTitleSize);
    metadata_.gameCode = decodeAsciiField(romData_, kGameCodeOffset, kGameCodeSize);
    metadata_.makerCode = decodeAsciiField(romData_, kMakerCodeOffset, kMakerCodeSize);
    metadata_.unitCode = romData_[kUnitCodeOffset];
    metadata_.deviceType = romData_[kDeviceTypeOffset];
    metadata_.softwareVersion = romData_[kSoftwareVersionOffset];
    metadata_.complementCheck = romData_[kComplementOffset];

    metadata_.validFixedByte = romData_[kFixedValueOffset] == 0x96;

    metadata_.validNintendoLogo = std::equal(
        kNintendoLogo.begin(),
        kNintendoLogo.end(),
        romData_.begin() + static_cast<std::ptrdiff_t>(kNintendoLogoOffset)
    );

    u8 sum = 0;
    for (std::size_t i = kTitleOffset; i <= kSoftwareVersionOffset; ++i) {
        sum = static_cast<u8>(sum + romData_[i]);
    }
    const u8 expected = static_cast<u8>(0U - static_cast<u8>(0x19U + sum));
    metadata_.validHeaderChecksum = expected == metadata_.complementCheck;
}

void System::renderBootstrapFrame() {
    const u8 baseR = metadata_.validNintendoLogo ? 0x20 : 0x70;
    const u8 baseG = metadata_.validHeaderChecksum ? 0x90 : 0x40;
    const u8 baseB = metadata_.validFixedByte ? 0x20 : 0x70;

    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const auto idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth) + static_cast<std::size_t>(x);
            const u8 r = static_cast<u8>((baseR + x / 3) & 0xFF);
            const u8 g = static_cast<u8>((baseG + y / 2) & 0xFF);
            const u8 b = static_cast<u8>((baseB + ((x + y) / 4)) & 0xFF);
            framebuffer_[idx] = rgbTo565(r, g, b);
        }
    }
}

void System::renderExecutionFrame() {
    if (ppu_.render(framebuffer_)) {
        return;
    }

    const u32 pc = cpu_.pc();
    const u8 seedR = static_cast<u8>(((pc >> 4U) + frameCounter_) & 0xFFU);
    const u8 seedG = static_cast<u8>((cpu_.reg(0) + (frameCounter_ * 3U)) & 0xFFU);
    const u8 seedB = static_cast<u8>((cpu_.reg(1) + (frameCounter_ * 5U)) & 0xFFU);

    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const auto idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth) + static_cast<std::size_t>(x);
            const u8 r = static_cast<u8>((seedR + x) & 0xFFU);
            const u8 g = static_cast<u8>((seedG + y + (x / 5)) & 0xFFU);
            const u8 b = static_cast<u8>((seedB + (x / 2) + (y / 3)) & 0xFFU);
            framebuffer_[idx] = rgbTo565(r, g, b);
        }
    }
}

} // namespace gb::gba
