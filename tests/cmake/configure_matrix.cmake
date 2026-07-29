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

function(curl_is_available result)
    set(probe_source_dir "${TEST_ROOT}/curl_probe_source")
    set(probe_build_dir "${TEST_ROOT}/curl_probe_build")
    file(REMOVE_RECURSE "${probe_source_dir}" "${probe_build_dir}")
    file(MAKE_DIRECTORY "${probe_source_dir}")
    file(WRITE "${probe_source_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.16)
project(orbitalboy_curl_probe LANGUAGES C)
find_package(CURL QUIET)
if (NOT CURL_FOUND)
    message(FATAL_ERROR "CURL not found")
endif()
]=])
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${probe_source_dir}" -B "${probe_build_dir}"
        RESULT_VARIABLE result_code
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if (result_code EQUAL 0)
        set("${result}" TRUE PARENT_SCOPE)
    else()
        set("${result}" FALSE PARENT_SCOPE)
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
assert_target("${gb_only_BUILD_DIR}" gbgba_experimental_tests FALSE)

configure_case(
    ra_disabled
    -DGBEMU_ENABLE_GBA=OFF
    -DGBEMU_BUILD_EXPERIMENTAL_GBA=OFF
    -DGBEMU_USE_SDL2=OFF
    -DGBEMU_ENABLE_RETROACHIEVEMENTS=OFF
    -DBUILD_TESTING=OFF
)
assert_target("${ra_disabled_BUILD_DIR}" rcheevos FALSE)

curl_is_available(curl_available)
if (curl_available)
    configure_case(
        ra_enabled
        -DGBEMU_ENABLE_GBA=OFF
        -DGBEMU_BUILD_EXPERIMENTAL_GBA=OFF
        -DGBEMU_USE_SDL2=OFF
        -DGBEMU_ENABLE_RETROACHIEVEMENTS=ON
        -DBUILD_TESTING=ON
    )
    assert_target("${ra_enabled_BUILD_DIR}" rcheevos TRUE)
    assert_target("${ra_enabled_BUILD_DIR}" gbfrontend_support TRUE)
    assert_target("${ra_enabled_BUILD_DIR}" gbemu_tests TRUE)
endif()
if (curl_available AND NOT DEFINED ra_enabled_BUILD_DIR)
    message(FATAL_ERROR "CURL is discoverable, but ra_enabled was not configured")
endif()

configure_case(
    experimental
    -DGBEMU_ENABLE_GBA=OFF
    -DGBEMU_BUILD_EXPERIMENTAL_GBA=ON
    -DGBEMU_USE_SDL2=OFF
    -DBUILD_TESTING=ON
)
assert_target("${experimental_BUILD_DIR}" gbgba_experimental TRUE)
assert_target("${experimental_BUILD_DIR}" gbgba_experimental_tests TRUE)

message(STATUS "OrbitalBoy CMake configuration matrix passed")
