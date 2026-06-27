#include "safe_paths.h"

#include <cassert>
#include <filesystem>
#include <fstream>

void runSafePathTests() {
    std::string error;
    const auto normal = orbitalboy::mcp::safeConfiguredPath("state.json", &error);
    assert(normal.has_value());
    assert(error.empty());

    error.clear();
    const auto traversal = orbitalboy::mcp::safeConfiguredPath("../secret.json", &error);
    assert(!traversal.has_value());
    assert(!error.empty());

    orbitalboy::mcp::ServerConfig cfg;
    cfg.statePath = std::filesystem::temp_directory_path() / "orbitalboy_mcp_tests" / "state.json";
    error.clear();
    const auto screenshot = orbitalboy::mcp::safeScreenshotPath(
        cfg.statePath.parent_path() / "screen.png",
        cfg,
        &error
    );
    assert(screenshot.has_value());

    error.clear();
    const auto outside = orbitalboy::mcp::safeScreenshotPath("/tmp/outside.png", cfg, &error);
    assert(!outside.has_value());
}
