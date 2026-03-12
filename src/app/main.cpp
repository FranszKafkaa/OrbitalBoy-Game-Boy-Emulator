#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>

#include "gb/app/app_options.hpp"
#include "gb/app/headless_runner.hpp"
#include "gb/app/rom_suite_runner.hpp"
#include "gb/app/runtime_paths.hpp"
#include "gb/app/sdl_frontend.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/core/gba/system.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>
#ifdef GBEMU_USE_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif
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
    if (!options.bootRomPath.empty()) {
        if (!gb.loadBootRomFromFile(options.bootRomPath)) {
            std::cerr << "falha ao carregar boot ROM: " << options.bootRomPath << "\n";
            return false;
        }
    } else {
        gb.clearBootRom();
    }
    gb.setPreciseTiming(options.preciseTiming);

    if (!gb.loadRom(options.romPath)) {
        std::cerr << "falha ao carregar ROM: " << options.romPath << "\n";
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

    const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
    if (gb.loadBatteryRamFromFile(batteryPath)) {
        std::cout << "save interno carregado: " << batteryPath << "\n";
    }
    const std::string rtcPath = gb::rtcPathForRom(options.romPath);
    if (gb.loadRtcFromFile(rtcPath)) {
        std::cout << "rtc carregado: " << rtcPath << "\n";
    }
    return true;
}

bool loadGbaGame(gb::gba::System& system, const gb::AppOptions& options) {
    if (!system.loadRomFromFile(options.romPath)) {
        std::cerr << "falha ao carregar ROM GBA: " << options.romPath << "\n";
        return false;
    }

    const auto& meta = system.metadata();
    const std::string fallbackTitle = std::filesystem::path(options.romPath).filename().string();
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

    resolvedTargetSystem = resolveTargetSystem(options);

    if (resolvedTargetSystem == ResolvedTargetSystem::Gba) {
        if (options.hardwareMode != gb::HardwareModePreference::Auto) {
            std::cerr << "aviso: --hardware so se aplica ao modo GB, ignorando para GBA\n";
        }

        gb::gba::System gbaSystem;
        if (!loadGbaGame(gbaSystem, options)) {
            return 1;
        }

#ifdef GBEMU_USE_SDL2
        if (!options.headless) {
            return gb::runGbaRealtime(gbaSystem, options.scale);
        }
#else
        if (!options.headless) {
            std::cout << "SDL2 nao detectado no build. Executando GBA em modo headless.\n";
        }
#endif

        const int frames = std::max(1, options.frames);
        for (int i = 0; i < frames; ++i) {
            gbaSystem.runFrame();
        }
        std::cout << "execucao headless GBA finalizada (" << frames << " frames)\n";
        return 0;
    }

#ifdef GBEMU_USE_SDL2
    if (!options.headless) {
        return runRealtimeFlow(options);
    }
#else
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
