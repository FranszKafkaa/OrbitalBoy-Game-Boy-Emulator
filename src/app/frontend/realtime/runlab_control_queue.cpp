#include "gb/app/frontend/realtime/runlab_control_queue.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace gb::frontend {

namespace {

constexpr int kMaxControlFrames = 600;
constexpr std::size_t kMaxPendingCommands = 256;
constexpr std::size_t kMaxAnnotations = 32;

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string normalizeName(std::string text) {
    text = toLowerAscii(std::move(text));
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
        return ch == '_' || ch == '-' || std::isspace(ch) != 0;
    }), text.end());
    return text;
}

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::optional<std::string> findJsonString(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = line.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos = line.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::nullopt;
    pos = line.find('"', pos + 1);
    if (pos == std::string::npos) return std::nullopt;
    std::string out;
    bool escaped = false;
    for (++pos; pos < line.size(); ++pos) {
        const char ch = line[pos];
        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') return out;
        out.push_back(ch);
    }
    return std::nullopt;
}

int findJsonInt(const std::string& line, const std::string& key, int fallback) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = line.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = line.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
    const std::size_t start = pos;
    if (pos < line.size() && line[pos] == '-') ++pos;
    while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) ++pos;
    if (pos == start) return fallback;
    try {
        return std::stoi(line.substr(start, pos - start));
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> findJsonStringArray(const std::string& line, const std::string& key) {
    std::vector<std::string> out;
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = line.find(needle);
    if (pos == std::string::npos) return out;
    pos = line.find('[', pos + needle.size());
    if (pos == std::string::npos) return out;
    ++pos;
    while (pos < line.size()) {
        while (pos < line.size() && (std::isspace(static_cast<unsigned char>(line[pos])) || line[pos] == ',')) ++pos;
        if (pos >= line.size() || line[pos] == ']') break;
        if (line[pos] != '"') break;
        std::string value;
        bool escaped = false;
        for (++pos; pos < line.size(); ++pos) {
            const char ch = line[pos];
            if (escaped) {
                value.push_back(ch);
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                ++pos;
                out.push_back(value);
                break;
            }
            value.push_back(ch);
        }
    }
    return out;
}

std::string escapeJson(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else out.push_back(ch);
    }
    return out;
}

} // namespace

std::uint8_t runLabControlButtonMaskFromName(const std::string& raw) {
    const std::string name = normalizeName(raw);
    if (name == "right" || name == "r") return static_cast<std::uint8_t>(1u << 0);
    if (name == "left" || name == "l") return static_cast<std::uint8_t>(1u << 1);
    if (name == "up" || name == "u") return static_cast<std::uint8_t>(1u << 2);
    if (name == "down" || name == "d") return static_cast<std::uint8_t>(1u << 3);
    if (name == "a") return static_cast<std::uint8_t>(1u << 4);
    if (name == "b") return static_cast<std::uint8_t>(1u << 5);
    if (name == "select" || name == "sel") return static_cast<std::uint8_t>(1u << 6);
    if (name == "start" || name == "st") return static_cast<std::uint8_t>(1u << 7);
    return 0;
}

std::optional<RunLabControlCommand> parseRunLabControlCommandLine(const std::string& line, std::string* error) {
    const auto commandText = findJsonString(line, "command");
    if (!commandText.has_value()) {
        setError(error, "missing command");
        return std::nullopt;
    }

    RunLabControlCommand command{};
    const std::string type = normalizeName(commandText.value());
    if (type == "heartbeat" || type == "clientheartbeat") {
        command.type = RunLabControlCommandType::ReleaseAll;
        command.frames = 0;
        command.buttonMask = 0;
        return command;
    }
    if (type == "annotation" || type == "annotate") {
        command.type = RunLabControlCommandType::Annotation;
        command.frames = std::clamp(findJsonInt(line, "frames", 180), 1, kMaxControlFrames);
        command.buttonMask = 0;
        command.annotation.label = findJsonString(line, "label").value_or("OBJECT");
        command.annotation.type = findJsonString(line, "type").value_or("object");
        command.annotation.x = std::clamp(findJsonInt(line, "x", 0), 0, 159);
        command.annotation.y = std::clamp(findJsonInt(line, "y", 0), 0, 143);
        command.annotation.w = std::clamp(findJsonInt(line, "w", 8), 1, 160);
        command.annotation.h = std::clamp(findJsonInt(line, "h", 8), 1, 144);
        command.annotation.frames = command.frames;
        return command;
    }
    if (type == "hold") {
        command.type = RunLabControlCommandType::Hold;
    } else if (type == "tap") {
        command.type = RunLabControlCommandType::Tap;
    } else if (type == "releaseall" || type == "release") {
        command.type = RunLabControlCommandType::ReleaseAll;
    } else if (type == "advanceframes" || type == "advance") {
        command.type = RunLabControlCommandType::AdvanceFrames;
    } else if (type == "pause") {
        command.type = RunLabControlCommandType::Pause;
    } else if (type == "resume") {
        command.type = RunLabControlCommandType::Resume;
    } else if (type == "stepframe" || type == "stepframes" || type == "step") {
        command.type = RunLabControlCommandType::StepFrame;
    } else {
        setError(error, "unknown command");
        return std::nullopt;
    }

    command.frames = std::clamp(findJsonInt(line, "frames", command.type == RunLabControlCommandType::Tap ? 6 : 1), 1, kMaxControlFrames);
    if (command.type == RunLabControlCommandType::ReleaseAll
        || command.type == RunLabControlCommandType::Pause
        || command.type == RunLabControlCommandType::Resume
        || command.type == RunLabControlCommandType::StepFrame) {
        if (command.type != RunLabControlCommandType::StepFrame) {
            command.frames = 1;
        }
        command.buttonMask = 0;
        return command;
    }

    std::uint8_t mask = 0;
    for (const auto& button : findJsonStringArray(line, "buttons")) {
        const std::uint8_t bit = runLabControlButtonMaskFromName(button);
        if (bit == 0) {
            setError(error, "unknown button: " + button);
            return std::nullopt;
        }
        mask = static_cast<std::uint8_t>(mask | bit);
    }
    if (command.type != RunLabControlCommandType::AdvanceFrames && mask == 0) {
        setError(error, "missing buttons");
        return std::nullopt;
    }
    command.buttonMask = command.type == RunLabControlCommandType::AdvanceFrames ? 0 : mask;
    return command;
}

std::string makeRunLabControlCommandJson(
    const std::string& command,
    const std::vector<std::string>& buttons,
    int frames
) {
    std::ostringstream out;
    out << "{\"command\":\"" << escapeJson(command) << "\",\"buttons\":[";
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (i != 0) out << ',';
        out << '"' << escapeJson(buttons[i]) << '"';
    }
    out << "],\"frames\":" << std::clamp(frames, 1, kMaxControlFrames) << "}";
    return out.str();
}

void RunLabControlQueue::configure(const std::filesystem::path& path, bool enabled) {
    path_ = path;
    enabled_ = enabled && !path.empty();
    initialized_ = false;
    offset_ = 0;
    pending_.clear();
    activeMask_ = 0;
    remainingFrames_ = 0;
    framesSinceClientHeartbeat_ = 1000000;
    annotations_.clear();
    lastMessage_.clear();
}

bool RunLabControlQueue::enabled() const { return enabled_; }
const std::filesystem::path& RunLabControlQueue::path() const { return path_; }
std::size_t RunLabControlQueue::pendingCount() const { return pending_.size(); }
const std::string& RunLabControlQueue::lastMessage() const { return lastMessage_; }

std::string RunLabControlQueue::currentCommandName() const {
    if (remainingFrames_ > 0) {
        return activeMask_ == 0 ? "advance_frames" : "input";
    }
    if (pending_.empty()) {
        return {};
    }
    switch (pending_.front().type) {
    case RunLabControlCommandType::Hold: return "hold";
    case RunLabControlCommandType::Tap: return "tap";
    case RunLabControlCommandType::ReleaseAll: return "release_all";
    case RunLabControlCommandType::AdvanceFrames: return "advance_frames";
    case RunLabControlCommandType::Pause: return "pause";
    case RunLabControlCommandType::Resume: return "resume";
    case RunLabControlCommandType::StepFrame: return "step_frame";
    case RunLabControlCommandType::Annotation: return "annotation";
    }
    return {};
}

int RunLabControlQueue::remainingFrames() const {
    return remainingFrames_;
}

bool RunLabControlQueue::clientRecentlySeen() const {
    return framesSinceClientHeartbeat_ <= 240;
}

int RunLabControlQueue::framesSinceClientHeartbeat() const {
    return framesSinceClientHeartbeat_;
}

std::vector<RunLabVisualAnnotation> RunLabControlQueue::annotations() const {
    return annotations_;
}

void RunLabControlQueue::seekToEnd() {
    initialized_ = true;
    offset_ = 0;
    std::ifstream in(path_, std::ios::binary | std::ios::ate);
    if (in) {
        offset_ = static_cast<std::uintmax_t>(in.tellg());
    }
}

void RunLabControlQueue::enqueue(const RunLabControlCommand& command) {
    if (pending_.size() >= kMaxPendingCommands) {
        lastMessage_ = "RUNLAB MCP QUEUE FULL";
        return;
    }
    pending_.push_back(command);
}

void RunLabControlQueue::poll() {
    if (!enabled_) return;
    if (!initialized_) {
        seekToEnd();
        return;
    }
    std::error_code ec;
    const auto size = std::filesystem::exists(path_, ec) ? std::filesystem::file_size(path_, ec) : 0;
    if (ec) return;
    if (size < offset_) {
        offset_ = 0;
    }
    if (size == offset_) return;

    std::ifstream in(path_, std::ios::binary);
    if (!in) return;
    in.seekg(static_cast<std::streamoff>(offset_));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string error;
        const std::string commandText = findJsonString(line, "command").value_or("");
        const std::string normalized = normalizeName(commandText);
        if (normalized == "heartbeat" || normalized == "clientheartbeat") {
            framesSinceClientHeartbeat_ = 0;
            lastMessage_ = "RUNLAB MCP CLIENT";
            continue;
        }
        auto command = parseRunLabControlCommandLine(line, &error);
        if (command.has_value()) {
            enqueue(command.value());
            lastMessage_ = "RUNLAB MCP CMD";
        } else {
            lastMessage_ = "RUNLAB MCP BAD CMD";
        }
    }
    offset_ = static_cast<std::uintmax_t>(in.tellg() >= 0 ? in.tellg() : static_cast<std::streampos>(size));
}

RunLabControlTick RunLabControlQueue::tick() {
    RunLabControlTick out{};
    if (!enabled_) return out;
    if (framesSinceClientHeartbeat_ < 1000000) {
        ++framesSinceClientHeartbeat_;
    }
    for (auto& annotation : annotations_) {
        --annotation.frames;
    }
    annotations_.erase(
        std::remove_if(annotations_.begin(), annotations_.end(), [](const RunLabVisualAnnotation& annotation) {
            return annotation.frames <= 0;
        }),
        annotations_.end()
    );

    if (remainingFrames_ <= 0 && !pending_.empty()) {
        const RunLabControlCommand command = pending_.front();
        pending_.pop_front();
        out.consumedCommand = true;
        switch (command.type) {
        case RunLabControlCommandType::Hold:
        case RunLabControlCommandType::Tap:
            activeMask_ = command.buttonMask;
            remainingFrames_ = command.frames;
            lastMessage_ = "RUNLAB MCP INPUT";
            break;
        case RunLabControlCommandType::AdvanceFrames:
            activeMask_ = 0;
            remainingFrames_ = command.frames;
            lastMessage_ = "RUNLAB MCP ADVANCE";
            break;
        case RunLabControlCommandType::Pause:
            activeMask_ = 0;
            remainingFrames_ = 0;
            lastMessage_ = "RUNLAB MCP PAUSE";
            out.requestPause = true;
            out.message = lastMessage_;
            out.commandName = "pause";
            return out;
        case RunLabControlCommandType::Resume:
            activeMask_ = 0;
            remainingFrames_ = 0;
            lastMessage_ = "RUNLAB MCP RESUME";
            out.requestResume = true;
            out.message = lastMessage_;
            out.commandName = "resume";
            return out;
        case RunLabControlCommandType::StepFrame:
            activeMask_ = 0;
            remainingFrames_ = 0;
            lastMessage_ = "RUNLAB MCP STEP";
            out.stepFrames = command.frames;
            out.message = lastMessage_;
            out.commandName = "step_frame";
            return out;
        case RunLabControlCommandType::ReleaseAll:
            activeMask_ = 0;
            remainingFrames_ = 0;
            lastMessage_ = "RUNLAB MCP RELEASE";
            out.message = lastMessage_;
            out.commandName = "release_all";
            return out;
        case RunLabControlCommandType::Annotation:
            if (annotations_.size() >= kMaxAnnotations) {
                annotations_.erase(annotations_.begin());
            }
            annotations_.push_back(command.annotation);
            lastMessage_ = "RUNLAB MCP ANNOTATE";
            out.message = lastMessage_;
            out.commandName = "annotation";
            return out;
        }
    }

    if (remainingFrames_ > 0) {
        out.active = true;
        out.buttonMask = activeMask_;
        --remainingFrames_;
        if (remainingFrames_ == 0) {
            activeMask_ = 0;
        }
    }
    out.message = lastMessage_;
    out.commandName = currentCommandName();
    return out;
}

void RunLabControlQueue::clear() {
    pending_.clear();
    activeMask_ = 0;
    remainingFrames_ = 0;
    annotations_.clear();
    lastMessage_ = "RUNLAB MCP CLEAR";
}

} // namespace gb::frontend
