add_executable(orbitalboy-mcp
    src/json_rpc.cpp
    src/main.cpp
    src/mcp_prompts.cpp
    src/mcp_resources.cpp
    src/mcp_server.cpp
    src/mcp_tools.cpp
    src/runlab_client.cpp
    src/runlab_state.cpp
    src/safe_paths.cpp
)

target_compile_features(orbitalboy-mcp PRIVATE cxx_std_17)
target_include_directories(orbitalboy-mcp PRIVATE src)

if (MSVC)
    target_compile_options(orbitalboy-mcp PRIVATE /W4)
else()
    target_compile_options(orbitalboy-mcp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (BUILD_TESTING)
    add_executable(orbitalboy_mcp_tests
        src/json_rpc.cpp
        src/mcp_prompts.cpp
        src/mcp_resources.cpp
        src/mcp_server.cpp
        src/mcp_tools.cpp
        src/runlab_client.cpp
        src/runlab_state.cpp
        src/safe_paths.cpp
        tests/test_runlab_state.cpp
        tests/test_safe_paths.cpp
        tests/test_tools.cpp
    )
    target_compile_features(orbitalboy_mcp_tests PRIVATE cxx_std_17)
    target_include_directories(orbitalboy_mcp_tests PRIVATE src)
    if (MSVC)
        target_compile_options(orbitalboy_mcp_tests PRIVATE /W4)
    else()
        target_compile_options(orbitalboy_mcp_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME orbitalboy_mcp_tests COMMAND orbitalboy_mcp_tests)
endif()
