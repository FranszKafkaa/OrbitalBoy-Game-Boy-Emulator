#include "gb/core/gba/ppu.hpp"

#include "gb/core/environment.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <string>

namespace gb::gba {

namespace {

constexpr u16 kDisplayModeMask = 0x0007U;
constexpr u16 kFrameSelectMask = 0x0010U;
constexpr u16 kObjMapping1dMask = 0x0040U;
constexpr u16 kForcedBlankMask = 0x0080U;
constexpr u16 kBgEnableMasks[4] = {0x0100U, 0x0200U, 0x0400U, 0x0800U};
constexpr u16 kObjEnableMask = 0x1000U;
constexpr u16 kWin0EnableMask = 0x2000U;
constexpr u16 kWin1EnableMask = 0x4000U;
constexpr u16 kObjWinEnableMask = 0x8000U;
constexpr u16 kAnyWindowEnableMask = kWin0EnableMask | kWin1EnableMask | kObjWinEnableMask;
constexpr u16 kBgMosaicMask = 0x0040U;
constexpr u16 kObjMosaicMask = 0x1000U;

constexpr std::size_t kBitmapPage1Offset = 0xA000U;
constexpr std::size_t kObjTileBaseMode012Offset = 0x10000U;
constexpr std::size_t kTextBgCharDataLimit = 0x18000U;  // Full 96 KiB VRAM — real GBA has no 64 KiB BG tile restriction
constexpr std::size_t kObjTileBaseMode345Offset = 0x14000U;
constexpr u32 kBgWrapMask = 0x2000U;

constexpr u32 kBgAffinePaOffset = 0x0020U;
constexpr u32 kBgAffinePbOffset = 0x0022U;
constexpr u32 kBgAffinePcOffset = 0x0024U;
constexpr u32 kBgAffinePdOffset = 0x0026U;
constexpr u32 kBgAffineXOffset = 0x0028U;
constexpr u32 kBgAffineYOffset = 0x002CU;

constexpr u32 kWin0HOffset = 0x0040U;
constexpr u32 kWin1HOffset = 0x0042U;
constexpr u32 kWin0VOffset = 0x0044U;
constexpr u32 kWin1VOffset = 0x0046U;
constexpr u32 kWinInOffset = 0x0048U;
constexpr u32 kWinOutOffset = 0x004AU;
constexpr u32 kMosaicOffset = 0x004CU;
constexpr u32 kBldCntOffset = 0x0050U;
constexpr u32 kBldAlphaOffset = 0x0052U;
constexpr u32 kBldYOffset = 0x0054U;

using Clock = std::chrono::steady_clock;

std::uint64_t elapsedNs(Clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

int32_t readIoSigned16(const Memory& memory, u32 offset) {
    return static_cast<int32_t>(static_cast<std::int16_t>(memory.readIo16(offset)));
}

int32_t readIoSignedAffineRef28(const Memory& memory, u32 offset) {
    const u32 lo = memory.readIo16(offset);
    const u32 hi = memory.readIo16(offset + 2U);
    const u32 value = lo | (hi << 16U);
    const u32 masked = value & 0x0FFFFFFFU;
    const u32 signExtended = (masked & 0x08000000U) != 0U
        ? (masked | 0xF0000000U)
        : masked;
    return static_cast<int32_t>(signExtended);
}

int32_t wrapCoordinate(int32_t value, int32_t size) {
    if (size <= 0) {
        return 0;
    }
    int32_t wrapped = value % size;
    if (wrapped < 0) {
        wrapped += size;
    }
    return wrapped;
}

u32 textBgBlocksWide(u32 sizeIndex);
u32 textBgBlocksHigh(u32 sizeIndex);
u32 textBgScreenWidth(u32 sizeIndex);
u32 textBgScreenHeight(u32 sizeIndex);
u32 textBgScreenBlockIndex(u32 sizeIndex, u32 tileX, u32 tileY);

std::size_t mode0ScreenBlockOffset(u32 sizeIndex, u32 tileX, u32 tileY) {
    return static_cast<std::size_t>(textBgScreenBlockIndex(sizeIndex, tileX, tileY) * 0x800U);
}

bool decodeObjSize(u8 shape, u8 size, int& width, int& height) {
    switch (shape) {
    case 0: { // square
        static constexpr int kSquare[] = {8, 16, 32, 64};
        width = kSquare[size & 0x3U];
        height = kSquare[size & 0x3U];
        return true;
    }
    case 1: // horizontal
        switch (size & 0x3U) {
        case 0:
            width = 16;
            height = 8;
            return true;
        case 1:
            width = 32;
            height = 8;
            return true;
        case 2:
            width = 32;
            height = 16;
            return true;
        case 3:
            width = 64;
            height = 32;
            return true;
        default:
            return false;
        }
    case 2: // vertical
        switch (size & 0x3U) {
        case 0:
            width = 8;
            height = 16;
            return true;
        case 1:
            width = 8;
            height = 32;
            return true;
        case 2:
            width = 16;
            height = 32;
            return true;
        case 3:
            width = 32;
            height = 64;
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

bool resolveObjTileNumber(u16 displayMode, u16 attr2, bool color256, u32& tileNumber, u32& totalObjBlocks) {
    tileNumber = static_cast<u32>(attr2 & 0x03FFU);
    totalObjBlocks = displayMode >= 3U ? 512U : 1024U;
    if (displayMode >= 3U) {
        if (tileNumber < 0x0200U) {
            return false;
        }
        tileNumber -= 0x0200U;
    }
    if (color256) {
        tileNumber &= ~1U;
    }
    tileNumber %= totalObjBlocks;
    return true;
}

std::size_t resolveObj2DTexelOffset(
    u32 tileBase,
    u32 totalObjBlocks,
    bool color256,
    int tileX,
    int tileY,
    int inTileX,
    int inTileY
) {
    const u32 blockStrideX = color256 ? 2U : 1U;
    // GBA 2D OBJ mapping: the tile-slot grid is always 32 slots wide, regardless of bpp.
    // Per GBATek: TileNo = Base + (LY/8)*32 + (LX/8)*blockStrideX
    // For 8bpp the horizontal step is 2, but the row advance is still 32 (not 64).
    constexpr u32 rowStrideBlocks = 32U;
    const u32 blockNumber = (tileBase
        + static_cast<u32>(tileY) * rowStrideBlocks
        + static_cast<u32>(tileX) * blockStrideX) % totalObjBlocks;
    const u32 texelByte = color256
        ? static_cast<u32>(inTileY) * 8U + static_cast<u32>(inTileX)
        : static_cast<u32>(inTileY) * 4U + static_cast<u32>(inTileX / 2);
    return static_cast<std::size_t>(blockNumber) * 32U + static_cast<std::size_t>(texelByte);
}

bool ppuObjLoggingEnabled() {
    static const bool enabled = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_PPU_OBJ");
    return enabled;
}

bool ppuBgLoggingEnabled() {
    static const bool enabled = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_PPU_BG");
    return enabled;
}

bool ppuBgPipelineLoggingEnabled() {
    static const bool enabled = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_BG_PIPELINE");
    return enabled;
}

bool ppuFrameRegsLoggingEnabled() {
    static const bool enabled = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_FRAME_REGS");
    return enabled;
}

void logFrameRegisters(
    int frameMode,
    u16 dispcnt,
    const std::array<u16, 4>& bgCnt,
    const std::array<u16, 4>& bgHofs,
    const std::array<u16, 4>& bgVofs,
    u16 bldCnt,
    u16 bldAlpha,
    u16 bldY,
    u16 winIn,
    u16 winOut,
    u16 win0H, u16 win0V,
    u16 win1H, u16 win1V
) {
    if (!ppuFrameRegsLoggingEnabled()) {
        return;
    }
    static int frameCount = 0;
    static const int logEveryNFrames = []() {
        const auto val = gb::readEnvironmentVariable("GBEMU_GBA_LOG_FRAME_INTERVAL");
        if (!val.has_value() || val->empty()) { return 1; }
        try { return std::max(1, std::stoi(*val)); } catch (...) { return 1; }
    }();
    if ((frameCount % logEveryNFrames) != 0) {
        ++frameCount;
        return;
    }
    ++frameCount;

    const bool obj1D    = (dispcnt & 0x0040U) != 0U;
    const bool objEn    = (dispcnt & 0x1000U) != 0U;
    const bool win0En   = (dispcnt & 0x2000U) != 0U;
    const bool win1En   = (dispcnt & 0x4000U) != 0U;
    const bool objWinEn = (dispcnt & 0x8000U) != 0U;
    const bool forcedBlk = (dispcnt & 0x0080U) != 0U;
    const u8 blendMode  = static_cast<u8>((bldCnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(bldAlpha & 0x1FU);
    const u8 evb = static_cast<u8>((bldAlpha >> 8U) & 0x1FU);
    const u8 evy = static_cast<u8>(bldY & 0x1FU);

    std::cerr
        << "[GBA][PPU][FRAME] frame#" << (frameCount - 1)
        << " mode=" << frameMode
        << std::hex
        << " DISPCNT=0x" << dispcnt
        << std::dec
        << " forcedBlank=" << forcedBlk
        << " objEn=" << objEn
        << " objMap=" << (obj1D ? "1D" : "2D")
        << " win0=" << win0En << " win1=" << win1En << " objWin=" << objWinEn
        << "\n";
    for (int bg = 0; bg < 4; ++bg) {
        const u16 cnt = bgCnt[static_cast<std::size_t>(bg)];
        const bool enabled = (dispcnt & static_cast<u16>(0x0100U << static_cast<unsigned>(bg))) != 0U;
        const u8 prio  = static_cast<u8>(cnt & 0x3U);
        const u32 charBlk = static_cast<u32>((cnt >> 2U) & 0x3U);
        const bool is256   = (cnt & 0x0080U) != 0U;
        const u32 scrBlk   = static_cast<u32>((cnt >> 8U) & 0x1FU);
        const u32 sizeIdx  = static_cast<u32>((cnt >> 14U) & 0x3U);
        const u16 hofs = bgHofs[static_cast<std::size_t>(bg)] & 0x01FFU;
        const u16 vofs = bgVofs[static_cast<std::size_t>(bg)] & 0x01FFU;
        std::cerr
            << "[GBA][PPU][FRAME]   BG" << bg
            << std::hex << " BGxCNT=0x" << cnt << std::dec
            << " en=" << enabled
            << " prio=" << static_cast<unsigned>(prio)
            << " charBlk=" << charBlk
            << " scrBlk=" << scrBlk
            << " size=" << sizeIdx
            << " bpp=" << (is256 ? 8 : 4)
            << " hofs=" << hofs
            << " vofs=" << vofs
            << "\n";
    }
    // Decode BLDCNT first/second target bits for at-a-glance diagnostics.
    // Bit order: BG0 BG1 BG2 BG3 OBJ BD (bits 0-5 for first, 8-13 for second).
    static constexpr const char* kLayerNames[] = {"BG0","BG1","BG2","BG3","OBJ","BD"};
    auto buildTargetStr = [&](int startBit) -> std::string {
        std::string s;
        for (int i = 0; i < 6; ++i) {
            if ((bldCnt & static_cast<u16>(1U << static_cast<unsigned>(startBit + i))) != 0U) {
                if (!s.empty()) s += '+';
                s += kLayerNames[i];
            }
        }
        return s.empty() ? "none" : s;
    };
    std::cerr
        << "[GBA][PPU][FRAME] "
        << std::hex
        << " BLDCNT=0x" << bldCnt
        << " BLDALPHA=0x" << bldAlpha
        << " BLDY=0x" << bldY
        << std::dec
        << " blendMode=" << static_cast<unsigned>(blendMode)
        << " EVA=" << static_cast<unsigned>(eva)
        << " EVB=" << static_cast<unsigned>(evb)
        << " EVY=" << static_cast<unsigned>(evy)
        << " 1st=" << buildTargetStr(0)
        << " 2nd=" << buildTargetStr(8)
        << "\n";
    if (win0En || win1En || objWinEn) {
        std::cerr
            << "[GBA][PPU][FRAME] "
            << std::hex
            << " WININ=0x" << winIn
            << " WINOUT=0x" << winOut
            << " WIN0H=0x" << win0H
            << " WIN0V=0x" << win0V
            << " WIN1H=0x" << win1H
            << " WIN1V=0x" << win1V
            << std::dec
            << "\n";
    }
}

bool sceneCompareLoggingEnabled() {
    static const bool enabled = gb::hasEnvironmentVariable("GBEMU_GBA_LOG_SCENE_CMP");
    return enabled;
}

struct SceneState {
    u16 dispcnt = 0;
    std::array<u16, 4> bgCnt{};
    std::array<u16, 4> hofsMin{}, hofsMax{};
    std::array<u16, 4> vofsMin{}, vofsMax{};
    u16 bldCnt = 0;
    u16 bldAlpha = 0;
    u16 bldY = 0;
    u16 winIn = 0;
    u16 winOut = 0;
    u16 win0H = 0, win0V = 0;
    u16 win1H = 0, win1V = 0;
    int dispcntChanges = 0;
    std::array<int, 4> bgCntChanges{};
};

// Defined later — forward-declare the helpers used by the log.
u32 textBgScreenWidth(u32 sizeIndex);
u32 textBgScreenHeight(u32 sizeIndex);

// Scene-compare state lives at file scope; the actual log function is Ppu::logSceneCompare().
static int sceneFrameNumber = 0;
static SceneState prevSceneState{};
static bool hasPrevScene = false;

int readIntEnvironmentOrDefault(const char* name, int fallback) {
    const auto value = gb::readEnvironmentVariable(name);
    if (!value.has_value() || value->empty()) {
        return fallback;
    }
    try {
        return std::stoi(*value);
    } catch (...) {
        return fallback;
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

u32 textBgBlocksWide(u32 sizeIndex) {
    return (sizeIndex == 1U || sizeIndex == 3U) ? 2U : 1U;
}

u32 textBgBlocksHigh(u32 sizeIndex) {
    return (sizeIndex == 2U || sizeIndex == 3U) ? 2U : 1U;
}

u32 textBgScreenWidth(u32 sizeIndex) {
    return textBgBlocksWide(sizeIndex) * 256U;
}

u32 textBgScreenHeight(u32 sizeIndex) {
    return textBgBlocksHigh(sizeIndex) * 256U;
}

u32 textBgScreenBlockIndex(u32 sizeIndex, u32 tileX, u32 tileY) {
    const u32 blockX = tileX / 32U;
    const u32 blockY = tileY / 32U;
    assert(blockX < textBgBlocksWide(sizeIndex));
    assert(blockY < textBgBlocksHigh(sizeIndex));
    if (sizeIndex == 1U) {
        return blockX;
    }
    if (sizeIndex == 2U) {
        return blockY;
    }
    if (sizeIndex == 3U) {
        return blockY * 2U + blockX;
    }
    return 0U;
}

int ppuBgLogLayerFilter() {
    static const int filter = readIntEnvironmentOrDefault("GBEMU_GBA_LOG_PPU_BG_LAYER", -1);
    return filter;
}

int ppuBgLogXFilter() {
    static const int filter = readIntEnvironmentOrDefault("GBEMU_GBA_LOG_PPU_BG_X", -1);
    return filter;
}

int ppuBgLogYFilter() {
    static const int filter = readIntEnvironmentOrDefault("GBEMU_GBA_LOG_PPU_BG_Y", -1);
    return filter;
}

void logObjDecodeSample(
    int obj,
    int screenX,
    int screenY,
    u16 dispcnt,
    u16 attr0,
    u16 attr1,
    u16 attr2,
    bool obj1D,
    bool color256,
    u32 tileBase,
    u32 totalObjBlocks,
    int tileX,
    int tileY,
    std::size_t texelOffset,
    u8 colorIndex,
    u8 paletteBank,
    u8 priority
) {
    if (!ppuObjLoggingEnabled()) {
        return;
    }
    static int emitted = 0;
    if (emitted >= 48) {
        if (emitted == 48) {
            std::cerr << "[GBA][PPU][OBJ] log limit reached\n";
            ++emitted;
        }
        return;
    }
    ++emitted;
    std::cerr << "[GBA][PPU][OBJ] obj#" << obj
              << " xy=(" << screenX << "," << screenY << ")"
              << " dispcnt=0x" << std::hex << dispcnt
              << " attr0=0x" << attr0
              << " attr1=0x" << attr1
              << " attr2=0x" << attr2
              << std::dec
              << " map=" << (obj1D ? "1D" : "2D")
              << " bpp=" << (color256 ? 8 : 4)
              << " tileBase=" << tileBase
              << " totalBlocks=" << totalObjBlocks
              << " tileXY=(" << tileX << "," << tileY << ")"
              << " texelOff=0x" << std::hex << texelOffset
              << std::dec
              << " color=" << static_cast<unsigned>(colorIndex)
              << " palBank=" << static_cast<unsigned>(paletteBank)
              << " prio=" << static_cast<unsigned>(priority)
              << "\n";
}

void logBgDecodeSample(const Ppu::TextBgDebugSample& sample) {
    if (!ppuBgLoggingEnabled()) {
        return;
    }
    if (ppuBgLogLayerFilter() >= 0 && sample.bgIndex != ppuBgLogLayerFilter()) {
        return;
    }
    if (ppuBgLogXFilter() >= 0 && sample.screenX != ppuBgLogXFilter()) {
        return;
    }
    if (ppuBgLogYFilter() >= 0 && sample.screenY != ppuBgLogYFilter()) {
        return;
    }
    static int emitted = 0;
    if (emitted >= 48) {
        if (emitted == 48) {
            std::cerr << "[GBA][PPU][BG] log limit reached\n";
            ++emitted;
        }
        return;
    }
    ++emitted;
    std::cerr << "[GBA][PPU][BG] bg" << sample.bgIndex
              << " xy=(" << sample.screenX << "," << sample.screenY << ")"
              << " src=(" << sample.sourceX << "," << sample.sourceY << ")"
              << " tile=(" << sample.tileX << "," << sample.tileY << ")"
              << " px=(" << static_cast<unsigned>(sample.pixelX) << "," << static_cast<unsigned>(sample.pixelY) << ")"
              << " block=(" << static_cast<unsigned>(sample.blockX) << "," << static_cast<unsigned>(sample.blockY) << ")"
              << " screenBlock=" << static_cast<unsigned>(sample.screenBlock)
              << " dispcnt=0x" << std::hex << sample.dispcnt
              << " bgcnt=0x" << sample.bgcnt
              << " mapAddr=0x" << sample.mapAddress
              << " mapEntry=0x" << sample.mapEntry
              << " charBase=0x" << sample.charBase
              << " screenBase=0x" << sample.screenBase
              << " tileAddr=0x" << sample.tileAddress
              << std::dec
              << " tile=" << sample.tileNumber
              << " hflip=" << static_cast<unsigned>(sample.hflip)
              << " vflip=" << static_cast<unsigned>(sample.vflip)
              << " palBank=" << static_cast<unsigned>(sample.paletteBank)
              << " color=" << static_cast<unsigned>(sample.colorIndex)
              << " prio=" << static_cast<unsigned>(sample.priority)
              << " bpp=" << (sample.color256 ? 8 : 4)
              << " visible=" << static_cast<unsigned>(sample.visible)
              << "\n";
}

void logSelectedObjectSummary(
    int obj,
    int x,
    int y,
    int width,
    int height,
    u16 dispcnt,
    u16 attr0,
    u16 attr1,
    u16 attr2,
    bool affine,
    bool doubleSize,
    bool color256,
    bool obj1D,
    u8 objMode,
    u8 paletteBank,
    u8 priority
) {
    std::cerr << "[GBA][PPU][OBJSEL] obj#" << obj
              << " pos=(" << x << "," << y << ")"
              << " size=" << width << "x" << height
              << " dispcnt=0x" << std::hex << dispcnt
              << " attr0=0x" << attr0
              << " attr1=0x" << attr1
              << " attr2=0x" << attr2
              << std::dec
              << " affine=" << static_cast<unsigned>(affine)
              << " double=" << static_cast<unsigned>(doubleSize)
              << " mode=" << static_cast<unsigned>(objMode)
              << " map=" << (obj1D ? "1D" : "2D")
              << " bpp=" << (color256 ? 8 : 4)
              << " tile=" << static_cast<unsigned>(attr2 & 0x03FFU)
              << " palBank=" << static_cast<unsigned>(paletteBank)
              << " prio=" << static_cast<unsigned>(priority)
              << "\n";
}

void logObjectVisibleOnScanline(
    int scanline,
    int obj,
    int x,
    int y,
    int width,
    int height,
    u16 attr0,
    u16 attr1,
    u16 attr2,
    bool color256,
    bool obj1D,
    u8 paletteBank,
    u8 priority
) {
    std::cerr << "[GBA][PPU][SCANLINE] y=" << scanline
              << " obj#" << obj
              << " pos=(" << x << "," << y << ")"
              << " size=" << width << "x" << height
              << " attr0=0x" << std::hex << attr0
              << " attr1=0x" << attr1
              << " attr2=0x" << attr2
              << std::dec
              << " map=" << (obj1D ? "1D" : "2D")
              << " bpp=" << (color256 ? 8 : 4)
              << " tile=" << static_cast<unsigned>(attr2 & 0x03FFU)
              << " palBank=" << static_cast<unsigned>(paletteBank)
              << " prio=" << static_cast<unsigned>(priority)
              << "\n";
}

struct ObjRenderEntry {
    int objIndex = 0;
    int x = 0;
    int y = 0;
    int baseWidth = 0;
    int baseHeight = 0;
    int renderWidth = 0;
    int renderHeight = 0;
    int clippedStartX = 0;
    int clippedEndX = 0;
    int clippedStartY = 0;
    int clippedEndY = 0;
    int tilesPerRow1D = 0;
    std::int32_t pa = 0;
    std::int32_t pb = 0;
    std::int32_t pc = 0;
    std::int32_t pd = 0;
    u16 attr0 = 0;
    u16 attr1 = 0;
    u16 attr2 = 0;
    u8 objMode = 0;
    u8 objPriority = 0;
    u8 paletteBank = 0;
    bool affine = false;
    bool doubleSize = false;
    bool color256 = false;
    bool mosaicEnabled = false;
    bool hflip = false;
    bool vflip = false;
};

int mosaicSpan(u16 mosaic, unsigned shift) {
    return 1 + static_cast<int>((mosaic >> shift) & 0x0FU);
}

int mosaicSampleCoord(int coord, int span) {
    if (span <= 1) {
        return coord;
    }
    return coord - (coord % span);
}

void fillScanline(std::array<gb::u16, gb::gba::Ppu::FramebufferSize>& framebuffer, int y, gb::u16 color) {
    const std::size_t lineStart = static_cast<std::size_t>(y) * static_cast<std::size_t>(gb::gba::Ppu::ScreenWidth);
    const std::size_t lineEnd = lineStart + static_cast<std::size_t>(gb::gba::Ppu::ScreenWidth);
    std::fill(framebuffer.begin() + static_cast<std::ptrdiff_t>(lineStart), framebuffer.begin() + static_cast<std::ptrdiff_t>(lineEnd), color);
}

void resolveObjRenderConfig(u16 dispcnt, u16& mode, std::size_t& objTileBase, bool& obj1D) {
    mode = static_cast<u16>(dispcnt & kDisplayModeMask);
    objTileBase = mode >= 3U ? kObjTileBaseMode345Offset : kObjTileBaseMode012Offset;
    obj1D = (dispcnt & kObjMapping1dMask) != 0U;
}

} // namespace

void Ppu::connectMemory(Memory* memory) {
    memory_ = memory;
}

void Ppu::reset() {
    scanlineCycles_ = 0;
    scanline_ = 0;
    prevVblank_ = false;
    prevHblank_ = false;
    prevVcounterMatch_ = false;
    completedMemorySnapshotValid_ = false;
    clearRasterLineSnapshots();
    completedRasterLineSnapshots_.fill(RasterLineSnapshot{});
    clearAffineLineSnapshots();
    completedBg2LineSnapshots_.fill(AffineLineSnapshot{});
    completedBg3LineSnapshots_.fill(AffineLineSnapshot{});
    completedVram_.fill(0U);
    completedPram_.fill(0U);
    completedOam_.fill(0U);
    captureRasterLineSnapshot(0);
    captureAffineLineSnapshot(0);
    updateIoRegisters();
}

void Ppu::step(int cpuCycles) {
    if (memory_ == nullptr || cpuCycles <= 0) {
        return;
    }

    const std::uint32_t delta = static_cast<std::uint32_t>(cpuCycles);
    if (scanlineCycles_ < HblankStartCycle) {
        const std::uint32_t nextCycles = scanlineCycles_ + delta;
        if (nextCycles < HblankStartCycle) {
            scanlineCycles_ = nextCycles;
            return;
        }
        if (nextCycles < CyclesPerLine) {
            scanlineCycles_ = nextCycles;
            updateIoRegisters();
            return;
        }
    } else {
        const std::uint32_t nextCycles = scanlineCycles_ + delta;
        if (nextCycles < CyclesPerLine) {
            scanlineCycles_ = nextCycles;
            return;
        }
    }

    std::uint32_t remainingCycles = static_cast<std::uint32_t>(cpuCycles);
    while (remainingCycles > 0U) {
        if (scanlineCycles_ < HblankStartCycle) {
            const std::uint32_t toHblank = HblankStartCycle - scanlineCycles_;
            if (remainingCycles < toHblank) {
                scanlineCycles_ += remainingCycles;
                break;
            }

            scanlineCycles_ = HblankStartCycle;
            remainingCycles -= toHblank;
            updateIoRegisters();
            continue;
        }

        const std::uint32_t toLineEnd = CyclesPerLine - scanlineCycles_;
        if (remainingCycles < toLineEnd) {
            scanlineCycles_ += remainingCycles;
            break;
        }

        scanlineCycles_ = 0;
        remainingCycles -= toLineEnd;
        scanline_ = static_cast<std::uint16_t>((scanline_ + 1U) % TotalLines);
        if (scanline_ == 0U) {
            completedRasterLineSnapshots_ = rasterLineSnapshots_;
            completedBg2LineSnapshots_ = bg2LineSnapshots_;
            completedBg3LineSnapshots_ = bg3LineSnapshots_;
            completedVram_ = memory_->vram();
            completedPram_ = memory_->pram();
            completedOam_ = memory_->oam();
            completedMemorySnapshotValid_ = true;
            clearRasterLineSnapshots();
            clearAffineLineSnapshots();
        }
        if (scanline_ < VisibleLines) {
            captureRasterLineSnapshot(static_cast<int>(scanline_));
            captureAffineLineSnapshot(static_cast<int>(scanline_));
        }
        updateIoRegisters();
    }
}

bool Ppu::render(std::array<u16, FramebufferSize>& framebuffer) const {
    if (memory_ == nullptr) {
        return false;
    }

    activeDebugConfig_ = readDebugConfig();
    lastRenderStats_ = RenderStats{};
    windowMaskCacheEnabled_ = true;
    std::fill(windowMaskCacheLineReady_.begin(), windowMaskCacheLineReady_.end(), false);
    const auto renderStart = Clock::now();

    const RasterLineSnapshot line0 = rasterSnapshotForLine(0);
    const u16 dispcnt = line0.dispcnt;
    const u16 mode = static_cast<u16>(dispcnt & kDisplayModeMask);

    logFrameRegisters(
        static_cast<int>(mode),
        dispcnt,
        line0.bgCnt,
        line0.bgHofs,
        line0.bgVofs,
        line0.bldCnt,
        line0.bldAlpha,
        line0.bldY,
        line0.winIn,
        line0.winOut,
        line0.win0H, line0.win0V,
        line0.win1H, line0.win1V
    );

    logSceneCompare();

    bool rendered = false;
    switch (mode) {
    case 0U:
        rendered = renderMode0(framebuffer);
        break;
    case 1U:
        rendered = renderMode1(framebuffer);
        break;
    case 2U:
        rendered = renderMode2(framebuffer);
        break;
    case 3U:
        rendered = renderMode3(framebuffer);
        break;
    case 4U:
        rendered = renderMode4(framebuffer);
        break;
    case 5U:
        rendered = renderMode5(framebuffer);
        break;
    default:
        rendered = false;
        break;
    }
    windowMaskCacheEnabled_ = false;
    lastRenderStats_.totalNs = elapsedNs(renderStart);
    return rendered;
}

bool Ppu::debugPixel(int x, int y, PixelDebugInfo& out) const {
    out = PixelDebugInfo{};
    if (memory_ == nullptr || x < 0 || x >= ScreenWidth || y < 0 || y >= ScreenHeight) {
        return false;
    }

    windowMaskCacheEnabled_ = false;

    auto& layerPixels = layerScratch_;
    auto& objWindowMask = objWindowScratch_;
    u16 backdropRaw = 0;
    if (!buildDebugLayerPixelsMode012(layerPixels, objWindowMask, backdropRaw)) {
        activeObjWindowMask_ = nullptr;
        return false;
    }

    const RasterLineSnapshot line = rasterSnapshotForLine(y);
    const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
        + static_cast<std::size_t>(x);
    const u8 windowMask = (line.dispcnt & kAnyWindowEnableMask) != 0U ? windowMaskForPixel(x, y, line) : 0x3FU;
    const LayerPixel& pixel = layerPixels[pixelIndex];
    const u16 finalRaw = applyColorEffect(pixel, line, backdropRaw, windowMask);

    out.valid = true;
    out.x = x;
    out.y = y;
    out.dispcnt = line.dispcnt;
    out.bldCnt = line.bldCnt;
    out.bldAlpha = line.bldAlpha;
    out.bldY = line.bldY;
    out.win0H = line.win0H;
    out.win0V = line.win0V;
    out.win1H = line.win1H;
    out.win1V = line.win1V;
    out.winIn = line.winIn;
    out.winOut = line.winOut;
    out.backdropRawColor555 = backdropRaw;
    out.finalRawColor555 = finalRaw;
    out.finalRgb565 = bgr555ToRgb565(finalRaw);
    out.windowMask = windowMask;
    out.topLayer = pixel.layer;
    out.topPriority = pixel.priority;
    out.secondLayer = pixel.secondLayer;
    out.secondPriority = pixel.secondPriority;
    out.hasSecond = pixel.hasSecond;
    out.semiTransparentObj = pixel.semiTransparentObj;
    out.insideWin0 = pointInsideWindowRect(x, y, line.win0H, line.win0V);
    out.insideWin1 = pointInsideWindowRect(x, y, line.win1H, line.win1V);
    out.insideObjWin = objWindowMask[pixelIndex];

    const u16 bldcnt = line.bldCnt;
    out.blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    out.eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(line.bldAlpha & 0x1FU)));
    out.evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((line.bldAlpha >> 8U) & 0x1FU)));
    out.evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(line.bldY & 0x1FU)));
    out.colorEffectEnabledByWindow = (windowMask & 0x20U) != 0U;
    const u8 topLayerBit = blendLayerBitFromLayerId(pixel.layer);
    out.firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;
    out.alphaBlendRequested = out.blendMode == 1U || pixel.semiTransparentObj;

    bool effectiveHasSecond = pixel.hasSecond;
    bool blockedByObj = false;
    u8 effectiveSecondLayer = pixel.secondLayer;
    if (pixel.semiTransparentObj && effectiveHasSecond && effectiveSecondLayer == 5U) {
        effectiveHasSecond = false;
        blockedByObj = true;
    }
    if (!effectiveHasSecond && !blockedByObj && pixel.layer != 4U) {
        effectiveHasSecond = true;
        effectiveSecondLayer = 4U;
    }
    if (effectiveHasSecond) {
        const u8 secondLayerBit = blendLayerBitFromLayerId(effectiveSecondLayer);
        out.secondTarget = (bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U;
    }
    const bool canAlphaBlend = pixel.semiTransparentObj || (out.blendMode == 1U && out.firstTarget);
    out.alphaBlendApplied = out.colorEffectEnabledByWindow && canAlphaBlend && effectiveHasSecond && out.secondTarget;
    out.brightenApplied = out.colorEffectEnabledByWindow && !out.alphaBlendApplied && out.blendMode == 2U && out.firstTarget;
    out.darkenApplied = out.colorEffectEnabledByWindow && !out.alphaBlendApplied && out.blendMode == 3U && out.firstTarget;

    activeObjWindowMask_ = nullptr;
    return true;
}

bool Ppu::debugTextBgSample(int bgIndex, int x, int y, TextBgDebugSample& out) const {
    out = TextBgDebugSample{};
    if (memory_ == nullptr || bgIndex < 0 || bgIndex >= 4 || x < 0 || x >= ScreenWidth || y < 0 || y >= ScreenHeight) {
        return false;
    }

    const RasterLineSnapshot line = rasterSnapshotForLine(y);
    return decodeTextBgSample(line, bgIndex, x, y, out);
}

bool Ppu::buildDebugLayerPixelsMode012(
    std::array<LayerPixel, FramebufferSize>& layerPixels,
    std::array<bool, FramebufferSize>& objWindowMask,
    u16& backdropRaw
) const {
    const RasterLineSnapshot line0 = rasterSnapshotForLine(0);
    const u16 mode = static_cast<u16>(line0.dispcnt & kDisplayModeMask);
    if (mode > 2U) {
        return false;
    }

    backdropRaw = readBgPaletteColor(0);
    const u16 backdrop = bgr555ToRgb565(backdropRaw);
    for (auto& px : layerPixels) {
        px = LayerPixel{
            backdrop,
            backdropRaw,
            0U,
            4U,
            4U,
            4U,
            0U,
            true,
            false,
            false,
        };
    }

    bool objWindowNeeded = false;
    for (int lineIndex = 0; lineIndex < ScreenHeight; ++lineIndex) {
        const RasterLineSnapshot line = rasterSnapshotForLine(lineIndex);
        if (!activeDebugConfig_.disableObj
            && (line.dispcnt & (kObjEnableMask | kObjWinEnableMask)) == (kObjEnableMask | kObjWinEnableMask)) {
            objWindowNeeded = true;
            break;
        }
    }

    if (objWindowNeeded) {
        buildObjWindowMask(objWindowMask);
        activeObjWindowMask_ = &objWindowMask;
    } else {
        activeObjWindowMask_ = nullptr;
    }

    switch (mode) {
    case 0U:
        if (!activeDebugConfig_.disableBg) {
            for (int bg = 0; bg < 4; ++bg) {
                renderTextBackground(bg, layerPixels);
            }
        }
        break;
    case 1U:
        if (!activeDebugConfig_.disableBg) {
            renderTextBackground(0, layerPixels);
            renderTextBackground(1, layerPixels);
            renderAffineBackground(2, layerPixels);
        }
        break;
    case 2U:
        if (!activeDebugConfig_.disableBg) {
            renderAffineBackground(2, layerPixels);
            renderAffineBackground(3, layerPixels);
        }
        break;
    default:
        activeObjWindowMask_ = nullptr;
        return false;
    }

    if (!activeDebugConfig_.disableObj) {
        renderObjects(layerPixels);
    }
    return true;
}

const Ppu::RenderStats& Ppu::lastRenderStats() const {
    return lastRenderStats_;
}

bool Ppu::renderMode0(std::array<u16, FramebufferSize>& framebuffer) const {
    auto& layerPixels = layerScratch_;
    auto& objWindowMask = objWindowScratch_;
    const u16 backdropRaw = readBgPaletteColor(0);
    const u16 backdrop = bgr555ToRgb565(backdropRaw);
    for (auto& px : layerPixels) {
        px = LayerPixel{
            backdrop,
            backdropRaw,
            0U,
            4U,
            4U,
            4U,
            0U,
            true,
            false,
            false,
        };
    }

    bool objWindowNeeded = false;
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if (!activeDebugConfig_.disableObj
            && (line.dispcnt & (kObjEnableMask | kObjWinEnableMask)) == (kObjEnableMask | kObjWinEnableMask)) {
            objWindowNeeded = true;
            break;
        }
    }
    if (objWindowNeeded) {
        buildObjWindowMask(objWindowMask);
        activeObjWindowMask_ = &objWindowMask;
    } else {
        activeObjWindowMask_ = nullptr;
    }
    if (!activeDebugConfig_.disableBg) {
        const auto bgStart = Clock::now();
        for (int bg = 0; bg < 4; ++bg) {
            renderTextBackground(bg, layerPixels);
        }
        lastRenderStats_.bgNs += elapsedNs(bgStart);
    }
    if (!activeDebugConfig_.disableObj) {
        const auto objStart = Clock::now();
        renderObjects(layerPixels);
        lastRenderStats_.objNs += elapsedNs(objStart);
    }

    const auto composeStart = Clock::now();

    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if ((line.dispcnt & kForcedBlankMask) != 0U) {
            fillScanline(framebuffer, y, static_cast<u16>(0xFFFFU));
            continue;
        }
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const u16 bldcnt = line.bldCnt;
        static_cast<void>(bldcnt);
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
            framebuffer[i] = bgr555ToRgb565(applyColorEffect(px, line, backdropRaw, windowMask));
        }
    }
    lastRenderStats_.composeNs += elapsedNs(composeStart);
    activeObjWindowMask_ = nullptr;
    return true;
}

bool Ppu::renderMode1(std::array<u16, FramebufferSize>& framebuffer) const {
    auto& layerPixels = layerScratch_;
    auto& objWindowMask = objWindowScratch_;
    const u16 backdropRaw = readBgPaletteColor(0);
    const u16 backdrop = bgr555ToRgb565(backdropRaw);
    for (auto& px : layerPixels) {
        px = LayerPixel{
            backdrop,
            backdropRaw,
            0U,
            4U,
            4U,
            4U,
            0U,
            true,
            false,
            false,
        };
    }

    bool objWindowNeeded = false;
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if (!activeDebugConfig_.disableObj
            && (line.dispcnt & (kObjEnableMask | kObjWinEnableMask)) == (kObjEnableMask | kObjWinEnableMask)) {
            objWindowNeeded = true;
            break;
        }
    }
    if (objWindowNeeded) {
        buildObjWindowMask(objWindowMask);
        activeObjWindowMask_ = &objWindowMask;
    } else {
        activeObjWindowMask_ = nullptr;
    }
    if (!activeDebugConfig_.disableBg) {
        const auto bgStart = Clock::now();
        renderTextBackground(0, layerPixels);
        renderTextBackground(1, layerPixels);
        renderAffineBackground(2, layerPixels);
        lastRenderStats_.bgNs += elapsedNs(bgStart);
    }
    if (!activeDebugConfig_.disableObj) {
        const auto objStart = Clock::now();
        renderObjects(layerPixels);
        lastRenderStats_.objNs += elapsedNs(objStart);
    }

    const auto composeStart = Clock::now();

    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if ((line.dispcnt & kForcedBlankMask) != 0U) {
            fillScanline(framebuffer, y, static_cast<u16>(0xFFFFU));
            continue;
        }
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const u16 bldcnt = line.bldCnt;
        static_cast<void>(bldcnt);
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
            framebuffer[i] = bgr555ToRgb565(applyColorEffect(px, line, backdropRaw, windowMask));
        }
    }
    lastRenderStats_.composeNs += elapsedNs(composeStart);
    activeObjWindowMask_ = nullptr;
    return true;
}

bool Ppu::renderMode2(std::array<u16, FramebufferSize>& framebuffer) const {
    auto& layerPixels = layerScratch_;
    auto& objWindowMask = objWindowScratch_;
    const u16 backdropRaw = readBgPaletteColor(0);
    const u16 backdrop = bgr555ToRgb565(backdropRaw);
    for (auto& px : layerPixels) {
        px = LayerPixel{
            backdrop,
            backdropRaw,
            0U,
            4U,
            4U,
            4U,
            0U,
            true,
            false,
            false,
        };
    }

    bool objWindowNeeded = false;
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if (!activeDebugConfig_.disableObj
            && (line.dispcnt & (kObjEnableMask | kObjWinEnableMask)) == (kObjEnableMask | kObjWinEnableMask)) {
            objWindowNeeded = true;
            break;
        }
    }
    if (objWindowNeeded) {
        buildObjWindowMask(objWindowMask);
        activeObjWindowMask_ = &objWindowMask;
    } else {
        activeObjWindowMask_ = nullptr;
    }
    if (!activeDebugConfig_.disableBg) {
        const auto bgStart = Clock::now();
        renderAffineBackground(2, layerPixels);
        renderAffineBackground(3, layerPixels);
        lastRenderStats_.bgNs += elapsedNs(bgStart);
    }
    if (!activeDebugConfig_.disableObj) {
        const auto objStart = Clock::now();
        renderObjects(layerPixels);
        lastRenderStats_.objNs += elapsedNs(objStart);
    }

    const auto composeStart = Clock::now();

    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if ((line.dispcnt & kForcedBlankMask) != 0U) {
            fillScanline(framebuffer, y, static_cast<u16>(0xFFFFU));
            continue;
        }
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const u16 bldcnt = line.bldCnt;
        static_cast<void>(bldcnt);
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
            framebuffer[i] = bgr555ToRgb565(applyColorEffect(px, line, backdropRaw, windowMask));
        }
    }
    lastRenderStats_.composeNs += elapsedNs(composeStart);
    activeObjWindowMask_ = nullptr;
    return true;
}

bool Ppu::renderMode3(std::array<u16, FramebufferSize>& framebuffer) const {
    auto& layerPixels = layerScratch_;
    auto& objWindowMask = objWindowScratch_;
    const u16 backdropRaw = readBgPaletteColor(0);
    const u16 backdrop = bgr555ToRgb565(backdropRaw);
    for (auto& px : layerPixels) {
        px = LayerPixel{
            backdrop,
            backdropRaw,
            0U,
            4U,
            4U,
            4U,
            0U,
            true,
            false,
            false,
        };
    }
    bool objWindowNeeded = false;
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if (!activeDebugConfig_.disableObj
            && (line.dispcnt & (kObjEnableMask | kObjWinEnableMask)) == (kObjEnableMask | kObjWinEnableMask)) {
            objWindowNeeded = true;
            break;
        }
    }
    if (objWindowNeeded) {
        buildObjWindowMask(objWindowMask);
        activeObjWindowMask_ = &objWindowMask;
    } else {
        activeObjWindowMask_ = nullptr;
    }
    if (!activeDebugConfig_.disableBg) {
        const auto bgStart = Clock::now();
        for (int y = 0; y < ScreenHeight; ++y) {
            const RasterLineSnapshot line = rasterSnapshotForLine(y);
            const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
            const u8 bgPriority = static_cast<u8>(line.bgCnt[2] & 0x3U);
            const bool mosaicEnabled = (line.bgCnt[2] & kBgMosaicMask) != 0U;
            const int mosaicXSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 0U) : 1;
            const int mosaicYSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 4U) : 1;
            const int sourceY = mosaicEnabled ? mosaicSampleCoord(y, mosaicYSpan) : y;
            for (int x = 0; x < ScreenWidth; ++x) {
                const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(x);
                const u8 winMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
                if (!layerEnabledByWindowMask(winMask, 2U)) {
                    continue;
                }
                const int sourceX = mosaicEnabled ? mosaicSampleCoord(x, mosaicXSpan) : x;
                const auto byteIndex = (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(sourceX)) * 2U;
                composeLayer(layerPixels, pixelIndex, readVram16(byteIndex), bgPriority, 2U);
            }
        }
        lastRenderStats_.bgNs += elapsedNs(bgStart);
    }

    if (!activeDebugConfig_.disableObj) {
        const auto objStart = Clock::now();
        renderObjects(layerPixels);
        lastRenderStats_.objNs += elapsedNs(objStart);
    }
    const auto composeStart = Clock::now();
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if ((line.dispcnt & kForcedBlankMask) != 0U) {
            fillScanline(framebuffer, y, static_cast<u16>(0xFFFFU));
            continue;
        }
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const u16 bldcnt = line.bldCnt;
        static_cast<void>(bldcnt);
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
            framebuffer[i] = bgr555ToRgb565(applyColorEffect(px, line, backdropRaw, windowMask));
        }
    }
    lastRenderStats_.composeNs += elapsedNs(composeStart);
    activeObjWindowMask_ = nullptr;
    return true;
}

bool Ppu::renderMode4(std::array<u16, FramebufferSize>& framebuffer) const {
    auto& layerPixels = layerScratch_;
    auto& objWindowMask = objWindowScratch_;
    const u16 backdropRaw = readBgPaletteColor(0);
    const u16 backdrop = bgr555ToRgb565(backdropRaw);
    for (auto& px : layerPixels) {
        px = LayerPixel{
            backdrop,
            backdropRaw,
            0U,
            4U,
            4U,
            4U,
            0U,
            true,
            false,
            false,
        };
    }

    bool objWindowNeeded = false;
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if (!activeDebugConfig_.disableObj
            && (line.dispcnt & (kObjEnableMask | kObjWinEnableMask)) == (kObjEnableMask | kObjWinEnableMask)) {
            objWindowNeeded = true;
            break;
        }
    }
    if (objWindowNeeded) {
        buildObjWindowMask(objWindowMask);
        activeObjWindowMask_ = &objWindowMask;
    } else {
        activeObjWindowMask_ = nullptr;
    }
    if (!activeDebugConfig_.disableBg) {
        const auto& vram = activeVram();
        const auto bgStart = Clock::now();
        for (int y = 0; y < ScreenHeight; ++y) {
            const RasterLineSnapshot line = rasterSnapshotForLine(y);
            const bool frame1 = (line.dispcnt & kFrameSelectMask) != 0U;
            const std::size_t pageBase = frame1 ? kBitmapPage1Offset : 0U;
            const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
            const u8 bgPriority = static_cast<u8>(line.bgCnt[2] & 0x3U);
            const bool mosaicEnabled = (line.bgCnt[2] & kBgMosaicMask) != 0U;
            const int mosaicXSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 0U) : 1;
            const int mosaicYSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 4U) : 1;
            const int sourceY = mosaicEnabled ? mosaicSampleCoord(y, mosaicYSpan) : y;
            for (int x = 0; x < ScreenWidth; ++x) {
                const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(x);
                const u8 winMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
                if (!layerEnabledByWindowMask(winMask, 2U)) {
                    continue;
                }
                const int sourceX = mosaicEnabled ? mosaicSampleCoord(x, mosaicXSpan) : x;
                const std::size_t byteIndex = pageBase
                    + static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(sourceX);
                const u8 colorIndex = byteIndex < vram.size() ? vram[byteIndex] : 0U;
                composeLayer(layerPixels, pixelIndex, readBgPaletteColor(colorIndex), bgPriority, 2U);
            }
        }
        lastRenderStats_.bgNs += elapsedNs(bgStart);
    }

    if (!activeDebugConfig_.disableObj) {
        const auto objStart = Clock::now();
        renderObjects(layerPixels);
        lastRenderStats_.objNs += elapsedNs(objStart);
    }
    const auto composeStart = Clock::now();
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if ((line.dispcnt & kForcedBlankMask) != 0U) {
            fillScanline(framebuffer, y, static_cast<u16>(0xFFFFU));
            continue;
        }
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const u16 bldcnt = line.bldCnt;
        static_cast<void>(bldcnt);
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
            framebuffer[i] = bgr555ToRgb565(applyColorEffect(px, line, backdropRaw, windowMask));
        }
    }
    lastRenderStats_.composeNs += elapsedNs(composeStart);
    activeObjWindowMask_ = nullptr;
    return true;
}

bool Ppu::renderMode5(std::array<u16, FramebufferSize>& framebuffer) const {
    auto& layerPixels = layerScratch_;
    auto& objWindowMask = objWindowScratch_;
    const u16 backdropRaw = readBgPaletteColor(0);
    const u16 backdrop = bgr555ToRgb565(backdropRaw);
    for (auto& px : layerPixels) {
        px = LayerPixel{
            backdrop,
            backdropRaw,
            0U,
            4U,
            4U,
            4U,
            0U,
            true,
            false,
            false,
        };
    }

    bool objWindowNeeded = false;
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if (!activeDebugConfig_.disableObj
            && (line.dispcnt & (kObjEnableMask | kObjWinEnableMask)) == (kObjEnableMask | kObjWinEnableMask)) {
            objWindowNeeded = true;
            break;
        }
    }
    if (objWindowNeeded) {
        buildObjWindowMask(objWindowMask);
        activeObjWindowMask_ = &objWindowMask;
    } else {
        activeObjWindowMask_ = nullptr;
    }
    constexpr int kMode5Width = 160;
    constexpr int kMode5Height = 128;
    if (!activeDebugConfig_.disableBg) {
        const auto bgStart = Clock::now();
        for (int y = 0; y < kMode5Height; ++y) {
            const RasterLineSnapshot line = rasterSnapshotForLine(y);
            const bool frame1 = (line.dispcnt & kFrameSelectMask) != 0U;
            const std::size_t pageBase = frame1 ? kBitmapPage1Offset : 0U;
            const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
            const u8 bgPriority = static_cast<u8>(line.bgCnt[2] & 0x3U);
            const bool mosaicEnabled = (line.bgCnt[2] & kBgMosaicMask) != 0U;
            const int mosaicXSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 0U) : 1;
            const int mosaicYSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 4U) : 1;
            const int sourceY = mosaicEnabled ? mosaicSampleCoord(y, mosaicYSpan) : y;
            for (int x = 0; x < kMode5Width; ++x) {
                const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(x);
                const u8 winMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
                if (!layerEnabledByWindowMask(winMask, 2U)) {
                    continue;
                }
                const int sourceX = mosaicEnabled ? mosaicSampleCoord(x, mosaicXSpan) : x;
                const auto byteIndex = pageBase
                    + (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(kMode5Width)
                    + static_cast<std::size_t>(sourceX)) * 2U;
                composeLayer(layerPixels, pixelIndex, readVram16(byteIndex), bgPriority, 2U);
            }
        }
        lastRenderStats_.bgNs += elapsedNs(bgStart);
    }

    if (!activeDebugConfig_.disableObj) {
        const auto objStart = Clock::now();
        renderObjects(layerPixels);
        lastRenderStats_.objNs += elapsedNs(objStart);
    }
    const auto composeStart = Clock::now();
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if ((line.dispcnt & kForcedBlankMask) != 0U) {
            fillScanline(framebuffer, y, static_cast<u16>(0xFFFFU));
            continue;
        }
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const u16 bldcnt = line.bldCnt;
        static_cast<void>(bldcnt);
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
            framebuffer[i] = bgr555ToRgb565(applyColorEffect(px, line, backdropRaw, windowMask));
        }
    }
    lastRenderStats_.composeNs += elapsedNs(composeStart);
    activeObjWindowMask_ = nullptr;
    return true;
}

void Ppu::renderTextBackground(int bgIndex, std::array<LayerPixel, FramebufferSize>& layerPixels) const {
    if (bgIndex < 0 || bgIndex >= 4 || !bgEnabledByDebugMask(activeDebugConfig_, bgIndex)) {
        return;
    }

    const auto& vram = activeVram();
    const auto& pram = activePram();
    const bool logBg = ppuBgLoggingEnabled();
    const bool logBgPipeline = ppuBgPipelineLoggingEnabled();
    const bool needDebugSamples = logBg || logBgPipeline;
    const int logLayerFilter = ppuBgLogLayerFilter();
    const int logXFilter = ppuBgLogXFilter();
    const int logYFilter = ppuBgLogYFilter();

    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if ((line.dispcnt & kBgEnableMasks[bgIndex]) == 0U) {
            continue;
        }

        const u16 bgcnt = line.bgCnt[static_cast<std::size_t>(bgIndex)];
        const u16 hofs = static_cast<u16>(line.bgHofs[static_cast<std::size_t>(bgIndex)] & 0x01FFU);
        const u16 vofs = static_cast<u16>(line.bgVofs[static_cast<std::size_t>(bgIndex)] & 0x01FFU);
        const u8 priority = static_cast<u8>(bgcnt & 0x3U);
        const bool color256 = (bgcnt & 0x0080U) != 0U;
        const bool mosaicEnabled = (bgcnt & kBgMosaicMask) != 0U;
        const u32 charBase = static_cast<u32>((bgcnt >> 2U) & 0x3U) * 0x4000U;
        const u32 screenBase = static_cast<u32>((bgcnt >> 8U) & 0x1FU) * 0x800U;
        const u32 sizeIndex = static_cast<u32>((bgcnt >> 14U) & 0x3U);
        const u32 screenWidth = textBgScreenWidth(sizeIndex);
        const u32 screenHeight = textBgScreenHeight(sizeIndex);
        const int mosaicXSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 0U) : 1;
        const int mosaicYSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 4U) : 1;
        const int sourceY = mosaicEnabled ? mosaicSampleCoord(y, mosaicYSpan) : y;
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        u32 cachedTileX = std::numeric_limits<u32>::max();
        u32 cachedTileY = std::numeric_limits<u32>::max();
        u16 cachedTileNumber = 0U;
        bool cachedHflip = false;
        bool cachedVflip = false;
        u8 cachedPaletteBank = 0U;
        bool cachedMapValid = false;

        for (int x = 0; x < ScreenWidth; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
            if (!layerEnabledByWindowMask(windowMask, static_cast<u8>(bgIndex))) {
                continue;
            }

            const int sourceX = mosaicEnabled ? mosaicSampleCoord(x, mosaicXSpan) : x;
            const u32 sx = (static_cast<u32>(sourceX) + static_cast<u32>(hofs)) % screenWidth;
            const u32 sy = (static_cast<u32>(sourceY) + static_cast<u32>(vofs)) % screenHeight;
            const u32 tileX = sx / 8U;
            const u32 tileY = sy / 8U;
            const u8 pixelX = static_cast<u8>(sx & 7U);
            const u8 pixelY = static_cast<u8>(sy & 7U);
            if (tileX != cachedTileX || tileY != cachedTileY) {
                cachedTileX = tileX;
                cachedTileY = tileY;
                const std::size_t mapBase = static_cast<std::size_t>(screenBase)
                    + mode0ScreenBlockOffset(sizeIndex, tileX, tileY);
                const std::size_t mapIndex = static_cast<std::size_t>((tileY % 32U) * 32U + (tileX % 32U));
                const std::size_t mapAddress = mapBase + mapIndex * 2U;
                if (mapAddress + 1U >= vram.size()) {
                    cachedMapValid = false;
                    continue;
                }
                const u16 mapEntry = static_cast<u16>(
                    static_cast<u16>(vram[mapAddress])
                    | static_cast<u16>(static_cast<u16>(vram[mapAddress + 1U]) << 8U)
                );
                cachedTileNumber = static_cast<u16>(mapEntry & 0x03FFU);
                cachedHflip = (mapEntry & 0x0400U) != 0U;
                cachedVflip = (mapEntry & 0x0800U) != 0U;
                cachedPaletteBank = static_cast<u8>((mapEntry >> 12U) & 0x0FU);
                cachedMapValid = true;
            }
            if (!cachedMapValid) {
                continue;
            }

            const u8 tilePx = cachedHflip ? static_cast<u8>(7U - pixelX) : pixelX;
            const u8 tilePy = cachedVflip ? static_cast<u8>(7U - pixelY) : pixelY;

            u8 colorIndex = 0U;
            if (color256) {
                const std::size_t tileAddress = static_cast<std::size_t>(charBase)
                    + static_cast<std::size_t>(cachedTileNumber) * 64U
                    + static_cast<std::size_t>(tilePy) * 8U
                    + static_cast<std::size_t>(tilePx);
                if (tileAddress >= kTextBgCharDataLimit || tileAddress >= vram.size()) {
                    continue;
                }
                colorIndex = vram[tileAddress];
                if (colorIndex == 0U) {
                    continue;
                }
            } else {
                const std::size_t tileAddress = static_cast<std::size_t>(charBase)
                    + static_cast<std::size_t>(cachedTileNumber) * 32U
                    + static_cast<std::size_t>(tilePy) * 4U
                    + static_cast<std::size_t>(tilePx / 2U);
                if (tileAddress >= kTextBgCharDataLimit || tileAddress >= vram.size()) {
                    continue;
                }
                const u8 packed = vram[tileAddress];
                const u8 index4 = (tilePx & 1U) == 0U
                    ? static_cast<u8>(packed & 0x0FU)
                    : static_cast<u8>((packed >> 4U) & 0x0FU);
                if (index4 == 0U) {
                    continue;
                }
                colorIndex = static_cast<u8>(cachedPaletteBank * 16U + index4);
            }

            const std::size_t paletteByteIndex = static_cast<std::size_t>(colorIndex) * 2U;
            if (paletteByteIndex + 1U >= pram.size()) {
                continue;
            }
            const u16 rawColor555 = static_cast<u16>(
                static_cast<u16>(pram[paletteByteIndex])
                | static_cast<u16>(static_cast<u16>(pram[paletteByteIndex + 1U]) << 8U)
            );

            if (needDebugSamples) {
                const bool selectedByFilter = (logLayerFilter < 0 || logLayerFilter == bgIndex)
                    && (logXFilter < 0 || logXFilter == x)
                    && (logYFilter < 0 || logYFilter == y);
                const bool anyExplicitFilter = logLayerFilter >= 0 || logXFilter >= 0 || logYFilter >= 0;
                const bool earlyProbe = !anyExplicitFilter && (x < 4 && y < 2);
                if (selectedByFilter || earlyProbe) {
                    TextBgDebugSample sample{};
                    if (decodeTextBgSample(line, bgIndex, x, y, sample) && sample.visible && logBg) {
                        logBgDecodeSample(sample);
                    }
                }
            }

            composeLayer(
                layerPixels,
                pixelIndex,
                rawColor555,
                priority,
                static_cast<u8>(bgIndex)
            );
        }
    }
}

bool Ppu::decodeTextBgSample(
    const RasterLineSnapshot& line,
    int bgIndex,
    int screenX,
    int screenY,
    TextBgDebugSample& out
) const {
    out = TextBgDebugSample{};
    if (memory_ == nullptr || bgIndex < 0 || bgIndex >= 4 || screenX < 0 || screenX >= ScreenWidth || screenY < 0 || screenY >= ScreenHeight) {
        return false;
    }

    const u16 bgcnt = line.bgCnt[static_cast<std::size_t>(bgIndex)];
    const u16 hofs = static_cast<u16>(line.bgHofs[static_cast<std::size_t>(bgIndex)] & 0x01FFU);
    const u16 vofs = static_cast<u16>(line.bgVofs[static_cast<std::size_t>(bgIndex)] & 0x01FFU);
    const u8 priority = static_cast<u8>(bgcnt & 0x3U);
    const bool color256 = (bgcnt & 0x0080U) != 0U;
    const bool mosaicEnabled = (bgcnt & kBgMosaicMask) != 0U;
    const u32 charBase = static_cast<u32>((bgcnt >> 2U) & 0x3U) * 0x4000U;
    const u32 screenBase = static_cast<u32>((bgcnt >> 8U) & 0x1FU) * 0x800U;
    const u32 sizeIndex = static_cast<u32>((bgcnt >> 14U) & 0x3U);
    const u32 screenWidth = textBgScreenWidth(sizeIndex);
    const u32 screenHeight = textBgScreenHeight(sizeIndex);
    const int mosaicXSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 0U) : 1;
    const int mosaicYSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 4U) : 1;
    const int sourceX = mosaicEnabled ? mosaicSampleCoord(screenX, mosaicXSpan) : screenX;
    const int sourceY = mosaicEnabled ? mosaicSampleCoord(screenY, mosaicYSpan) : screenY;
    const u32 sx = (static_cast<u32>(sourceX) + static_cast<u32>(hofs)) % screenWidth;
    const u32 sy = (static_cast<u32>(sourceY) + static_cast<u32>(vofs)) % screenHeight;
    const u32 tileX = sx / 8U;
    const u32 tileY = sy / 8U;
    const u8 pixelX = static_cast<u8>(sx & 7U);
    const u8 pixelY = static_cast<u8>(sy & 7U);
    const u8 blockX = static_cast<u8>(tileX / 32U);
    const u8 blockY = static_cast<u8>(tileY / 32U);
    const u8 screenBlock = static_cast<u8>(textBgScreenBlockIndex(sizeIndex, tileX, tileY));
    const std::size_t mapBase = static_cast<std::size_t>(screenBase) + mode0ScreenBlockOffset(sizeIndex, tileX, tileY);
    const std::size_t mapIndex = static_cast<std::size_t>((tileY % 32U) * 32U + (tileX % 32U));
    const std::size_t mapAddress = mapBase + mapIndex * 2U;
    const u16 mapEntry = readBgVram16(mapAddress);
    const u16 tileNumber = static_cast<u16>(mapEntry & 0x03FFU);
    const bool hflip = (mapEntry & 0x0400U) != 0U;
    const bool vflip = (mapEntry & 0x0800U) != 0U;
    const u8 paletteBank = static_cast<u8>((mapEntry >> 12U) & 0x0FU);
    const u8 tilePx = hflip ? static_cast<u8>(7U - pixelX) : pixelX;
    const u8 tilePy = vflip ? static_cast<u8>(7U - pixelY) : pixelY;

    out.valid = true;
    out.bgIndex = bgIndex;
    out.screenX = screenX;
    out.screenY = screenY;
    out.dispcnt = line.dispcnt;
    out.bgcnt = bgcnt;
    out.hofs = hofs;
    out.vofs = vofs;
    out.priority = priority;
    out.color256 = color256;
    out.sizeIndex = static_cast<u8>(sizeIndex);
    out.charBase = charBase;
    out.screenBase = screenBase;
    out.screenWidth = screenWidth;
    out.screenHeight = screenHeight;
    out.sourceX = static_cast<u32>(sourceX);
    out.sourceY = static_cast<u32>(sourceY);
    out.tileX = tileX;
    out.tileY = tileY;
    out.pixelX = pixelX;
    out.pixelY = pixelY;
    out.blockX = blockX;
    out.blockY = blockY;
    out.screenBlock = screenBlock;
    out.mapAddress = static_cast<u32>(mapAddress & 0xFFFFU);
    out.mapEntry = mapEntry;
    out.tileNumber = tileNumber;
    out.hflip = hflip;
    out.vflip = vflip;
    out.paletteBank = paletteBank;

    if (color256) {
        const std::size_t tileAddress = static_cast<std::size_t>(charBase)
            + static_cast<std::size_t>(tileNumber) * 64U
            + static_cast<std::size_t>(tilePy) * 8U
            + static_cast<std::size_t>(tilePx);
        out.tileAddress = static_cast<u32>(tileAddress);
        if (tileAddress >= kTextBgCharDataLimit) {
            out.colorIndex = 0U;
            out.visible = false;
        } else {
            out.colorIndex = readBgVram8(tileAddress);
            out.visible = out.colorIndex != 0U;
        }
    } else {
        const std::size_t tileAddress = static_cast<std::size_t>(charBase)
            + static_cast<std::size_t>(tileNumber) * 32U
            + static_cast<std::size_t>(tilePy) * 4U
            + static_cast<std::size_t>(tilePx / 2U);
        out.tileAddress = static_cast<u32>(tileAddress);
        if (tileAddress >= kTextBgCharDataLimit) {
            out.visible = false;
            out.colorIndex = 0U;
        } else {
            const u8 packed = readBgVram8(tileAddress);
            const u8 index4 = (tilePx & 1U) == 0U
                ? static_cast<u8>(packed & 0x0FU)
                : static_cast<u8>((packed >> 4U) & 0x0FU);
            out.visible = index4 != 0U;
            out.colorIndex = out.visible ? static_cast<u8>(paletteBank * 16U + index4) : 0U;
        }
    }

    if ((line.dispcnt & kBgEnableMasks[bgIndex]) == 0U) {
        out.visible = false;
    }

    if (ppuBgPipelineLoggingEnabled()) {
        static int emitted = 0;
        static const int logLimit = []() {
            return std::max(1, readIntEnvironmentBase0OrDefault("GBEMU_GBA_LOG_BG_PIPELINE_LIMIT", 128));
        }();
        const int layerFilter = ppuBgLogLayerFilter();
        const int xFilter = ppuBgLogXFilter();
        const int yFilter = ppuBgLogYFilter();
        const bool selectedByFilter = (layerFilter < 0 || layerFilter == bgIndex)
            && (xFilter < 0 || xFilter == screenX)
            && (yFilter < 0 || yFilter == screenY);
        const bool anyExplicitFilter = layerFilter >= 0 || xFilter >= 0 || yFilter >= 0;
        const bool earlyProbe = !anyExplicitFilter && (screenX < 4 && screenY < 2);
        if ((selectedByFilter || earlyProbe) && emitted < logLimit) {
            ++emitted;
            std::cerr
                << "[GBA][PPU][BGPIPE] path=renderTextBackground->decodeTextBgSample"
                << " bg=" << bgIndex
                << " mode=" << static_cast<unsigned>(line.dispcnt & kDisplayModeMask)
                << " xy=(" << screenX << "," << screenY << ")"
                << " src=(" << sourceX << "," << sourceY << ")"
                << " scroll=(" << hofs << "," << vofs << ")"
                << " size=" << static_cast<unsigned>(sizeIndex)
                << " bpp=" << (color256 ? 8 : 4)
                << " tile=(" << tileX << "," << tileY << ")"
                << " quadrant=(" << static_cast<unsigned>(blockX) << "," << static_cast<unsigned>(blockY) << ")"
                << " screenBlock=" << static_cast<unsigned>(screenBlock)
                << std::hex
                << " charBase=0x" << charBase
                << " screenBase=0x" << screenBase
                << " mapBase=0x" << mapBase
                << " mapAddr=0x" << mapAddress
                << " mapEntry=0x" << mapEntry
                << " tileAddr=0x" << out.tileAddress
                << std::dec
                << " mapIndex=" << mapIndex
                << " tileNo=" << tileNumber
                << " hflip=" << static_cast<unsigned>(hflip)
                << " vflip=" << static_cast<unsigned>(vflip)
                << " pal=" << static_cast<unsigned>(paletteBank)
                << " color=" << static_cast<unsigned>(out.colorIndex)
                << " vis=" << static_cast<unsigned>(out.visible)
                << " bgcnt={" << std::hex
                << "0x" << line.bgCnt[0] << ",0x" << line.bgCnt[1] << ",0x" << line.bgCnt[2] << ",0x" << line.bgCnt[3]
                << "}"
                << " hofs={" << (line.bgHofs[0] & 0x01FFU) << "," << (line.bgHofs[1] & 0x01FFU)
                << "," << (line.bgHofs[2] & 0x01FFU) << "," << (line.bgHofs[3] & 0x01FFU)
                << "}"
                << " vofs={" << (line.bgVofs[0] & 0x01FFU) << "," << (line.bgVofs[1] & 0x01FFU)
                << "," << (line.bgVofs[2] & 0x01FFU) << "," << (line.bgVofs[3] & 0x01FFU)
                << "}"
                << std::dec
                << "\n";
        } else if (emitted == logLimit) {
            std::cerr << "[GBA][PPU][BGPIPE] log limit reached\n";
            ++emitted;
        }
    }
    return true;
}

void Ppu::renderAffineBackground(int bgIndex, std::array<LayerPixel, FramebufferSize>& layerPixels) const {
    if (bgIndex < 2 || bgIndex > 3 || !bgEnabledByDebugMask(activeDebugConfig_, bgIndex)) {
        return;
    }

    bool anyVisibleMosaic = false;
    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        const u16 bgcnt = line.bgCnt[static_cast<std::size_t>(bgIndex)];
        if ((line.dispcnt & kBgEnableMasks[bgIndex]) != 0U && (bgcnt & kBgMosaicMask) != 0U) {
            anyVisibleMosaic = true;
            break;
        }
    }

    if (!anyVisibleMosaic) {
        std::int64_t prevLineStartX = 0;
        std::int64_t prevLineStartY = 0;
        std::int32_t prevPb = 0;
        std::int32_t prevPd = 0;
        std::int32_t prevRegX = 0;
        std::int32_t prevRegY = 0;
        bool havePreviousLine = false;

        for (int y = 0; y < ScreenHeight; ++y) {
            const AffineLineSnapshot affine = affineSnapshotForLine(bgIndex, y);
            const RasterLineSnapshot line = rasterSnapshotForLine(y);
            std::int64_t lineStartX = affine.xRef;
            std::int64_t lineStartY = affine.yRef;
            if (havePreviousLine) {
                const bool explicitRefWrite = affine.xRef != prevRegX || affine.yRef != prevRegY;
                if (!explicitRefWrite) {
                    lineStartX = prevLineStartX + static_cast<std::int64_t>(prevPb);
                    lineStartY = prevLineStartY + static_cast<std::int64_t>(prevPd);
                }
            }

            const u16 bgcnt = line.bgCnt[static_cast<std::size_t>(bgIndex)];
            const bool layerEnabled = (line.dispcnt & kBgEnableMasks[bgIndex]) != 0U;
            const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
            const u8 priority = static_cast<u8>(bgcnt & 0x3U);
            const bool wrap = (bgcnt & kBgWrapMask) != 0U;
            const u32 charBase = static_cast<u32>((bgcnt >> 2U) & 0x3U) * 0x4000U;
            const u32 screenBase = static_cast<u32>((bgcnt >> 8U) & 0x1FU) * 0x800U;
            const u32 sizeIndex = static_cast<u32>((bgcnt >> 14U) & 0x3U);
            const int affineSize = 128 << static_cast<int>(sizeIndex);
            const int tilesPerLine = affineSize / 8;

            if (layerEnabled) {
                for (int x = 0; x < ScreenWidth; ++x) {
                    const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
                    if (!layerEnabledByWindowMask(windowMask, static_cast<u8>(bgIndex))) {
                        continue;
                    }
                    const std::int64_t affineX =
                        lineStartX + static_cast<std::int64_t>(affine.pa) * static_cast<std::int64_t>(x);
                    const std::int64_t affineY =
                        lineStartY + static_cast<std::int64_t>(affine.pc) * static_cast<std::int64_t>(x);

                    int sx = static_cast<int>(affineX >> 8U);
                    int sy = static_cast<int>(affineY >> 8U);
                    if (wrap) {
                        sx = wrapCoordinate(sx, affineSize);
                        sy = wrapCoordinate(sy, affineSize);
                    } else if (sx < 0 || sx >= affineSize || sy < 0 || sy >= affineSize) {
                        continue;
                    }

                    const u32 tileX = static_cast<u32>(sx / 8);
                    const u32 tileY = static_cast<u32>(sy / 8);
                    const u32 inTileX = static_cast<u32>(sx & 7);
                    const u32 inTileY = static_cast<u32>(sy & 7);

                    const std::size_t mapIndex = static_cast<std::size_t>(screenBase)
                        + static_cast<std::size_t>(tileY) * static_cast<std::size_t>(tilesPerLine)
                        + static_cast<std::size_t>(tileX);
                    const u32 tileNumber = static_cast<u32>(readBgVram8(mapIndex));
                    const std::size_t texelOffset = static_cast<std::size_t>(charBase)
                        + static_cast<std::size_t>(tileNumber) * 64U
                        + static_cast<std::size_t>(inTileY) * 8U
                        + static_cast<std::size_t>(inTileX);
                    const u8 colorIndex = readBgVram8(texelOffset);
                    if (colorIndex == 0U) {
                        continue;
                    }

                    const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                        + static_cast<std::size_t>(x);
                    composeLayer(layerPixels, pixelIndex, readBgPaletteColor(colorIndex), priority, static_cast<u8>(bgIndex));
                }
            }

            prevLineStartX = lineStartX;
            prevLineStartY = lineStartY;
            prevPb = affine.pb;
            prevPd = affine.pd;
            prevRegX = affine.xRef;
            prevRegY = affine.yRef;
            havePreviousLine = true;
        }
        return;
    }

    std::array<AffineLineSnapshot, ScreenHeight> affineLines{};
    std::array<RasterLineSnapshot, ScreenHeight> rasterLines{};
    std::array<std::int64_t, ScreenHeight> lineStartXs{};
    std::array<std::int64_t, ScreenHeight> lineStartYs{};

    std::int64_t prevLineStartX = 0;
    std::int64_t prevLineStartY = 0;
    std::int32_t prevPb = 0;
    std::int32_t prevPd = 0;
    std::int32_t prevRegX = 0;
    std::int32_t prevRegY = 0;
    bool havePreviousLine = false;

    for (int y = 0; y < ScreenHeight; ++y) {
        const AffineLineSnapshot affine = affineSnapshotForLine(bgIndex, y);
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        std::int64_t lineStartX = affine.xRef;
        std::int64_t lineStartY = affine.yRef;
        if (havePreviousLine) {
            const bool explicitRefWrite = affine.xRef != prevRegX || affine.yRef != prevRegY;
            if (!explicitRefWrite) {
                lineStartX = prevLineStartX + static_cast<std::int64_t>(prevPb);
                lineStartY = prevLineStartY + static_cast<std::int64_t>(prevPd);
            }
        }

        affineLines[static_cast<std::size_t>(y)] = affine;
        rasterLines[static_cast<std::size_t>(y)] = line;
        lineStartXs[static_cast<std::size_t>(y)] = lineStartX;
        lineStartYs[static_cast<std::size_t>(y)] = lineStartY;

        prevLineStartX = lineStartX;
        prevLineStartY = lineStartY;
        prevPb = affine.pb;
        prevPd = affine.pd;
        prevRegX = affine.xRef;
        prevRegY = affine.yRef;
        havePreviousLine = true;
    }

    for (int y = 0; y < ScreenHeight; ++y) {
        const RasterLineSnapshot& line = rasterLines[static_cast<std::size_t>(y)];
        const AffineLineSnapshot& affine = affineLines[static_cast<std::size_t>(y)];

        const u16 bgcnt = line.bgCnt[static_cast<std::size_t>(bgIndex)];
        const bool layerEnabled = (line.dispcnt & kBgEnableMasks[bgIndex]) != 0U;
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const u8 priority = static_cast<u8>(bgcnt & 0x3U);
        const bool wrap = (bgcnt & kBgWrapMask) != 0U;
        const bool mosaicEnabled = (bgcnt & kBgMosaicMask) != 0U;
        const u32 charBase = static_cast<u32>((bgcnt >> 2U) & 0x3U) * 0x4000U;
        const u32 screenBase = static_cast<u32>((bgcnt >> 8U) & 0x1FU) * 0x800U;
        const u32 sizeIndex = static_cast<u32>((bgcnt >> 14U) & 0x3U);
        const int affineSize = 128 << static_cast<int>(sizeIndex);
        const int tilesPerLine = affineSize / 8;
        const int mosaicXSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 0U) : 1;
        const int mosaicYSpan = mosaicEnabled ? mosaicSpan(line.mosaic, 4U) : 1;
        const int sampleLine = mosaicEnabled ? mosaicSampleCoord(y, mosaicYSpan) : y;
        const AffineLineSnapshot& sampleAffine = affineLines[static_cast<std::size_t>(sampleLine)];
        const std::int64_t sampleLineStartX = lineStartXs[static_cast<std::size_t>(sampleLine)];
        const std::int64_t sampleLineStartY = lineStartYs[static_cast<std::size_t>(sampleLine)];
        static_cast<void>(affine);

        if (layerEnabled) {
            for (int x = 0; x < ScreenWidth; ++x) {
                const u8 windowMask = windowingEnabled ? windowMaskForPixel(x, y, line) : 0x3FU;
                if (!layerEnabledByWindowMask(windowMask, static_cast<u8>(bgIndex))) {
                    continue;
                }
                const int sourceX = mosaicEnabled ? mosaicSampleCoord(x, mosaicXSpan) : x;
                const std::int64_t affineX = sampleLineStartX
                    + static_cast<std::int64_t>(sampleAffine.pa) * static_cast<std::int64_t>(sourceX);
                const std::int64_t affineY = sampleLineStartY
                    + static_cast<std::int64_t>(sampleAffine.pc) * static_cast<std::int64_t>(sourceX);

                int sx = static_cast<int>(affineX >> 8U);
                int sy = static_cast<int>(affineY >> 8U);
                if (wrap) {
                    sx = wrapCoordinate(sx, affineSize);
                    sy = wrapCoordinate(sy, affineSize);
                } else if (sx < 0 || sx >= affineSize || sy < 0 || sy >= affineSize) {
                    continue;
                }

                const u32 tileX = static_cast<u32>(sx / 8);
                const u32 tileY = static_cast<u32>(sy / 8);
                const u32 inTileX = static_cast<u32>(sx & 7);
                const u32 inTileY = static_cast<u32>(sy & 7);

                const std::size_t mapIndex = static_cast<std::size_t>(screenBase)
                    + static_cast<std::size_t>(tileY) * static_cast<std::size_t>(tilesPerLine)
                    + static_cast<std::size_t>(tileX);
                const u32 tileNumber = static_cast<u32>(readBgVram8(mapIndex));
                const std::size_t texelOffset = static_cast<std::size_t>(charBase)
                    + static_cast<std::size_t>(tileNumber) * 64U
                    + static_cast<std::size_t>(inTileY) * 8U
                    + static_cast<std::size_t>(inTileX);
                const u8 colorIndex = readBgVram8(texelOffset);
                if (colorIndex == 0U) {
                    continue;
                }

                const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(x);
                composeLayer(layerPixels, pixelIndex, readBgPaletteColor(colorIndex), priority, static_cast<u8>(bgIndex));
            }
        }
    }
}

void Ppu::buildObjWindowMask(std::array<bool, FramebufferSize>& objWindowMask) const {
    const auto statsStart = Clock::now();
    std::fill(objWindowMask.begin(), objWindowMask.end(), false);
    if (memory_ == nullptr) {
        return;
    }

    const auto& vram = activeVram();
    std::array<ObjRenderEntry, 128> visibleObjects{};
    int visibleObjectCount = 0;
    std::array<std::array<std::uint8_t, 128>, ScreenHeight> scanlineObjectIndices{};
    std::array<std::uint8_t, ScreenHeight> scanlineObjectCounts{};

    for (int obj = 0; obj < 128; ++obj) {
        const std::size_t base = static_cast<std::size_t>(obj) * 8U;
        const u16 attr0 = readOam16(base + 0U);
        const u16 attr1 = readOam16(base + 2U);
        const u16 attr2 = readOam16(base + 4U);

        const bool affine = (attr0 & 0x0100U) != 0U;
        const bool affineParamBit = (attr0 & 0x0200U) != 0U;
        if (!affine && affineParamBit) {
            continue;
        }
        const bool doubleSize = affine && affineParamBit;
        const u8 objMode = static_cast<u8>((attr0 >> 10U) & 0x3U);
        if (objMode != 2U) {
            continue;
        }

        const bool color256 = (attr0 & 0x2000U) != 0U;
        const bool mosaicEnabled = (attr0 & kObjMosaicMask) != 0U;
        const u8 shape = static_cast<u8>((attr0 >> 14U) & 0x3U);
        const u8 size = static_cast<u8>((attr1 >> 14U) & 0x3U);
        int baseWidth = 0;
        int baseHeight = 0;
        if (!decodeObjSize(shape, size, baseWidth, baseHeight)) {
            continue;
        }
        const int renderWidth = affine && doubleSize ? baseWidth * 2 : baseWidth;
        const int renderHeight = affine && doubleSize ? baseHeight * 2 : baseHeight;

        int x = static_cast<int>(attr1 & 0x01FFU);
        int y = static_cast<int>(attr0 & 0x00FFU);
        if (x >= 256) {
            x -= 512;
        }
        if (y >= 160) {
            y -= 256;
        }

        const bool hflip = !affine && (attr1 & 0x1000U) != 0U;
        const bool vflip = !affine && (attr1 & 0x2000U) != 0U;
        const int tilesPerRow1D = std::max(1, baseWidth / 8);
        const int clippedStartX = std::max(0, x);
        const int clippedEndX = std::min(ScreenWidth, x + renderWidth);
        const int clippedStartY = std::max(0, y);
        const int clippedEndY = std::min(ScreenHeight, y + renderHeight);
        if (clippedStartX >= clippedEndX || clippedStartY >= clippedEndY) {
            continue;
        }

        ObjRenderEntry& entry = visibleObjects[static_cast<std::size_t>(visibleObjectCount)];
        entry.objIndex = obj;
        entry.x = x;
        entry.y = y;
        entry.baseWidth = baseWidth;
        entry.baseHeight = baseHeight;
        entry.renderWidth = renderWidth;
        entry.renderHeight = renderHeight;
        entry.clippedStartX = clippedStartX;
        entry.clippedEndX = clippedEndX;
        entry.clippedStartY = clippedStartY;
        entry.clippedEndY = clippedEndY;
        entry.tilesPerRow1D = tilesPerRow1D;
        entry.attr0 = attr0;
        entry.attr1 = attr1;
        entry.attr2 = attr2;
        entry.objMode = objMode;
        entry.affine = affine;
        entry.doubleSize = doubleSize;
        entry.color256 = color256;
        entry.mosaicEnabled = mosaicEnabled;
        entry.hflip = hflip;
        entry.vflip = vflip;
        if (affine) {
            const u32 affineIndex = static_cast<u32>((attr1 >> 9U) & 0x1FU);
            const std::size_t affineBase = static_cast<std::size_t>(affineIndex) * 32U;
            entry.pa = static_cast<std::int16_t>(readOam16(affineBase + 6U));
            entry.pb = static_cast<std::int16_t>(readOam16(affineBase + 14U));
            entry.pc = static_cast<std::int16_t>(readOam16(affineBase + 22U));
            entry.pd = static_cast<std::int16_t>(readOam16(affineBase + 30U));
        }

        for (int line = clippedStartY; line < clippedEndY; ++line) {
            auto& count = scanlineObjectCounts[static_cast<std::size_t>(line)];
            scanlineObjectIndices[static_cast<std::size_t>(line)][static_cast<std::size_t>(count)] =
                static_cast<std::uint8_t>(visibleObjectCount);
            ++count;
        }
        ++visibleObjectCount;
    }

    for (int screenY = 0; screenY < ScreenHeight; ++screenY) {
        const RasterLineSnapshot line = rasterSnapshotForLine(screenY);
        if ((line.dispcnt & kObjWinEnableMask) == 0U) {
            continue;
        }

        u16 lineMode = 0U;
        std::size_t lineObjTileBase = 0U;
        bool lineObj1D = false;
        resolveObjRenderConfig(line.dispcnt, lineMode, lineObjTileBase, lineObj1D);
        const std::uint8_t objectCount = scanlineObjectCounts[static_cast<std::size_t>(screenY)];

        for (std::uint8_t objectSlot = 0; objectSlot < objectCount; ++objectSlot) {
            const ObjRenderEntry& entry = visibleObjects[
                scanlineObjectIndices[static_cast<std::size_t>(screenY)][static_cast<std::size_t>(objectSlot)]
            ];
            u32 tileBase = 0U;
            u32 totalObjBlocks = 0U;
            if (!resolveObjTileNumber(lineMode, entry.attr2, entry.color256, tileBase, totalObjBlocks)) {
                continue;
            }

            const int py = screenY - entry.y;
            const int objMosaicXSpan = entry.mosaicEnabled ? mosaicSpan(line.mosaic, 8U) : 1;
            const int objMosaicYSpan = entry.mosaicEnabled ? mosaicSpan(line.mosaic, 12U) : 1;
            for (int screenX = entry.clippedStartX; screenX < entry.clippedEndX; ++screenX) {
                const int px = screenX - entry.x;
                const int samplePx = entry.mosaicEnabled ? mosaicSampleCoord(px, objMosaicXSpan) : px;
                const int samplePy = entry.mosaicEnabled ? mosaicSampleCoord(py, objMosaicYSpan) : py;

                int localX = 0;
                int localY = 0;
                if (entry.affine) {
                    const int dx = samplePx - (entry.renderWidth / 2);
                    const int dy = samplePy - (entry.renderHeight / 2);
                    const std::int32_t srcX = ((entry.pa * dx + entry.pb * dy) >> 8) + (entry.baseWidth / 2);
                    const std::int32_t srcY = ((entry.pc * dx + entry.pd * dy) >> 8) + (entry.baseHeight / 2);
                    if (srcX < 0 || srcX >= entry.baseWidth || srcY < 0 || srcY >= entry.baseHeight) {
                        continue;
                    }
                    localX = static_cast<int>(srcX);
                    localY = static_cast<int>(srcY);
                } else {
                    localX = entry.hflip ? (entry.baseWidth - 1 - samplePx) : samplePx;
                    localY = entry.vflip ? (entry.baseHeight - 1 - samplePy) : samplePy;
                }

                const int tileX = localX / 8;
                const int tileY = localY / 8;
                const int inTileX = localX & 7;
                const int inTileY = localY & 7;
                const u32 blockStrideX = entry.color256 ? 2U : 1U;
                std::size_t texelOffset = 0U;
                if (lineObj1D) {
                    u32 blockOffset = 0;
                    const u32 rowBlocks = static_cast<u32>(entry.tilesPerRow1D) * blockStrideX;
                    blockOffset = static_cast<u32>(tileY) * rowBlocks + static_cast<u32>(tileX) * blockStrideX;
                    const u32 blockNumber = (tileBase + blockOffset) % totalObjBlocks;
                    texelOffset = lineObjTileBase
                        + static_cast<std::size_t>(blockNumber) * 32U
                        + (entry.color256
                            ? static_cast<std::size_t>(inTileY) * 8U + static_cast<std::size_t>(inTileX)
                            : static_cast<std::size_t>(inTileY) * 4U + static_cast<std::size_t>(inTileX / 2));
                } else {
                    texelOffset = lineObjTileBase
                        + resolveObj2DTexelOffset(tileBase, totalObjBlocks, entry.color256, tileX, tileY, inTileX, inTileY);
                }

                bool opaquePixel = false;
                if (entry.color256) {
                    if (texelOffset < vram.size()) {
                        opaquePixel = vram[texelOffset] != 0U;
                    }
                } else {
                    if (texelOffset < vram.size()) {
                        const u8 packed = vram[texelOffset];
                        const u8 index4 = (inTileX & 1) == 0
                            ? static_cast<u8>(packed & 0x0FU)
                            : static_cast<u8>((packed >> 4U) & 0x0FU);
                        opaquePixel = index4 != 0U;
                    }
                }
                if (!opaquePixel) {
                    continue;
                }

                const std::size_t pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(screenX);
                objWindowMask[pixelIndex] = true;
                ++lastRenderStats_.objWindowPixels;
            }
        }
    }
    lastRenderStats_.objWindowNs += elapsedNs(statsStart);
}

void Ppu::renderObjects(std::array<LayerPixel, FramebufferSize>& layerPixels) const {
    const auto& vram = activeVram();
    std::array<ObjRenderEntry, 128> visibleObjects{};
    int visibleObjectCount = 0;
    std::array<std::array<std::uint8_t, 128>, ScreenHeight> scanlineObjectIndices{};
    std::array<std::uint8_t, ScreenHeight> scanlineObjectCounts{};

    for (int obj = 0; obj < 128; ++obj) {
        const std::size_t base = static_cast<std::size_t>(obj) * 8U;
        const u16 attr0 = readOam16(base + 0U);
        const u16 attr1 = readOam16(base + 2U);
        const u16 attr2 = readOam16(base + 4U);

        const bool affine = (attr0 & 0x0100U) != 0U;
        const bool affineParamBit = (attr0 & 0x0200U) != 0U;
        if (!affine && affineParamBit) {
            // No modo nao-afim, bit9 indica OBJ disabled.
            continue;
        }
        const bool doubleSize = affine && affineParamBit;
        const u8 objMode = static_cast<u8>((attr0 >> 10U) & 0x3U);
        if (objMode == 2U) { // obj window nao suportado aqui
            continue;
        }

        const bool color256 = (attr0 & 0x2000U) != 0U;
        const bool mosaicEnabled = (attr0 & kObjMosaicMask) != 0U;
        const u8 shape = static_cast<u8>((attr0 >> 14U) & 0x3U);
        const u8 size = static_cast<u8>((attr1 >> 14U) & 0x3U);

        int baseWidth = 0;
        int baseHeight = 0;
        if (!decodeObjSize(shape, size, baseWidth, baseHeight)) {
            continue;
        }
        const int renderWidth = affine && doubleSize ? baseWidth * 2 : baseWidth;
        const int renderHeight = affine && doubleSize ? baseHeight * 2 : baseHeight;

        int x = static_cast<int>(attr1 & 0x01FFU);
        int y = static_cast<int>(attr0 & 0x00FFU);
        if (x >= 256) {
            x -= 512;
        }
        if (y >= 160) {
            y -= 256;
        }

        const bool hflip = !affine && (attr1 & 0x1000U) != 0U;
        const bool vflip = !affine && (attr1 & 0x2000U) != 0U;
        const u8 objPriority = static_cast<u8>((attr2 >> 10U) & 0x3U);
        const u8 paletteBank = static_cast<u8>((attr2 >> 12U) & 0x0FU);
        const int tilesPerRow1D = std::max(1, baseWidth / 8);
        const int clippedStartX = std::max(0, x);
        const int clippedEndX = std::min(ScreenWidth, x + renderWidth);
        const int clippedStartY = std::max(0, y);
        const int clippedEndY = std::min(ScreenHeight, y + renderHeight);
        if (clippedStartX >= clippedEndX || clippedStartY >= clippedEndY) {
            continue;
        }

        ++lastRenderStats_.visibleObjectsFrame;
        noteVisibleObjectOnScanlines(clippedStartY, clippedEndY);

        if (activeDebugConfig_.logObjScanline >= clippedStartY && activeDebugConfig_.logObjScanline < clippedEndY) {
            const u16 scanlineDispcnt = rasterSnapshotForLine(activeDebugConfig_.logObjScanline).dispcnt;
            logObjectVisibleOnScanline(
                activeDebugConfig_.logObjScanline,
                obj,
                x,
                y,
                renderWidth,
                renderHeight,
                attr0,
                attr1,
                attr2,
                color256,
                (scanlineDispcnt & kObjMapping1dMask) != 0U,
                paletteBank,
                objPriority
            );
        }
        if (shouldLogObject(obj)) {
            const u16 line0Dispcnt = rasterSnapshotForLine(clippedStartY).dispcnt;
            logSelectedObjectSummary(
                obj,
                x,
                y,
                renderWidth,
                renderHeight,
                line0Dispcnt,
                attr0,
                attr1,
                attr2,
                affine,
                doubleSize,
                color256,
                (line0Dispcnt & kObjMapping1dMask) != 0U,
                objMode,
                paletteBank,
                objPriority
            );
        }

        ObjRenderEntry& entry = visibleObjects[static_cast<std::size_t>(visibleObjectCount)];
        entry.objIndex = obj;
        entry.x = x;
        entry.y = y;
        entry.baseWidth = baseWidth;
        entry.baseHeight = baseHeight;
        entry.renderWidth = renderWidth;
        entry.renderHeight = renderHeight;
        entry.clippedStartX = clippedStartX;
        entry.clippedEndX = clippedEndX;
        entry.clippedStartY = clippedStartY;
        entry.clippedEndY = clippedEndY;
        entry.tilesPerRow1D = tilesPerRow1D;
        entry.attr0 = attr0;
        entry.attr1 = attr1;
        entry.attr2 = attr2;
        entry.objMode = objMode;
        entry.objPriority = objPriority;
        entry.paletteBank = paletteBank;
        entry.affine = affine;
        entry.doubleSize = doubleSize;
        entry.color256 = color256;
        entry.mosaicEnabled = mosaicEnabled;
        entry.hflip = hflip;
        entry.vflip = vflip;
        if (affine) {
            const u32 affineIndex = static_cast<u32>((attr1 >> 9U) & 0x1FU);
            const std::size_t affineBase = static_cast<std::size_t>(affineIndex) * 32U;
            entry.pa = static_cast<std::int16_t>(readOam16(affineBase + 6U));
            entry.pb = static_cast<std::int16_t>(readOam16(affineBase + 14U));
            entry.pc = static_cast<std::int16_t>(readOam16(affineBase + 22U));
            entry.pd = static_cast<std::int16_t>(readOam16(affineBase + 30U));
        }

        for (int line = clippedStartY; line < clippedEndY; ++line) {
            auto& count = scanlineObjectCounts[static_cast<std::size_t>(line)];
            scanlineObjectIndices[static_cast<std::size_t>(line)][static_cast<std::size_t>(count)] =
                static_cast<std::uint8_t>(visibleObjectCount);
            ++count;
        }
        ++visibleObjectCount;
    }

    for (int screenY = 0; screenY < ScreenHeight; ++screenY) {
        const RasterLineSnapshot line = rasterSnapshotForLine(screenY);
        if ((line.dispcnt & kObjEnableMask) == 0U) {
            continue;
        }

        u16 lineMode = 0U;
        std::size_t lineObjTileBase = 0U;
        bool lineObj1D = false;
        resolveObjRenderConfig(line.dispcnt, lineMode, lineObjTileBase, lineObj1D);
        const bool windowingEnabled = (line.dispcnt & kAnyWindowEnableMask) != 0U;
        const std::uint8_t objectCount = scanlineObjectCounts[static_cast<std::size_t>(screenY)];

        for (std::uint8_t objectSlot = 0; objectSlot < objectCount; ++objectSlot) {
            const ObjRenderEntry& entry = visibleObjects[
                scanlineObjectIndices[static_cast<std::size_t>(screenY)][static_cast<std::size_t>(objectSlot)]
            ];
            u32 tileBase = 0U;
            u32 totalObjBlocks = 0U;
            if (!resolveObjTileNumber(lineMode, entry.attr2, entry.color256, tileBase, totalObjBlocks)) {
                continue;
            }

            const int py = screenY - entry.y;
            const int objMosaicXSpan = entry.mosaicEnabled ? mosaicSpan(line.mosaic, 8U) : 1;
            const int objMosaicYSpan = entry.mosaicEnabled ? mosaicSpan(line.mosaic, 12U) : 1;
            for (int screenX = entry.clippedStartX; screenX < entry.clippedEndX; ++screenX) {
                const u8 windowMask = windowingEnabled ? windowMaskForPixel(screenX, screenY, line) : 0x3FU;
                if (!layerEnabledByWindowMask(windowMask, 4U)) {
                    continue;
                }
                const int px = screenX - entry.x;
                const int samplePx = entry.mosaicEnabled ? mosaicSampleCoord(px, objMosaicXSpan) : px;
                const int samplePy = entry.mosaicEnabled ? mosaicSampleCoord(py, objMosaicYSpan) : py;
                ++lastRenderStats_.objPixelsTested;

                if (activeDebugConfig_.objBoundingBoxesOnly) {
                    if (samplePx != 0
                        && samplePx != entry.renderWidth - 1
                        && samplePy != 0
                        && samplePy != entry.renderHeight - 1) {
                        continue;
                    }
                    const std::size_t pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(ScreenWidth)
                        + static_cast<std::size_t>(screenX);
                    composeLayer(
                        layerPixels,
                        pixelIndex,
                        debugOutlineColor555(entry.objPriority),
                        entry.objPriority,
                        5U,
                        false
                    );
                    ++lastRenderStats_.objPixelsDrawn;
                    continue;
                }

                int localX = 0;
                int localY = 0;
                if (entry.affine) {
                    const int dx = samplePx - (entry.renderWidth / 2);
                    const int dy = samplePy - (entry.renderHeight / 2);
                    const std::int32_t srcX = ((entry.pa * dx + entry.pb * dy) >> 8) + (entry.baseWidth / 2);
                    const std::int32_t srcY = ((entry.pc * dx + entry.pd * dy) >> 8) + (entry.baseHeight / 2);
                    if (srcX < 0 || srcX >= entry.baseWidth || srcY < 0 || srcY >= entry.baseHeight) {
                        continue;
                    }
                    localX = static_cast<int>(srcX);
                    localY = static_cast<int>(srcY);
                } else {
                    localX = entry.hflip ? (entry.baseWidth - 1 - samplePx) : samplePx;
                    localY = entry.vflip ? (entry.baseHeight - 1 - samplePy) : samplePy;
                }

                const int tileX = localX / 8;
                const int tileY = localY / 8;
                const int inTileX = localX & 7;
                const int inTileY = localY & 7;
                const u32 blockStrideX = entry.color256 ? 2U : 1U;
                std::size_t texelOffset = 0U;
                if (lineObj1D) {
                    u32 blockOffset = 0;
                    const u32 rowBlocks = static_cast<u32>(entry.tilesPerRow1D) * blockStrideX;
                    blockOffset = static_cast<u32>(tileY) * rowBlocks + static_cast<u32>(tileX) * blockStrideX;
                    const u32 blockNumber = (tileBase + blockOffset) % totalObjBlocks;
                    texelOffset = lineObjTileBase
                        + static_cast<std::size_t>(blockNumber) * 32U
                        + (entry.color256
                            ? static_cast<std::size_t>(inTileY) * 8U + static_cast<std::size_t>(inTileX)
                            : static_cast<std::size_t>(inTileY) * 4U + static_cast<std::size_t>(inTileX / 2));
                } else {
                    texelOffset = lineObjTileBase
                        + resolveObj2DTexelOffset(tileBase, totalObjBlocks, entry.color256, tileX, tileY, inTileX, inTileY);
                }

                u8 colorIndex = 0;
                if (entry.color256) {
                    if (texelOffset >= vram.size()) {
                        continue;
                    }
                    colorIndex = vram[texelOffset];
                    if (colorIndex == 0U) {
                        continue;
                    }
                } else {
                    if (texelOffset >= vram.size()) {
                        continue;
                    }
                    const u8 packed = vram[texelOffset];
                    const u8 index4 = (inTileX & 1) == 0
                        ? static_cast<u8>(packed & 0x0FU)
                        : static_cast<u8>((packed >> 4U) & 0x0FU);
                    if (index4 == 0U) {
                        continue;
                    }
                    colorIndex = static_cast<u8>(entry.paletteBank * 16U + index4);
                }

                if (screenX < 4 && screenY < 2) {
                    logObjDecodeSample(
                        entry.objIndex,
                        screenX,
                        screenY,
                        line.dispcnt,
                        entry.attr0,
                        entry.attr1,
                        entry.attr2,
                        lineObj1D,
                        entry.color256,
                        tileBase,
                        totalObjBlocks,
                        tileX,
                        tileY,
                        texelOffset,
                        colorIndex,
                        entry.paletteBank,
                        entry.objPriority
                    );
                }

                const std::size_t pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(screenX);
                composeLayer(
                    layerPixels,
                    pixelIndex,
                    readObjPaletteColor(colorIndex),
                    entry.objPriority,
                    5U,
                    entry.objMode == 1U
                );
                ++lastRenderStats_.objPixelsDrawn;
            }
        }
    }
}

void Ppu::composeLayer(
    std::array<LayerPixel, FramebufferSize>& layerPixels,
    std::size_t pixelIndex,
    u16 rawColor555,
    u8 priority,
    u8 layer,
    bool semiTransparentObj
) const {
    LayerPixel& dst = layerPixels[pixelIndex];
    const auto outranks = [](u8 candidatePriority, u8 candidateLayer, u8 currentPriority, u8 currentLayer) {
        if (candidateLayer == 5U) {
            return candidatePriority < currentPriority
                || (candidatePriority == currentPriority && currentLayer <= 4U);
        }
        if (currentLayer == 5U) {
            return candidatePriority < currentPriority;
        }
        return candidatePriority < currentPriority
            || (candidatePriority == currentPriority && candidateLayer < currentLayer);
    };

    if (outranks(priority, layer, dst.priority, dst.layer)) {
        if (dst.opaque) {
            dst.secondRawColor555 = dst.rawColor555;
            dst.secondPriority = dst.priority;
            dst.secondLayer = dst.layer;
            dst.hasSecond = true;
        } else {
            dst.hasSecond = false;
        }
        dst.rawColor555 = rawColor555;
        dst.color = bgr555ToRgb565(rawColor555);
        dst.priority = priority;
        dst.layer = layer;
        dst.opaque = true;
        dst.semiTransparentObj = semiTransparentObj;
        return;
    }

    if (!dst.hasSecond || outranks(priority, layer, dst.secondPriority, dst.secondLayer)) {
        dst.secondRawColor555 = rawColor555;
        dst.secondPriority = priority;
        dst.secondLayer = layer;
        dst.hasSecond = true;
    }
}

u16 Ppu::applyColorEffect(
    const LayerPixel& pixel,
    const RasterLineSnapshot& line,
    u16 backdropRaw,
    u8 windowMask
) const {
    u16 outRaw = pixel.rawColor555;
    if (activeDebugConfig_.disableBlend) {
        return outRaw;
    }
    if ((windowMask & 0x20U) == 0U) {
        return outRaw;
    }

    const u16 bldcnt = line.bldCnt;
    const u8 blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(line.bldAlpha & 0x1FU)));
    const u8 evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((line.bldAlpha >> 8U) & 0x1FU)));
    const u8 evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(line.bldY & 0x1FU)));
    const u8 topLayerBit = blendLayerBitFromLayerId(pixel.layer);
    const bool firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;

    const bool alphaBlendRequested = blendMode == 1U || pixel.semiTransparentObj;
    if (alphaBlendRequested) {
        bool hasSecond = pixel.hasSecond;
        bool blockedByObj = false;
        u16 secondRaw = pixel.secondRawColor555;
        u8 secondLayer = pixel.secondLayer;
        if (pixel.semiTransparentObj && hasSecond && secondLayer == 5U) {
            hasSecond = false;
            blockedByObj = true;
        }
        if (!hasSecond && !blockedByObj && pixel.layer != 4U) {
            hasSecond = true;
            secondRaw = backdropRaw;
            secondLayer = 4U;
        }
        const bool canAlphaBlend = pixel.semiTransparentObj || (blendMode == 1U && firstTarget);
        if (canAlphaBlend && hasSecond) {
            const u8 secondLayerBit = blendLayerBitFromLayerId(secondLayer);
            if ((bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U) {
                return blendAlpha555(outRaw, secondRaw, eva, evb);
            }
        }
        if (pixel.semiTransparentObj) {
            return outRaw;
        }
    }

    if (blendMode == 2U && firstTarget) {
        return brighten555(outRaw, evy);
    }
    if (blendMode == 3U && firstTarget) {
        return darken555(outRaw, evy);
    }
    return outRaw;
}

u8 Ppu::windowMaskForPixel(int x, int y) const {
    const RasterLineSnapshot line = rasterSnapshotForLine(y);
    return windowMaskForPixel(x, y, line);
}

u8 Ppu::windowMaskForPixel(int x, int y, const RasterLineSnapshot& line) const {
    if (!windowMaskCacheEnabled_) {
        return windowMaskForPixelUncached(x, y, line);
    }
    if (y < 0 || y >= ScreenHeight || x < 0 || x >= ScreenWidth) {
        return 0x3FU;
    }
    if ((line.dispcnt & kAnyWindowEnableMask) == 0U || activeDebugConfig_.disableWindow) {
        return 0x3FU;
    }

    const std::size_t lineIndex = static_cast<std::size_t>(y);
    if (!windowMaskCacheLineReady_[lineIndex]) {
        const RasterLineSnapshot cachedLine = rasterSnapshotForLine(y);
        const std::size_t rowBase = lineIndex * static_cast<std::size_t>(ScreenWidth);
        for (int sx = 0; sx < ScreenWidth; ++sx) {
            windowMaskCache_[rowBase + static_cast<std::size_t>(sx)] = windowMaskForPixelUncached(sx, y, cachedLine);
        }
        windowMaskCacheLineReady_[lineIndex] = true;
    }

    const std::size_t pixelIndex = lineIndex * static_cast<std::size_t>(ScreenWidth)
        + static_cast<std::size_t>(x);
    return windowMaskCache_[pixelIndex];
}

u8 Ppu::windowMaskForPixelUncached(int x, int y, const RasterLineSnapshot& line) const {
    if (activeDebugConfig_.disableWindow) {
        return 0x3FU;
    }
    const u16 dispcnt = line.dispcnt;
    const bool win0Enabled = (dispcnt & kWin0EnableMask) != 0U;
    const bool win1Enabled = (dispcnt & kWin1EnableMask) != 0U;
    const bool objWinEnabled = (dispcnt & kObjWinEnableMask) != 0U;
    if (!win0Enabled && !win1Enabled && !objWinEnabled) {
        return 0x3FU;
    }

    const u16 winIn = line.winIn;
    const u16 winOut = line.winOut;
    u8 activeMask = static_cast<u8>(winOut & 0x3FU);

    if (win0Enabled) {
        if (pointInsideWindowRect(x, y, line.win0H, line.win0V)) {
            activeMask = static_cast<u8>(winIn & 0x3FU);
            return activeMask;
        }
    }
    if (win1Enabled) {
        if (pointInsideWindowRect(x, y, line.win1H, line.win1V)) {
            activeMask = static_cast<u8>((winIn >> 8U) & 0x3FU);
            return activeMask;
        }
    }

    if (objWinEnabled && activeObjWindowMask_ != nullptr) {
        const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
            + static_cast<std::size_t>(x);
        if (pixelIndex < activeObjWindowMask_->size() && (*activeObjWindowMask_)[pixelIndex]) {
            return static_cast<u8>((winOut >> 8U) & 0x3FU);
        }
    }
    return activeMask;
}

bool Ppu::pointInsideWindowRange(int value, int start, int end, int limit) {
    if (value < 0 || value >= limit) {
        return false;
    }
    start = std::clamp(start, 0, limit);
    end = std::clamp(end, 0, limit);
    if (start == end) {
        // Match mGBA/GBA behavior: equal bounds select no pixels.
        return false;
    }
    if (start < end) {
        return value >= start && value < end;
    }
    return value >= start || value < end;
}

bool Ppu::pointInsideWindowRect(int x, int y, u16 winH, u16 winV) {
    const int xStart = static_cast<int>((winH >> 8U) & 0xFFU);
    const int xEnd = static_cast<int>(winH & 0xFFU);
    const int yStart = static_cast<int>((winV >> 8U) & 0xFFU);
    const int yEnd = static_cast<int>(winV & 0xFFU);
    return pointInsideWindowRange(x, xStart, xEnd, ScreenWidth)
        && pointInsideWindowRange(y, yStart, yEnd, ScreenHeight);
}

bool Ppu::layerEnabledByWindowMask(u8 mask, u8 layerBit) {
    if (layerBit > 4U) {
        return true;
    }
    return (mask & static_cast<u8>(1U << layerBit)) != 0U;
}

bool Ppu::bgEnabledByDebugMask(const DebugConfig& config, int bgIndex) {
    return bgIndex >= 0
        && bgIndex < 4
        && (config.bgMask & static_cast<u8>(1U << static_cast<unsigned>(bgIndex))) != 0U;
}

u16 Ppu::blendAlpha555(u16 top, u16 bottom, u8 eva, u8 evb) {
    const u8 rTop = static_cast<u8>(top & 0x1FU);
    const u8 gTop = static_cast<u8>((top >> 5U) & 0x1FU);
    const u8 bTop = static_cast<u8>((top >> 10U) & 0x1FU);
    const u8 rBot = static_cast<u8>(bottom & 0x1FU);
    const u8 gBot = static_cast<u8>((bottom >> 5U) & 0x1FU);
    const u8 bBot = static_cast<u8>((bottom >> 10U) & 0x1FU);

    const u16 r = static_cast<u16>(std::min<int>(31, (rTop * eva + rBot * evb) / 16));
    const u16 g = static_cast<u16>(std::min<int>(31, (gTop * eva + gBot * evb) / 16));
    const u16 b = static_cast<u16>(std::min<int>(31, (bTop * eva + bBot * evb) / 16));
    return static_cast<u16>(r | static_cast<u16>(g << 5U) | static_cast<u16>(b << 10U));
}

u16 Ppu::brighten555(u16 color, u8 evy) {
    const u8 rIn = static_cast<u8>(color & 0x1FU);
    const u8 gIn = static_cast<u8>((color >> 5U) & 0x1FU);
    const u8 bIn = static_cast<u8>((color >> 10U) & 0x1FU);
    const u16 r = static_cast<u16>(std::min<int>(31, rIn + ((31 - rIn) * evy) / 16));
    const u16 g = static_cast<u16>(std::min<int>(31, gIn + ((31 - gIn) * evy) / 16));
    const u16 b = static_cast<u16>(std::min<int>(31, bIn + ((31 - bIn) * evy) / 16));
    return static_cast<u16>(r | static_cast<u16>(g << 5U) | static_cast<u16>(b << 10U));
}

u16 Ppu::darken555(u16 color, u8 evy) {
    const u8 rIn = static_cast<u8>(color & 0x1FU);
    const u8 gIn = static_cast<u8>((color >> 5U) & 0x1FU);
    const u8 bIn = static_cast<u8>((color >> 10U) & 0x1FU);
    const u16 r = static_cast<u16>(std::max<int>(0, rIn - (rIn * evy) / 16));
    const u16 g = static_cast<u16>(std::max<int>(0, gIn - (gIn * evy) / 16));
    const u16 b = static_cast<u16>(std::max<int>(0, bIn - (bIn * evy) / 16));
    return static_cast<u16>(r | static_cast<u16>(g << 5U) | static_cast<u16>(b << 10U));
}

u8 Ppu::blendLayerBitFromLayerId(u8 layerId) {
    if (layerId <= 3U) {
        return layerId;
    }
    if (layerId == 5U) {
        return 4U; // OBJ
    }
    return 5U; // backdrop
}

u16 Ppu::readVram16(std::size_t byteIndex) const {
    const auto& vram = activeVram();
    if (byteIndex + 1U >= vram.size()) {
        return 0;
    }
    return static_cast<u16>(
        static_cast<u16>(vram[byteIndex])
        | static_cast<u16>(static_cast<u16>(vram[byteIndex + 1U]) << 8U)
    );
}

u8 Ppu::readBgVram8(std::size_t byteIndex) const {
    const auto& vram = activeVram();
    if (vram.empty()) {
        return 0;
    }
    const std::size_t bgIndex = byteIndex;
    if (bgIndex >= vram.size()) {
        return 0;
    }
    return vram[bgIndex];
}

u16 Ppu::readBgVram16(std::size_t byteIndex) const {
    const u8 lo = readBgVram8(byteIndex);
    const u8 hi = readBgVram8(byteIndex + 1U);
    return static_cast<u16>(static_cast<u16>(lo) | static_cast<u16>(static_cast<u16>(hi) << 8U));
}

u16 Ppu::readOam16(std::size_t byteIndex) const {
    const auto& oam = activeOam();
    if (byteIndex + 1U >= oam.size()) {
        return 0;
    }
    return static_cast<u16>(
        static_cast<u16>(oam[byteIndex])
        | static_cast<u16>(static_cast<u16>(oam[byteIndex + 1U]) << 8U)
    );
}

u16 Ppu::readBgPaletteColor(u8 colorIndex) const {
    const auto& pram = activePram();
    const std::size_t byteIndex = static_cast<std::size_t>(colorIndex) * 2U;
    if (byteIndex + 1U >= pram.size()) {
        return 0;
    }
    return static_cast<u16>(
        static_cast<u16>(pram[byteIndex])
        | static_cast<u16>(static_cast<u16>(pram[byteIndex + 1U]) << 8U)
    );
}

u16 Ppu::readObjPaletteColor(u8 colorIndex) const {
    const auto& pram = activePram();
    const std::size_t byteIndex = 0x200U + static_cast<std::size_t>(colorIndex) * 2U;
    if (byteIndex + 1U >= pram.size()) {
        return 0;
    }
    return static_cast<u16>(
        static_cast<u16>(pram[byteIndex])
        | static_cast<u16>(static_cast<u16>(pram[byteIndex + 1U]) << 8U)
    );
}

Ppu::DebugConfig Ppu::readDebugConfig() {
    DebugConfig config{};
    config.disableBg = gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_DISABLE_BG");
    config.disableObj = gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_DISABLE_OBJ");
    config.disableBlend = gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_DISABLE_BLEND")
        || gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_DISABLE_BLEND_WINDOW");
    config.disableWindow = gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_DISABLE_WINDOW")
        || gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_DISABLE_BLEND_WINDOW");
    config.objBoundingBoxesOnly = gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_OBJ_BBOX");
    config.logObjScanline = readIntEnvironmentOrDefault("GBEMU_GBA_LOG_OBJ_SCANLINE", -1);
    config.bgMask = static_cast<u8>(std::clamp(readIntEnvironmentBase0OrDefault("GBEMU_GBA_DEBUG_BG_MASK", 0x0F), 0, 0x0F));

    if (gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_ONLY_BG0")) {
        config.bgMask = 0x01U;
        config.disableObj = true;
    }
    if (gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_ONLY_BG1")) {
        config.bgMask = 0x02U;
        config.disableObj = true;
    }
    if (gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_ONLY_BG2")) {
        config.bgMask = 0x04U;
        config.disableObj = true;
    }
    if (gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_ONLY_BG3")) {
        config.bgMask = 0x08U;
        config.disableObj = true;
    }
    if (gb::environmentVariableEnabled("GBEMU_GBA_DEBUG_ONLY_OBJ")) {
        config.bgMask = 0x00U;
        config.disableBg = false;
        config.disableObj = false;
    }

    if (const auto ids = gb::readEnvironmentVariable("GBEMU_GBA_LOG_OBJ_IDS")) {
        std::size_t start = 0;
        while (start < ids->size()) {
            std::size_t end = ids->find(',', start);
            if (end == std::string::npos) {
                end = ids->size();
            }
            const std::string token = ids->substr(start, end - start);
            try {
                const int value = std::stoi(token);
                if (value >= 0 && value < static_cast<int>(config.selectedObjIds.size())) {
                    config.selectedObjIds[static_cast<std::size_t>(value)] = true;
                    config.anySelectedObjIds = true;
                }
            } catch (...) {
            }
            start = end + 1U;
        }
    }

    return config;
}

bool Ppu::shouldLogObject(int objIndex) const {
    return activeDebugConfig_.anySelectedObjIds
        && objIndex >= 0
        && objIndex < static_cast<int>(activeDebugConfig_.selectedObjIds.size())
        && activeDebugConfig_.selectedObjIds[static_cast<std::size_t>(objIndex)];
}

u16 Ppu::debugOutlineColor555(int objPriority) {
    static constexpr std::array<u16, 4> kPriorityColors = {
        0x001FU,
        0x03E0U,
        0x7C00U,
        0x03FFU,
    };
    return kPriorityColors[static_cast<std::size_t>(objPriority & 0x3)];
}

void Ppu::noteVisibleObjectOnScanlines(int startY, int endY) const {
    startY = std::max(startY, 0);
    endY = std::min(endY, ScreenHeight);
    for (int line = startY; line < endY; ++line) {
        auto& count = lastRenderStats_.visibleObjectsPerScanline[static_cast<std::size_t>(line)];
        count = static_cast<std::uint16_t>(count + 1U);
        lastRenderStats_.maxVisibleObjectsOnScanline = std::max(lastRenderStats_.maxVisibleObjectsOnScanline, count);
    }
}

const std::array<u8, Memory::VramSize>& Ppu::activeVram() const {
    if (completedMemorySnapshotValid_) {
        return completedVram_;
    }
    return memory_->vram();
}

const std::array<u8, Memory::PramSize>& Ppu::activePram() const {
    if (completedMemorySnapshotValid_) {
        return completedPram_;
    }
    return memory_->pram();
}

const std::array<u8, Memory::OamSize>& Ppu::activeOam() const {
    if (completedMemorySnapshotValid_) {
        return completedOam_;
    }
    return memory_->oam();
}

void Ppu::clearRasterLineSnapshots() {
    for (auto& s : rasterLineSnapshots_) {
        s = RasterLineSnapshot{};
    }
}

Ppu::RasterLineSnapshot Ppu::readCurrentRasterSnapshot() const {
    if (memory_ == nullptr) {
        return RasterLineSnapshot{};
    }
    RasterLineSnapshot out{};
    out.dispcnt = memory_->readIo16(DispcntOffset);
    out.win0H = memory_->readIo16(kWin0HOffset);
    out.win1H = memory_->readIo16(kWin1HOffset);
    out.win0V = memory_->readIo16(kWin0VOffset);
    out.win1V = memory_->readIo16(kWin1VOffset);
    out.winIn = memory_->readIo16(kWinInOffset);
    out.winOut = memory_->readIo16(kWinOutOffset);
    out.mosaic = memory_->readIo16(kMosaicOffset);
    out.bldCnt = memory_->readIo16(kBldCntOffset);
    out.bldAlpha = memory_->readIo16(kBldAlphaOffset);
    out.bldY = memory_->readIo16(kBldYOffset);
    for (std::size_t bg = 0; bg < out.bgCnt.size(); ++bg) {
        const u32 bgIndex = static_cast<u32>(bg);
        out.bgCnt[bg] = memory_->readIo16(Bg0CntOffset + bgIndex * 2U);
        out.bgHofs[bg] = memory_->readIo16(Bg0HofsOffset + bgIndex * 4U);
        out.bgVofs[bg] = memory_->readIo16(Bg0VofsOffset + bgIndex * 4U);
    }
    out.valid = true;
    return out;
}

void Ppu::captureRasterLineSnapshot(int line) {
    if (line < 0 || line >= static_cast<int>(VisibleLines)) {
        return;
    }
    rasterLineSnapshots_[static_cast<std::size_t>(line)] = readCurrentRasterSnapshot();
}

Ppu::RasterLineSnapshot Ppu::rasterSnapshotForLine(int line) const {
    if (line >= 0 && line < static_cast<int>(VisibleLines)) {
        const std::size_t index = static_cast<std::size_t>(line);
        const RasterLineSnapshot& completed = completedRasterLineSnapshots_[index];
        if (completed.valid) {
            return completed;
        }
        const RasterLineSnapshot& s = rasterLineSnapshots_[index];
        if (s.valid) {
            return s;
        }
    }
    return readCurrentRasterSnapshot();
}

void Ppu::clearAffineLineSnapshots() {
    for (auto& s : bg2LineSnapshots_) {
        s = AffineLineSnapshot{};
    }
    for (auto& s : bg3LineSnapshots_) {
        s = AffineLineSnapshot{};
    }
}

Ppu::AffineLineSnapshot Ppu::readCurrentAffineSnapshot(int bgIndex) const {
    if (memory_ == nullptr || (bgIndex != 2 && bgIndex != 3)) {
        return AffineLineSnapshot{};
    }
    const u32 base = bgIndex == 2 ? 0U : 0x10U;
    AffineLineSnapshot out{};
    out.pa = readIoSigned16(*memory_, kBgAffinePaOffset + base);
    out.pb = readIoSigned16(*memory_, kBgAffinePbOffset + base);
    out.pc = readIoSigned16(*memory_, kBgAffinePcOffset + base);
    out.pd = readIoSigned16(*memory_, kBgAffinePdOffset + base);
    out.xRef = readIoSignedAffineRef28(*memory_, kBgAffineXOffset + base);
    out.yRef = readIoSignedAffineRef28(*memory_, kBgAffineYOffset + base);
    out.valid = true;
    return out;
}

void Ppu::captureAffineLineSnapshot(int line) {
    if (line < 0 || line >= static_cast<int>(VisibleLines)) {
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(line);
    bg2LineSnapshots_[idx] = readCurrentAffineSnapshot(2);
    bg3LineSnapshots_[idx] = readCurrentAffineSnapshot(3);
}

Ppu::AffineLineSnapshot Ppu::affineSnapshotForLine(int bgIndex, int line) const {
    if (bgIndex != 2 && bgIndex != 3) {
        return AffineLineSnapshot{};
    }
    if (line >= 0 && line < static_cast<int>(VisibleLines)) {
        const std::size_t idx = static_cast<std::size_t>(line);
        const AffineLineSnapshot& completed = bgIndex == 2
            ? completedBg2LineSnapshots_[idx]
            : completedBg3LineSnapshots_[idx];
        if (completed.valid) {
            return completed;
        }
        const AffineLineSnapshot& s = bgIndex == 2 ? bg2LineSnapshots_[idx] : bg3LineSnapshots_[idx];
        if (s.valid) {
            return s;
        }
    }
    return readCurrentAffineSnapshot(bgIndex);
}

void Ppu::fillBackdrop(std::array<u16, FramebufferSize>& framebuffer) const {
    const u16 backdrop = bgr555ToRgb565(readBgPaletteColor(0));
    std::fill(framebuffer.begin(), framebuffer.end(), backdrop);
}

std::uint16_t Ppu::scanline() const {
    return scanline_;
}

bool Ppu::inVblank() const {
    return scanline_ >= VisibleLines;
}

bool Ppu::inHblank() const {
    return scanlineCycles_ >= HblankStartCycle;
}

void Ppu::updateIoRegisters() {
    if (memory_ == nullptr) {
        return;
    }

    memory_->writeIo16(VcountOffset, scanline_);

    const u16 previousDispstat = memory_->readIo16(DispstatOffset);
    const bool vblank = inVblank();
    const bool hblank = inHblank();
    const u16 lyc = static_cast<u16>((previousDispstat >> 8U) & 0x00FFU);
    const bool vcounterMatch = lyc == scanline_;

    u16 newDispstat = static_cast<u16>(previousDispstat & ~static_cast<u16>(0x0007U));
    if (vblank) {
        newDispstat = static_cast<u16>(newDispstat | 0x0001U);
    }
    if (hblank) {
        newDispstat = static_cast<u16>(newDispstat | 0x0002U);
    }
    if (vcounterMatch) {
        newDispstat = static_cast<u16>(newDispstat | 0x0004U);
    }
    memory_->writeIo16(DispstatOffset, newDispstat);

    if (!prevVblank_ && vblank && (previousDispstat & 0x0008U) != 0U) {
        memory_->requestInterrupt(static_cast<u16>(1U << 0U));
    }
    if (!prevHblank_ && hblank && (previousDispstat & 0x0010U) != 0U) {
        memory_->requestInterrupt(static_cast<u16>(1U << 1U));
    }
    if (!prevVcounterMatch_ && vcounterMatch && (previousDispstat & 0x0020U) != 0U) {
        memory_->requestInterrupt(static_cast<u16>(1U << 2U));
    }
    if (!prevVblank_ && vblank) {
        memory_->triggerDmaStart(1U);
    }
    if (!prevHblank_ && hblank && !vblank) {
        memory_->triggerDmaStart(2U);
    }

    prevVblank_ = vblank;
    prevHblank_ = hblank;
    prevVcounterMatch_ = vcounterMatch;
}

void Ppu::logSceneCompare() const {
    if (!sceneCompareLoggingEnabled()) {
        return;
    }

    // Build current scene state across all 160 scanlines.
    SceneState cur{};
    const RasterLineSnapshot line0 = rasterSnapshotForLine(0);
    cur.dispcnt = line0.dispcnt;
    cur.bldCnt = line0.bldCnt;
    cur.bldAlpha = line0.bldAlpha;
    cur.bldY = line0.bldY;
    cur.winIn = line0.winIn;
    cur.winOut = line0.winOut;
    cur.win0H = line0.win0H;
    cur.win0V = line0.win0V;
    cur.win1H = line0.win1H;
    cur.win1V = line0.win1V;
    for (int bg = 0; bg < 4; ++bg) {
        cur.bgCnt[bg] = line0.bgCnt[bg];
        cur.hofsMin[bg] = static_cast<u16>(line0.bgHofs[bg] & 0x01FFU);
        cur.hofsMax[bg] = cur.hofsMin[bg];
        cur.vofsMin[bg] = static_cast<u16>(line0.bgVofs[bg] & 0x01FFU);
        cur.vofsMax[bg] = cur.vofsMin[bg];
    }

    u16 prevDispcnt = line0.dispcnt;
    std::array<u16, 4> prevBgCnt = line0.bgCnt;
    for (int y = 1; y < ScreenHeight; ++y) {
        const RasterLineSnapshot line = rasterSnapshotForLine(y);
        if (line.dispcnt != prevDispcnt) {
            ++cur.dispcntChanges;
            prevDispcnt = line.dispcnt;
        }
        for (int bg = 0; bg < 4; ++bg) {
            if (line.bgCnt[bg] != prevBgCnt[bg]) {
                ++cur.bgCntChanges[bg];
                prevBgCnt[bg] = line.bgCnt[bg];
            }
            const u16 h = static_cast<u16>(line.bgHofs[bg] & 0x01FFU);
            const u16 v = static_cast<u16>(line.bgVofs[bg] & 0x01FFU);
            if (h < cur.hofsMin[bg]) cur.hofsMin[bg] = h;
            if (h > cur.hofsMax[bg]) cur.hofsMax[bg] = h;
            if (v < cur.vofsMin[bg]) cur.vofsMin[bg] = v;
            if (v > cur.vofsMax[bg]) cur.vofsMax[bg] = v;
        }
    }

    // Detect whether anything changed from previous frame.
    const bool changed = !hasPrevScene
        || cur.dispcnt != prevSceneState.dispcnt
        || cur.bgCnt != prevSceneState.bgCnt
        || cur.bldCnt != prevSceneState.bldCnt
        || cur.bldAlpha != prevSceneState.bldAlpha
        || cur.bldY != prevSceneState.bldY
        || cur.winIn != prevSceneState.winIn
        || cur.winOut != prevSceneState.winOut
        || cur.hofsMin != prevSceneState.hofsMin
        || cur.hofsMax != prevSceneState.hofsMax
        || cur.vofsMin != prevSceneState.vofsMin
        || cur.vofsMax != prevSceneState.vofsMax;

    if (changed) {
        const u16 mode = static_cast<u16>(cur.dispcnt & 0x0007U);
        std::cerr << "[SCENE] frame=" << sceneFrameNumber
                  << " mode=" << mode
                  << std::hex << " DISPCNT=0x" << cur.dispcnt << std::dec
                  << " dispcntMidFrameChanges=" << cur.dispcntChanges
                  << "\n";
        for (int bg = 0; bg < 4; ++bg) {
            const u16 cnt = cur.bgCnt[bg];
            const bool en = (cur.dispcnt & static_cast<u16>(0x0100U << static_cast<unsigned>(bg))) != 0U;
            const u8 prio = static_cast<u8>(cnt & 0x3U);
            const u32 charBlk = static_cast<u32>((cnt >> 2U) & 0x3U);
            const u32 charBase = charBlk * 0x4000U;
            const bool is256 = (cnt & 0x0080U) != 0U;
            const u32 scrBlk = static_cast<u32>((cnt >> 8U) & 0x1FU);
            const u32 screenBase = scrBlk * 0x800U;
            const u32 sizeIdx = static_cast<u32>((cnt >> 14U) & 0x3U);
            const u32 sw = textBgScreenWidth(sizeIdx);
            const u32 sh = textBgScreenHeight(sizeIdx);
            const bool mosaic = (cnt & 0x0040U) != 0U;
            std::cerr << "[SCENE]   BG" << bg
                      << std::hex << " CNT=0x" << cnt << std::dec
                      << " en=" << en
                      << " prio=" << static_cast<unsigned>(prio)
                      << " charBlk=" << charBlk
                      << std::hex << " charBase=0x" << charBase << std::dec
                      << " scrBlk=" << scrBlk
                      << std::hex << " scrBase=0x" << screenBase << std::dec
                      << " size=" << sizeIdx << "(" << sw << "x" << sh << ")"
                      << " bpp=" << (is256 ? 8 : 4)
                      << " mosaic=" << mosaic
                      << " hofs=[" << cur.hofsMin[bg] << ".." << cur.hofsMax[bg] << "]"
                      << " vofs=[" << cur.vofsMin[bg] << ".." << cur.vofsMax[bg] << "]"
                      << " cntMidFrameChanges=" << cur.bgCntChanges[bg]
                      << "\n";
        }
        // Blend
        const u8 blendMode = static_cast<u8>((cur.bldCnt >> 6U) & 0x3U);
        const u8 eva = static_cast<u8>(cur.bldAlpha & 0x1FU);
        const u8 evb = static_cast<u8>((cur.bldAlpha >> 8U) & 0x1FU);
        const u8 evy = static_cast<u8>(cur.bldY & 0x1FU);
        std::cerr << "[SCENE]   BLEND"
                  << std::hex
                  << " BLDCNT=0x" << cur.bldCnt
                  << " BLDALPHA=0x" << cur.bldAlpha
                  << " BLDY=0x" << cur.bldY
                  << std::dec
                  << " mode=" << static_cast<unsigned>(blendMode)
                  << " EVA=" << static_cast<unsigned>(eva)
                  << " EVB=" << static_cast<unsigned>(evb)
                  << " EVY=" << static_cast<unsigned>(evy)
                  << "\n";
        // Window
        std::cerr << "[SCENE]   WIN"
                  << std::hex
                  << " IN=0x" << cur.winIn
                  << " OUT=0x" << cur.winOut
                  << " W0H=0x" << cur.win0H
                  << " W0V=0x" << cur.win0V
                  << " W1H=0x" << cur.win1H
                  << " W1V=0x" << cur.win1V
                  << std::dec << "\n";
        // Per-scanline HOFS/VOFS detail for any BG that varies across scanlines
        for (int bg = 0; bg < 4; ++bg) {
            const bool en = (cur.dispcnt & static_cast<u16>(0x0100U << static_cast<unsigned>(bg))) != 0U;
            if (!en) continue;
            if (cur.hofsMin[bg] == cur.hofsMax[bg] && cur.vofsMin[bg] == cur.vofsMax[bg]) {
                continue;  // No per-scanline variance — skip detail
            }
            std::cerr << "[SCENE]   BG" << bg << " per-scanline scroll: ";
            for (int y = 0; y < ScreenHeight; y += 8) {
                const RasterLineSnapshot line = rasterSnapshotForLine(y);
                const u16 h = static_cast<u16>(line.bgHofs[bg] & 0x01FFU);
                const u16 v = static_cast<u16>(line.bgVofs[bg] & 0x01FFU);
                std::cerr << "y" << y << "=(" << h << "," << v << ") ";
            }
            std::cerr << "\n";
        }
    }
    prevSceneState = cur;
    hasPrevScene = true;
    ++sceneFrameNumber;
}

u16 Ppu::bgr555ToRgb565(u16 pixel) {
    const u16 r5 = static_cast<u16>(pixel & 0x1FU);
    const u16 g5 = static_cast<u16>((pixel >> 5U) & 0x1FU);
    const u16 b5 = static_cast<u16>((pixel >> 10U) & 0x1FU);
    const u16 g6 = static_cast<u16>((g5 << 1U) | (g5 >> 4U));
    return static_cast<u16>((r5 << 11U) | (g6 << 5U) | b5);
}

} // namespace gb::gba
