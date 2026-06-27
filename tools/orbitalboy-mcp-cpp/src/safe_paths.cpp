#include "safe_paths.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace orbitalboy::mcp {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::filesystem::path absoluteLexical(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}

bool isUnder(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto c = absoluteLexical(child);
    const auto p = absoluteLexical(parent);
    auto cit = c.begin();
    auto pit = p.begin();
    for (; pit != p.end(); ++pit, ++cit) {
        if (cit == c.end() || *cit != *pit) {
            return false;
        }
    }
    return true;
}

} // namespace

bool pathHasTraversal(const std::filesystem::path& path) {
    for (const auto& part : path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> safeConfiguredPath(const std::filesystem::path& path, std::string* error) {
    if (path.empty()) {
        setError(error, "empty path");
        return std::nullopt;
    }
    if (pathHasTraversal(path)) {
        setError(error, "path traversal is not allowed");
        return std::nullopt;
    }
    return absoluteLexical(path);
}

std::string readTextFile(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        setError(error, "unable to read file: " + path.string());
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ServerConfig parseConfig(int argc, char** argv) {
    ServerConfig cfg;
    if (const char* env = std::getenv("ORBITALBOY_RUNLAB_STATE_PATH")) {
        cfg.statePath = env;
    }
    if (const char* env = std::getenv("ORBITALBOY_RUNLAB_PROFILE_PATH")) {
        cfg.profilePath = std::filesystem::path(env);
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--state" && i + 1 < argc) {
            cfg.statePath = argv[++i];
        } else if (arg == "--profile" && i + 1 < argc) {
            cfg.profilePath = std::filesystem::path(argv[++i]);
        }
    }
    return cfg;
}

std::optional<std::filesystem::path> safeScreenshotPath(
    const std::filesystem::path& path,
    const ServerConfig& config,
    std::string* error
) {
    if (path.empty() || pathHasTraversal(path)) {
        setError(error, "unsafe screenshot path");
        return std::nullopt;
    }
    const auto absolute = absoluteLexical(path);
    const auto stateDir = absoluteLexical(config.statePath).parent_path();
    if (isUnder(absolute, stateDir)) {
        return absolute;
    }
    if (config.profilePath.has_value() && isUnder(absolute, absoluteLexical(config.profilePath.value()).parent_path())) {
        return absolute;
    }
    setError(error, "screenshot path is outside allowed directories");
    return std::nullopt;
}

} // namespace orbitalboy::mcp
