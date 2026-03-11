#include <iostream>
#include <string>

#include "gb/app/app_options.hpp"
#include "gb/core/gameboy.hpp"
#include "gb/app/headless_runner.hpp"
#include "gb/app/rom_suite_runner.hpp"
#include "gb/app/runtime_paths.hpp"
#include "gb/app/sdl_frontend.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>
#ifdef GBEMU_USE_SDL2_IMAGE
#include <SDL2/SDL_image.h>
#endif
#endif

namespace {

bool loadGame(gb::GameBoy& gb, const std::string& romPath) {
    if (!gb.loadRom(romPath)) {
        std::cerr << "falha ao carregar ROM: " << romPath << "\n";
        return false;
    }

    std::cout << "ROM carregada: " << gb.cartridge().title() << "\n";
    if (gb.cartridge().shouldRunInCgbMode()) {
        std::cout << "modo CGB: suporte inicial experimental ativado\n";
    } else if (gb.cartridge().cgbSupported()) {
        std::cout << "modo DMG: ROM compativel com CGB detectada (.gb)\n";
    }

    const std::string batteryPath = gb::batteryRamPathForRom(romPath);
    if (gb.loadBatteryRamFromFile(batteryPath)) {
        std::cout << "save interno carregado: " << batteryPath << "\n";
    }
    const std::string rtcPath = gb::rtcPathForRom(romPath);
    if (gb.loadRtcFromFile(rtcPath)) {
        std::cout << "rtc carregado: " << rtcPath << "\n";
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
        gb::GameBoy gb;
        if (!loadGame(gb, options.romPath)) {
            return 1;
        }

        const std::string batteryPath = gb::batteryRamPathForRom(options.romPath);
        const std::string palettePath = gb::palettePathForRom(options.romPath);
        const std::string rtcPath = gb::rtcPathForRom(options.romPath);
        const std::string filtersPath = gb::filtersPathForRom(options.romPath);
        const std::string captureDir = gb::captureDirForRom(options.romPath);
        const int rc = gb::runRealtime(
            gb,
            options.scale,
            options.audioBuffer,
            gb::statePathForRom(options.romPath),
            gb::legacyStatePathForRom(options.romPath),
            batteryPath,
            palettePath,
            rtcPath,
            filtersPath,
            captureDir
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

    if (!options.romSuiteManifest.empty()) {
        return gb::runRomCompatibilitySuite(options.romSuiteManifest);
    }

#ifndef GBEMU_USE_SDL2
    (void)options.chooseRom;
#endif

    if (options.romPath.empty()) {
#ifdef GBEMU_USE_SDL2
        options.chooseRom = true;
        options.headless = false;
#endif
    }

#ifdef GBEMU_USE_SDL2
    if (options.chooseRom && !openRomSelector(options)) {
        return 1;
    }
#endif

    if (options.romPath.empty()) {
        std::cerr << "nenhuma ROM selecionada.\n";
#ifndef GBEMU_USE_SDL2
        std::cerr << "use: gbemu --rom <rom.gb>\n";
        std::cerr << "ou compile com SDL2 para usar seletor grafico.\n";
#endif
        std::cerr << "audio: --audio-buffer 1024 (256..8192)\n";
        return 1;
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
    if (!loadGame(gb, options.romPath)) {
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
