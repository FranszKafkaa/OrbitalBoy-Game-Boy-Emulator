#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gb/core/gameboy.hpp"

namespace tests {

struct ScopedPath {
    ScopedPath() = default;

    explicit ScopedPath(std::filesystem::path path)
        : path_(std::move(path)) {}

    ScopedPath(const ScopedPath&) = delete;
    ScopedPath& operator=(const ScopedPath&) = delete;

    ScopedPath(ScopedPath&& other) noexcept
        : path_(std::move(other.path_)) {
        other.path_.clear();
    }

    ScopedPath& operator=(ScopedPath&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        cleanup();
        path_ = std::move(other.path_);
        other.path_.clear();
        return *this;
    }

    ~ScopedPath() {
        cleanup();
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    void cleanup() {
        if (path_.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        path_.clear();
    }

    std::filesystem::path path_;
};

struct RomPatch {
    gb::u16 address = 0;
    std::vector<gb::u8> bytes;
};

struct RomSpec {
    std::string name = "gbemu_test";
    std::string title = "GBTEST";
    std::size_t romBanks = 2;
    gb::u8 cartridgeType = 0x00;
    gb::u8 ramCode = 0x00;
    gb::u8 cgbFlag = 0x00;
    bool fillBanksWithIndex = false;
    gb::u16 origin = 0x0100;
    std::vector<gb::u8> program;
    std::vector<RomPatch> patches;
};

inline std::filesystem::path makeTempPath(const std::string& prefix, const std::string& extension) {
    static std::uint64_t counter = 0;
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string name = prefix + "_" + std::to_string(now) + "_" + std::to_string(++counter) + extension;
    return std::filesystem::temp_directory_path() / name;
}

inline bool writeBinaryFile(const std::filesystem::path& path, const std::vector<gb::u8>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

inline std::vector<gb::u8> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<gb::u8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline std::vector<gb::u8> buildRomImage(const RomSpec& spec) {
    const std::size_t banks = spec.romBanks == 0 ? 1 : spec.romBanks;
    std::vector<gb::u8> rom(banks * 0x4000, 0x00);

    if (spec.fillBanksWithIndex) {
        for (std::size_t bank = 0; bank < banks; ++bank) {
            std::fill(
                rom.begin() + static_cast<std::ptrdiff_t>(bank * 0x4000),
                rom.begin() + static_cast<std::ptrdiff_t>((bank + 1) * 0x4000),
                static_cast<gb::u8>(bank & 0xFF)
            );
        }
    }

    const std::size_t titleLen = std::min<std::size_t>(spec.title.size(), 16);
    for (std::size_t i = 0; i < titleLen; ++i) {
        rom[0x134 + i] = static_cast<gb::u8>(spec.title[i]);
    }

    rom[0x143] = spec.cgbFlag;
    rom[0x147] = spec.cartridgeType;
    rom[0x149] = spec.ramCode;

    for (std::size_t i = 0; i < spec.program.size(); ++i) {
        const auto address = static_cast<std::size_t>(spec.origin) + i;
        if (address < rom.size()) {
            rom[address] = spec.program[i];
        }
    }

    for (const auto& patch : spec.patches) {
        for (std::size_t i = 0; i < patch.bytes.size(); ++i) {
            const auto address = static_cast<std::size_t>(patch.address) + i;
            if (address < rom.size()) {
                rom[address] = patch.bytes[i];
            }
        }
    }

    return rom;
}

inline std::filesystem::path writeTempRom(const RomSpec& spec) {
    const auto path = makeTempPath(spec.name.empty() ? "gbemu_rom" : spec.name, ".gb");
    (void)writeBinaryFile(path, buildRomImage(spec));
    return path;
}

inline void runUntilHalt(gb::GameBoy& gb, int maxSteps = 20000) {
    for (int i = 0; i < maxSteps; ++i) {
        gb.step();
        if (gb.cpu().isHalted()) {
            return;
        }
    }
    throw std::runtime_error("CPU nao entrou em HALT dentro do limite de passos");
}

inline void runSteps(gb::GameBoy& gb, int count) {
    for (int i = 0; i < count; ++i) {
        gb.step();
    }
}

} // namespace tests
