cmake_minimum_required(VERSION 3.16)
project(gameboy_emulator_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${_cfg} _cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} ${CMAKE_BINARY_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper} ${CMAKE_BINARY_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg_upper} ${CMAKE_BINARY_DIR})
endforeach()

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

option(GBEMU_USE_SDL2 "Build with SDL2 real-time frontend when available" ON)
option(GBEMU_BUILD_MCP "Build read-only OrbitalBoy RunLab MCP stdio adapter" OFF)
option(GBEMU_ENABLE_GBA "Build native mGBA runtime support" ON)
option(GBEMU_BUILD_EXPERIMENTAL_GBA "Build the in-house experimental GBA core and tests" OFF)
option(GBEMU_ENABLE_RETROACHIEVEMENTS "Build RetroAchievements support for GB/GBC" OFF)

if (GBEMU_ENABLE_GBA)
    find_path(MGBA_INCLUDE_DIR mgba/core/core.h)
    find_library(MGBA_LIBRARY NAMES mgba libmgba)
    if (NOT MGBA_INCLUDE_DIR OR NOT MGBA_LIBRARY)
        message(FATAL_ERROR
            "GBEMU_ENABLE_GBA=ON requires native mGBA headers and library. "
            "Install mGBA or use -DGBEMU_ENABLE_GBA=OFF for a GB/GBC-only build.")
    endif()
endif()

set(GBEMU_CORE_SOURCES
    src/core/gameboy.cpp
    src/core/gameboy_state_io.cpp
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

set(GBEMU_EXPERIMENTAL_GBA_SOURCES
    src/core/gba/system.cpp
    src/core/gba/memory.cpp
    src/core/gba/cpu.cpp
    src/core/gba/ppu.cpp
    src/core/gba/apu.cpp
)

set(GBEMU_APP_SOURCES
    src/app/main.cpp
    src/app/cover_fetcher.cpp
    src/app/headless_runner.cpp
    src/app/sdl_frontend.cpp
)

set(GBEMU_REALTIME_SUPPORT_SOURCES
    src/app/frontend/realtime_options.cpp
    src/app/frontend/realtime/debug_session.cpp
    src/app/frontend/realtime/emulation_worker.cpp
    src/app/frontend/realtime/netplay_session.cpp
    src/app/frontend/realtime/realtime_session.cpp
    src/app/frontend/realtime/runlab_session.cpp
    src/app/frontend/realtime/sdl_session_view.cpp
    src/app/frontend/realtime/session_persistence.cpp
    src/app/frontend/realtime/frame_timeline.cpp
    src/app/frontend/realtime/save_slots.cpp
    src/app/frontend/realtime/cheat_engine.cpp
    src/app/frontend/realtime/runlab_control_queue.cpp
    src/app/frontend/realtime/control_bindings.cpp
    src/app/frontend/realtime/link_transport.cpp
    src/app/frontend/realtime/network_config.cpp
    src/app/frontend/realtime/retroachievements_config.cpp
    src/app/frontend/realtime/retroachievements_memory.cpp
    src/app/frontend/realtime/retroachievements_progress.cpp
    src/app/frontend/realtime/top_menu.cpp
    src/app/frontend/realtime/ui_message_queue.cpp
)

set(GBEMU_SDL_FRONTEND_SOURCES
    src/app/frontend/debug_ui.cpp
    src/app/frontend/realtime/audio_ring_buffer.cpp
    src/app/frontend/gba_realtime.cpp
    src/app/frontend/realtime.cpp
    src/app/frontend/rom_selector.cpp
)

set(GBEMU_FRONTEND_SUPPORT_SOURCES
    src/app/app_options.cpp
    src/app/runtime_paths.cpp
    src/app/rom_suite_runner.cpp
    src/app/frontend/runlab.cpp
    src/app/frontend/realtime_support.cpp
    ${GBEMU_REALTIME_SUPPORT_SOURCES}
)

set(GBEMU_TEST_SOURCES
    tests/test_main.cpp
    tests/cpu_behavior_tests.cpp
    tests/cartridge_tests.cpp
    tests/bus_tests.cpp
    tests/peripheral_tests.cpp
    tests/state_and_options_tests.cpp
    tests/frontend_feature_tests.cpp
)

if (WIN32 AND GBEMU_USE_SDL2)
    set(_gbemu_vcpkg_roots)
    if (DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        list(APPEND _gbemu_vcpkg_roots "$ENV{VCPKG_ROOT}")
    endif()
    list(APPEND _gbemu_vcpkg_roots "C:/vcpkg")

    foreach(_root IN LISTS _gbemu_vcpkg_roots)
        foreach(_triplet IN ITEMS x64-windows x64-windows-static)
            if (EXISTS "${_root}/installed/${_triplet}/share/sdl2/SDL2Config.cmake")
                list(APPEND CMAKE_PREFIX_PATH "${_root}/installed/${_triplet}")
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
endif()

if (GBEMU_USE_SDL2)
    find_package(SDL2 CONFIG QUIET)
endif()

add_library(gbcore ${GBEMU_CORE_SOURCES})

target_include_directories(gbcore PUBLIC include)

add_library(gbgba_mgba src/core/gba/mgba_core.cpp)
target_include_directories(gbgba_mgba PUBLIC include)
if (GBEMU_ENABLE_GBA)
    target_include_directories(gbgba_mgba PRIVATE ${MGBA_INCLUDE_DIR})
    target_link_libraries(gbgba_mgba PUBLIC ${MGBA_LIBRARY})
    target_compile_definitions(gbgba_mgba PUBLIC GBEMU_HAVE_MGBA=1)
    message(STATUS "mGBA core enabled: ${MGBA_LIBRARY}")
else()
    message(STATUS "Native mGBA runtime support disabled")
endif()

if (GBEMU_BUILD_EXPERIMENTAL_GBA)
    add_library(gbgba_experimental ${GBEMU_EXPERIMENTAL_GBA_SOURCES})
    target_include_directories(gbgba_experimental PUBLIC include)
endif()

add_library(gbfrontend_support ${GBEMU_FRONTEND_SUPPORT_SOURCES})
target_include_directories(gbfrontend_support PUBLIC include)
target_link_libraries(gbfrontend_support PUBLIC gbcore)
if (SDL2_FOUND)
    target_link_libraries(gbfrontend_support PUBLIC SDL2::SDL2)
    target_compile_definitions(gbfrontend_support PUBLIC GBEMU_USE_SDL2=1 SDL_MAIN_HANDLED=1)
endif()

if (GBEMU_ENABLE_RETROACHIEVEMENTS)
    enable_language(C)
    find_package(CURL REQUIRED)
    add_subdirectory(third_party/rcheevos EXCLUDE_FROM_ALL)
    target_sources(gbfrontend_support PRIVATE
        src/app/frontend/realtime/retroachievements_http.cpp
        src/app/frontend/realtime/retroachievements_session.cpp
    )
    target_link_libraries(gbfrontend_support PUBLIC rcheevos CURL::libcurl)
    if (WIN32)
        target_link_libraries(gbfrontend_support PRIVATE advapi32)
    endif()
    target_compile_definitions(gbfrontend_support PUBLIC
        GBEMU_ENABLE_RETROACHIEVEMENTS=1
        RC_CLIENT_SUPPORTS_HASH=1
    )
    list(APPEND GBEMU_TEST_SOURCES tests/retroachievements_tests.cpp)
endif()

add_executable(gbemu ${GBEMU_APP_SOURCES})
target_link_libraries(gbemu PRIVATE gbgba_mgba gbfrontend_support)
if (WIN32 AND MSVC)
    target_link_options(gbemu PRIVATE /STACK:8388608)
endif()

if (GBEMU_USE_SDL2)
    if (SDL2_FOUND)
        target_sources(gbemu PRIVATE ${GBEMU_SDL_FRONTEND_SOURCES})
        target_link_libraries(gbemu PRIVATE SDL2::SDL2)
        if (TARGET SDL2::SDL2main)
            target_link_libraries(gbemu PRIVATE SDL2::SDL2main)
        endif()
        if (WIN32)
            target_link_libraries(gbemu PRIVATE ole32 windowscodecs)
        endif()
        if (APPLE)
            target_link_libraries(gbemu PRIVATE "-framework CoreFoundation" "-framework CoreGraphics" "-framework ImageIO")
        endif()
        target_compile_definitions(gbemu PRIVATE GBEMU_USE_SDL2=1 SDL_MAIN_HANDLED=1)
        add_custom_command(TARGET gbemu POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:gbemu>
        )
        find_package(SDL2_image CONFIG QUIET)
        if (SDL2_image_FOUND)
            target_link_libraries(gbemu PRIVATE SDL2_image::SDL2_image)
            target_compile_definitions(gbemu PRIVATE GBEMU_USE_SDL2_IMAGE=1)
            add_custom_command(TARGET gbemu POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2_image::SDL2_image> $<TARGET_FILE_DIR:gbemu>
            )
        endif()
    else()
        message(WARNING "SDL2 not found. Building without realtime frontend (headless only).")
    endif()
endif()

if (MSVC)
    target_compile_options(gbcore PRIVATE /W4 /EHsc)
    target_compile_options(gbgba_mgba PRIVATE /W4 /EHsc)
    target_compile_options(gbfrontend_support PRIVATE /W4 /EHsc)
    target_compile_options(gbemu PRIVATE /W4 /EHsc)
    if (TARGET gbgba_experimental)
        target_compile_options(gbgba_experimental PRIVATE /W4 /EHsc)
    endif()
else()
    target_compile_options(gbcore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gbgba_mgba PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gbfrontend_support PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(gbemu PRIVATE -Wall -Wextra -Wpedantic)
    if (TARGET gbgba_experimental)
        target_compile_options(gbgba_experimental PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

include(CTest)
if (GBEMU_BUILD_MCP)
    add_subdirectory(tools/orbitalboy-mcp-cpp)
endif()

if (BUILD_TESTING)
    add_executable(gbemu_tests
        ${GBEMU_TEST_SOURCES}
    )
    target_link_libraries(gbemu_tests PRIVATE gbfrontend_support)
    if (SDL2_FOUND)
        target_sources(gbemu_tests PRIVATE
            src/app/frontend/debug_ui.cpp
        )
        target_link_libraries(gbemu_tests PRIVATE SDL2::SDL2)
        target_compile_definitions(gbemu_tests PRIVATE GBEMU_USE_SDL2=1 SDL_MAIN_HANDLED=1)
    endif()
    if (MSVC)
        target_compile_options(gbemu_tests PRIVATE /W4 /EHsc)
        target_link_options(gbemu_tests PRIVATE /STACK:8388608)
    else()
        target_compile_options(gbemu_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbemu_tests COMMAND gbemu_tests)

    if (GBEMU_BUILD_EXPERIMENTAL_GBA)
        add_executable(gbgba_experimental_tests
            tests/test_main.cpp
            tests/gba_phase2_tests.cpp
        )
        target_link_libraries(gbgba_experimental_tests PRIVATE gbgba_experimental)
        if (MSVC)
            target_compile_options(gbgba_experimental_tests PRIVATE /W4 /EHsc)
        else()
            target_compile_options(gbgba_experimental_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME gbgba_experimental_tests COMMAND gbgba_experimental_tests)
        set_tests_properties(gbgba_experimental_tests PROPERTIES LABELS "experimental;gba")
    endif()
endif()
