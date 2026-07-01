#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace orbitalboy::mcp {

struct ServerConfig {
    std::filesystem::path statePath = ".runlab/current-state.json";
    std::filesystem::path controlQueuePath = ".runlab/commands.jsonl";
    std::filesystem::path promptQueuePath = ".runlab/prompts.jsonl";
    std::filesystem::path feedbackQueuePath = ".runlab/feedback.jsonl";
    bool autoRunPrompts = true;
    std::optional<std::filesystem::path> profilePath;
};

std::optional<std::filesystem::path> safeConfiguredPath(const std::filesystem::path& path, std::string* error = nullptr);
bool pathHasTraversal(const std::filesystem::path& path);
std::string readTextFile(const std::filesystem::path& path, std::string* error = nullptr);
bool appendTextFile(const std::filesystem::path& path, const std::string& text, std::string* error = nullptr);
ServerConfig parseConfig(int argc, char** argv);
std::optional<std::filesystem::path> safeScreenshotPath(
    const std::filesystem::path& path,
    const ServerConfig& config,
    std::string* error = nullptr
);

} // namespace orbitalboy::mcp
