#include "gb/app/headless_runner.hpp"

#include <fstream>
#include <iostream>

namespace {

void writeFrameAsPPM(const std::string& path, const gb::PPU& ppu) {
    static constexpr int width = gb::PPU::ScreenWidth;
    static constexpr int height = gb::PPU::ScreenHeight;
    static constexpr int palette[4] = {255, 192, 96, 0};

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }

    out << "P6\n" << width << " " << height << "\n255\n";

    const auto& frame = ppu.framebuffer();
    for (auto shade : frame) {
        const auto v = static_cast<unsigned char>(palette[shade & 0x03]);
        out.write(reinterpret_cast<const char*>(&v), 1);
        out.write(reinterpret_cast<const char*>(&v), 1);
        out.write(reinterpret_cast<const char*>(&v), 1);
    }
}

} // namespace

namespace gb {

int runHeadless(GameBoy& gb, int frames) {
    for (int i = 0; i < frames; ++i) {
        gb.runFrame();
    }

    writeFrameAsPPM("frame.ppm", gb.ppu());

    const auto& regs = gb.cpu().regs();
    std::cout << "Emulacao finalizada (" << frames << " frames)\n";
    std::cout << "PC=" << std::hex << regs.pc << " SP=" << regs.sp << " A=" << static_cast<int>(regs.a) << "\n";
    std::cout << "Framebuffer salvo em frame.ppm\n";
    return 0;
}

} // namespace gb

