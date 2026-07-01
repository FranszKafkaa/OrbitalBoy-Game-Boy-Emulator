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

bool appendTextFile(const std::filesystem::path& path, const std::string& text, std::string* error) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            setError(error, "unable to create directory: " + parent.string());
            return false;
        }
    }
    std::ofstream out(path, std::ios::app);
    if (!out) {
        setError(error, "unable to append file: " + path.string());
        return false;
    }
    out << text;
    if (!out) {
        setError(error, "unable to write file: " + path.string());
        return false;
    }
    return true;
}

ServerConfig parseConfig(int argc, char** argv) {
    ServerConfig cfg;
    if (const char* env = std::getenv("ORBITALBOY_RUNLAB_STATE_PATH")) {
        cfg.statePath = env;
    }
    if (const char* env = std::getenv("ORBITALBOY_RUNLAB_CONTROL_QUEUE_PATH")) {
        cfg.controlQueuePath = env;
    }
    if (const char* env = std::getenv("ORBITALBOY_RUNLAB_PROMPT_QUEUE_PATH")) {
        cfg.promptQueuePath = env;
    }
    if (const char* env = std::getenv("ORBITALBOY_RUNLAB_FEEDBACK_QUEUE_PATH")) {
        cfg.feedbackQueuePath = env;
    }
    if (const char* env = std::getenv("ORBITALBOY_RUNLAB_PROFILE_PATH")) {
        cfg.profilePath = std::filesystem::path(env);
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--state" && i + 1 < argc) {
            cfg.statePath = argv[++i];
        } else if (arg == "--control-queue" && i + 1 < argc) {
            cfg.controlQueuePath = argv[++i];
        } else if (arg == "--prompt-queue" && i + 1 < argc) {
            cfg.promptQueuePath = argv[++i];
        } else if (arg == "--feedback-queue" && i + 1 < argc) {
            cfg.feedbackQueuePath = argv[++i];
        } else if (arg == "--no-auto-prompt-runner") {
            cfg.autoRunPrompts = false;
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
