#include "gb/app/frontend/realtime/runlab_session.hpp"

#include <fstream>

namespace gb::frontend {

namespace {

std::string escapeJson(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::optional<std::string> jsonString(const std::string& line, const std::string& key) {
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
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return out;
        } else {
            out.push_back(ch);
        }
    }
    return std::nullopt;
}

} // namespace

RunLabSession::RunLabSession(std::string statePath, Clock::time_point lastExportTime)
    : statePath_(statePath.empty() ? ".runlab/current-state.json" : std::move(statePath)),
      lastExportTime_(lastExportTime) {
    const std::filesystem::path stateFile(statePath_);
    directory_ = stateFile.has_parent_path() ? stateFile.parent_path() : std::filesystem::path(".runlab");
    screenshotPath_ = (directory_ / "current-screen.ppm").string();
    promptQueuePath_ = (directory_ / "prompts.jsonl").string();
    feedbackQueuePath_ = (directory_ / "feedback.jsonl").string();
}

const std::string& RunLabSession::statePath() const {
    return statePath_;
}

const std::filesystem::path& RunLabSession::directory() const {
    return directory_;
}

const std::string& RunLabSession::screenshotPath() const {
    return screenshotPath_;
}

const std::string& RunLabSession::promptQueuePath() const {
    return promptQueuePath_;
}

const std::string& RunLabSession::feedbackQueuePath() const {
    return feedbackQueuePath_;
}

bool RunLabSession::submitPrompt(
    const std::string& prompt,
    std::uint64_t id,
    std::uint64_t frame
) const {
    if (prompt.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    std::ofstream out(promptQueuePath_, std::ios::app);
    if (!out) {
        return false;
    }
    out << "{\"id\":" << id
        << ",\"frame\":" << frame
        << ",\"status\":\"pending\""
        << ",\"prompt\":\"" << escapeJson(prompt) << "\"}\n";
    return static_cast<bool>(out);
}

std::optional<std::string> RunLabSession::pollFeedback() {
    std::error_code ec;
    const std::filesystem::path path(feedbackQueuePath_);
    const auto size = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0;
    if (ec) return std::nullopt;
    if (!feedbackInitialized_) {
        feedbackInitialized_ = true;
        feedbackOffset_ = size;
        return std::nullopt;
    }
    if (size < feedbackOffset_) {
        feedbackOffset_ = 0;
    }
    if (size == feedbackOffset_) {
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(static_cast<std::streamoff>(feedbackOffset_));
    std::string line;
    std::string latest;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            latest = jsonString(line, "message").value_or("");
        }
    }
    feedbackOffset_ = static_cast<std::uintmax_t>(
        in.tellg() >= 0 ? in.tellg() : static_cast<std::streampos>(size)
    );
    if (latest.empty()) {
        return std::nullopt;
    }
    return latest;
}

bool RunLabSession::exportDue(std::uint64_t frame, Clock::time_point now) const {
    const bool frameDue = frame != lastExportFrame_ && (frame % 15u) == 0u;
    const bool timeDue = now - lastExportTime_ >= std::chrono::milliseconds(250);
    return frameDue || timeDue;
}

void RunLabSession::markExported(std::uint64_t frame, Clock::time_point now) {
    lastExportFrame_ = frame;
    lastExportTime_ = now;
}

} // namespace gb::frontend
