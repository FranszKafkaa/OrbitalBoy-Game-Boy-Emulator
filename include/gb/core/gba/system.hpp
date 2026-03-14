#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "gb/core/gba/cpu.hpp"
#include "gb/core/gba/memory.hpp"
#include "gb/core/gba/ppu.hpp"
#include "gb/core/types.hpp"

namespace gb::gba {

struct RomMetadata {
    std::string title{};
    std::string gameCode{};
    std::string makerCode{};
    bool validNintendoLogo = false;
    bool validFixedByte = false;
    bool validHeaderChecksum = false;
    u8 unitCode = 0;
    u8 deviceType = 0;
    u8 softwareVersion = 0;
    u8 complementCheck = 0;
};

struct InputState {
    bool a = false;
    bool b = false;
    bool select = false;
    bool start = false;
    bool right = false;
    bool left = false;
    bool up = false;
    bool down = false;
    bool r = false;
    bool l = false;
};

struct CompatibilityProfile {
    std::string name = "default";
    bool forceScanlineFrameSync = false;
    bool enableAdaptiveScanlineFallback = true;
    bool useFlashCompatibilityMode = false;
    int flashVendorId = -1;
    int flashDeviceId = -1;
    int forcedEepromAddressBits = 0; // 0=auto, 6/14 for override
    bool strictBackupFileSize = false;
};

class System {
public:
    static constexpr int ScreenWidth = 240;
    static constexpr int ScreenHeight = 160;
    static constexpr std::size_t FramebufferSize = static_cast<std::size_t>(ScreenWidth) * static_cast<std::size_t>(ScreenHeight);

    [[nodiscard]] bool loadRomFromFile(const std::string& path);

    [[nodiscard]] const std::string& loadedRomPath() const;
    [[nodiscard]] const std::vector<u8>& romData() const;
    [[nodiscard]] const RomMetadata& metadata() const;
    [[nodiscard]] bool loaded() const;
    [[nodiscard]] bool hasPersistentBackup() const;
    [[nodiscard]] const std::string& backupTypeName() const;
    [[nodiscard]] const CompatibilityProfile& compatibilityProfile() const;
    bool loadBackupFromFile(const std::string& path);
    bool saveBackupToFile(const std::string& path) const;

    [[nodiscard]] const std::array<u16, FramebufferSize>& framebuffer() const;
    [[nodiscard]] std::array<u16, FramebufferSize>& framebuffer();
    [[nodiscard]] const Memory& memory() const;
    [[nodiscard]] Memory& memory();
    [[nodiscard]] const CpuArm7tdmi& cpu() const;
    [[nodiscard]] CpuArm7tdmi& cpu();

    void reset();
    void runFrame();
    void runInstructions(int instructionCount);
    void setInputState(const InputState& input);

private:
    void runUntilFrameBoundary(int targetBusCycles, int instructionLimit);
    void refreshMetadata();
    void configureCompatibilityProfile();
    void renderBootstrapFrame();
    void renderExecutionFrame();

    std::string romPath_{};
    std::vector<u8> romData_{};
    RomMetadata metadata_{};
    CompatibilityProfile compatibilityProfile_{};
    Memory memory_{};
    Ppu ppu_{};
    CpuArm7tdmi cpu_{};
    std::array<u16, FramebufferSize> framebuffer_{};
    std::uint32_t frameCounter_ = 0;
    bool adaptiveScanlineSync_ = false;
    int startupNoDisplayFrames_ = 0;
};

} // namespace gb::gba
