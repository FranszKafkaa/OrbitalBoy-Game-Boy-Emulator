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
option(GBEMU_ENABLE_RETROACHIEVEMENTS "Build RetroAchievements support for GB/GBC/GBA" OFF)

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
    src/achievements/runtime/frontend_achievement_bridge.cpp
    src/achievements/runtime/owned_achievement_runtime.cpp
    src/achievements/runtime/achievement_frame_attachment.cpp
    src/achievements/runtime/achievement_frame_bridge.cpp
    src/achievements/runtime/condition_evaluator.cpp
    src/achievements/adapters/memory_adapters.cpp
    src/core/gba/system.cpp
    src/core/gba/memory.cpp
    src/core/gba/cpu.cpp
    src/core/gba/ppu.cpp
    src/core/gba/apu.cpp
    src/achievements/runtime/achievement_runtime.cpp
    src/achievements/security/secret_string.cpp
    src/achievements/protocol/http_transport.cpp
    src/achievements/config/achievement_config.cpp
    src/achievements/storage/private_file_io.cpp
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
    src/app/frontend/realtime/retroachievements_memory.cpp
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
    tests/achievements/config/achievement_config_compatibility_tests.cpp
    tests/achievements/security/secret_string_compatibility_tests.cpp
    tests/achievements/runtime/achievement_runtime_tests.cpp
    tests/achievements/runtime/achievement_models_tests.cpp
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

add_library(gbachievements_hashing
    src/achievements/hashing/md5.cpp
    src/achievements/hashing/rom_hasher.cpp
)
target_include_directories(gbachievements_hashing PUBLIC include)
if (MSVC)
    target_compile_options(gbachievements_hashing PRIVATE /W4 /EHsc)
else()
    target_compile_options(gbachievements_hashing PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
target_link_libraries(gbfrontend_support PUBLIC gbcore gbachievements_hashing)
if (WIN32)
    target_link_libraries(gbfrontend_support PRIVATE advapi32)
endif()
if (SDL2_FOUND)
    target_link_libraries(gbfrontend_support PUBLIC SDL2::SDL2)
    target_compile_definitions(gbfrontend_support PUBLIC GBEMU_USE_SDL2=1 SDL_MAIN_HANDLED=1)
endif()

if (GBEMU_ENABLE_RETROACHIEVEMENTS)
    enable_language(C)
    find_package(CURL REQUIRED)
    add_subdirectory(third_party/rcheevos EXCLUDE_FROM_ALL)
    target_sources(gbfrontend_support PRIVATE
        src/achievements/adapters/rcheevos/achievement_runtime_factory.cpp
        src/achievements/adapters/rcheevos/rcheevos_achievement_runtime.cpp
        src/achievements/adapters/curl/curl_http_executor.cpp
        src/app/frontend/realtime/retroachievements_http.cpp
        src/app/frontend/realtime/retroachievements_image_cache.cpp
        src/app/frontend/realtime/retroachievements_lifecycle.cpp
        src/app/frontend/realtime/retroachievements_progress.cpp
        src/app/frontend/realtime/retroachievements_session.cpp
        src/app/frontend/realtime/retroachievements_ui.cpp
    )
    target_include_directories(gbfrontend_support PRIVATE third_party/rcheevos/src)
    target_link_libraries(gbfrontend_support PUBLIC rcheevos CURL::libcurl)
    if (WIN32)
        target_link_libraries(gbfrontend_support PRIVATE bcrypt)
    endif()
    target_compile_definitions(gbfrontend_support PUBLIC
        GBEMU_ENABLE_RETROACHIEVEMENTS=1
        RC_CLIENT_SUPPORTS_HASH=1
    )
    list(APPEND GBEMU_TEST_SOURCES
        tests/retroachievements_tests.cpp
        tests/achievements/protocol/legacy_ra_http_forward_declaration_tests.cpp
    )
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
        if (GBEMU_ENABLE_RETROACHIEVEMENTS)
            find_package(SDL2_image CONFIG QUIET)
        endif()
        if (GBEMU_ENABLE_RETROACHIEVEMENTS AND SDL2_image_FOUND)
            target_link_libraries(gbfrontend_support PUBLIC SDL2_image::SDL2_image)
            target_compile_definitions(gbfrontend_support PUBLIC GBEMU_USE_SDL2_IMAGE=1)
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
    add_executable(gbachievements_hashing_tests
        tests/test_main.cpp
        tests/achievements/hashing/rom_hashing_tests.cpp
    )
    target_include_directories(gbachievements_hashing_tests PRIVATE include)
    target_link_libraries(gbachievements_hashing_tests PRIVATE gbachievements_hashing)
    if (MSVC)
        target_compile_options(gbachievements_hashing_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_hashing_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_hashing_tests COMMAND gbachievements_hashing_tests)

    add_executable(gbachievements_config_storage_tests
        tests/test_main.cpp
        tests/achievements/config/achievement_config_tests.cpp
        tests/achievements/storage/private_file_io_tests.cpp
        src/achievements/config/achievement_config.cpp
        src/achievements/storage/private_file_io.cpp
        src/achievements/security/secret_string.cpp
    )
    target_include_directories(gbachievements_config_storage_tests PRIVATE include)
    if (WIN32)
        target_link_libraries(gbachievements_config_storage_tests PRIVATE advapi32)
    endif()
    if (MSVC)
        target_compile_options(gbachievements_config_storage_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_config_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(
        NAME gbachievements_config_storage_tests
        COMMAND gbachievements_config_storage_tests
    )

    add_executable(gbachievements_security_tests
        tests/test_main.cpp
        tests/achievements/security/secret_string_tests.cpp
        src/achievements/security/secret_string.cpp
    )
    target_include_directories(gbachievements_security_tests PRIVATE include)
    if (MSVC)
        target_compile_options(gbachievements_security_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_security_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_security_tests COMMAND gbachievements_security_tests)

    add_executable(gbachievements_http_transport_tests
        tests/test_main.cpp
        tests/achievements/protocol/http_transport_tests.cpp
        src/achievements/protocol/http_transport.cpp
        src/achievements/security/secret_string.cpp
    )
    target_include_directories(gbachievements_http_transport_tests PRIVATE include)
    if (MSVC)
        target_compile_options(gbachievements_http_transport_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_http_transport_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_http_transport_tests COMMAND gbachievements_http_transport_tests)

    # Deliberately isolated from application and transport dependencies.
    add_executable(gbachievements_json_parser_tests
        tests/test_main.cpp
        tests/achievements/protocol/json_parser_tests.cpp
        src/achievements/protocol/json_value.cpp
        src/achievements/protocol/json_parser.cpp
    )
    target_include_directories(gbachievements_json_parser_tests PRIVATE include)
    if (MSVC)
        target_compile_options(gbachievements_json_parser_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_json_parser_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_json_parser_tests COMMAND gbachievements_json_parser_tests)

    # Deliberately isolated from application, runtime, and rcheevos dependencies.
    add_executable(gbachievements_condition_parser_tests
        tests/test_main.cpp
        tests/achievements/parser/condition_parser_tests.cpp
        src/achievements/parser/condition_parser.cpp
    )
    target_include_directories(gbachievements_condition_parser_tests PRIVATE include)
    if (MSVC)
        target_compile_options(gbachievements_condition_parser_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_condition_parser_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_condition_parser_tests COMMAND gbachievements_condition_parser_tests)

    # Deliberately isolated from live achievement runtime and rcheevos dependencies.
    add_executable(gbachievements_condition_evaluator_tests
        tests/test_main.cpp
        tests/achievements/runtime/condition_evaluator_tests.cpp
        src/achievements/runtime/condition_evaluator.cpp
    )
    target_include_directories(gbachievements_condition_evaluator_tests PRIVATE include)
    if (MSVC)
        target_compile_options(gbachievements_condition_evaluator_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_condition_evaluator_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_condition_evaluator_tests COMMAND gbachievements_condition_evaluator_tests)

    add_executable(gbachievements_frame_bridge_tests
        tests/test_main.cpp
        tests/achievements/runtime/achievement_frame_bridge_tests.cpp
        src/achievements/runtime/achievement_frame_bridge.cpp
        src/achievements/runtime/condition_evaluator.cpp
    )
    target_include_directories(gbachievements_frame_bridge_tests PRIVATE include)
    if (MSVC)
        target_compile_options(gbachievements_frame_bridge_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_frame_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_frame_bridge_tests COMMAND gbachievements_frame_bridge_tests)

    add_executable(gbachievements_memory_adapter_tests
        tests/test_main.cpp
        tests/achievements/adapters/memory_adapters_tests.cpp
        tests/achievements/runtime/achievement_frame_clock_tests.cpp
        src/achievements/adapters/memory_adapters.cpp
        src/achievements/runtime/achievement_frame_bridge.cpp
        src/achievements/runtime/condition_evaluator.cpp
        src/core/gba/system.cpp
        src/core/gba/memory.cpp
        src/core/gba/cpu.cpp
        src/core/gba/ppu.cpp
        src/core/gba/apu.cpp
        src/core/gba/mgba_core.cpp
    )
    target_include_directories(gbachievements_memory_adapter_tests PRIVATE include)
    target_link_libraries(gbachievements_memory_adapter_tests PRIVATE gbcore)
    if (MSVC)
        target_compile_options(gbachievements_memory_adapter_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_memory_adapter_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_memory_adapter_tests COMMAND gbachievements_memory_adapter_tests)

    add_executable(gbcore_frame_observer_tests
        tests/test_main.cpp
        tests/core_frame_observer_tests.cpp
        src/core/gba/system.cpp
        src/core/gba/memory.cpp
        src/core/gba/cpu.cpp
        src/core/gba/ppu.cpp
        src/core/gba/apu.cpp
        src/core/gba/mgba_core.cpp
    )
    target_include_directories(gbcore_frame_observer_tests PRIVATE include)
    target_link_libraries(gbcore_frame_observer_tests PRIVATE gbcore)
    if (MSVC)
        target_compile_options(gbcore_frame_observer_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbcore_frame_observer_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbcore_frame_observer_tests COMMAND gbcore_frame_observer_tests)

    add_executable(gbachievements_frame_attachment_tests
        tests/test_main.cpp
        tests/achievements/runtime/achievement_frame_attachment_tests.cpp
        src/achievements/runtime/achievement_frame_attachment.cpp
        src/achievements/runtime/achievement_frame_bridge.cpp
        src/achievements/runtime/condition_evaluator.cpp
        src/achievements/adapters/memory_adapters.cpp
        src/core/gba/system.cpp
        src/core/gba/memory.cpp
        src/core/gba/cpu.cpp
        src/core/gba/ppu.cpp
        src/core/gba/apu.cpp
        src/core/gba/mgba_core.cpp
    )
    target_include_directories(gbachievements_frame_attachment_tests PRIVATE include)
    target_link_libraries(gbachievements_frame_attachment_tests PRIVATE gbcore)
    if (MSVC)
        target_compile_options(gbachievements_frame_attachment_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_frame_attachment_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_frame_attachment_tests COMMAND gbachievements_frame_attachment_tests)

    add_executable(gbfrontend_achievement_bridge_tests
        tests/test_main.cpp
        tests/achievements/runtime/frontend_achievement_bridge_tests.cpp
        src/achievements/runtime/frontend_achievement_bridge.cpp
        src/achievements/runtime/achievement_frame_attachment.cpp
        src/achievements/runtime/achievement_frame_bridge.cpp
        src/achievements/runtime/condition_evaluator.cpp
        src/achievements/adapters/memory_adapters.cpp
        src/core/gba/system.cpp
        src/core/gba/memory.cpp
        src/core/gba/cpu.cpp
        src/core/gba/ppu.cpp
        src/core/gba/apu.cpp
    )
    target_include_directories(gbfrontend_achievement_bridge_tests PRIVATE include)
    target_link_libraries(gbfrontend_achievement_bridge_tests PRIVATE gbcore gbgba_mgba)
    if (MSVC)
        target_compile_options(gbfrontend_achievement_bridge_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbfrontend_achievement_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbfrontend_achievement_bridge_tests COMMAND gbfrontend_achievement_bridge_tests)

    add_executable(gbachievements_owned_runtime_tests
        tests/test_main.cpp
        tests/achievements/runtime/owned_achievement_runtime_tests.cpp
        src/achievements/runtime/owned_achievement_runtime.cpp
        src/achievements/runtime/achievement_runtime.cpp
        src/achievements/runtime/achievement_frame_attachment.cpp
        src/achievements/runtime/achievement_frame_bridge.cpp
        src/achievements/runtime/condition_evaluator.cpp
        src/achievements/security/secret_string.cpp
        src/achievements/adapters/memory_adapters.cpp
        src/core/gba/system.cpp
        src/core/gba/memory.cpp
        src/core/gba/cpu.cpp
        src/core/gba/ppu.cpp
        src/core/gba/apu.cpp
        src/core/gba/mgba_core.cpp
    )
    target_include_directories(gbachievements_owned_runtime_tests PRIVATE include)
    target_link_libraries(gbachievements_owned_runtime_tests PRIVATE gbcore)
    if (MSVC)
        target_compile_options(gbachievements_owned_runtime_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievements_owned_runtime_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievements_owned_runtime_tests COMMAND gbachievements_owned_runtime_tests)

    add_executable(gbachievement_models_tests
        tests/test_main.cpp
        tests/achievements/runtime/achievement_models_tests.cpp
    )
    target_include_directories(gbachievement_models_tests PRIVATE include)
    if (MSVC)
        target_compile_options(gbachievement_models_tests PRIVATE /W4 /EHsc)
    else()
        target_compile_options(gbachievement_models_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbachievement_models_tests COMMAND gbachievement_models_tests)

    add_executable(gbemu_tests
        ${GBEMU_TEST_SOURCES}
    )
    target_link_libraries(gbemu_tests PRIVATE gbfrontend_support gbgba_mgba)
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
