#pragma once

#include "json_rpc.h"
#include "safe_paths.h"

#include <optional>
#include <string>

namespace orbitalboy::mcp {

class RunLabState {
public:
    static RunLabState load(const ServerConfig& config);

    [[nodiscard]] bool ok() const;
    [[nodiscard]] const std::string& error() const;
    [[nodiscard]] const Json& root() const;
    [[nodiscard]] Json status() const;
    [[nodiscard]] Json entities(const std::string& typeFilter = {}) const;
    [[nodiscard]] Json memoryLabels(const std::string& entityFilter = {}) const;
    [[nodiscard]] Json events(int limit = 20, const std::string& typeFilter = {}) const;
    [[nodiscard]] Json activeGoal() const;
    [[nodiscard]] Json splits() const;
    [[nodiscard]] Json analysisSummary() const;
    [[nodiscard]] std::optional<std::string> profileJson(const ServerConfig& config, bool includeEvents) const;

private:
    Json root_;
    std::string error_;
};

std::string jsonText(const Json& value);
std::string statusText(const Json& status);

} // namespace orbitalboy::mcp
