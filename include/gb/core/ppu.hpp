#pragma once

#include <array>

#include "gb/core/types.hpp"

namespace gb {

class PPU {
public:
    static constexpr int ScreenWidth = 160;
    static constexpr int ScreenHeight = 144;

    struct State {
        u32 modeClock = 0;
        u8 mode = 2;
        u8 lcdc = 0x91;
        u8 stat = 0x85;
        u8 scy = 0;
        u8 scx = 0;
        u8 ly = 0;
        u8 lyc = 0;
        u8 bgp = 0xFC;
        u8 obp0 = 0xFF;
        u8 obp1 = 0xFF;
        u8 wy = 0;
        u8 wx = 0;
        u8 dma = 0xFF;
        bool vblankInterruptRequested = false;
        bool lcdInterruptRequested = false;
        std::array<u8, ScreenWidth * ScreenHeight> framebuffer{};
    };

    void tick(
        u32 cycles,
        std::array<u8, 0x2000>& vramBank0,
        std::array<u8, 0x2000>& vramBank1,
        std::array<u8, 0xA0>& oam,
        bool cgbMode,
        const std::array<u8, 0x40>& bgPalette,
        const std::array<u8, 0x40>& objPalette
    );

    u8 read(u16 address) const;
    void write(u16 address, u8 value);

    bool consumeVBlankInterrupt();
    bool consumeLcdInterrupt();

    [[nodiscard]] const std::array<u8, ScreenWidth * ScreenHeight>& framebuffer() const;
    [[nodiscard]] const std::array<u16, ScreenWidth * ScreenHeight>& colorFramebuffer() const;
    [[nodiscard]] State state() const;
    void loadState(const State& state);

private:
    void renderScanline(
        const std::array<u8, 0x2000>& vramBank0,
        const std::array<u8, 0x2000>& vramBank1,
        const std::array<u8, 0xA0>& oam,
        bool cgbMode,
        const std::array<u8, 0x40>& bgPalette,
        const std::array<u8, 0x40>& objPalette
    );
    u8 paletteMap(u8 palette, u8 colorId) const;
    u16 cgbPaletteColor(const std::array<u8, 0x40>& paletteData, u8 paletteIndex, u8 colorId) const;

    u32 modeClock_ = 0;
    u8 mode_ = 2;

    u8 lcdc_ = 0x91;
    u8 stat_ = 0x85;
    u8 scy_ = 0;
    u8 scx_ = 0;
    u8 ly_ = 0;
    u8 lyc_ = 0;
    u8 bgp_ = 0xFC;
    u8 obp0_ = 0xFF;
    u8 obp1_ = 0xFF;
    u8 wy_ = 0;
    u8 wx_ = 0;
    u8 dma_ = 0xFF;

    bool vblankInterruptRequested_ = false;
    bool lcdInterruptRequested_ = false;

    std::array<u8, ScreenWidth * ScreenHeight> framebuffer_{};
    std::array<u16, ScreenWidth * ScreenHeight> colorFramebuffer_{};
};

} // namespace gb
