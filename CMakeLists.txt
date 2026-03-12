cmake_minimum_required(VERSION 3.16)
project(gameboy_emulator_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GBEMU_USE_SDL2 "Build with SDL2 real-time frontend when available" ON)

add_library(gbcore
    src/core/gameboy.cpp
    src/core/gameboy_state_io.cpp
    src/core/gba/system.cpp
    src/core/gba/memory.cpp
    src/core/gba/cpu.cpp
    src/core/gba/ppu.cpp
    src/core/cartridge.cpp
    src/core/cartridge/mappers/no_mbc.cpp
    src/core/cartridge/mappers/mbc1.cpp
    src/core/cartridge/mappers/mmm01.cpp
    src/core/cartridge/mappers/mbc2.cpp
    src/core/cartridge/mappers/mbc3.cpp
    src/core/cartridge/mappers/mbc5.cpp
    src/core/cartridge/mappers/huc1.cpp
    src/core/cartridge/mappers/huc3.cpp
    src/core/cartridge/mappers/mbc7.cpp
    src/core/cartridge/mappers/pocket_camera.cpp
    src/core/bus.cpp
    src/core/cpu.cpp
    src/core/cpu_alu.cpp
    src/core/ppu.cpp
    src/core/apu.cpp
    src/core/timer.cpp
    src/core/joypad.cpp
)

target_include_directories(gbcore PUBLIC include)

add_executable(gbemu
    src/app/main.cpp
    src/app/app_options.cpp
    src/app/headless_runner.cpp
    src/app/rom_suite_runner.cpp
    src/app/runtime_paths.cpp
    src/app/sdl_frontend.cpp
    src/app/frontend/debug_ui.cpp
    src/app/frontend/realtime_support.cpp
    src/app/frontend/realtime/frame_timeline.cpp
    src/app/frontend/realtime/audio_ring_buffer.cpp
    src/app/frontend/realtime/save_slots.cpp
    src/app/frontend/realtime/cheat_engine.cpp
    src/app/frontend/realtime/replay_io.cpp
    src/app/frontend/realtime/control_bindings.cpp
    src/app/frontend/realtime/link_transport.cpp
    src/app/frontend/realtime/network_config.cpp
    src/app/frontend/realtime/top_menu.cpp
    src/app/frontend/gba_realtime.cpp
    src/app/frontend/realtime.cpp
    src/app/frontend/rom_selector.cpp
)
target_link_libraries(gbemu PRIVATE gbcore)

if (GBEMU_USE_SDL2)
    find_package(SDL2 REQUIRED)
    target_link_libraries(gbemu PRIVATE SDL2::SDL2)
    target_compile_definitions(gbemu PRIVATE GBEMU_USE_SDL2=1)
    find_package(SDL2_image QUIET)
    if (SDL2_image_FOUND)
        target_link_libraries(gbemu PRIVATE SDL2_image::SDL2_image)
        target_compile_definitions(gbemu PRIVATE GBEMU_USE_SDL2_IMAGE=1)
    endif()
endif()

if (MSVC)
    target_compile_options(gbcore PRIVATE /W4)
    target_compile_options(gbemu PRIVATE /W4)
else()
    target_compile_options(gbcore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gbemu PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(gbemu_tests
        src/app/app_options.cpp
        src/app/rom_suite_runner.cpp
        src/app/frontend/realtime/save_slots.cpp
        src/app/frontend/realtime/cheat_engine.cpp
        src/app/frontend/realtime/replay_io.cpp
        src/app/frontend/realtime/control_bindings.cpp
        src/app/frontend/realtime/link_transport.cpp
        src/app/frontend/realtime/network_config.cpp
        src/app/frontend/realtime/frame_timeline.cpp
        src/app/frontend/realtime/top_menu.cpp
        tests/test_main.cpp
        tests/cpu_behavior_tests.cpp
        tests/cartridge_tests.cpp
        tests/bus_tests.cpp
        tests/peripheral_tests.cpp
        tests/state_and_options_tests.cpp
        tests/gba_phase2_tests.cpp
        tests/frontend_feature_tests.cpp
    )
    target_link_libraries(gbemu_tests PRIVATE gbcore)
    if (MSVC)
        target_compile_options(gbemu_tests PRIVATE /W4)
    else()
        target_compile_options(gbemu_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbemu_tests COMMAND gbemu_tests)

    set(GBEMU_TEST_SUITES
        apu
        bus
        cartridge
        cpu
        joypad
        options
        ppu
        state
        timer
    )
    foreach(suite IN LISTS GBEMU_TEST_SUITES)
        add_test(NAME gbemu_suite_${suite} COMMAND gbemu_tests --suite ${suite})
        set_tests_properties(gbemu_suite_${suite} PROPERTIES LABELS "suite;${suite}")
    endforeach()
endif()
