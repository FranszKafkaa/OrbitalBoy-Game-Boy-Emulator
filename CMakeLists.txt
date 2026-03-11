cmake_minimum_required(VERSION 3.16)
project(gameboy_emulator_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GBEMU_USE_SDL2 "Build with SDL2 real-time frontend when available" ON)

add_library(gbcore
    src/gameboy.cpp
    src/cartridge.cpp
    src/bus.cpp
    src/cpu.cpp
    src/ppu.cpp
    src/apu.cpp
    src/timer.cpp
    src/joypad.cpp
)

target_include_directories(gbcore PUBLIC include)

add_executable(gbemu src/main.cpp)
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
        tests/cpu_smoke_tests.cpp
    )
    target_link_libraries(gbemu_tests PRIVATE gbcore)
    if (MSVC)
        target_compile_options(gbemu_tests PRIVATE /W4)
    else()
        target_compile_options(gbemu_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME gbemu_tests COMMAND gbemu_tests)
endif()
