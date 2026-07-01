#pragma once

#include <array>
#include <cstdint>

#include "gb/core/gba/memory.hpp"
#include "gb/core/types.hpp"

namespace gb::gba {

class Ppu {
public:
    static constexpr int ScreenWidth = 240;
    static constexpr int ScreenHeight = 160;
    static constexpr std::size_t FramebufferSize = static_cast<std::size_t>(ScreenWidth) * static_cast<std::size_t>(ScreenHeight);

    struct PixelDebugInfo {
        bool valid = false;
        int x = 0;
        int y = 0;
        u16 dispcnt = 0;
        u16 bldCnt = 0;
        u16 bldAlpha = 0;
        u16 bldY = 0;
        u16 win0H = 0;
        u16 win0V = 0;
        u16 win1H = 0;
        u16 win1V = 0;
        u16 winIn = 0;
        u16 winOut = 0;
        u16 backdropRawColor555 = 0;
        u16 finalRawColor555 = 0;
        u16 finalRgb565 = 0;
        u8 windowMask = 0;
        u8 topLayer = 0;
        u8 topPriority = 0;
        u8 secondLayer = 0;
        u8 secondPriority = 0;
        bool hasSecond = false;
        bool semiTransparentObj = false;
        bool insideWin0 = false;
        bool insideWin1 = false;
        bool insideObjWin = false;
        u8 blendMode = 0;
        u8 eva = 0;
        u8 evb = 0;
        u8 evy = 0;
        bool colorEffectEnabledByWindow = false;
        bool firstTarget = false;
        bool secondTarget = false;
        bool alphaBlendRequested = false;
        bool alphaBlendApplied = false;
        bool brightenApplied = false;
        bool darkenApplied = false;
    };

    struct TextBgDebugSample {
        bool valid = false;
        bool visible = false;
        int bgIndex = 0;
        int screenX = 0;
        int screenY = 0;
        u16 dispcnt = 0;
        u16 bgcnt = 0;
        u16 hofs = 0;
        u16 vofs = 0;
        u8 priority = 0;
        bool color256 = false;
        u8 sizeIndex = 0;
        u32 charBase = 0;
        u32 screenBase = 0;
        u32 screenWidth = 0;
        u32 screenHeight = 0;
        u32 sourceX = 0;
        u32 sourceY = 0;
        u32 tileX = 0;
        u32 tileY = 0;
        u8 pixelX = 0;
        u8 pixelY = 0;
        u8 blockX = 0;
        u8 blockY = 0;
        u8 screenBlock = 0;
        u16 mapEntry = 0;
        u16 tileNumber = 0;
        bool hflip = false;
        bool vflip = false;
        u8 paletteBank = 0;
        u8 colorIndex = 0;
        u32 mapAddress = 0;
        u32 tileAddress = 0;
    };

    struct RenderStats {
        std::uint64_t totalNs = 0;
        std::uint64_t bgNs = 0;
        std::uint64_t objNs = 0;
        std::uint64_t objWindowNs = 0;
        std::uint64_t composeNs = 0;
        std::uint32_t visibleObjectsFrame = 0;
        std::uint32_t objPixelsTested = 0;
        std::uint32_t objPixelsDrawn = 0;
        std::uint32_t objWindowPixels = 0;
        std::uint16_t maxVisibleObjectsOnScanline = 0;
        std::array<std::uint16_t, ScreenHeight> visibleObjectsPerScanline{};
    };

    static constexpr u32 DispcntOffset = 0x0000U;
    static constexpr u32 DispstatOffset = 0x0004U;
    static constexpr u32 VcountOffset = 0x0006U;

    static constexpr std::uint16_t VisibleLines = 160;
    static constexpr std::uint16_t TotalLines = 228;
    static constexpr std::uint32_t CyclesPerLine = 1232;
    static constexpr std::uint32_t HblankStartCycle = 1006;

    void connectMemory(Memory* memory);
    void reset();
    void step(int cpuCycles);

    [[nodiscard]] bool render(std::array<u16, FramebufferSize>& framebuffer) const;
    [[nodiscard]] bool debugPixel(int x, int y, PixelDebugInfo& out) const;
    [[nodiscard]] bool debugTextBgSample(int bgIndex, int x, int y, TextBgDebugSample& out) const;
    [[nodiscard]] const RenderStats& lastRenderStats() const;

    [[nodiscard]] std::uint16_t scanline() const;
    [[nodiscard]] bool inVblank() const;
    [[nodiscard]] bool inHblank() const;

private:
    struct DebugConfig {
        bool disableBg = false;
        bool disableObj = false;
        bool disableBlend = false;
        bool disableWindow = false;
        bool objBoundingBoxesOnly = false;
        int logObjScanline = -1;
        u8 bgMask = 0x0F;
        bool anySelectedObjIds = false;
        std::array<bool, 128> selectedObjIds{};
    };

    struct AffineLineSnapshot {
        std::int32_t pa = 0;
        std::int32_t pb = 0;
        std::int32_t pc = 0;
        std::int32_t pd = 0;
        std::int32_t xRef = 0;
        std::int32_t yRef = 0;
        bool valid = false;
    };

    struct LayerPixel {
        u16 color = 0;
        u16 rawColor555 = 0;
        u16 secondRawColor555 = 0;
        u8 priority = 4;
        u8 secondPriority = 4;
        u8 layer = 0;
        u8 secondLayer = 0;
        bool opaque = false;
        bool hasSecond = false;
        bool semiTransparentObj = false;
    };

    struct RasterLineSnapshot {
        u16 dispcnt = 0;
        u16 win0H = 0;
        u16 win1H = 0;
        u16 win0V = 0;
        u16 win1V = 0;
        u16 winIn = 0;
        u16 winOut = 0;
        u16 mosaic = 0;
        u16 bldCnt = 0;
        u16 bldAlpha = 0;
        u16 bldY = 0;
        std::array<u16, 4> bgCnt{};
        std::array<u16, 4> bgHofs{};
        std::array<u16, 4> bgVofs{};
        bool valid = false;
    };

    static constexpr u32 Bg0CntOffset = 0x0008U;
    static constexpr u32 Bg0HofsOffset = 0x0010U;
    static constexpr u32 Bg0VofsOffset = 0x0012U;

    [[nodiscard]] bool renderMode0(std::array<u16, FramebufferSize>& framebuffer) const;
    [[nodiscard]] bool renderMode1(std::array<u16, FramebufferSize>& framebuffer) const;
    [[nodiscard]] bool renderMode2(std::array<u16, FramebufferSize>& framebuffer) const;
    [[nodiscard]] bool renderMode3(std::array<u16, FramebufferSize>& framebuffer) const;
    [[nodiscard]] bool renderMode4(std::array<u16, FramebufferSize>& framebuffer) const;
    [[nodiscard]] bool renderMode5(std::array<u16, FramebufferSize>& framebuffer) const;
    [[nodiscard]] bool buildDebugLayerPixelsMode012(
        std::array<LayerPixel, FramebufferSize>& layerPixels,
        std::array<bool, FramebufferSize>& objWindowMask,
        u16& backdropRaw
    ) const;
    void renderTextBackground(
        int bgIndex,
        std::array<LayerPixel, FramebufferSize>& layerPixels
    ) const;
    [[nodiscard]] bool decodeTextBgSample(
        const RasterLineSnapshot& line,
        int bgIndex,
        int screenX,
        int screenY,
        TextBgDebugSample& out
    ) const;
    void renderAffineBackground(
        int bgIndex,
        std::array<LayerPixel, FramebufferSize>& layerPixels
    ) const;
    void renderObjects(std::array<LayerPixel, FramebufferSize>& layerPixels) const;
    void buildObjWindowMask(std::array<bool, FramebufferSize>& objWindowMask) const;
    void composeLayer(
        std::array<LayerPixel, FramebufferSize>& layerPixels,
        std::size_t pixelIndex,
        u16 rawColor555,
        u8 priority,
        u8 layer,
        bool semiTransparentObj = false
    ) const;
    [[nodiscard]] u16 applyColorEffect(
        const LayerPixel& pixel,
        const RasterLineSnapshot& line,
        u16 backdropRaw,
        u8 windowMask
    ) const;
    [[nodiscard]] u8 windowMaskForPixel(int x, int y) const;
    [[nodiscard]] u8 windowMaskForPixel(int x, int y, const RasterLineSnapshot& line) const;
    [[nodiscard]] u8 windowMaskForPixelUncached(int x, int y, const RasterLineSnapshot& line) const;
    [[nodiscard]] static bool pointInsideWindowRange(int value, int start, int end, int limit);
    [[nodiscard]] static bool pointInsideWindowRect(int x, int y, u16 winH, u16 winV);
    [[nodiscard]] static bool layerEnabledByWindowMask(u8 mask, u8 layerBit);
    [[nodiscard]] static bool bgEnabledByDebugMask(const DebugConfig& config, int bgIndex);
    [[nodiscard]] static u16 blendAlpha555(u16 top, u16 bottom, u8 eva, u8 evb);
    [[nodiscard]] static u16 brighten555(u16 color, u8 evy);
    [[nodiscard]] static u16 darken555(u16 color, u8 evy);
    [[nodiscard]] static u8 blendLayerBitFromLayerId(u8 layerId);

    [[nodiscard]] u16 readVram16(std::size_t byteIndex) const;
    [[nodiscard]] u8 readBgVram8(std::size_t byteIndex) const;
    [[nodiscard]] u16 readBgVram16(std::size_t byteIndex) const;
    [[nodiscard]] u16 readOam16(std::size_t byteIndex) const;
    [[nodiscard]] u16 readBgPaletteColor(u8 colorIndex) const;
    [[nodiscard]] u16 readObjPaletteColor(u8 colorIndex) const;
    [[nodiscard]] const std::array<u8, Memory::VramSize>& activeVram() const;
    [[nodiscard]] const std::array<u8, Memory::PramSize>& activePram() const;
    [[nodiscard]] const std::array<u8, Memory::OamSize>& activeOam() const;
    [[nodiscard]] static DebugConfig readDebugConfig();
    [[nodiscard]] bool shouldLogObject(int objIndex) const;
    [[nodiscard]] static u16 debugOutlineColor555(int objPriority);
    void noteVisibleObjectOnScanlines(int startY, int endY) const;

    void fillBackdrop(std::array<u16, FramebufferSize>& framebuffer) const;
    void clearRasterLineSnapshots();
    [[nodiscard]] RasterLineSnapshot readCurrentRasterSnapshot() const;
    void captureRasterLineSnapshot(int line);
    [[nodiscard]] RasterLineSnapshot rasterSnapshotForLine(int line) const;
    void clearAffineLineSnapshots();
    void captureAffineLineSnapshot(int line);
    [[nodiscard]] AffineLineSnapshot readCurrentAffineSnapshot(int bgIndex) const;
    [[nodiscard]] AffineLineSnapshot affineSnapshotForLine(int bgIndex, int line) const;
    void updateIoRegisters();
    void logSceneCompare() const;
    [[nodiscard]] static u16 bgr555ToRgb565(u16 pixel);

    Memory* memory_ = nullptr;
    std::uint32_t scanlineCycles_ = 0;
    std::uint16_t scanline_ = 0;
    bool prevVblank_ = false;
    bool prevHblank_ = false;
    bool prevVcounterMatch_ = false;
    std::array<RasterLineSnapshot, VisibleLines> rasterLineSnapshots_{};
    std::array<RasterLineSnapshot, VisibleLines> completedRasterLineSnapshots_{};
    std::array<AffineLineSnapshot, VisibleLines> bg2LineSnapshots_{};
    std::array<AffineLineSnapshot, VisibleLines> bg3LineSnapshots_{};
    std::array<AffineLineSnapshot, VisibleLines> completedBg2LineSnapshots_{};
    std::array<AffineLineSnapshot, VisibleLines> completedBg3LineSnapshots_{};
    std::array<u8, Memory::VramSize> completedVram_{};
    std::array<u8, Memory::PramSize> completedPram_{};
    std::array<u8, Memory::OamSize> completedOam_{};
    bool completedMemorySnapshotValid_ = false;
    mutable DebugConfig activeDebugConfig_{};
    mutable RenderStats lastRenderStats_{};
    mutable const std::array<bool, FramebufferSize>* activeObjWindowMask_ = nullptr;
    mutable bool windowMaskCacheEnabled_ = false;
    mutable std::array<u8, FramebufferSize> windowMaskCache_{};
    mutable std::array<bool, VisibleLines> windowMaskCacheLineReady_{};
    mutable std::array<LayerPixel, FramebufferSize> layerScratch_{};
    mutable std::array<bool, FramebufferSize> objWindowScratch_{};
};

} // namespace gb::gba
