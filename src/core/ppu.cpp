#include "gb/core/ppu.hpp"

namespace gb {

namespace {
constexpr u32 OAMCycles = 80;
constexpr u32 TransferCycles = 172;
constexpr u32 HBlankCycles = 204;
constexpr u32 FullLineCycles = 456;
constexpr u8 VisibleLines = 144;
constexpr u8 TotalLines = 154;
} // namespace

void PPU::tick(
    u32 cycles,
    std::array<u8, 0x2000>& vramBank0,
    std::array<u8, 0x2000>& vramBank1,
    std::array<u8, 0xA0>& oam,
    bool cgbMode,
    const std::array<u8, 0x40>& bgPalette,
    const std::array<u8, 0x40>& objPalette
) {
    if ((lcdc_ & 0x80) == 0) {
        ly_ = 0;
        mode_ = 0;
        modeClock_ = 0;
        return;
    }

    modeClock_ += cycles;

    switch (mode_) {
    case 2:
        if (modeClock_ >= OAMCycles) {
            modeClock_ -= OAMCycles;
            mode_ = 3;
        }
        break;
    case 3:
        if (modeClock_ >= TransferCycles) {
            modeClock_ -= TransferCycles;
            renderScanline(vramBank0, vramBank1, oam, cgbMode, bgPalette, objPalette);
            mode_ = 0;
        }
        break;
    case 0:
        if (modeClock_ >= HBlankCycles) {
            modeClock_ -= HBlankCycles;
            ++ly_;
            if (ly_ == VisibleLines) {
                mode_ = 1;
                vblankInterruptRequested_ = true;
            } else {
                mode_ = 2;
            }
        }
        break;
    case 1:
        if (modeClock_ >= FullLineCycles) {
            modeClock_ -= FullLineCycles;
            ++ly_;
            if (ly_ >= TotalLines) {
                ly_ = 0;
                mode_ = 2;
            }
        }
        break;
    default:
        mode_ = 2;
        break;
    }

    stat_ = static_cast<u8>((stat_ & 0xFC) | mode_);
    const bool lycEqual = ly_ == lyc_;
    if (lycEqual) {
        stat_ |= 0x04;
        if (stat_ & 0x40) {
            lcdInterruptRequested_ = true;
        }
    } else {
        stat_ &= static_cast<u8>(~0x04);
    }
}

u8 PPU::read(u16 address) const {
    switch (address) {
    case 0xFF40: return lcdc_;
    case 0xFF41: return static_cast<u8>(stat_ | 0x80);
    case 0xFF42: return scy_;
    case 0xFF43: return scx_;
    case 0xFF44: return ly_;
    case 0xFF45: return lyc_;
    case 0xFF47: return bgp_;
    case 0xFF48: return obp0_;
    case 0xFF49: return obp1_;
    case 0xFF4A: return wy_;
    case 0xFF4B: return wx_;
    case 0xFF46: return dma_;
    default: return 0xFF;
    }
}

void PPU::write(u16 address, u8 value) {
    switch (address) {
    case 0xFF40: lcdc_ = value; break;
    case 0xFF41:
        stat_ = static_cast<u8>((stat_ & 0x07) | (value & 0x78));
        if ((stat_ & 0x40) && ly_ == lyc_) {
            lcdInterruptRequested_ = true;
        }
        break;
    case 0xFF42: scy_ = value; break;
    case 0xFF43: scx_ = value; break;
    case 0xFF44:
        ly_ = 0;
        if ((stat_ & 0x40) && ly_ == lyc_) {
            lcdInterruptRequested_ = true;
        }
        break;
    case 0xFF45:
        lyc_ = value;
        if ((stat_ & 0x40) && ly_ == lyc_) {
            lcdInterruptRequested_ = true;
        }
        break;
    case 0xFF47: bgp_ = value; break;
    case 0xFF48: obp0_ = value; break;
    case 0xFF49: obp1_ = value; break;
    case 0xFF4A: wy_ = value; break;
    case 0xFF4B: wx_ = value; break;
    case 0xFF46: dma_ = value; break;
    default: break;
    }
}

bool PPU::consumeVBlankInterrupt() {
    if (!vblankInterruptRequested_) {
        return false;
    }
    vblankInterruptRequested_ = false;
    return true;
}

bool PPU::consumeLcdInterrupt() {
    if (!lcdInterruptRequested_) {
        return false;
    }
    lcdInterruptRequested_ = false;
    return true;
}

const std::array<u8, PPU::ScreenWidth * PPU::ScreenHeight>& PPU::framebuffer() const {
    return framebuffer_;
}

const std::array<u16, PPU::ScreenWidth * PPU::ScreenHeight>& PPU::colorFramebuffer() const {
    return colorFramebuffer_;
}

PPU::State PPU::state() const {
    State s{};
    s.modeClock = modeClock_;
    s.mode = mode_;
    s.lcdc = lcdc_;
    s.stat = stat_;
    s.scy = scy_;
    s.scx = scx_;
    s.ly = ly_;
    s.lyc = lyc_;
    s.bgp = bgp_;
    s.obp0 = obp0_;
    s.obp1 = obp1_;
    s.wy = wy_;
    s.wx = wx_;
    s.dma = dma_;
    s.vblankInterruptRequested = vblankInterruptRequested_;
    s.lcdInterruptRequested = lcdInterruptRequested_;
    s.framebuffer = framebuffer_;
    return s;
}

void PPU::loadState(const State& s) {
    modeClock_ = s.modeClock;
    mode_ = s.mode;
    lcdc_ = s.lcdc;
    stat_ = s.stat;
    scy_ = s.scy;
    scx_ = s.scx;
    ly_ = s.ly;
    lyc_ = s.lyc;
    bgp_ = s.bgp;
    obp0_ = s.obp0;
    obp1_ = s.obp1;
    wy_ = s.wy;
    wx_ = s.wx;
    dma_ = s.dma;
    vblankInterruptRequested_ = s.vblankInterruptRequested;
    lcdInterruptRequested_ = s.lcdInterruptRequested;
    framebuffer_ = s.framebuffer;
    for (std::size_t i = 0; i < framebuffer_.size(); ++i) {
        const u8 shade = framebuffer_[i] & 0x03;
        const u8 v = static_cast<u8>(31 - shade * 10);
        colorFramebuffer_[i] = static_cast<u16>((v << 10) | (v << 5) | v);
    }
}

u8 PPU::paletteMap(u8 palette, u8 colorId) const {
    return static_cast<u8>((palette >> (colorId * 2)) & 0x03);
}

u16 PPU::cgbPaletteColor(const std::array<u8, 0x40>& paletteData, u8 paletteIndex, u8 colorId) const {
    const std::size_t idx = static_cast<std::size_t>((paletteIndex & 0x07) * 8 + (colorId & 0x03) * 2);
    const u16 lo = paletteData[idx];
    const u16 hi = paletteData[idx + 1];
    return static_cast<u16>(lo | (hi << 8));
}

void PPU::renderScanline(
    const std::array<u8, 0x2000>& vramBank0,
    const std::array<u8, 0x2000>& vramBank1,
    const std::array<u8, 0xA0>& oam,
    bool cgbMode,
    const std::array<u8, 0x40>& bgPalette,
    const std::array<u8, 0x40>& objPalette
) {
    if (ly_ >= ScreenHeight) {
        return;
    }

    for (int x = 0; x < ScreenWidth; ++x) {
        framebuffer_[ly_ * ScreenWidth + x] = 0;
        colorFramebuffer_[ly_ * ScreenWidth + x] = 0x7FFF;
    }

    std::array<u8, ScreenWidth> bgColorId{};
    bgColorId.fill(0);
    std::array<bool, ScreenWidth> bgPriority{};
    bgPriority.fill(false);

    const bool bgEnabled = (lcdc_ & 0x01) != 0;
    if (bgEnabled) {
        const bool useSignedTiles = (lcdc_ & 0x10) == 0;
        const u16 tileDataBase = useSignedTiles ? 0x1000 : 0x0000;
        const u16 bgMapBase = (lcdc_ & 0x08) ? 0x1C00 : 0x1800;

        const u8 y = static_cast<u8>(ly_ + scy_);
        const u16 tileRow = static_cast<u16>((y / 8) * 32);

        for (int x = 0; x < ScreenWidth; ++x) {
            const u8 px = static_cast<u8>(x + scx_);
            const u16 tileCol = px / 8;
            const u16 tileIdxAddress = static_cast<u16>(bgMapBase + tileRow + tileCol);
            const u8 tileNum = vramBank0[tileIdxAddress & 0x1FFF];
            const u8 attr = cgbMode ? vramBank1[tileIdxAddress & 0x1FFF] : 0;

            u16 tileAddr = 0;
            if (useSignedTiles) {
                const i8 signedIdx = static_cast<i8>(tileNum);
                tileAddr = static_cast<u16>(tileDataBase + signedIdx * 16);
            } else {
                tileAddr = static_cast<u16>(tileDataBase + tileNum * 16);
            }

            u8 tileLine = static_cast<u8>(y % 8);
            if (cgbMode && (attr & 0x40)) {
                tileLine = static_cast<u8>(7 - tileLine);
            }
            const u8 line = static_cast<u8>(tileLine * 2);
            const auto& tileBank = (cgbMode && (attr & 0x08)) ? vramBank1 : vramBank0;
            const u8 lo = tileBank[(tileAddr + line) & 0x1FFF];
            const u8 hi = tileBank[(tileAddr + line + 1) & 0x1FFF];

            u8 tileX = static_cast<u8>(px % 8);
            if (cgbMode && (attr & 0x20)) {
                tileX = static_cast<u8>(7 - tileX);
            }
            const u8 bit = static_cast<u8>(7 - tileX);
            const u8 colorId = static_cast<u8>(((hi >> bit) & 0x01) << 1 | ((lo >> bit) & 0x01));
            bgColorId[x] = colorId;
            bgPriority[x] = cgbMode ? ((attr & 0x80) != 0) : false;
            framebuffer_[ly_ * ScreenWidth + x] = paletteMap(bgp_, colorId);
            if (cgbMode) {
                colorFramebuffer_[ly_ * ScreenWidth + x] = cgbPaletteColor(bgPalette, static_cast<u8>(attr & 0x07), colorId);
            } else {
                const u8 shade = paletteMap(bgp_, colorId);
                const u8 v = static_cast<u8>(31 - shade * 10);
                colorFramebuffer_[ly_ * ScreenWidth + x] = static_cast<u16>((v << 10) | (v << 5) | v);
            }
        }
    }

    const bool windowEnabled = (lcdc_ & 0x20) != 0;
    if (windowEnabled && ly_ >= wy_) {
        const int windowXStart = static_cast<int>(wx_) - 7;
        if (windowXStart < ScreenWidth) {
            const bool useSignedTiles = (lcdc_ & 0x10) == 0;
            const u16 tileDataBase = useSignedTiles ? 0x1000 : 0x0000;
            const u16 windowMapBase = (lcdc_ & 0x40) ? 0x1C00 : 0x1800;
            const u8 windowY = static_cast<u8>(ly_ - wy_);
            const u16 tileRow = static_cast<u16>((windowY / 8) * 32);

            for (int x = 0; x < ScreenWidth; ++x) {
                if (x < windowXStart) {
                    continue;
                }
                const u8 wxPixel = static_cast<u8>(x - windowXStart);
                const u16 tileCol = wxPixel / 8;
                const u16 tileIdxAddress = static_cast<u16>(windowMapBase + tileRow + tileCol);
                const u8 tileNum = vramBank0[tileIdxAddress & 0x1FFF];
                const u8 attr = cgbMode ? vramBank1[tileIdxAddress & 0x1FFF] : 0;

                u16 tileAddr = 0;
                if (useSignedTiles) {
                    const i8 signedIdx = static_cast<i8>(tileNum);
                    tileAddr = static_cast<u16>(tileDataBase + signedIdx * 16);
                } else {
                    tileAddr = static_cast<u16>(tileDataBase + tileNum * 16);
                }

                u8 tileLine = static_cast<u8>(windowY % 8);
                if (cgbMode && (attr & 0x40)) {
                    tileLine = static_cast<u8>(7 - tileLine);
                }
                const u8 line = static_cast<u8>(tileLine * 2);
                const auto& tileBank = (cgbMode && (attr & 0x08)) ? vramBank1 : vramBank0;
                const u8 lo = tileBank[(tileAddr + line) & 0x1FFF];
                const u8 hi = tileBank[(tileAddr + line + 1) & 0x1FFF];
                u8 tileX = static_cast<u8>(wxPixel % 8);
                if (cgbMode && (attr & 0x20)) {
                    tileX = static_cast<u8>(7 - tileX);
                }
                const u8 bit = static_cast<u8>(7 - tileX);
                const u8 colorId = static_cast<u8>(((hi >> bit) & 0x01) << 1 | ((lo >> bit) & 0x01));
                bgColorId[x] = colorId;
                bgPriority[x] = cgbMode ? ((attr & 0x80) != 0) : false;
                framebuffer_[ly_ * ScreenWidth + x] = paletteMap(bgp_, colorId);
                if (cgbMode) {
                    colorFramebuffer_[ly_ * ScreenWidth + x] = cgbPaletteColor(bgPalette, static_cast<u8>(attr & 0x07), colorId);
                } else {
                    const u8 shade = paletteMap(bgp_, colorId);
                    const u8 v = static_cast<u8>(31 - shade * 10);
                    colorFramebuffer_[ly_ * ScreenWidth + x] = static_cast<u16>((v << 10) | (v << 5) | v);
                }
            }
        }
    }

    const bool spritesEnabled = (lcdc_ & 0x02) != 0;
    if (!spritesEnabled) {
        return;
    }

    const bool tallSprites = (lcdc_ & 0x04) != 0;
    const int spriteHeight = tallSprites ? 16 : 8;
    int drawn = 0;

    for (int i = 0; i < 40 && drawn < 10; ++i) {
        const int o = i * 4;
        const int spriteY = static_cast<int>(oam[o]) - 16;
        const int spriteX = static_cast<int>(oam[o + 1]) - 8;
        u8 tile = oam[o + 2];
        const u8 attr = oam[o + 3];

        if (ly_ < spriteY || ly_ >= spriteY + spriteHeight) {
            continue;
        }
        ++drawn;

        int line = static_cast<int>(ly_) - spriteY;
        if (attr & 0x40) {
            line = spriteHeight - 1 - line;
        }

        if (tallSprites) {
            tile &= 0xFE;
            if (line >= 8) {
                tile = static_cast<u8>(tile + 1);
                line -= 8;
            }
        }

        const u16 tileAddr = static_cast<u16>(tile * 16 + line * 2);
        const auto& spriteBank = (cgbMode && (attr & 0x08)) ? vramBank1 : vramBank0;
        const u8 lo = spriteBank[tileAddr & 0x1FFF];
        const u8 hi = spriteBank[(tileAddr + 1) & 0x1FFF];
        const u8 palette = (attr & 0x10) ? obp1_ : obp0_;
        const bool lowPriority = (attr & 0x80) != 0;
        const bool xFlip = (attr & 0x20) != 0;
        const u8 cgbPaletteIdx = static_cast<u8>(attr & 0x07);

        for (int px = 0; px < 8; ++px) {
            const int screenX = spriteX + px;
            if (screenX < 0 || screenX >= ScreenWidth) {
                continue;
            }

            const int bitIndex = xFlip ? px : (7 - px);
            const u8 colorId = static_cast<u8>(((hi >> bitIndex) & 0x01) << 1 | ((lo >> bitIndex) & 0x01));
            if (colorId == 0) {
                continue;
            }
            if (cgbMode) {
                if (bgPriority[screenX] && bgColorId[screenX] != 0) {
                    continue;
                }
                if (lowPriority && bgColorId[screenX] != 0) {
                    continue;
                }
            } else {
                if (lowPriority && bgColorId[screenX] != 0) {
                    continue;
                }
            }

            framebuffer_[ly_ * ScreenWidth + screenX] = paletteMap(palette, colorId);
            if (cgbMode) {
                colorFramebuffer_[ly_ * ScreenWidth + screenX] = cgbPaletteColor(objPalette, cgbPaletteIdx, colorId);
            } else {
                const u8 shade = paletteMap(palette, colorId);
                const u8 v = static_cast<u8>(31 - shade * 10);
                colorFramebuffer_[ly_ * ScreenWidth + screenX] = static_cast<u16>((v << 10) | (v << 5) | v);
            }
        }
    }
}

} // namespace gb
