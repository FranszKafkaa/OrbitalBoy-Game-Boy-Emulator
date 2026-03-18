#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "gb/app/app_options.hpp"
#include "gb/app/headless_runner.hpp"
#include "gb/app/rom_suite_runner.hpp"
#include "gb/app/runtime_paths.hpp"
#include "gb/app/sdl_frontend.hpp"
#include "gb/core/environment.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/core/gba/system.hpp"

#ifdef GBEMU_USE_SDL2
#include "gb/app/sdl_compat.hpp"
#endif

namespace {

enum class ResolvedTargetSystem {
    Gb,
    Gba,
};

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool hasGbaExtension(const std::string& romPath) {
    if (romPath.empty()) {
        return false;
    }
    const std::string ext = toLowerAscii(std::filesystem::path(romPath).extension().string());
    return ext == ".gba";
}

struct HeadlessGbaInputStep {
    int startFrame = 0;
    int endFrame = 0;
    gb::gba::InputState state{};
};

void writeLittleEndian32(std::ofstream& out, std::uint32_t value);
void writeLittleEndian16(std::ofstream& out, std::uint16_t value);

std::optional<gb::gba::InputState> parseHeadlessGbaInputButtons(const std::string& text) {
    gb::gba::InputState state{};
    std::size_t start = 0;
    bool sawButton = false;
    while (start <= text.size()) {
        const std::size_t end = text.find(',', start);
        const std::string token = toLowerAscii(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!token.empty()) {
            sawButton = true;
            if (token == "a") {
                state.a = true;
            } else if (token == "b") {
                state.b = true;
            } else if (token == "select") {
                state.select = true;
            } else if (token == "start") {
                state.start = true;
            } else if (token == "right") {
                state.right = true;
            } else if (token == "left") {
                state.left = true;
            } else if (token == "up") {
                state.up = true;
            } else if (token == "down") {
                state.down = true;
            } else if (token == "r") {
                state.r = true;
            } else if (token == "l") {
                state.l = true;
            } else {
                return std::nullopt;
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (!sawButton) {
        return std::nullopt;
    }
    return state;
}

std::optional<HeadlessGbaInputStep> parseHeadlessGbaInputStep(const std::string& text) {
    const std::size_t atPos = text.find('@');
    if (atPos == std::string::npos || atPos == 0U || atPos + 1U >= text.size()) {
        return std::nullopt;
    }
    const auto state = parseHeadlessGbaInputButtons(text.substr(0, atPos));
    if (!state.has_value()) {
        return std::nullopt;
    }

    const std::string frameRangeText = text.substr(atPos + 1U);
    const std::size_t dashPos = frameRangeText.find('-');
    const std::string startText = frameRangeText.substr(0, dashPos);
    const std::string endText = dashPos == std::string::npos ? startText : frameRangeText.substr(dashPos + 1U);
    if (startText.empty() || endText.empty()) {
        return std::nullopt;
    }

    try {
        const int startFrame = std::stoi(startText);
        const int endFrame = std::stoi(endText);
        if (startFrame < 0 || endFrame < startFrame) {
            return std::nullopt;
        }
        return HeadlessGbaInputStep{startFrame, endFrame, *state};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool writeStereoPcm16Wav(const std::string& path, int sampleRate, const std::vector<std::int16_t>& samples) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    const std::uint16_t channels = 2;
    const std::uint16_t bitsPerSample = 16;
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate * channels * (bitsPerSample / 8));
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8));
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t riffBytes = 36U + dataBytes;

    out.write("RIFF", 4);
    writeLittleEndian32(out, riffBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLittleEndian32(out, 16U);
    writeLittleEndian16(out, 1U);
    writeLittleEndian16(out, channels);
    writeLittleEndian32(out, static_cast<std::uint32_t>(sampleRate));
    writeLittleEndian32(out, byteRate);
    writeLittleEndian16(out, blockAlign);
    writeLittleEndian16(out, bitsPerSample);
    out.write("data", 4);
    writeLittleEndian32(out, dataBytes);
    if (!samples.empty()) {
        out.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(dataBytes));
    }
    return static_cast<bool>(out);
}

std::vector<HeadlessGbaInputStep> parseHeadlessGbaInputScript(const std::string& text) {
    std::vector<HeadlessGbaInputStep> steps;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(';', start);
        const std::string token = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!token.empty()) {
            const auto step = parseHeadlessGbaInputStep(token);
            if (!step.has_value()) {
                return {};
            }
            steps.push_back(*step);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    std::sort(steps.begin(), steps.end(), [](const HeadlessGbaInputStep& lhs, const HeadlessGbaInputStep& rhs) {
        if (lhs.startFrame != rhs.startFrame) {
            return lhs.startFrame < rhs.startFrame;
        }
        return lhs.endFrame < rhs.endFrame;
    });
    return steps;
}

gb::gba::InputState headlessGbaInputForFrame(const std::vector<HeadlessGbaInputStep>& script, int frame) {
    gb::gba::InputState state{};
    for (const HeadlessGbaInputStep& step : script) {
        if (frame < step.startFrame) {
            break;
        }
        if (frame > step.endFrame) {
            continue;
        }
        state.a = state.a || step.state.a;
        state.b = state.b || step.state.b;
        state.select = state.select || step.state.select;
        state.start = state.start || step.state.start;
        state.right = state.right || step.state.right;
        state.left = state.left || step.state.left;
        state.up = state.up || step.state.up;
        state.down = state.down || step.state.down;
        state.r = state.r || step.state.r;
        state.l = state.l || step.state.l;
    }
    return state;
}

void writeLittleEndian32(std::ofstream& out, std::uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xFFU),
        static_cast<unsigned char>((value >> 8U) & 0xFFU),
        static_cast<unsigned char>((value >> 16U) & 0xFFU),
        static_cast<unsigned char>((value >> 24U) & 0xFFU),
    };
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeLittleEndian16(std::ofstream& out, std::uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xFFU),
        static_cast<unsigned char>((value >> 8U) & 0xFFU),
    };
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeGbaFrameAsBmp(const std::string& path, const gb::gba::System& gbaSystem) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }

    constexpr int width = gb::gba::System::ScreenWidth;
    constexpr int height = gb::gba::System::ScreenHeight;
    constexpr int bytesPerPixel = 3;
    const std::uint32_t rowStride = static_cast<std::uint32_t>((width * bytesPerPixel + 3) & ~3);
    const std::uint32_t pixelDataSize = rowStride * static_cast<std::uint32_t>(height);
    const std::uint32_t fileSize = 14U + 40U + pixelDataSize;

    out.put('B');
    out.put('M');
    writeLittleEndian32(out, fileSize);
    writeLittleEndian16(out, 0U);
    writeLittleEndian16(out, 0U);
    writeLittleEndian32(out, 54U);
    writeLittleEndian32(out, 40U);
    writeLittleEndian32(out, static_cast<std::uint32_t>(width));
    writeLittleEndian32(out, static_cast<std::uint32_t>(height));
    writeLittleEndian16(out, 1U);
    writeLittleEndian16(out, 24U);
    writeLittleEndian32(out, 0U);
    writeLittleEndian32(out, pixelDataSize);
    writeLittleEndian32(out, 2835U);
    writeLittleEndian32(out, 2835U);
    writeLittleEndian32(out, 0U);
    writeLittleEndian32(out, 0U);

    const unsigned char padding[3] = {0U, 0U, 0U};
    const auto& frame = gbaSystem.framebuffer();
    for (int y = height - 1; y >= 0; --y) {
        const auto rowBase = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            const gb::u16 pixel = frame[rowBase + static_cast<std::size_t>(x)];
            const unsigned char b = static_cast<unsigned char>((pixel & 0x1FU) * 255U / 31U);
            const unsigned char g = static_cast<unsigned char>(((pixel >> 5U) & 0x3FU) * 255U / 63U);
            const unsigned char r = static_cast<unsigned char>(((pixel >> 11U) & 0x1FU) * 255U / 31U);
            out.write(reinterpret_cast<const char*>(&b), 1);
            out.write(reinterpret_cast<const char*>(&g), 1);
            out.write(reinterpret_cast<const char*>(&r), 1);
        }
        out.write(reinterpret_cast<const char*>(padding), rowStride - static_cast<std::uint32_t>(width * bytesPerPixel));
    }
}

std::size_t gbaVramOffset(std::uint32_t address) {
    std::uint32_t offset = address & 0x1FFFFU;
    if (offset >= 0x18000U) {
        offset -= 0x8000U;
    }
    return static_cast<std::size_t>(offset);
}

gb::u16 readGbaVram16(const gb::gba::Memory& memory, std::uint32_t address) {
    const auto& vram = memory.vram();
    const std::size_t offset = gbaVramOffset(address);
    if (offset + 1U >= vram.size()) {
        return 0U;
    }
    return static_cast<gb::u16>(
        static_cast<gb::u16>(vram[offset])
        | static_cast<gb::u16>(static_cast<gb::u16>(vram[offset + 1U]) << 8U)
    );
}

std::size_t gbaMode0ScreenBlockOffset(std::uint32_t sizeIndex, std::uint32_t tileX, std::uint32_t tileY) {
    const std::uint32_t blockX = tileX / 32U;
    const std::uint32_t blockY = tileY / 32U;
    if (sizeIndex == 0U) {
        return 0U;
    }
    if (sizeIndex == 1U) {
        return static_cast<std::size_t>(blockX * 0x800U);
    }
    if (sizeIndex == 2U) {
        return static_cast<std::size_t>(blockY * 0x800U);
    }
    return static_cast<std::size_t>((blockY * 2U + blockX) * 0x800U);
}

void dumpGbaRegularBgSample(const gb::gba::Memory& memory, int bgIndex, gb::u16 bgcnt, gb::u16 hofs, gb::u16 vofs, int screenX, int screenY) {
    const bool color256 = (bgcnt & 0x0080U) != 0U;
    const std::uint32_t charBase = static_cast<std::uint32_t>((bgcnt >> 2U) & 0x3U) * 0x4000U;
    const std::uint32_t screenBase = static_cast<std::uint32_t>((bgcnt >> 8U) & 0x1FU) * 0x800U;
    const std::uint32_t sizeIndex = static_cast<std::uint32_t>((bgcnt >> 14U) & 0x3U);
    const std::uint32_t screenWidth = (sizeIndex == 1U || sizeIndex == 3U) ? 512U : 256U;
    const std::uint32_t screenHeight = (sizeIndex == 2U || sizeIndex == 3U) ? 512U : 256U;
    const std::uint32_t sx = (static_cast<std::uint32_t>(screenX) + static_cast<std::uint32_t>(hofs & 0x01FFU)) % screenWidth;
    const std::uint32_t sy = (static_cast<std::uint32_t>(screenY) + static_cast<std::uint32_t>(vofs & 0x01FFU)) % screenHeight;
    const std::uint32_t tileX = sx / 8U;
    const std::uint32_t tileY = sy / 8U;
    const std::uint32_t pixelX = sx & 7U;
    const std::uint32_t pixelY = sy & 7U;
    const std::size_t mapBase = static_cast<std::size_t>(screenBase) + gbaMode0ScreenBlockOffset(sizeIndex, tileX, tileY);
    const std::size_t mapIndex = static_cast<std::size_t>((tileY % 32U) * 32U + (tileX % 32U));
    const gb::u16 mapEntry = readGbaVram16(memory, 0x06000000U + static_cast<std::uint32_t>(mapBase + mapIndex * 2U));
    const std::uint32_t tileNumber = static_cast<std::uint32_t>(mapEntry & 0x03FFU);
    const bool hflip = (mapEntry & 0x0400U) != 0U;
    const bool vflip = (mapEntry & 0x0800U) != 0U;
    const std::uint32_t paletteBank = static_cast<std::uint32_t>((mapEntry >> 12U) & 0x0FU);
    const std::uint32_t tilePx = hflip ? (7U - pixelX) : pixelX;
    const std::uint32_t tilePy = vflip ? (7U - pixelY) : pixelY;
    std::uint32_t tileAddr = 0U;
    std::uint32_t packedValue = 0U;
    std::uint32_t colorIndex = 0U;
    if (color256) {
        tileAddr = 0x06000000U + charBase + tileNumber * 64U + tilePy * 8U + tilePx;
        packedValue = memory.read8(tileAddr);
        colorIndex = packedValue;
    } else {
        tileAddr = 0x06000000U + charBase + tileNumber * 32U + tilePy * 4U + tilePx / 2U;
        packedValue = memory.read8(tileAddr);
        colorIndex = (tilePx & 1U) == 0U ? static_cast<std::uint32_t>(packedValue & 0x0FU)
                                         : static_cast<std::uint32_t>((packedValue >> 4U) & 0x0FU);
        colorIndex += paletteBank * 16U;
    }

    std::cout << " sample bg" << bgIndex
              << "(" << screenX << "," << screenY << ")"
              << " map=0x" << std::hex << mapEntry
              << " tile=" << std::dec << tileNumber
              << " pal=" << paletteBank
              << " ci=" << colorIndex
              << " tileAddr=0x" << std::hex << tileAddr
              << " packed=0x" << packedValue
              << std::dec
              << " texel=(" << tilePx << "," << tilePy << ")";
}

struct GbaSamplePoint {
    int x = 0;
    int y = 0;
};

constexpr std::array<GbaSamplePoint, 4> kDefaultGbaDebugSamplePoints = {{
    {8, 24},
    {80, 72},
    {120, 72},
    {120, 96},
}};

std::optional<GbaSamplePoint> parseGbaSamplePointToken(const std::string& token) {
    const std::size_t comma = token.find(',');
    if (comma == std::string::npos || comma == 0U || comma + 1U >= token.size()) {
        return std::nullopt;
    }
    try {
        const int x = std::stoi(token.substr(0U, comma));
        const int y = std::stoi(token.substr(comma + 1U));
        if (x < 0 || x >= gb::gba::Ppu::ScreenWidth || y < 0 || y >= gb::gba::Ppu::ScreenHeight) {
            return std::nullopt;
        }
        return GbaSamplePoint{x, y};
    } catch (...) {
        return std::nullopt;
    }
}

const std::vector<GbaSamplePoint>& gbaDebugSamplePoints() {
    static const std::vector<GbaSamplePoint> points = []() {
        const auto env = gb::readEnvironmentVariable("GBEMU_GBA_DEBUG_SAMPLE_POINTS");
        if (!env.has_value() || env->empty()) {
            return std::vector<GbaSamplePoint>(
                kDefaultGbaDebugSamplePoints.begin(),
                kDefaultGbaDebugSamplePoints.end()
            );
        }

        std::vector<GbaSamplePoint> parsed;
        std::size_t start = 0U;
        while (start <= env->size()) {
            const std::size_t end = env->find(';', start);
            const std::string token = env->substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!token.empty()) {
                const auto point = parseGbaSamplePointToken(token);
                if (point.has_value()) {
                    parsed.push_back(*point);
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1U;
        }

        if (parsed.empty()) {
            return std::vector<GbaSamplePoint>(
                kDefaultGbaDebugSamplePoints.begin(),
                kDefaultGbaDebugSamplePoints.end()
            );
        }
        return parsed;
    }();
    return points;
}

void dumpGbaRegularBgStats(const gb::gba::Memory& memory, int bgIndex, gb::u16 bgcnt) {
    const std::uint32_t screenBase = static_cast<std::uint32_t>((bgcnt >> 8U) & 0x1FU) * 0x800U;
    const std::uint32_t sizeIndex = static_cast<std::uint32_t>((bgcnt >> 14U) & 0x3U);
    const int blocksWide = (sizeIndex == 1U || sizeIndex == 3U) ? 2 : 1;
    const int blocksHigh = (sizeIndex == 2U || sizeIndex == 3U) ? 2 : 1;
    std::uint32_t minTile = 0x3FFU;
    std::uint32_t maxTile = 0U;
    std::uint32_t tileOver511 = 0U;
    std::uint32_t tileOver255 = 0U;
    std::uint32_t entries = 0U;

    for (int by = 0; by < blocksHigh; ++by) {
        for (int bx = 0; bx < blocksWide; ++bx) {
            const std::uint32_t blockTileX = static_cast<std::uint32_t>(bx) * 32U;
            const std::uint32_t blockTileY = static_cast<std::uint32_t>(by) * 32U;
            const std::uint32_t blockBase = screenBase
                + static_cast<std::uint32_t>(gbaMode0ScreenBlockOffset(sizeIndex, blockTileX, blockTileY));
            for (std::uint32_t i = 0; i < 1024U; ++i) {
                const gb::u16 entry = readGbaVram16(memory, 0x06000000U + blockBase + i * 2U);
                const std::uint32_t tile = static_cast<std::uint32_t>(entry & 0x03FFU);
                minTile = std::min(minTile, tile);
                maxTile = std::max(maxTile, tile);
                tileOver511 += tile > 511U ? 1U : 0U;
                tileOver255 += tile > 255U ? 1U : 0U;
                ++entries;
            }
        }
    }

    std::cout << " bg" << bgIndex
              << " screenBase=0x" << std::hex << screenBase
              << " size=" << sizeIndex
              << " minTile=" << std::dec << minTile
              << " maxTile=" << maxTile
              << " >255=" << tileOver255
              << " >511=" << tileOver511
              << "/" << entries;

    const gb::u16 hofs = memory.readIo16(0x0010U + static_cast<gb::u32>(bgIndex) * 4U);
    const gb::u16 vofs = memory.readIo16(0x0012U + static_cast<gb::u32>(bgIndex) * 4U);
    for (const GbaSamplePoint point : gbaDebugSamplePoints()) {
        dumpGbaRegularBgSample(memory, bgIndex, bgcnt, hofs, vofs, point.x, point.y);
    }
}

gb::u16 readGbaOam16(const gb::gba::Memory& memory, std::size_t byteIndex) {
    const auto& oam = memory.oam();
    if (byteIndex + 1U >= oam.size()) {
        return 0U;
    }
    return static_cast<gb::u16>(
        static_cast<gb::u16>(oam[byteIndex])
        | static_cast<gb::u16>(static_cast<gb::u16>(oam[byteIndex + 1U]) << 8U)
    );
}

void dumpGbaObjStats(const gb::gba::Memory& memory) {
    std::uint32_t active = 0U;
    std::uint32_t color256 = 0U;
    std::uint32_t affine = 0U;
    std::uint32_t affineDouble = 0U;
    std::uint32_t affineNearWrapY = 0U;
    std::uint32_t semiTransparent = 0U;
    std::uint32_t disabled = 0U;
    std::uint32_t maxTile = 0U;

    for (int obj = 0; obj < 128; ++obj) {
        const std::size_t base = static_cast<std::size_t>(obj) * 8U;
        const gb::u16 attr0 = readGbaOam16(memory, base + 0U);
        const gb::u16 attr1 = readGbaOam16(memory, base + 2U);
        const gb::u16 attr2 = readGbaOam16(memory, base + 4U);
        const bool isAffine = (attr0 & 0x0100U) != 0U;
        const bool affineParamBit = (attr0 & 0x0200U) != 0U;
        const bool isDoubleSize = isAffine && affineParamBit;
        if (!isAffine && affineParamBit) {
            ++disabled;
            continue;
        }
        ++active;
        affine += isAffine ? 1U : 0U;
        affineDouble += isDoubleSize ? 1U : 0U;
        affineNearWrapY += (isAffine && static_cast<unsigned>(attr0 & 0x00FFU) >= 128U) ? 1U : 0U;
        color256 += (attr0 & 0x2000U) != 0U ? 1U : 0U;
        semiTransparent += ((attr0 >> 10U) & 0x3U) == 1U ? 1U : 0U;
        maxTile = std::max(maxTile, static_cast<std::uint32_t>(attr2 & 0x03FFU));
        static_cast<void>(attr1);
    }

    std::cout << " obj active=" << active
              << " disabled=" << disabled
              << " affine=" << affine
              << " affineDouble=" << affineDouble
              << " affineY>=128=" << affineNearWrapY
              << " 8bpp=" << color256
              << " semi=" << semiTransparent
              << " maxTile=" << maxTile
              << " objMapping=" << (((memory.readIo16(0x000U) & 0x0040U) != 0U) ? "1D" : "2D")
              << "\n";
}

const char* gbaLayerName(std::uint8_t layer) {
    switch (layer) {
    case 0:
        return "BG0";
    case 1:
        return "BG1";
    case 2:
        return "BG2";
    case 3:
        return "BG3";
    case 4:
        return "BACKDROP";
    case 5:
        return "OBJ";
    default:
        return "UNKNOWN";
    }
}

bool decodeGbaObjSize(std::uint8_t shape, std::uint8_t size, int& width, int& height) {
    switch (shape) {
    case 0:
        switch (size & 0x3U) {
        case 0: width = 8; height = 8; return true;
        case 1: width = 16; height = 16; return true;
        case 2: width = 32; height = 32; return true;
        case 3: width = 64; height = 64; return true;
        default: return false;
        }
    case 1:
        switch (size & 0x3U) {
        case 0: width = 16; height = 8; return true;
        case 1: width = 32; height = 8; return true;
        case 2: width = 32; height = 16; return true;
        case 3: width = 64; height = 32; return true;
        default: return false;
        }
    case 2:
        switch (size & 0x3U) {
        case 0: width = 8; height = 16; return true;
        case 1: width = 8; height = 32; return true;
        case 2: width = 16; height = 32; return true;
        case 3: width = 32; height = 64; return true;
        default: return false;
        }
    default:
        return false;
    }
}

void dumpGbaPixelComposition(const gb::gba::System& gbaSystem) {
    const auto& ppu = gbaSystem.ppu();
    std::array<gb::u16, gb::gba::System::FramebufferSize> rerendered{};
    const bool rerenderedOk = ppu.render(rerendered);
    const auto& storedFramebuffer = gbaSystem.framebuffer();
    for (const GbaSamplePoint point : gbaDebugSamplePoints()) {
        gb::gba::Ppu::PixelDebugInfo info{};
        if (!ppu.debugPixel(point.x, point.y, info) || !info.valid) {
            std::cout << " pixel(" << point.x << "," << point.y << ") debug=unavailable\n";
            continue;
        }

        const std::size_t pixelIndex = static_cast<std::size_t>(point.y) * static_cast<std::size_t>(gb::gba::Ppu::ScreenWidth)
            + static_cast<std::size_t>(point.x);
        const gb::u16 storedPixel = storedFramebuffer[pixelIndex];
        const gb::u16 rerenderedPixel = rerenderedOk ? rerendered[pixelIndex] : 0U;

        std::cout << " pixel(" << point.x << "," << point.y << ")"
                  << " top=" << gbaLayerName(info.topLayer) << ":p" << static_cast<int>(info.topPriority)
                  << " second=";
        if (info.hasSecond) {
            std::cout << gbaLayerName(info.secondLayer) << ":p" << static_cast<int>(info.secondPriority);
        } else {
            std::cout << "none";
        }
        std::cout << " winMask=0x" << std::hex << static_cast<unsigned>(info.windowMask)
                  << " win0=" << static_cast<unsigned>(info.insideWin0)
                  << " win1=" << static_cast<unsigned>(info.insideWin1)
                  << " objWin=" << static_cast<unsigned>(info.insideObjWin)
                  << " blendMode=" << std::dec << static_cast<unsigned>(info.blendMode)
                  << " fxWin=" << static_cast<unsigned>(info.colorEffectEnabledByWindow)
                  << " first=" << static_cast<unsigned>(info.firstTarget)
                  << " secondTarget=" << static_cast<unsigned>(info.secondTarget)
                  << " alphaReq=" << static_cast<unsigned>(info.alphaBlendRequested)
                  << " alphaApplied=" << static_cast<unsigned>(info.alphaBlendApplied)
                  << " brighten=" << static_cast<unsigned>(info.brightenApplied)
                  << " darken=" << static_cast<unsigned>(info.darkenApplied)
                  << " raw=0x" << std::hex << info.finalRawColor555
                  << " rgb565=0x" << info.finalRgb565
                  << " stored=0x" << storedPixel;
        if (rerenderedOk) {
            std::cout << " rerender=0x" << rerenderedPixel;
        }
        std::cout << std::dec << "\n";
    }
}

void dumpGbaVisibleObjDetails(const gb::gba::Memory& memory) {
    std::uint32_t visible = 0U;
    std::uint32_t coveringSamples = 0U;
    std::array<std::uint32_t, 4> perPriority{};
    std::uint32_t printed = 0U;

    for (int obj = 0; obj < 128; ++obj) {
        const std::size_t base = static_cast<std::size_t>(obj) * 8U;
        const gb::u16 attr0 = readGbaOam16(memory, base + 0U);
        const gb::u16 attr1 = readGbaOam16(memory, base + 2U);
        const gb::u16 attr2 = readGbaOam16(memory, base + 4U);
        const bool affine = (attr0 & 0x0100U) != 0U;
        const bool affineParamBit = (attr0 & 0x0200U) != 0U;
        if (!affine && affineParamBit) {
            continue;
        }

        int width = 0;
        int height = 0;
        const std::uint8_t shape = static_cast<std::uint8_t>((attr0 >> 14U) & 0x3U);
        const std::uint8_t size = static_cast<std::uint8_t>((attr1 >> 14U) & 0x3U);
        if (!decodeGbaObjSize(shape, size, width, height)) {
            continue;
        }

        const bool doubleSize = affine && affineParamBit;
        if (doubleSize) {
            width *= 2;
            height *= 2;
        }

        int x = static_cast<int>(attr1 & 0x01FFU);
        int y = static_cast<int>(attr0 & 0x00FFU);
        if (x >= 256) {
            x -= 512;
        }
        if (y >= 160) {
            y -= 256;
        }
        if (x + width <= 0 || x >= gb::gba::Ppu::ScreenWidth || y + height <= 0 || y >= gb::gba::Ppu::ScreenHeight) {
            continue;
        }

        ++visible;
        const std::uint8_t priority = static_cast<std::uint8_t>((attr2 >> 10U) & 0x3U);
        ++perPriority[priority];
        const std::uint8_t objMode = static_cast<std::uint8_t>((attr0 >> 10U) & 0x3U);
        const bool color256 = (attr0 & 0x2000U) != 0U;

        std::string sampleHits;
        for (const GbaSamplePoint point : gbaDebugSamplePoints()) {
            if (point.x >= x && point.x < x + width && point.y >= y && point.y < y + height) {
                if (!sampleHits.empty()) {
                    sampleHits += ",";
                }
                sampleHits += "(" + std::to_string(point.x) + "," + std::to_string(point.y) + ")";
            }
        }
        const bool hitsSample = !sampleHits.empty();
        coveringSamples += hitsSample ? 1U : 0U;

        if (hitsSample || printed < 24U) {
            std::cout << " obj#" << obj
                      << " pos=(" << x << "," << y << ")"
                      << " size=" << width << "x" << height
                      << " tile=" << static_cast<unsigned>(attr2 & 0x03FFU)
                      << " prio=" << static_cast<unsigned>(priority)
                      << " mode=" << static_cast<unsigned>(objMode)
                      << " affine=" << static_cast<unsigned>(affine)
                      << " double=" << static_cast<unsigned>(doubleSize)
                      << " 8bpp=" << static_cast<unsigned>(color256);
            if (hitsSample) {
                std::cout << " samples=" << sampleHits;
            }
            std::cout << "\n";
            ++printed;
        }
    }

    std::cout << " objVisible=" << visible
              << " sampleCovering=" << coveringSamples
              << " prio0=" << perPriority[0]
              << " prio1=" << perPriority[1]
              << " prio2=" << perPriority[2]
              << " prio3=" << perPriority[3];
    if (visible > printed) {
        std::cout << " omitted=" << (visible - printed);
    }
    std::cout << "\n";
}

void dumpGbaVideoState(const gb::gba::System& gbaSystem) {
    const auto& memory = gbaSystem.memory();
    std::cout << std::hex;
    std::cout << " video"
              << " bg0hofs=0x" << memory.readIo16(0x0010U)
              << " bg0vofs=0x" << memory.readIo16(0x0012U)
              << " bg1hofs=0x" << memory.readIo16(0x0014U)
              << " bg1vofs=0x" << memory.readIo16(0x0016U)
              << " win0h=0x" << memory.readIo16(0x0040U)
              << " win1h=0x" << memory.readIo16(0x0042U)
              << " win0v=0x" << memory.readIo16(0x0044U)
              << " win1v=0x" << memory.readIo16(0x0046U)
              << " winin=0x" << memory.readIo16(0x0048U)
              << " winout=0x" << memory.readIo16(0x004AU)
              << " bldcnt=0x" << memory.readIo16(0x0050U)
              << " bldalpha=0x" << memory.readIo16(0x0052U)
              << " bldy=0x" << memory.readIo16(0x0054U)
              << std::dec << "\n";

    dumpGbaRegularBgStats(memory, 0, memory.readIo16(0x0008U));
    std::cout << "\n";
    dumpGbaRegularBgStats(memory, 1, memory.readIo16(0x000AU));
    std::cout << "\n";
    dumpGbaPixelComposition(gbaSystem);
    dumpGbaObjStats(memory);
    dumpGbaVisibleObjDetails(memory);
    std::cout << " writes pram8=" << memory.pramByteWriteCount()
              << " vram8=" << memory.vramByteWriteCount()
              << " oam8=" << memory.oamByteWriteCount()
              << "\n";
}

ResolvedTargetSystem resolveTargetSystem(const gb::AppOptions& options) {
    if (options.targetSystem == gb::TargetSystemPreference::Gb) {
        return ResolvedTargetSystem::Gb;
    }
    if (options.targetSystem == gb::TargetSystemPreference::Gba) {
        return ResolvedTargetSystem::Gba;
    }
    if (hasGbaExtension(options.romPath)) {
        return ResolvedTargetSystem::Gba;
    }
    return ResolvedTargetSystem::Gb;
}

bool loadGame(gb::GameBoy& gb, const gb::AppOptions& options) {
    const std::string resolvedRomPath = gb::resolveRomPathForRuntime(options.romPath);
    if (!options.bootRomPath.empty()) {
        if (!gb.loadBootRomFromFile(options.bootRomPath)) {
            std::cerr << "falha ao carregar boot ROM: " << options.bootRomPath << "\n";
            return false;
        }
    } else {
        gb.clearBootRom();
    }
    gb.setPreciseTiming(options.preciseTiming);

    if (!gb.loadRom(resolvedRomPath)) {
        std::cerr << "falha ao carregar ROM: " << resolvedRomPath << "\n";
        return false;
    }

    bool runInCgb = gb.cartridge().shouldRunInCgbMode();
    if (options.hardwareMode == gb::HardwareModePreference::Dmg) {
        if (gb.cartridge().cgbOnly()) {
            std::cerr << "aviso: ROM exige CGB; ignorando --hardware dmg\n";
            runInCgb = true;
        } else {
            runInCgb = false;
        }
    } else if (options.hardwareMode == gb::HardwareModePreference::Cgb) {
        if (!gb.cartridge().cgbSupported()) {
            std::cerr << "aviso: ROM sem suporte CGB; usando DMG\n";
            runInCgb = false;
        } else {
            runInCgb = true;
        }
    }
    gb.setHardwareMode(runInCgb);

    std::cout << "ROM carregada: " << gb.cartridge().title() << "\n";
    if (gb.runningInCgbMode()) {
        std::cout << "hardware emulado: CGB\n";
    } else {
        std::cout << "hardware emulado: DMG\n";
    }
    if (gb.cartridge().cgbSupported() && !gb.cartridge().cgbOnly()) {
        std::cout << "ROM dual-mode detectada (DMG/CGB)\n";
    }

    const std::string batteryPath = gb::batteryRamPathForRom(resolvedRomPath);
    if (gb.loadBatteryRamFromFile(batteryPath)) {
        std::cout << "save interno carregado: " << batteryPath << "\n";
    }
    const std::string rtcPath = gb::rtcPathForRom(resolvedRomPath);
    if (gb.loadRtcFromFile(rtcPath)) {
        std::cout << "rtc carregado: " << rtcPath << "\n";
    }
    return true;
}

bool loadGbaGame(gb::gba::System& system, const gb::AppOptions& options) {
    const std::string resolvedRomPath = gb::resolveRomPathForRuntime(options.romPath);
    if (!system.loadRomFromFile(resolvedRomPath)) {
        std::cerr << "falha ao carregar ROM GBA: " << resolvedRomPath << "\n";
        return false;
    }

    const auto& meta = system.metadata();
    const std::string fallbackTitle = std::filesystem::path(resolvedRomPath).filename().string();
    std::cout << "ROM GBA carregada: " << (meta.title.empty() ? fallbackTitle : meta.title) << "\n";
    if (!meta.gameCode.empty()) {
        std::cout << "game code: " << meta.gameCode << "\n";
    }
    if (!meta.makerCode.empty()) {
        std::cout << "maker code: " << meta.makerCode << "\n";
    }
    std::cout << "header: logo=" << (meta.validNintendoLogo ? "ok" : "invalida")
              << " fixed=" << (meta.validFixedByte ? "ok" : "invalido")
              << " checksum=" << (meta.validHeaderChecksum ? "ok" : "invalido") << "\n";
    std::cout << "perfil de compatibilidade: " << system.compatibilityProfile().name << "\n";

    if (system.hasPersistentBackup()) {
        const std::string batteryPath = gb::batteryRamPathForRom(resolvedRomPath);
        std::cout << "backup detectado: " << system.backupTypeName() << "\n";
        const bool hasFile = std::filesystem::exists(std::filesystem::path(batteryPath));
        if (system.loadBackupFromFile(batteryPath)) {
            std::cout << "save interno GBA carregado: " << batteryPath << "\n";
        } else if (hasFile) {
            std::cout << "save interno GBA ignorado (formato incompativel): " << batteryPath << "\n";
        }
    }
    return true;
}

#ifdef GBEMU_USE_SDL2
bool openRomSelector(gb::AppOptions& options) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "erro SDL_Init para seletor de ROM: " << SDL_GetError() << "\n";
        return false;
    }
#ifdef GBEMU_USE_SDL2_IMAGE
    const int imgFlags = IMG_INIT_JPG | IMG_INIT_PNG;
    IMG_Init(imgFlags);
#endif

    options.romPath = gb::chooseRomWithSdlDialog();

#ifdef GBEMU_USE_SDL2_IMAGE
    IMG_Quit();
#endif
    SDL_Quit();

    if (options.romPath.empty()) {
        std::cerr << "nenhuma ROM selecionada.\n";
        return false;
    }
    return true;
}

int runRealtimeFlow(gb::AppOptions& options) {
    while (true) {
        const ResolvedTargetSystem targetSystem = resolveTargetSystem(options);
        if (targetSystem == ResolvedTargetSystem::Gba) {
            gb::gba::System gbaSystem;
            if (!loadGbaGame(gbaSystem, options)) {
                return 1;
            }

            const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
            const int rc = gb::runGbaRealtime(gbaSystem, options.scale);
            if (gbaSystem.hasPersistentBackup() && gbaSystem.saveBackupToFile(batteryPath)) {
                std::cout << "save interno GBA gravado: " << batteryPath << "\n";
            }

            if (rc == 2) {
                if (!openRomSelector(options)) {
                    return 0;
                }
                continue;
            }
            return rc;
        }

        gb::GameBoy gb;
        if (!loadGame(gb, options)) {
            return 1;
        }

        const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
        const std::string controlsPath = gb::controlsPathForRom(options.romPath);
        const std::string cheatsPath = gb::cheatsPathForRom(options.romPath);
        const std::string palettePath = gb::palettePathForRom(options.romPath);
        const std::string rtcPath = gb::rtcPathForRom(options.romPath);
        const std::string replayPath = gb::replayPathForRom(options.romPath);
        const std::string filtersPath = gb::filtersPathForRom(options.romPath);
        const std::string captureDir = gb::captureDirForRom(options.romPath);
        const int rc = gb::runRealtime(
            gb,
            options.scale,
            options.audioBuffer,
            gb::statePathForRom(options.romPath),
            gb::legacyStatePathForRom(options.romPath),
            batteryPath,
            controlsPath,
            cheatsPath,
            palettePath,
            rtcPath,
            replayPath,
            filtersPath,
            captureDir,
            options.linkConnect,
            options.linkHostPort,
            options.netplayConnect,
            options.netplayHostPort,
            options.netplayDelayFrames
        );

        if (rc == 2) {
            if (!openRomSelector(options)) {
                return 0;
            }
            continue;
        }
        return rc;
    }
}
#endif

} // namespace

int main(int argc, char** argv) {
#ifdef GBEMU_USE_SDL2
    SDL_SetMainReady();
#endif

    gb::AppOptions options{};
    std::string parseError;
    if (!gb::parseAppOptions(argc, argv, options, parseError)) {
        std::cerr << parseError << "\n";
        return 1;
    }

    ResolvedTargetSystem resolvedTargetSystem = resolveTargetSystem(options);

    if (!options.romSuiteManifest.empty()) {
        if (resolvedTargetSystem == ResolvedTargetSystem::Gba) {
            std::cerr << "--rom-suite suporta apenas ROMs de Game Boy (fluxo GBA ainda experimental)\n";
            return 1;
        }
        return gb::runRomCompatibilitySuite(options.romSuiteManifest);
    }

#ifndef GBEMU_USE_SDL2
    (void)options.chooseRom;
#endif

    if (options.romPath.empty() && resolvedTargetSystem == ResolvedTargetSystem::Gb) {
#ifdef GBEMU_USE_SDL2
        options.chooseRom = true;
        options.headless = false;
#endif
    }

#ifdef GBEMU_USE_SDL2
    if (options.chooseRom && resolvedTargetSystem == ResolvedTargetSystem::Gb && !openRomSelector(options)) {
        return 1;
    }
#endif

    if (options.romPath.empty()) {
        std::cerr << "nenhuma ROM selecionada.\n";
#ifndef GBEMU_USE_SDL2
        std::cerr << "use: gbemu --rom <rom.gb>\n";
        std::cerr << "ou compile com SDL2 para usar seletor grafico.\n";
#endif
        if (resolvedTargetSystem == ResolvedTargetSystem::Gba) {
            std::cerr << "para GBA use: gbemu --system gba --rom <jogo.gba>\n";
        }
        std::cerr << "audio: --audio-buffer 1024 (256..8192)\n";
        return 1;
    }
    options.romPath = gb::resolveRomPathForRuntime(options.romPath);

    resolvedTargetSystem = resolveTargetSystem(options);

#ifdef GBEMU_USE_SDL2
    if (!options.headless) {
        return runRealtimeFlow(options);
    }
#else
    if (!options.headless && resolvedTargetSystem == ResolvedTargetSystem::Gba) {
        std::cout << "SDL2 nao detectado no build. Executando GBA em modo headless.\n";
    }
#endif

    if (resolvedTargetSystem == ResolvedTargetSystem::Gba) {
        if (options.hardwareMode != gb::HardwareModePreference::Auto) {
            std::cerr << "aviso: --hardware so se aplica ao modo GB, ignorando para GBA\n";
        }

        gb::gba::System gbaSystem;
        if (!loadGbaGame(gbaSystem, options)) {
            return 1;
        }

        const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
        std::vector<HeadlessGbaInputStep> headlessInputScript{};
        const bool dumpAudio = gb::environmentVariableEnabled("GBEMU_GBA_HEADLESS_DUMP_AUDIO");
        const std::string dumpAudioPath = gb::readEnvironmentVariable("GBEMU_GBA_HEADLESS_DUMP_AUDIO_PATH")
            .value_or("frame_gba.wav");
        std::vector<std::int16_t> capturedAudio{};
        if (const auto inputScriptText = gb::readEnvironmentVariable("GBEMU_GBA_HEADLESS_INPUT_SCRIPT")) {
            headlessInputScript = parseHeadlessGbaInputScript(*inputScriptText);
            if (headlessInputScript.empty()) {
                std::cerr << "aviso: script invalido em GBEMU_GBA_HEADLESS_INPUT_SCRIPT, ignorando\n";
            }
        }

        const int frames = std::max(1, options.frames);
        for (int i = 0; i < frames; ++i) {
            if (!headlessInputScript.empty()) {
                gbaSystem.setInputState(headlessGbaInputForFrame(headlessInputScript, i));
            }
            gbaSystem.runFrame();
            if (dumpAudio) {
                auto frameSamples = gbaSystem.apu().takeSamples();
                if (!frameSamples.empty()) {
                    capturedAudio.insert(capturedAudio.end(), frameSamples.begin(), frameSamples.end());
                }
            }
        }
        if (!dumpAudio) {
            (void)gbaSystem.apu().takeSamples();
        }
        if (gbaSystem.hasPersistentBackup() && gbaSystem.saveBackupToFile(batteryPath)) {
            std::cout << "save interno GBA gravado: " << batteryPath << "\n";
        }
        if (dumpAudio) {
            if (writeStereoPcm16Wav(dumpAudioPath, gb::gba::Apu::SampleRate, capturedAudio)) {
                std::cout << "audio GBA salvo em " << dumpAudioPath
                          << " (" << capturedAudio.size() / 2U << " amostras stereo)\n";
            } else {
                std::cerr << "falha ao salvar audio GBA em " << dumpAudioPath << "\n";
            }
        }
        if (gb::environmentVariableEnabled("GBEMU_GBA_HEADLESS_DUMP_FRAME")) {
            const std::string dumpPath = gb::readEnvironmentVariable("GBEMU_GBA_HEADLESS_DUMP_PATH")
                .value_or("frame_gba.bmp");
            writeGbaFrameAsBmp(dumpPath, gbaSystem);
            std::cout << "framebuffer GBA salvo em " << dumpPath << "\n";
        }
        if (gb::environmentVariableEnabled("GBEMU_GBA_HEADLESS_DUMP_STATE")) {
            const auto& memory = gbaSystem.memory();
            const auto& cpu = gbaSystem.cpu();
            const gb::u32 pc = cpu.pc();
            std::cout << std::hex;
            std::cout << "GBA state: pc=0x" << cpu.pc()
                      << " sp=0x" << cpu.reg(13)
                      << " lr=0x" << cpu.reg(14)
                      << " cpsr=0x" << cpu.cpsr()
                      << " irqHandler=0x" << memory.read32(0x03007FFCU)
                      << " op16=0x" << memory.read16(pc & ~1U)
                      << " op16n=0x" << memory.read16((pc & ~1U) + 2U)
                      << " op32=0x" << memory.read32(pc & ~3U)
                      << " ie=0x" << memory.interruptEnableRaw()
                      << " if=0x" << memory.interruptFlagsRaw()
                      << " ime=0x" << (memory.interruptMasterEnabled() ? 1U : 0U)
                      << " dispcnt=0x" << memory.readIo16(0x000U)
                      << " dispstat=0x" << memory.readIo16(0x004U)
                      << " vcount=0x" << memory.readIo16(0x006U)
                      << " bg0cnt=0x" << memory.readIo16(0x008U)
                      << " bg1cnt=0x" << memory.readIo16(0x00AU)
                      << " bg2cnt=0x" << memory.readIo16(0x00CU)
                      << " bg3cnt=0x" << memory.readIo16(0x00EU)
                      << " tm0=0x" << memory.readIo16(0x100U) << "/0x" << memory.readIo16(0x102U)
                      << " tm1=0x" << memory.readIo16(0x104U) << "/0x" << memory.readIo16(0x106U)
                      << " tm2=0x" << memory.readIo16(0x108U) << "/0x" << memory.readIo16(0x10AU)
                      << " tm3=0x" << memory.readIo16(0x10CU) << "/0x" << memory.readIo16(0x10EU)
                      << std::dec << "\n";
            dumpGbaVideoState(gbaSystem);
            // Dump first 128 bytes of PRAM as hex (64 palette colors)
            const auto& pramData = memory.pram();
            std::cout << " pram_hex:";
            for (int i = 0; i < 128 && i < static_cast<int>(pramData.size()); ++i) {
                if (i % 32 == 0) std::cout << "\n  ";
                std::cout << " " << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(pramData[static_cast<size_t>(i)]);
            }
            std::cout << std::dec << "\n";
        }
        std::cout << "execucao headless GBA finalizada (" << frames << " frames)\n";
        return 0;
    }

#ifndef GBEMU_USE_SDL2
    if (!options.headless) {
        std::cout << "SDL2 nao detectado no build. Executando em modo headless.\n";
    }
#endif

    gb::GameBoy gb;
    if (!loadGame(gb, options)) {
        return 1;
    }

    const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
    const std::string rtcPath = gb::rtcPathForRom(options.romPath);
    const int rc = gb::runHeadless(gb, options.frames);
    if (gb.saveBatteryRamToFile(batteryPath)) {
        std::cout << "save interno gravado: " << batteryPath << "\n";
    }
    if (gb.saveRtcToFile(rtcPath)) {
        std::cout << "rtc gravado: " << rtcPath << "\n";
    }
    return rc;
}
