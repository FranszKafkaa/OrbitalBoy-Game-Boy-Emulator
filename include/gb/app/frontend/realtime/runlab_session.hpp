#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace gb::frontend {

class RunLabSession {
public:
    using Clock = std::chrono::steady_clock;

    explicit RunLabSession(
        std::string statePath,
        Clock::time_point lastExportTime = Clock::now() - std::chrono::seconds(1)
    );

    [[nodiscard]] const std::string& statePath() const;
    [[nodiscard]] const std::filesystem::path& directory() const;
    [[nodiscard]] const std::string& screenshotPath() const;
    [[nodiscard]] const std::string& promptQueuePath() const;
    [[nodiscard]] const std::string& feedbackQueuePath() const;

    bool submitPrompt(const std::string& prompt, std::uint64_t id, std::uint64_t frame) const;
    std::optional<std::string> pollFeedback();

    [[nodiscard]] bool exportDue(
        std::uint64_t frame,
        Clock::time_point now = Clock::now()
    ) const;
    void markExported(std::uint64_t frame, Clock::time_point now = Clock::now());

private:
    std::string statePath_;
    std::filesystem::path directory_;
    std::string screenshotPath_;
    std::string promptQueuePath_;
    std::string feedbackQueuePath_;
    std::uintmax_t feedbackOffset_ = 0;
    bool feedbackInitialized_ = false;
    std::uint64_t lastExportFrame_ = UINT64_MAX;
    Clock::time_point lastExportTime_;
};

} // namespace gb::frontend
