#include "gb/core/gba/ppu.hpp"

#include <algorithm>

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

constexpr std::size_t kBitmapPage1Offset = 0xA000U;
constexpr std::size_t kObjTileBaseMode012Offset = 0x10000U;
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
constexpr u32 kBldCntOffset = 0x0050U;
constexpr u32 kBldAlphaOffset = 0x0052U;
constexpr u32 kBldYOffset = 0x0054U;

int32_t readIoSigned16(const Memory& memory, u32 offset) {
    return static_cast<int32_t>(static_cast<std::int16_t>(memory.readIo16(offset)));
}

int32_t readIoSigned32(const Memory& memory, u32 offset) {
    const u32 lo = memory.readIo16(offset);
    const u32 hi = memory.readIo16(offset + 2U);
    const u32 value = lo | (hi << 16U);
    return static_cast<int32_t>(value);
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

std::size_t mode0ScreenBlockOffset(u32 sizeIndex, u32 tileX, u32 tileY) {
    const u32 blockX = tileX / 32U;
    const u32 blockY = tileY / 32U;
    if (sizeIndex == 0U) {
        return 0U;
    }
    if (sizeIndex == 1U) { // 512x256
        return static_cast<std::size_t>(blockX * 0x800U);
    }
    if (sizeIndex == 2U) { // 256x512
        return static_cast<std::size_t>(blockY * 0x800U);
    }
    // 512x512
    return static_cast<std::size_t>((blockY * 2U + blockX) * 0x800U);
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
    updateIoRegisters();
}

void Ppu::step(int cpuCycles) {
    if (memory_ == nullptr || cpuCycles <= 0) {
        return;
    }

    scanlineCycles_ += static_cast<std::uint32_t>(cpuCycles);
    while (scanlineCycles_ >= CyclesPerLine) {
        scanlineCycles_ -= CyclesPerLine;
        scanline_ = static_cast<std::uint16_t>((scanline_ + 1U) % TotalLines);
    }

    updateIoRegisters();
}

bool Ppu::render(std::array<u16, FramebufferSize>& framebuffer) const {
    if (memory_ == nullptr) {
        return false;
    }

    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    if ((dispcnt & kForcedBlankMask) != 0U) {
        std::fill(framebuffer.begin(), framebuffer.end(), static_cast<u16>(0xFFFFU));
        return true;
    }
    const u16 mode = static_cast<u16>(dispcnt & kDisplayModeMask);
    switch (mode) {
    case 0U:
        return renderMode0(framebuffer);
    case 1U:
        return renderMode1(framebuffer);
    case 2U:
        return renderMode2(framebuffer);
    case 3U:
        return renderMode3(framebuffer);
    case 4U:
        return renderMode4(framebuffer);
    case 5U:
        return renderMode5(framebuffer);
    default:
        return false;
    }
}

bool Ppu::renderMode0(std::array<u16, FramebufferSize>& framebuffer) const {
    std::array<LayerPixel, FramebufferSize> layerPixels{};
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

    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    for (int bg = 0; bg < 4; ++bg) {
        if ((dispcnt & kBgEnableMasks[bg]) != 0U) {
            renderTextBackground(bg, layerPixels);
        }
    }
    if ((dispcnt & kObjEnableMask) != 0U) {
        renderObjects(layerPixels);
    }

    const u16 bldcnt = memory_->readIo16(kBldCntOffset);
    const u16 bldalpha = memory_->readIo16(kBldAlphaOffset);
    const u16 bldy = memory_->readIo16(kBldYOffset);
    const u8 blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldalpha & 0x1FU)));
    const u8 evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((bldalpha >> 8U) & 0x1FU)));
    const u8 evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldy & 0x1FU)));

    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            u16 outRaw = px.rawColor555;
            const u8 windowMask = windowMaskForPixel(x, y);
            const bool colorEffectsEnabled = (windowMask & 0x20U) != 0U;
            if (colorEffectsEnabled) {
                const u8 topLayerBit = blendLayerBitFromLayerId(px.layer);
                const bool firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;
                const bool alphaLike = (blendMode == 1U) || px.semiTransparentObj;
                if (alphaLike && (firstTarget || px.semiTransparentObj)) {
                    bool hasSecond = px.hasSecond;
                    u16 secondRaw = px.secondRawColor555;
                    u8 secondLayerBit = blendLayerBitFromLayerId(px.secondLayer);
                    if (!hasSecond && px.layer != 4U) {
                        hasSecond = true;
                        secondRaw = backdropRaw;
                        secondLayerBit = 5U;
                    }
                    if (hasSecond && (bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U) {
                        outRaw = blendAlpha555(outRaw, secondRaw, eva, evb);
                    }
                } else if (blendMode == 2U && firstTarget) {
                    outRaw = brighten555(outRaw, evy);
                } else if (blendMode == 3U && firstTarget) {
                    outRaw = darken555(outRaw, evy);
                }
            }
            framebuffer[i] = bgr555ToRgb565(outRaw);
        }
    }
    return true;
}

bool Ppu::renderMode1(std::array<u16, FramebufferSize>& framebuffer) const {
    std::array<LayerPixel, FramebufferSize> layerPixels{};
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

    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    if ((dispcnt & kBgEnableMasks[0]) != 0U) {
        renderTextBackground(0, layerPixels);
    }
    if ((dispcnt & kBgEnableMasks[1]) != 0U) {
        renderTextBackground(1, layerPixels);
    }
    if ((dispcnt & kBgEnableMasks[2]) != 0U) {
        renderAffineBackground(2, layerPixels);
    }
    if ((dispcnt & kObjEnableMask) != 0U) {
        renderObjects(layerPixels);
    }

    const u16 bldcnt = memory_->readIo16(kBldCntOffset);
    const u16 bldalpha = memory_->readIo16(kBldAlphaOffset);
    const u16 bldy = memory_->readIo16(kBldYOffset);
    const u8 blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldalpha & 0x1FU)));
    const u8 evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((bldalpha >> 8U) & 0x1FU)));
    const u8 evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldy & 0x1FU)));

    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            u16 outRaw = px.rawColor555;
            const u8 windowMask = windowMaskForPixel(x, y);
            const bool colorEffectsEnabled = (windowMask & 0x20U) != 0U;
            if (colorEffectsEnabled) {
                const u8 topLayerBit = blendLayerBitFromLayerId(px.layer);
                const bool firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;
                const bool alphaLike = (blendMode == 1U) || px.semiTransparentObj;
                if (alphaLike && (firstTarget || px.semiTransparentObj)) {
                    bool hasSecond = px.hasSecond;
                    u16 secondRaw = px.secondRawColor555;
                    u8 secondLayerBit = blendLayerBitFromLayerId(px.secondLayer);
                    if (!hasSecond && px.layer != 4U) {
                        hasSecond = true;
                        secondRaw = backdropRaw;
                        secondLayerBit = 5U;
                    }
                    if (hasSecond && (bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U) {
                        outRaw = blendAlpha555(outRaw, secondRaw, eva, evb);
                    }
                } else if (blendMode == 2U && firstTarget) {
                    outRaw = brighten555(outRaw, evy);
                } else if (blendMode == 3U && firstTarget) {
                    outRaw = darken555(outRaw, evy);
                }
            }
            framebuffer[i] = bgr555ToRgb565(outRaw);
        }
    }
    return true;
}

bool Ppu::renderMode2(std::array<u16, FramebufferSize>& framebuffer) const {
    std::array<LayerPixel, FramebufferSize> layerPixels{};
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

    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    if ((dispcnt & kBgEnableMasks[2]) != 0U) {
        renderAffineBackground(2, layerPixels);
    }
    if ((dispcnt & kBgEnableMasks[3]) != 0U) {
        renderAffineBackground(3, layerPixels);
    }
    if ((dispcnt & kObjEnableMask) != 0U) {
        renderObjects(layerPixels);
    }

    const u16 bldcnt = memory_->readIo16(kBldCntOffset);
    const u16 bldalpha = memory_->readIo16(kBldAlphaOffset);
    const u16 bldy = memory_->readIo16(kBldYOffset);
    const u8 blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldalpha & 0x1FU)));
    const u8 evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((bldalpha >> 8U) & 0x1FU)));
    const u8 evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldy & 0x1FU)));

    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            u16 outRaw = px.rawColor555;
            const u8 windowMask = windowMaskForPixel(x, y);
            const bool colorEffectsEnabled = (windowMask & 0x20U) != 0U;
            if (colorEffectsEnabled) {
                const u8 topLayerBit = blendLayerBitFromLayerId(px.layer);
                const bool firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;
                const bool alphaLike = (blendMode == 1U) || px.semiTransparentObj;
                if (alphaLike && (firstTarget || px.semiTransparentObj)) {
                    bool hasSecond = px.hasSecond;
                    u16 secondRaw = px.secondRawColor555;
                    u8 secondLayerBit = blendLayerBitFromLayerId(px.secondLayer);
                    if (!hasSecond && px.layer != 4U) {
                        hasSecond = true;
                        secondRaw = backdropRaw;
                        secondLayerBit = 5U;
                    }
                    if (hasSecond && (bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U) {
                        outRaw = blendAlpha555(outRaw, secondRaw, eva, evb);
                    }
                } else if (blendMode == 2U && firstTarget) {
                    outRaw = brighten555(outRaw, evy);
                } else if (blendMode == 3U && firstTarget) {
                    outRaw = darken555(outRaw, evy);
                }
            }
            framebuffer[i] = bgr555ToRgb565(outRaw);
        }
    }
    return true;
}

bool Ppu::renderMode3(std::array<u16, FramebufferSize>& framebuffer) const {
    std::array<LayerPixel, FramebufferSize> layerPixels{};
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
    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    const u8 bgPriority = static_cast<u8>(memory_->readIo16(Bg0CntOffset + 4U) & 0x3U);
    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const u8 winMask = windowMaskForPixel(x, y);
            if (!layerEnabledByWindowMask(winMask, 2U)) {
                continue;
            }
            const auto byteIndex = pixelIndex * 2U;
            composeLayer(layerPixels, pixelIndex, readVram16(byteIndex), bgPriority, 2U);
        }
    }

    if ((dispcnt & kObjEnableMask) != 0U) {
        renderObjects(layerPixels);
    }

    const u16 bldcnt = memory_->readIo16(kBldCntOffset);
    const u16 bldalpha = memory_->readIo16(kBldAlphaOffset);
    const u16 bldy = memory_->readIo16(kBldYOffset);
    const u8 blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldalpha & 0x1FU)));
    const u8 evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((bldalpha >> 8U) & 0x1FU)));
    const u8 evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldy & 0x1FU)));
    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            u16 outRaw = px.rawColor555;
            const u8 windowMask = windowMaskForPixel(x, y);
            const bool colorEffectsEnabled = (windowMask & 0x20U) != 0U;
            if (colorEffectsEnabled) {
                const u8 topLayerBit = blendLayerBitFromLayerId(px.layer);
                const bool firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;
                const bool alphaLike = (blendMode == 1U) || px.semiTransparentObj;
                if (alphaLike && (firstTarget || px.semiTransparentObj)) {
                    bool hasSecond = px.hasSecond;
                    u16 secondRaw = px.secondRawColor555;
                    u8 secondLayerBit = blendLayerBitFromLayerId(px.secondLayer);
                    if (!hasSecond && px.layer != 4U) {
                        hasSecond = true;
                        secondRaw = backdropRaw;
                        secondLayerBit = 5U;
                    }
                    if (hasSecond && (bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U) {
                        outRaw = blendAlpha555(outRaw, secondRaw, eva, evb);
                    }
                } else if (blendMode == 2U && firstTarget) {
                    outRaw = brighten555(outRaw, evy);
                } else if (blendMode == 3U && firstTarget) {
                    outRaw = darken555(outRaw, evy);
                }
            }
            framebuffer[i] = bgr555ToRgb565(outRaw);
        }
    }
    return true;
}

bool Ppu::renderMode4(std::array<u16, FramebufferSize>& framebuffer) const {
    std::array<LayerPixel, FramebufferSize> layerPixels{};
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

    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    const bool frame1 = (dispcnt & kFrameSelectMask) != 0U;
    const std::size_t pageBase = frame1 ? kBitmapPage1Offset : 0U;
    const auto& vram = memory_->vram();
    const u8 bgPriority = static_cast<u8>(memory_->readIo16(Bg0CntOffset + 4U) & 0x3U);
    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const u8 winMask = windowMaskForPixel(x, y);
            if (!layerEnabledByWindowMask(winMask, 2U)) {
                continue;
            }
            const std::size_t byteIndex = pageBase + pixelIndex;
            const u8 colorIndex = byteIndex < vram.size() ? vram[byteIndex] : 0U;
            composeLayer(layerPixels, pixelIndex, readBgPaletteColor(colorIndex), bgPriority, 2U);
        }
    }

    if ((dispcnt & kObjEnableMask) != 0U) {
        renderObjects(layerPixels);
    }

    const u16 bldcnt = memory_->readIo16(kBldCntOffset);
    const u16 bldalpha = memory_->readIo16(kBldAlphaOffset);
    const u16 bldy = memory_->readIo16(kBldYOffset);
    const u8 blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldalpha & 0x1FU)));
    const u8 evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((bldalpha >> 8U) & 0x1FU)));
    const u8 evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldy & 0x1FU)));
    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            u16 outRaw = px.rawColor555;
            const u8 windowMask = windowMaskForPixel(x, y);
            const bool colorEffectsEnabled = (windowMask & 0x20U) != 0U;
            if (colorEffectsEnabled) {
                const u8 topLayerBit = blendLayerBitFromLayerId(px.layer);
                const bool firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;
                const bool alphaLike = (blendMode == 1U) || px.semiTransparentObj;
                if (alphaLike && (firstTarget || px.semiTransparentObj)) {
                    bool hasSecond = px.hasSecond;
                    u16 secondRaw = px.secondRawColor555;
                    u8 secondLayerBit = blendLayerBitFromLayerId(px.secondLayer);
                    if (!hasSecond && px.layer != 4U) {
                        hasSecond = true;
                        secondRaw = backdropRaw;
                        secondLayerBit = 5U;
                    }
                    if (hasSecond && (bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U) {
                        outRaw = blendAlpha555(outRaw, secondRaw, eva, evb);
                    }
                } else if (blendMode == 2U && firstTarget) {
                    outRaw = brighten555(outRaw, evy);
                } else if (blendMode == 3U && firstTarget) {
                    outRaw = darken555(outRaw, evy);
                }
            }
            framebuffer[i] = bgr555ToRgb565(outRaw);
        }
    }
    return true;
}

bool Ppu::renderMode5(std::array<u16, FramebufferSize>& framebuffer) const {
    std::array<LayerPixel, FramebufferSize> layerPixels{};
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

    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    const bool frame1 = (dispcnt & kFrameSelectMask) != 0U;
    const std::size_t pageBase = frame1 ? kBitmapPage1Offset : 0U;

    constexpr int kMode5Width = 160;
    constexpr int kMode5Height = 128;
    const u8 bgPriority = static_cast<u8>(memory_->readIo16(Bg0CntOffset + 4U) & 0x3U);
    for (int y = 0; y < kMode5Height; ++y) {
        for (int x = 0; x < kMode5Width; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const u8 winMask = windowMaskForPixel(x, y);
            if (!layerEnabledByWindowMask(winMask, 2U)) {
                continue;
            }
            const auto byteIndex = pageBase
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(kMode5Width)
                + static_cast<std::size_t>(x)) * 2U;
            composeLayer(layerPixels, pixelIndex, readVram16(byteIndex), bgPriority, 2U);
        }
    }

    if ((dispcnt & kObjEnableMask) != 0U) {
        renderObjects(layerPixels);
    }

    const u16 bldcnt = memory_->readIo16(kBldCntOffset);
    const u16 bldalpha = memory_->readIo16(kBldAlphaOffset);
    const u16 bldy = memory_->readIo16(kBldYOffset);
    const u8 blendMode = static_cast<u8>((bldcnt >> 6U) & 0x3U);
    const u8 eva = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldalpha & 0x1FU)));
    const u8 evb = static_cast<u8>(std::min<u16>(16U, static_cast<u16>((bldalpha >> 8U) & 0x1FU)));
    const u8 evy = static_cast<u8>(std::min<u16>(16U, static_cast<u16>(bldy & 0x1FU)));
    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const LayerPixel& px = layerPixels[i];
            u16 outRaw = px.rawColor555;
            const u8 windowMask = windowMaskForPixel(x, y);
            const bool colorEffectsEnabled = (windowMask & 0x20U) != 0U;
            if (colorEffectsEnabled) {
                const u8 topLayerBit = blendLayerBitFromLayerId(px.layer);
                const bool firstTarget = (bldcnt & static_cast<u16>(1U << topLayerBit)) != 0U;
                const bool alphaLike = (blendMode == 1U) || px.semiTransparentObj;
                if (alphaLike && (firstTarget || px.semiTransparentObj)) {
                    bool hasSecond = px.hasSecond;
                    u16 secondRaw = px.secondRawColor555;
                    u8 secondLayerBit = blendLayerBitFromLayerId(px.secondLayer);
                    if (!hasSecond && px.layer != 4U) {
                        hasSecond = true;
                        secondRaw = backdropRaw;
                        secondLayerBit = 5U;
                    }
                    if (hasSecond && (bldcnt & static_cast<u16>(1U << (8U + secondLayerBit))) != 0U) {
                        outRaw = blendAlpha555(outRaw, secondRaw, eva, evb);
                    }
                } else if (blendMode == 2U && firstTarget) {
                    outRaw = brighten555(outRaw, evy);
                } else if (blendMode == 3U && firstTarget) {
                    outRaw = darken555(outRaw, evy);
                }
            }
            framebuffer[i] = bgr555ToRgb565(outRaw);
        }
    }
    return true;
}

void Ppu::renderTextBackground(int bgIndex, std::array<LayerPixel, FramebufferSize>& layerPixels) const {
    const u32 cntOffset = Bg0CntOffset + static_cast<u32>(bgIndex) * 2U;
    const u16 bgcnt = memory_->readIo16(cntOffset);
    const u16 hofs = static_cast<u16>(memory_->readIo16(Bg0HofsOffset + static_cast<u32>(bgIndex) * 4U) & 0x01FFU);
    const u16 vofs = static_cast<u16>(memory_->readIo16(Bg0VofsOffset + static_cast<u32>(bgIndex) * 4U) & 0x01FFU);
    const u8 priority = static_cast<u8>(bgcnt & 0x3U);
    const bool color256 = (bgcnt & 0x0080U) != 0U;
    const u32 charBase = static_cast<u32>((bgcnt >> 2U) & 0x3U) * 0x4000U;
    const u32 screenBase = static_cast<u32>((bgcnt >> 8U) & 0x1FU) * 0x800U;
    const u32 sizeIndex = static_cast<u32>((bgcnt >> 14U) & 0x3U);

    const u32 screenWidth = (sizeIndex == 1U || sizeIndex == 3U) ? 512U : 256U;
    const u32 screenHeight = (sizeIndex == 2U || sizeIndex == 3U) ? 512U : 256U;

    const auto& vram = memory_->vram();
    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const auto pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            const u8 windowMask = windowMaskForPixel(x, y);
            if (!layerEnabledByWindowMask(windowMask, static_cast<u8>(bgIndex))) {
                continue;
            }

            const u32 sx = (static_cast<u32>(x) + static_cast<u32>(hofs)) % screenWidth;
            const u32 sy = (static_cast<u32>(y) + static_cast<u32>(vofs)) % screenHeight;
            const u32 tileX = sx / 8U;
            const u32 tileY = sy / 8U;
            const u32 pixelX = sx & 7U;
            const u32 pixelY = sy & 7U;

            const std::size_t mapBase = static_cast<std::size_t>(screenBase)
                + mode0ScreenBlockOffset(sizeIndex, tileX, tileY);
            const std::size_t mapIndex = static_cast<std::size_t>((tileY % 32U) * 32U + (tileX % 32U));
            const u16 mapEntry = readVram16(mapBase + mapIndex * 2U);

            const u32 tileNumber = static_cast<u32>(mapEntry & 0x03FFU);
            const bool hflip = (mapEntry & 0x0400U) != 0U;
            const bool vflip = (mapEntry & 0x0800U) != 0U;
            const u32 paletteBank = static_cast<u32>((mapEntry >> 12U) & 0x0FU);

            const u32 tilePx = hflip ? (7U - pixelX) : pixelX;
            const u32 tilePy = vflip ? (7U - pixelY) : pixelY;

            u8 colorIndex = 0;
            if (color256) {
                const std::size_t tileOffset = static_cast<std::size_t>(charBase)
                    + static_cast<std::size_t>(tileNumber) * 64U
                    + static_cast<std::size_t>(tilePy) * 8U
                    + static_cast<std::size_t>(tilePx);
                if (tileOffset < vram.size()) {
                    colorIndex = vram[tileOffset];
                }
                if (colorIndex == 0U) {
                    continue;
                }
            } else {
                const std::size_t tileOffset = static_cast<std::size_t>(charBase)
                    + static_cast<std::size_t>(tileNumber) * 32U
                    + static_cast<std::size_t>(tilePy) * 4U
                    + static_cast<std::size_t>(tilePx / 2U);
                if (tileOffset < vram.size()) {
                    const u8 packed = vram[tileOffset];
                    const u8 index4 = (tilePx & 1U) == 0U
                        ? static_cast<u8>(packed & 0x0FU)
                        : static_cast<u8>((packed >> 4U) & 0x0FU);
                    if (index4 == 0U) {
                        continue;
                    }
                    colorIndex = static_cast<u8>(paletteBank * 16U + index4);
                } else {
                    continue;
                }
            }

            composeLayer(layerPixels, pixelIndex, readBgPaletteColor(colorIndex), priority, static_cast<u8>(bgIndex));
        }
    }
}

void Ppu::renderAffineBackground(int bgIndex, std::array<LayerPixel, FramebufferSize>& layerPixels) const {
    if (bgIndex < 2 || bgIndex > 3) {
        return;
    }

    const u32 cntOffset = Bg0CntOffset + static_cast<u32>(bgIndex) * 2U;
    const u16 bgcnt = memory_->readIo16(cntOffset);
    const u8 priority = static_cast<u8>(bgcnt & 0x3U);
    const bool wrap = (bgcnt & kBgWrapMask) != 0U;
    const u32 charBase = static_cast<u32>((bgcnt >> 2U) & 0x3U) * 0x4000U;
    const u32 screenBase = static_cast<u32>((bgcnt >> 8U) & 0x1FU) * 0x800U;
    const u32 sizeIndex = static_cast<u32>((bgcnt >> 14U) & 0x3U);
    const int affineSize = 128 << static_cast<int>(sizeIndex);
    const int tilesPerLine = affineSize / 8;

    const u32 bgAffineBase = bgIndex == 2 ? 0U : 0x10U;
    const int32_t pa = readIoSigned16(*memory_, kBgAffinePaOffset + bgAffineBase);
    const int32_t pb = readIoSigned16(*memory_, kBgAffinePbOffset + bgAffineBase);
    const int32_t pc = readIoSigned16(*memory_, kBgAffinePcOffset + bgAffineBase);
    const int32_t pd = readIoSigned16(*memory_, kBgAffinePdOffset + bgAffineBase);
    const int32_t xRef = readIoSigned32(*memory_, kBgAffineXOffset + bgAffineBase);
    const int32_t yRef = readIoSigned32(*memory_, kBgAffineYOffset + bgAffineBase);

    const auto& vram = memory_->vram();
    for (int y = 0; y < ScreenHeight; ++y) {
        for (int x = 0; x < ScreenWidth; ++x) {
            const u8 windowMask = windowMaskForPixel(x, y);
            if (!layerEnabledByWindowMask(windowMask, static_cast<u8>(bgIndex))) {
                continue;
            }
            const std::int64_t affineX = static_cast<std::int64_t>(xRef)
                + static_cast<std::int64_t>(pa) * static_cast<std::int64_t>(x)
                + static_cast<std::int64_t>(pb) * static_cast<std::int64_t>(y);
            const std::int64_t affineY = static_cast<std::int64_t>(yRef)
                + static_cast<std::int64_t>(pc) * static_cast<std::int64_t>(x)
                + static_cast<std::int64_t>(pd) * static_cast<std::int64_t>(y);

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
            if (mapIndex >= vram.size()) {
                continue;
            }
            const u32 tileNumber = static_cast<u32>(vram[mapIndex]);
            const std::size_t texelOffset = static_cast<std::size_t>(charBase)
                + static_cast<std::size_t>(tileNumber) * 64U
                + static_cast<std::size_t>(inTileY) * 8U
                + static_cast<std::size_t>(inTileX);
            if (texelOffset >= vram.size()) {
                continue;
            }
            const u8 colorIndex = vram[texelOffset];
            if (colorIndex == 0U) {
                continue;
            }

            const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(ScreenWidth)
                + static_cast<std::size_t>(x);
            composeLayer(layerPixels, pixelIndex, readBgPaletteColor(colorIndex), priority, static_cast<u8>(bgIndex));
        }
    }
}

void Ppu::renderObjects(std::array<LayerPixel, FramebufferSize>& layerPixels) const {
    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    const u16 mode = static_cast<u16>(dispcnt & kDisplayModeMask);
    const std::size_t objTileBase = mode >= 3U ? kObjTileBaseMode345Offset : kObjTileBaseMode012Offset;
    const bool obj1D = (dispcnt & kObjMapping1dMask) != 0U;
    const u32 totalObjBlocks = mode >= 3U ? 512U : 1024U;
    const u32 objTileMask = mode >= 3U ? 0x01FFU : 0x03FFU;
    const auto& vram = memory_->vram();

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
        const u32 tileBase = static_cast<u32>(attr2 & objTileMask);
        const u8 objPriority = static_cast<u8>((attr2 >> 10U) & 0x3U);
        const u8 paletteBank = static_cast<u8>((attr2 >> 12U) & 0x0FU);
        const int tilesPerRow1D = std::max(1, baseWidth / 8);

        std::int32_t pa = 0;
        std::int32_t pb = 0;
        std::int32_t pc = 0;
        std::int32_t pd = 0;
        if (affine) {
            const u32 affineIndex = static_cast<u32>((attr1 >> 9U) & 0x1FU);
            const std::size_t affineBase = static_cast<std::size_t>(affineIndex) * 32U;
            pa = static_cast<std::int16_t>(readOam16(affineBase + 6U));
            pb = static_cast<std::int16_t>(readOam16(affineBase + 14U));
            pc = static_cast<std::int16_t>(readOam16(affineBase + 22U));
            pd = static_cast<std::int16_t>(readOam16(affineBase + 30U));
        }

        for (int py = 0; py < renderHeight; ++py) {
            const int screenY = y + py;
            if (screenY < 0 || screenY >= ScreenHeight) {
                continue;
            }
            for (int px = 0; px < renderWidth; ++px) {
                const int screenX = x + px;
                if (screenX < 0 || screenX >= ScreenWidth) {
                    continue;
                }
                const u8 windowMask = windowMaskForPixel(screenX, screenY);
                if (!layerEnabledByWindowMask(windowMask, 4U)) {
                    continue;
                }

                int localX = 0;
                int localY = 0;
                if (affine) {
                    const int dx = px - (renderWidth / 2);
                    const int dy = py - (renderHeight / 2);
                    const std::int32_t srcX = ((pa * dx + pb * dy) >> 8) + (baseWidth / 2);
                    const std::int32_t srcY = ((pc * dx + pd * dy) >> 8) + (baseHeight / 2);
                    if (srcX < 0 || srcX >= baseWidth || srcY < 0 || srcY >= baseHeight) {
                        continue;
                    }
                    localX = static_cast<int>(srcX);
                    localY = static_cast<int>(srcY);
                } else {
                    localX = hflip ? (baseWidth - 1 - px) : px;
                    localY = vflip ? (baseHeight - 1 - py) : py;
                }

                const int tileX = localX / 8;
                const int tileY = localY / 8;
                const int inTileX = localX & 7;
                const int inTileY = localY & 7;
                const u32 blockStrideX = color256 ? 2U : 1U;

                u32 blockOffset = 0;
                if (obj1D) {
                    const u32 rowBlocks = static_cast<u32>(tilesPerRow1D) * blockStrideX;
                    blockOffset = static_cast<u32>(tileY) * rowBlocks + static_cast<u32>(tileX) * blockStrideX;
                } else {
                    blockOffset = static_cast<u32>(tileY) * 32U + static_cast<u32>(tileX) * blockStrideX;
                }
                const u32 blockNumber = (tileBase + blockOffset) % totalObjBlocks;

                u8 colorIndex = 0;
                if (color256) {
                    const std::size_t texelOffset = objTileBase
                        + static_cast<std::size_t>(blockNumber) * 32U
                        + static_cast<std::size_t>(inTileY) * 8U
                        + static_cast<std::size_t>(inTileX);
                    if (texelOffset >= vram.size()) {
                        continue;
                    }
                    colorIndex = vram[texelOffset];
                    if (colorIndex == 0U) {
                        continue;
                    }
                } else {
                    const std::size_t texelOffset = objTileBase
                        + static_cast<std::size_t>(blockNumber) * 32U
                        + static_cast<std::size_t>(inTileY) * 4U
                        + static_cast<std::size_t>(inTileX / 2);
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
                    colorIndex = static_cast<u8>(paletteBank * 16U + index4);
                }

                const std::size_t pixelIndex = static_cast<std::size_t>(screenY) * static_cast<std::size_t>(ScreenWidth)
                    + static_cast<std::size_t>(screenX);
                composeLayer(
                    layerPixels,
                    pixelIndex,
                    readObjPaletteColor(colorIndex),
                    objPriority,
                    5U,
                    objMode == 1U
                );
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

u8 Ppu::windowMaskForPixel(int x, int y) const {
    const u16 dispcnt = memory_->readIo16(DispcntOffset);
    const bool win0Enabled = (dispcnt & kWin0EnableMask) != 0U;
    const bool win1Enabled = (dispcnt & kWin1EnableMask) != 0U;
    const bool objWinEnabled = (dispcnt & kObjWinEnableMask) != 0U;
    if (!win0Enabled && !win1Enabled && !objWinEnabled) {
        return 0x3FU;
    }

    const u16 winIn = memory_->readIo16(kWinInOffset);
    const u16 winOut = memory_->readIo16(kWinOutOffset);
    u8 activeMask = static_cast<u8>(winOut & 0x3FU);

    if (win0Enabled) {
        const u16 win0H = memory_->readIo16(kWin0HOffset);
        const u16 win0V = memory_->readIo16(kWin0VOffset);
        if (pointInsideWindowRect(x, y, win0H, win0V)) {
            activeMask = static_cast<u8>(winIn & 0x3FU);
            return activeMask;
        }
    }
    if (win1Enabled) {
        const u16 win1H = memory_->readIo16(kWin1HOffset);
        const u16 win1V = memory_->readIo16(kWin1VOffset);
        if (pointInsideWindowRect(x, y, win1H, win1V)) {
            activeMask = static_cast<u8>((winIn >> 8U) & 0x3FU);
            return activeMask;
        }
    }

    // OBJ window ainda nao gera mascara propria aqui.
    return activeMask;
}

bool Ppu::pointInsideWindowRange(int value, int start, int end, int limit) {
    if (value < 0 || value >= limit) {
        return false;
    }
    start = std::clamp(start, 0, limit);
    end = std::clamp(end, 0, limit);
    if (start == end) {
        return true;
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
    const auto& vram = memory_->vram();
    if (byteIndex + 1U >= vram.size()) {
        return 0;
    }
    return static_cast<u16>(
        static_cast<u16>(vram[byteIndex])
        | static_cast<u16>(static_cast<u16>(vram[byteIndex + 1U]) << 8U)
    );
}

u16 Ppu::readOam16(std::size_t byteIndex) const {
    const auto& oam = memory_->oam();
    if (byteIndex + 1U >= oam.size()) {
        return 0;
    }
    return static_cast<u16>(
        static_cast<u16>(oam[byteIndex])
        | static_cast<u16>(static_cast<u16>(oam[byteIndex + 1U]) << 8U)
    );
}

u16 Ppu::readBgPaletteColor(u8 colorIndex) const {
    const auto& pram = memory_->pram();
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
    const auto& pram = memory_->pram();
    const std::size_t byteIndex = 0x200U + static_cast<std::size_t>(colorIndex) * 2U;
    if (byteIndex + 1U >= pram.size()) {
        return 0;
    }
    return static_cast<u16>(
        static_cast<u16>(pram[byteIndex])
        | static_cast<u16>(static_cast<u16>(pram[byteIndex + 1U]) << 8U)
    );
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
    if (!prevHblank_ && hblank) {
        memory_->triggerDmaStart(2U);
    }

    prevVblank_ = vblank;
    prevHblank_ = hblank;
    prevVcounterMatch_ = vcounterMatch;
}

u16 Ppu::bgr555ToRgb565(u16 pixel) {
    const u16 r5 = static_cast<u16>(pixel & 0x1FU);
    const u16 g5 = static_cast<u16>((pixel >> 5U) & 0x1FU);
    const u16 b5 = static_cast<u16>((pixel >> 10U) & 0x1FU);
    const u16 g6 = static_cast<u16>((g5 << 1U) | (g5 >> 4U));
    return static_cast<u16>((r5 << 11U) | (g6 << 5U) | b5);
}

} // namespace gb::gba
