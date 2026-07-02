#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gb/core/gba/debug.hpp"
#include "gb/core/gba/system.hpp"
#include "gb/core/types.hpp"

namespace gb::gba {

class MgbaCore {
public:
    static constexpr int ScreenWidth = 240;
    static constexpr int ScreenHeight = 160;
    static constexpr std::size_t FramebufferSize = static_cast<std::size_t>(ScreenWidth) * static_cast<std::size_t>(ScreenHeight);
    static constexpr int SampleRate = 44100;

    MgbaCore();
    ~MgbaCore();

    MgbaCore(const MgbaCore&) = delete;
    MgbaCore& operator=(const MgbaCore&) = delete;

    [[nodiscard]] bool loadRomFromFile(const std::string& romPath);
    void unload();

    void setInputState(const InputState& input);
    void runFrame();
    void stepInstruction();

    [[nodiscard]] const std::array<u16, FramebufferSize>& framebuffer() const;
    std::vector<std::int16_t> takeSamples();

    [[nodiscard]] bool debugAvailable() const;
    [[nodiscard]] GbaDebugSnapshot debugSnapshot() const;
    [[nodiscard]] std::optional<u8> debugRead8(u32 address) const;
    [[nodiscard]] std::optional<u16> debugRead16(u32 address) const;
    [[nodiscard]] std::optional<u32> debugRead32(u32 address) const;
    [[nodiscard]] bool debugWrite8(u32 address, u8 value);
    [[nodiscard]] bool debugWrite16(u32 address, u16 value);
    [[nodiscard]] bool debugWrite32(u32 address, u32 value);

    [[nodiscard]] bool loadBackupFromFile(const std::string& path);
    [[nodiscard]] bool saveBackupToFile(const std::string& path) const;
    [[nodiscard]] bool loadStateFromFile(const std::string& path);
    [[nodiscard]] bool saveStateToFile(const std::string& path) const;
    [[nodiscard]] bool loaded() const;
    [[nodiscard]] const std::string& loadedRomPath() const;
    [[nodiscard]] const std::string& coreName() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace gb::gba
