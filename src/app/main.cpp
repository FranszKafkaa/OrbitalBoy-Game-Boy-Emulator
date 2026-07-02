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
#include "gb/core/gba/libretro_core.hpp"
#include "gb/core/gba/mgba_core.hpp"

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

void writeGbaFrameAsBmp(const std::string& path, const gb::gba::LibretroCore& core) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }

    constexpr int width = gb::gba::LibretroCore::ScreenWidth;
    constexpr int height = gb::gba::LibretroCore::ScreenHeight;
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
    const auto& frame = core.framebuffer();
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

void writeGbaFrameAsBmp(const std::string& path, const gb::gba::MgbaCore& core) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }

    constexpr int width = gb::gba::MgbaCore::ScreenWidth;
    constexpr int height = gb::gba::MgbaCore::ScreenHeight;
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
    const auto& frame = core.framebuffer();
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

bool shouldUseLibretroGbaCore() {
    if (gb::hasEnvironmentVariable("GBEMU_GBA_LIBRETRO_CORE")) {
        return true;
    }
    const auto value = gb::readEnvironmentVariable("GBEMU_GBA_CORE");
    return value.has_value() && toLowerAscii(*value) == "libretro";
}

bool loadLibretroGbaGame(gb::gba::LibretroCore& core, const gb::AppOptions& options) {
    const std::string corePath = gb::gba::resolveLibretroGbaCorePath();
    if (corePath.empty()) {
        std::cerr << "core GBA libretro nao encontrado.\n"
                  << "defina GBEMU_GBA_LIBRETRO_CORE=/caminho/para/mgba_libretro.dylib\n";
        return false;
    }
    if (!core.loadCore(corePath)) {
        return false;
    }

    const std::string resolvedRomPath = gb::resolveRomPathForRuntime(options.romPath);
    if (!core.loadRomFromFile(resolvedRomPath)) {
        return false;
    }

    std::cout << "ROM GBA carregada no core externo: "
              << std::filesystem::path(resolvedRomPath).filename().string() << "\n";
    std::cout << "core externo: " << core.coreName() << "\n";
    std::cout << "core path: " << core.corePath() << "\n";

    const std::string batteryPath = gb::batteryRamPathForRom(resolvedRomPath);
    if (core.loadBackupFromFile(batteryPath)) {
        std::cout << "save interno GBA carregado: " << batteryPath << "\n";
    }
    return true;
}

bool loadNativeMgbaGame(gb::gba::MgbaCore& core, const gb::AppOptions& options) {
#ifndef GBEMU_HAVE_MGBA
    static_cast<void>(core);
    static_cast<void>(options);
    std::cerr << "core nativo mGBA nao foi encontrado no build.\n";
    return false;
#else
    const std::string resolvedRomPath = gb::resolveRomPathForRuntime(options.romPath);
    if (!core.loadRomFromFile(resolvedRomPath)) {
        return false;
    }

    std::cout << "ROM GBA carregada no core nativo: "
              << std::filesystem::path(resolvedRomPath).filename().string() << "\n";
    std::cout << "core nativo: " << core.coreName() << "\n";

    const std::string batteryPath = gb::batteryRamPathForRom(resolvedRomPath);
    if (core.loadBackupFromFile(batteryPath)) {
        std::cout << "save interno GBA carregado: " << batteryPath << "\n";
    }
    return true;
#endif
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
            if (shouldUseLibretroGbaCore()) {
                gb::gba::LibretroCore core;
                if (!loadLibretroGbaGame(core, options)) {
                    return 1;
                }
                const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
                const std::string statePath = gb::statePathForRom(options.romPath);
                const std::string captureDir = gb::captureDirForRom(options.romPath);
                const int rc = gb::runGbaLibretroRealtime(core, options.scale, statePath, batteryPath, captureDir);
                if (core.saveBackupToFile(batteryPath)) {
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

            gb::gba::MgbaCore core;
            if (!loadNativeMgbaGame(core, options)) {
                return 1;
            }
            const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
            const std::string statePath = gb::statePathForRom(options.romPath);
            const std::string captureDir = gb::captureDirForRom(options.romPath);
            const int rc = gb::runGbaMgbaRealtime(core, options.scale, statePath, batteryPath, captureDir);
            if (core.saveBackupToFile(batteryPath)) {
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
            options.netplayDelayFrames,
            options.runLabControl,
            options.runLabStatePath,
            options.runLabCommandQueuePath
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

        if (!shouldUseLibretroGbaCore()) {
            gb::gba::MgbaCore core;
            if (!loadNativeMgbaGame(core, options)) {
                return 1;
            }

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
                    core.setInputState(headlessGbaInputForFrame(headlessInputScript, i));
                }
                core.runFrame();
                if (dumpAudio) {
                    auto frameSamples = core.takeSamples();
                    if (!frameSamples.empty()) {
                        capturedAudio.insert(capturedAudio.end(), frameSamples.begin(), frameSamples.end());
                    }
                }
            }
            if (!dumpAudio) {
                (void)core.takeSamples();
            }

            const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
            if (core.saveBackupToFile(batteryPath)) {
                std::cout << "save interno GBA gravado: " << batteryPath << "\n";
            }
            if (dumpAudio) {
                if (writeStereoPcm16Wav(dumpAudioPath, gb::gba::MgbaCore::SampleRate, capturedAudio)) {
                    std::cout << "audio GBA salvo em " << dumpAudioPath
                              << " (" << capturedAudio.size() / 2U << " amostras stereo)\n";
                } else {
                    std::cerr << "falha ao salvar audio GBA em " << dumpAudioPath << "\n";
                }
            }
            if (gb::environmentVariableEnabled("GBEMU_GBA_HEADLESS_DUMP_FRAME")) {
                const std::string dumpPath = gb::readEnvironmentVariable("GBEMU_GBA_HEADLESS_DUMP_PATH")
                    .value_or("frame_gba.bmp");
                writeGbaFrameAsBmp(dumpPath, core);
                std::cout << "framebuffer GBA salvo em " << dumpPath << "\n";
            }
            std::cout << "execucao headless GBA mGBA finalizada (" << frames << " frames)\n";
            return 0;
        }

        gb::gba::LibretroCore core;
        if (!loadLibretroGbaGame(core, options)) {
            return 1;
        }

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
                core.setInputState(headlessGbaInputForFrame(headlessInputScript, i));
            }
            core.runFrame();
            if (dumpAudio) {
                auto frameSamples = core.takeSamples();
                if (!frameSamples.empty()) {
                    capturedAudio.insert(capturedAudio.end(), frameSamples.begin(), frameSamples.end());
                }
            }
        }
        if (!dumpAudio) {
            (void)core.takeSamples();
        }

        const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
        if (core.saveBackupToFile(batteryPath)) {
            std::cout << "save interno GBA gravado: " << batteryPath << "\n";
        }
        if (dumpAudio) {
            if (writeStereoPcm16Wav(dumpAudioPath, gb::gba::LibretroCore::SampleRate, capturedAudio)) {
                std::cout << "audio GBA salvo em " << dumpAudioPath
                          << " (" << capturedAudio.size() / 2U << " amostras stereo)\n";
            } else {
                std::cerr << "falha ao salvar audio GBA em " << dumpAudioPath << "\n";
            }
        }
        if (gb::environmentVariableEnabled("GBEMU_GBA_HEADLESS_DUMP_FRAME")) {
            const std::string dumpPath = gb::readEnvironmentVariable("GBEMU_GBA_HEADLESS_DUMP_PATH")
                .value_or("frame_gba.bmp");
            writeGbaFrameAsBmp(dumpPath, core);
            std::cout << "framebuffer GBA salvo em " << dumpPath << "\n";
        }
        std::cout << "execucao headless GBA libretro finalizada (" << frames << " frames)\n";
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
