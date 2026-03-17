#include "gb/core/gba/system.hpp"
#include "gb/core/environment.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <optional>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

CompatibilityProfile resolveCompatibilityProfileForGame(const std::string& gameCode) {
    CompatibilityProfile profile{};
    if (gameCode == "AA2E") {
        // Super Mario Advance 2 usa EEPROM 64 Kbit (14-bit commands no boot).
        profile.name = "super-mario-advance-2";
        profile.forcedEepromAddressBits = 14;
        profile.strictBackupFileSize = true;
        return profile;
    }
    if (gameCode == "AWRE") {
        // Advance Wars usa FLASH64; rejeitar tamanhos errados evita manter save legado inválido.
        profile.name = "advance-wars";
        profile.flashVendorId = 0x62;
        profile.flashDeviceId = 0x13;
        profile.strictBackupFileSize = true;
        return profile;
    }
    return profile;
}

bool renderFrameSafely(Ppu& ppu, std::array<u16, System::FramebufferSize>& framebuffer) {
#if defined(_WIN32) && defined(_MSC_VER)
    __try {
        return ppu.render(framebuffer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return ppu.render(framebuffer);
#endif
}

using Clock = std::chrono::steady_clock;

std::uint64_t elapsedNs(Clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

bool frameProfileLoggingEnabled() {
    return gb::environmentVariableEnabled("GBEMU_GBA_LOG_PROFILE");
}

bool frameSceneLoggingEnabled() {
    return gb::environmentVariableEnabled("GBEMU_GBA_LOG_SCENE")
        || gb::environmentVariableEnabled("GBEMU_GBA_LOG_FRAME_STATE");
}

int frameProfileLogEvery() {
    const auto value = gb::readEnvironmentVariable("GBEMU_GBA_LOG_PROFILE_EVERY");
    if (!value.has_value() || value->empty()) {
        return 60;
    }
    try {
        return std::max(1, std::stoi(*value));
    } catch (...) {
        return 60;
    }
}

int frameSceneLogEvery() {
    const auto value = gb::readEnvironmentVariable("GBEMU_GBA_LOG_SCENE_EVERY");
    if (!value.has_value() || value->empty()) {
        return 1;
    }
    try {
        return std::max(1, std::stoi(*value));
    } catch (...) {
        return 1;
    }
}

int readIntEnvironmentBase0OrDefault(const char* name, int fallback) {
    const auto value = gb::readEnvironmentVariable(name);
    if (!value.has_value() || value->empty()) {
        return fallback;
    }
    try {
        return std::stoi(*value, nullptr, 0);
    } catch (...) {
        return fallback;
    }
}

int frameSceneBgProbeWordCount() {
    return std::clamp(readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_SAMPLE_WORDS", 0), 0, 8);
}

u32 bgScreenBlockBase(const Ppu::TextBgDebugSample& sample) {
    return (sample.screenBase + static_cast<u32>(sample.screenBlock) * 0x800U) & 0x1FFFEU;
}

u32 bgCharBlockEnd(const Ppu::TextBgDebugSample& sample) {
    return sample.charBase + 0x3FFFU;
}

u32 bgTileBaseAddress(const Ppu::TextBgDebugSample& sample) {
    const u32 bytesPerTile = sample.color256 ? 64U : 32U;
    return sample.charBase + static_cast<u32>(sample.tileNumber) * bytesPerTile;
}

u32 bgTileEndAddress(const Ppu::TextBgDebugSample& sample) {
    const u32 bytesPerTile = sample.color256 ? 64U : 32U;
    return bgTileBaseAddress(sample) + bytesPerTile - 1U;
}

std::string dumpBgVramWords(const Memory& memory, u32 startOffset, int wordCount) {
    if (wordCount <= 0) {
        return {};
    }

    std::ostringstream out;
    out << std::hex;
    for (int index = 0; index < wordCount; ++index) {
        if (index != 0) {
            out << ',';
        }
        const u32 offset = (startOffset + static_cast<u32>(index * 2)) & 0x1FFFEU;
        out << "0x" << memory.read16(Memory::VramBase + offset);
    }
    return out.str();
}

struct FrameSceneBgProbeConfig {
    int bgIndex = -1;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    int limit = 4;
};

std::optional<FrameSceneBgProbeConfig> frameSceneBgProbeConfig() {
    const int bgIndex = readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_SAMPLE_BG", -1);
    if (bgIndex < 0 || bgIndex > 3) {
        return std::nullopt;
    }

    FrameSceneBgProbeConfig config{};
    config.bgIndex = bgIndex;
    config.x = readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_SAMPLE_X", 0);
    config.y = readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_SAMPLE_Y", 0);
    config.width = std::max(1, readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_SAMPLE_W", 1));
    config.height = std::max(1, readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_SAMPLE_H", 1));
    config.limit = std::clamp(readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_SAMPLE_LIMIT", 4), 1, 8);
    return config;
}

struct FrameSceneBgProbeHit {
    bool valid = false;
    int x = 0;
    int y = 0;
    Ppu::TextBgDebugSample sample{};
};

std::array<FrameSceneBgProbeHit, 8> collectFrameSceneBgProbeHits(
    const System& system,
    const FrameSceneBgProbeConfig& config,
    int& hitCount,
    bool& sampledOrigin,
    Ppu::TextBgDebugSample& originSample
) {
    std::array<FrameSceneBgProbeHit, 8> hits{};
    hitCount = 0;
    sampledOrigin = false;
    originSample = Ppu::TextBgDebugSample{};

    sampledOrigin = system.ppu().debugTextBgSample(config.bgIndex, config.x, config.y, originSample);

    const int startX = std::clamp(config.x, 0, Ppu::ScreenWidth - 1);
    const int startY = std::clamp(config.y, 0, Ppu::ScreenHeight - 1);
    const int endX = std::clamp(config.x + config.width, 0, Ppu::ScreenWidth);
    const int endY = std::clamp(config.y + config.height, 0, Ppu::ScreenHeight);
    for (int y = startY; y < endY && hitCount < config.limit; ++y) {
        for (int x = startX; x < endX && hitCount < config.limit; ++x) {
            Ppu::TextBgDebugSample sample{};
            if (!system.ppu().debugTextBgSample(config.bgIndex, x, y, sample) || !sample.visible) {
                continue;
            }
            hits[hitCount].valid = true;
            hits[hitCount].x = x;
            hits[hitCount].y = y;
            hits[hitCount].sample = sample;
            ++hitCount;
        }
    }
    return hits;
}

void logGbaFrameSceneState(const System& system, std::uint32_t frameNumber) {
    const Memory& memory = system.memory();
    const u16 dispcnt = memory.readIo16(Ppu::DispcntOffset);
    const u16 bg0cnt = memory.readIo16(0x0008U);
    const u16 bg1cnt = memory.readIo16(0x000AU);
    const u16 bg2cnt = memory.readIo16(0x000CU);
    const u16 bg3cnt = memory.readIo16(0x000EU);
    const u16 bg0hofs = memory.readIo16(0x0010U);
    const u16 bg0vofs = memory.readIo16(0x0012U);
    const u16 bg1hofs = memory.readIo16(0x0014U);
    const u16 bg1vofs = memory.readIo16(0x0016U);
    const u16 bg2hofs = memory.readIo16(0x0018U);
    const u16 bg2vofs = memory.readIo16(0x001AU);
    const u16 bg3hofs = memory.readIo16(0x001CU);
    const u16 bg3vofs = memory.readIo16(0x001EU);
    const u16 bldcnt = memory.readIo16(0x0050U);
    const u16 bldalpha = memory.readIo16(0x0052U);
    const u16 bldy = memory.readIo16(0x0054U);
    const u16 win0h = memory.readIo16(0x0040U);
    const u16 win1h = memory.readIo16(0x0042U);
    const u16 win0v = memory.readIo16(0x0044U);
    const u16 win1v = memory.readIo16(0x0046U);
    const u16 winin = memory.readIo16(0x0048U);
    const u16 winout = memory.readIo16(0x004AU);
    const u16 mode = static_cast<u16>(dispcnt & 0x0007U);
    const bool obj1d = (dispcnt & 0x0040U) != 0U;
    const bool objEnable = (dispcnt & 0x1000U) != 0U;
    const unsigned bgEnables = static_cast<unsigned>((dispcnt >> 8U) & 0x0FU);
    const auto& stats = system.lastFrameProfile().ppu;
    const auto bgProbe = frameSceneBgProbeConfig();
    const int bgProbeWordCount = frameSceneBgProbeWordCount();
    int bgProbeHitCount = 0;
    bool bgProbeOriginValid = false;
    Ppu::TextBgDebugSample bgProbeOriginSample{};
    const std::array<FrameSceneBgProbeHit, 8> bgProbeHits = bgProbe.has_value()
        ? collectFrameSceneBgProbeHits(system, *bgProbe, bgProbeHitCount, bgProbeOriginValid, bgProbeOriginSample)
        : std::array<FrameSceneBgProbeHit, 8>{};

    std::cerr << "[GBA][SCENE] frame=" << frameNumber
              << " mode=" << mode
              << " objMap=" << (obj1d ? "1D" : "2D")
              << " bgEn=0x" << std::hex << bgEnables
              << " objEn=" << static_cast<unsigned>(objEnable)
              << " dispcnt=0x" << dispcnt
              << " bg0cnt=0x" << bg0cnt
              << " bg1cnt=0x" << bg1cnt
              << " bg2cnt=0x" << bg2cnt
              << " bg3cnt=0x" << bg3cnt
              << " bg0ofs=(" << std::dec << bg0hofs << "," << bg0vofs << ")"
              << " bg1ofs=(" << bg1hofs << "," << bg1vofs << ")"
              << " bg2ofs=(" << bg2hofs << "," << bg2vofs << ")"
              << " bg3ofs=(" << bg3hofs << "," << bg3vofs << ")"
              << std::hex
              << " bldcnt=0x" << bldcnt
              << " bldalpha=0x" << bldalpha
              << " bldy=0x" << bldy
              << " win0h=0x" << win0h
              << " win0v=0x" << win0v
              << " win1h=0x" << win1h
              << " win1v=0x" << win1v
              << " winin=0x" << winin
              << " winout=0x" << winout
              << std::dec
              << " objVisible=" << stats.visibleObjectsFrame
              << " objMaxLine=" << stats.maxVisibleObjectsOnScanline
              << " objPixels=" << stats.objPixelsDrawn
              << " bgMs=" << (static_cast<double>(stats.bgNs) / 1000000.0)
              << " objMs=" << (static_cast<double>(stats.objNs) / 1000000.0)
              << " compMs=" << (static_cast<double>(stats.composeNs) / 1000000.0);

    if (bgProbe.has_value()) {
        std::cerr << " probe=bg" << bgProbe->bgIndex
                  << "@(" << bgProbe->x << "," << bgProbe->y << ")";
        if (bgProbe->width > 1 || bgProbe->height > 1) {
            std::cerr << " probeRect=" << bgProbe->width << "x" << bgProbe->height
                      << " probeHits=" << bgProbeHitCount;
            if (bgProbeOriginValid) {
                const u32 blockBase = bgScreenBlockBase(bgProbeOriginSample);
                const u32 mapBase = bgProbeOriginSample.mapAddress & 0x1FFFEU;
                const u32 tileBase = bgTileBaseAddress(bgProbeOriginSample);
                const u32 tileEnd = bgTileEndAddress(bgProbeOriginSample);
                std::cerr << " probeOriginVisible=" << static_cast<unsigned>(bgProbeOriginSample.visible)
                          << std::hex
                          << " probeOriginCnt=0x" << bgProbeOriginSample.bgcnt
                          << " probeOriginScreenBase=0x" << bgProbeOriginSample.screenBase
                          << " probeOriginMapAddr=0x" << bgProbeOriginSample.mapAddress
                          << " probeOriginTileAddr=0x" << bgProbeOriginSample.tileAddress
                          << " probeOriginEntry=0x" << bgProbeOriginSample.mapEntry
                          << std::dec
                          << " probeOriginSb=" << static_cast<unsigned>(bgProbeOriginSample.screenBlock)
                          << " probeOriginTile=(" << bgProbeOriginSample.tileX << "," << bgProbeOriginSample.tileY << ")"
                          << " probeOriginTileNo=" << bgProbeOriginSample.tileNumber
                          << " probeOriginColor=" << static_cast<unsigned>(bgProbeOriginSample.colorIndex)
                          << std::hex
                          << " probeWatchMap=0x" << blockBase << "-0x" << (blockBase + 0x7FFU)
                          << " probeWatchChar=0x" << bgProbeOriginSample.charBase << "-0x" << bgCharBlockEnd(bgProbeOriginSample)
                          << " probeWatchTile=0x" << tileBase << "-0x" << tileEnd
                          << std::dec;
                if (bgProbeWordCount > 0) {
                    std::cerr << std::hex
                              << " probeBlockWords@0x" << blockBase << '=' << dumpBgVramWords(memory, blockBase, bgProbeWordCount)
                              << " probeMapWords@0x" << mapBase << '=' << dumpBgVramWords(memory, mapBase, bgProbeWordCount)
                              << std::dec;
                }
            } else {
                std::cerr << " probeOriginValid=0";
            }
            for (int hitIndex = 0; hitIndex < bgProbeHitCount; ++hitIndex) {
                const auto& hit = bgProbeHits[hitIndex];
                std::cerr << " probeHit" << hitIndex
                          << "=(" << hit.x << "," << hit.y << ")"
                          << std::hex
                          << "#cnt=0x" << hit.sample.bgcnt
                          << "#entry=0x" << hit.sample.mapEntry
                          << std::dec
                          << "#sb=" << static_cast<unsigned>(hit.sample.screenBlock)
                          << "#tile=(" << hit.sample.tileX << "," << hit.sample.tileY << ")"
                          << "#tileNo=" << hit.sample.tileNumber
                          << "#pal=" << static_cast<unsigned>(hit.sample.paletteBank)
                          << "#flip=" << (hit.sample.hflip ? 'H' : '-') << (hit.sample.vflip ? 'V' : '-')
                          << "#color=" << static_cast<unsigned>(hit.sample.colorIndex);
            }
        } else if (bgProbeOriginValid) {
            const u32 blockBase = bgScreenBlockBase(bgProbeOriginSample);
            const u32 mapBase = bgProbeOriginSample.mapAddress & 0x1FFFEU;
            const u32 tileBase = bgTileBaseAddress(bgProbeOriginSample);
            const u32 tileEnd = bgTileEndAddress(bgProbeOriginSample);
            std::cerr << " probeValid=" << static_cast<unsigned>(bgProbeOriginSample.valid)
                      << " probeVisible=" << static_cast<unsigned>(bgProbeOriginSample.visible)
                      << std::hex
                      << " probeCnt=0x" << bgProbeOriginSample.bgcnt
                      << " probeScreenBase=0x" << bgProbeOriginSample.screenBase
                      << " probeMapAddr=0x" << bgProbeOriginSample.mapAddress
                      << " probeTileAddr=0x" << bgProbeOriginSample.tileAddress
                      << " probeEntry=0x" << bgProbeOriginSample.mapEntry
                      << std::dec
                      << " probeSb=" << static_cast<unsigned>(bgProbeOriginSample.screenBlock)
                      << " probeTile=(" << bgProbeOriginSample.tileX << "," << bgProbeOriginSample.tileY << ")"
                      << " probeTileNo=" << bgProbeOriginSample.tileNumber
                      << " probePal=" << static_cast<unsigned>(bgProbeOriginSample.paletteBank)
                      << " probeFlip=" << (bgProbeOriginSample.hflip ? 'H' : '-') << (bgProbeOriginSample.vflip ? 'V' : '-')
                      << " probeColor=" << static_cast<unsigned>(bgProbeOriginSample.colorIndex)
                      << std::hex
                      << " probeWatchMap=0x" << blockBase << "-0x" << (blockBase + 0x7FFU)
                      << " probeWatchChar=0x" << bgProbeOriginSample.charBase << "-0x" << bgCharBlockEnd(bgProbeOriginSample)
                      << " probeWatchTile=0x" << tileBase << "-0x" << tileEnd
                      << std::dec;
            if (bgProbeWordCount > 0) {
                std::cerr << std::hex
                          << " probeBlockWords@0x" << blockBase << '=' << dumpBgVramWords(memory, blockBase, bgProbeWordCount)
                          << " probeMapWords@0x" << mapBase << '=' << dumpBgVramWords(memory, mapBase, bgProbeWordCount)
                          << std::dec;
            }
        } else {
            std::cerr << " probeValid=0";
        }
    }

    std::cerr << "\n";
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
    adaptiveScanlineSync_ = false;
    startupNoDisplayFrames_ = 0;
    refreshMetadata();
    configureCompatibilityProfile();
    if (!memory_.loadRom(romData_)) {
        return false;
    }
    memory_.configureBackupBehavior(
        compatibilityProfile_.forcedEepromAddressBits,
        compatibilityProfile_.strictBackupFileSize
    );
    memory_.setFlashIdOverride(
        compatibilityProfile_.flashVendorId,
        compatibilityProfile_.flashDeviceId
    );
    bool flashCompatibilityMode = compatibilityProfile_.useFlashCompatibilityMode;
    if (gb::hasEnvironmentVariable("GBEMU_GBA_FLASH_COMPAT_MODE")) {
        flashCompatibilityMode = gb::environmentVariableEnabled("GBEMU_GBA_FLASH_COMPAT_MODE");
    }
    memory_.setFlashCompatibilityMode(flashCompatibilityMode);
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

bool System::hasPersistentBackup() const {
    return memory_.hasPersistentBackup();
}

const std::string& System::backupTypeName() const {
    return memory_.backupTypeName();
}

const CompatibilityProfile& System::compatibilityProfile() const {
    return compatibilityProfile_;
}

bool System::loadBackupFromFile(const std::string& path) {
    if (compatibilityProfile_.strictBackupFileSize) {
        std::error_code ec;
        const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
        if (!ec) {
            const std::size_t expectedSize = memory_.expectedBackupFileSize();
            if (expectedSize != 0U && fileSize != expectedSize) {
                return false;
            }
        }
    }
    return memory_.loadBackupFromFile(path);
}

bool System::saveBackupToFile(const std::string& path) const {
    return memory_.saveBackupToFile(path);
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

const Ppu& System::ppu() const {
    return ppu_;
}

Ppu& System::ppu() {
    return ppu_;
}

const System::FrameProfile& System::lastFrameProfile() const {
    return lastFrameProfile_;
}

const CpuArm7tdmi& System::cpu() const {
    return cpu_;
}

CpuArm7tdmi& System::cpu() {
    return cpu_;
}

void System::reset() {
    frameCounter_ = 0;
    adaptiveScanlineSync_ = false;
    startupNoDisplayFrames_ = 0;
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
        lastFrameProfile_ = FrameProfile{};
        return;
    }

    const auto frameStart = Clock::now();

    ++frameCounter_;
    if (compatibilityProfile_.enableAdaptiveScanlineFallback && frameCounter_ <= 600U) {
        const u16 dispcnt = memory_.readIo16(0U);
        const bool forcedBlank = (dispcnt & 0x0080U) != 0U;
        const bool anyVisibleLayer = (dispcnt & 0x1F00U) != 0U;
        if (forcedBlank || !anyVisibleLayer) {
            ++startupNoDisplayFrames_;
        } else {
            startupNoDisplayFrames_ = 0;
        }
        adaptiveScanlineSync_ = startupNoDisplayFrames_ >= 180;
    }

    bool frameSyncScanline = compatibilityProfile_.forceScanlineFrameSync || adaptiveScanlineSync_;
    if (gb::hasEnvironmentVariable("GBEMU_GBA_FRAME_SYNC_SCANLINE")) {
        frameSyncScanline = gb::environmentVariableEnabled("GBEMU_GBA_FRAME_SYNC_SCANLINE");
    }
    constexpr int kNominalBusCyclesPerFrame =
        static_cast<int>(Ppu::CyclesPerLine) * static_cast<int>(Ppu::TotalLines);
    const auto cpuStart = Clock::now();
    if (!frameSyncScanline) {
        runUntilFrameBoundary(kNominalBusCyclesPerFrame, 90000);
    } else {
        // Em modo de compatibilidade, permite margem maior para busy loops
        // sem perder o fechamento do frame no wrap do PPU.
        runUntilFrameBoundary(kNominalBusCyclesPerFrame, 300000);
    }
    lastFrameProfile_.cpuNs = elapsedNs(cpuStart);

    const auto renderStart = Clock::now();
    renderExecutionFrame();
    lastFrameProfile_.renderNs = elapsedNs(renderStart);
    lastFrameProfile_.ppu = ppu_.lastRenderStats();
    lastFrameProfile_.totalNs = elapsedNs(frameStart);

    if (frameProfileLoggingEnabled()) {
        const int logEvery = frameProfileLogEvery();
        if (logEvery > 0 && (frameCounter_ % static_cast<std::uint32_t>(logEvery)) == 0U) {
            std::cerr << "[GBA][PROFILE] frame=" << frameCounter_
                      << " totalMs=" << (static_cast<double>(lastFrameProfile_.totalNs) / 1000000.0)
                      << " cpuMs=" << (static_cast<double>(lastFrameProfile_.cpuNs) / 1000000.0)
                      << " renderMs=" << (static_cast<double>(lastFrameProfile_.renderNs) / 1000000.0)
                      << " bgMs=" << (static_cast<double>(lastFrameProfile_.ppu.bgNs) / 1000000.0)
                      << " objMs=" << (static_cast<double>(lastFrameProfile_.ppu.objNs) / 1000000.0)
                      << " objWinMs=" << (static_cast<double>(lastFrameProfile_.ppu.objWindowNs) / 1000000.0)
                      << " composeMs=" << (static_cast<double>(lastFrameProfile_.ppu.composeNs) / 1000000.0)
                      << " objVisible=" << lastFrameProfile_.ppu.visibleObjectsFrame
                      << " objPixelsTested=" << lastFrameProfile_.ppu.objPixelsTested
                      << " objPixelsDrawn=" << lastFrameProfile_.ppu.objPixelsDrawn
                      << " maxObjScanline=" << lastFrameProfile_.ppu.maxVisibleObjectsOnScanline
                      << "\n";
        }
    }
    if (frameSceneLoggingEnabled()) {
        const int logEvery = frameSceneLogEvery();
        if (logEvery > 0 && (frameCounter_ % static_cast<std::uint32_t>(logEvery)) == 0U) {
            logGbaFrameSceneState(*this, frameCounter_);
        }
    }
}

void System::runInstructions(int instructionCount) {
    if (instructionCount <= 0) {
        return;
    }
    for (int i = 0; i < instructionCount; ++i) {
        int totalBusCycles = cpu_.step();
        if (totalBusCycles <= 0) {
            break;
        }
        memory_.step(totalBusCycles);
        for (int extra = memory_.consumeDeferredBusCycles(); extra > 0; extra = memory_.consumeDeferredBusCycles()) {
            totalBusCycles += extra;
            memory_.step(extra);
        }
        ppu_.step(totalBusCycles);
        int ignoredAccumulatedCycles = 0;
        drainDeferredBusCycles(ignoredAccumulatedCycles);
    }
}

void System::drainDeferredBusCycles(int& accumulatedBusCycles) {
    for (int extra = memory_.consumeDeferredBusCycles(); extra > 0; extra = memory_.consumeDeferredBusCycles()) {
        accumulatedBusCycles += extra;
        memory_.step(extra);
        ppu_.step(extra);
    }
}

void System::runUntilFrameBoundary(int targetBusCycles, int instructionLimit) {
    if (targetBusCycles <= 0 || instructionLimit <= 0) {
        return;
    }

    int accumulatedBusCycles = 0;
    bool wrappedScanline = false;
    int instructions = 0;
    while (instructions < instructionLimit) {
        const std::uint16_t previousLine = ppu_.scanline();
        int totalBusCycles = cpu_.step();
        if (totalBusCycles <= 0) {
            break;
        }
        accumulatedBusCycles += totalBusCycles;
        memory_.step(totalBusCycles);
        for (int extra = memory_.consumeDeferredBusCycles(); extra > 0; extra = memory_.consumeDeferredBusCycles()) {
            accumulatedBusCycles += extra;
            totalBusCycles += extra;
            memory_.step(extra);
        }
        ppu_.step(totalBusCycles);
        drainDeferredBusCycles(accumulatedBusCycles);
        ++instructions;

        const std::uint16_t currentLine = ppu_.scanline();
        if (currentLine < previousLine) {
            wrappedScanline = true;
        }
        if (wrappedScanline && accumulatedBusCycles >= targetBusCycles) {
            return;
        }
    }

    // Fallback defensivo: se o budget foi consumido antes do wrap, continua
    // por um trecho curto ate encerrar um frame completo do PPU.
    constexpr int kRecoveryInstructionLimit = 20000;
    for (int i = 0; i < kRecoveryInstructionLimit; ++i) {
        const std::uint16_t previousLine = ppu_.scanline();
        int totalBusCycles = cpu_.step();
        if (totalBusCycles <= 0) {
            break;
        }
        memory_.step(totalBusCycles);
        for (int extra = memory_.consumeDeferredBusCycles(); extra > 0; extra = memory_.consumeDeferredBusCycles()) {
            totalBusCycles += extra;
            memory_.step(extra);
        }
        ppu_.step(totalBusCycles);
        int ignoredAccumulatedCycles = 0;
        drainDeferredBusCycles(ignoredAccumulatedCycles);
        if (ppu_.scanline() < previousLine) {
            break;
        }
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

void System::configureCompatibilityProfile() {
    compatibilityProfile_ = resolveCompatibilityProfileForGame(metadata_.gameCode);
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
    if (renderFrameSafely(ppu_, framebuffer_)) {
        return;
    }

    static bool warnedRenderFailure = false;
    if (!warnedRenderFailure) {
        std::cerr << "aviso: renderer GBA falhou; usando framebuffer de fallback.\n";
        warnedRenderFailure = true;
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
