#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
        }
        if (gbaSystem.hasPersistentBackup() && gbaSystem.saveBackupToFile(batteryPath)) {
            std::cout << "save interno GBA gravado: " << batteryPath << "\n";
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
