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

    [[nodiscard]] std::uint16_t scanline() const;
    [[nodiscard]] bool inVblank() const;
    [[nodiscard]] bool inHblank() const;

private:
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
    void renderTextBackground(
        int bgIndex,
        std::array<LayerPixel, FramebufferSize>& layerPixels
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
    [[nodiscard]] u8 windowMaskForPixel(int x, int y) const;
    [[nodiscard]] u8 windowMaskForPixel(int x, int y, const RasterLineSnapshot& line) const;
    [[nodiscard]] static bool pointInsideWindowRange(int value, int start, int end, int limit);
    [[nodiscard]] static bool pointInsideWindowRect(int x, int y, u16 winH, u16 winV);
    [[nodiscard]] static bool layerEnabledByWindowMask(u8 mask, u8 layerBit);
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
    mutable const std::array<bool, FramebufferSize>* activeObjWindowMask_ = nullptr;
};

} // namespace gb::gba
