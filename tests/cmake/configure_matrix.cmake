cmake_minimum_required(VERSION 3.16)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(TEST_ROOT "/tmp/orbitalboy-cmake-matrix")

function(configure_case name)
    set(build_dir "${TEST_ROOT}/${name}")
    file(REMOVE_RECURSE "${build_dir}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${PROJECT_ROOT}" -B "${build_dir}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "Configuration '${name}' failed unexpectedly.\n${output}\n${error}")
    endif()
    if("${output}\n${error}" MATCHES
       "Manually-specified variables were not used by the project")
        message(FATAL_ERROR
            "Configuration '${name}' ignored a required build option.\n${output}\n${error}")
    endif()
    set("${name}_BUILD_DIR" "${build_dir}" PARENT_SCOPE)
endfunction()

function(assert_target build_dir target expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --target help
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not list targets in ${build_dir}.\n${error}")
    endif()
    if(expected AND NOT output MATCHES "(^|\n)\\.\\.\\. ${target}(\n|$)")
        message(FATAL_ERROR "Expected target '${target}' in ${build_dir}.\n${output}")
    endif()
    if(NOT expected AND output MATCHES "(^|\n)\\.\\.\\. ${target}(\n|$)")
        message(FATAL_ERROR "Unexpected target '${target}' in ${build_dir}.\n${output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${TEST_ROOT}")

configure_case(
    gb_only
    -DGBEMU_ENABLE_GBA=OFF
    -DGBEMU_BUILD_EXPERIMENTAL_GBA=OFF
    -DGBEMU_USE_SDL2=OFF
    -DBUILD_TESTING=OFF
)
assert_target("${gb_only_BUILD_DIR}" gbemu TRUE)
assert_target("${gb_only_BUILD_DIR}" gbfrontend_support TRUE)
assert_target("${gb_only_BUILD_DIR}" gbgba_experimental FALSE)

configure_case(
    experimental
    -DGBEMU_ENABLE_GBA=OFF
    -DGBEMU_BUILD_EXPERIMENTAL_GBA=ON
    -DGBEMU_USE_SDL2=OFF
    -DBUILD_TESTING=ON
)
assert_target("${experimental_BUILD_DIR}" gbgba_experimental TRUE)

message(STATUS "OrbitalBoy CMake configuration matrix passed")
