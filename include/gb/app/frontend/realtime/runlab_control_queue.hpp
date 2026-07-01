#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gb::frontend {

enum class RunLabControlCommandType {
    Hold,
    Tap,
    ReleaseAll,
    AdvanceFrames,
    Pause,
    Resume,
    StepFrame,
    Annotation,
};

struct RunLabVisualAnnotation {
    std::string label{};
    std::string type{};
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int frames = 180;
};

struct RunLabControlCommand {
    RunLabControlCommandType type = RunLabControlCommandType::Hold;
    std::uint8_t buttonMask = 0;
    int frames = 1;
    RunLabVisualAnnotation annotation{};
};

struct RunLabControlTick {
    std::uint8_t buttonMask = 0;
    int stepFrames = 0;
    bool active = false;
    bool consumedCommand = false;
    bool requestPause = false;
    bool requestResume = false;
    std::string message{};
    std::string commandName{};
};

std::uint8_t runLabControlButtonMaskFromName(const std::string& raw);
std::optional<RunLabControlCommand> parseRunLabControlCommandLine(const std::string& line, std::string* error = nullptr);
std::string makeRunLabControlCommandJson(
    const std::string& command,
    const std::vector<std::string>& buttons,
    int frames
);

class RunLabControlQueue {
public:
    void configure(const std::filesystem::path& path, bool enabled);
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] std::size_t pendingCount() const;
    [[nodiscard]] const std::string& lastMessage() const;
    [[nodiscard]] std::string currentCommandName() const;
    [[nodiscard]] int remainingFrames() const;
    [[nodiscard]] bool clientRecentlySeen() const;
    [[nodiscard]] int framesSinceClientHeartbeat() const;
    [[nodiscard]] std::vector<RunLabVisualAnnotation> annotations() const;

    void poll();
    RunLabControlTick tick();
    void clear();

private:
    void seekToEnd();
    void enqueue(const RunLabControlCommand& command);

    std::filesystem::path path_{};
    bool enabled_ = false;
    bool initialized_ = false;
    std::uintmax_t offset_ = 0;
    std::deque<RunLabControlCommand> pending_{};
    std::uint8_t activeMask_ = 0;
    int remainingFrames_ = 0;
    int framesSinceClientHeartbeat_ = 1000000;
    std::vector<RunLabVisualAnnotation> annotations_{};
    std::string lastMessage_{};
};

} // namespace gb::frontend
