#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace gb {

inline std::optional<std::string> readEnvironmentVariable(const char* name) {
    if (name == nullptr || *name == '\0') {
        return std::nullopt;
    }

#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) {
        return std::nullopt;
    }
    std::unique_ptr<char, decltype(&std::free)> value(raw, &std::free);
    return std::string(value.get());
#else
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

inline bool hasEnvironmentVariable(const char* name) {
    return readEnvironmentVariable(name).has_value();
}

inline bool environmentVariableEnabled(const char* name) {
    const auto value = readEnvironmentVariable(name);
    if (!value.has_value()) {
        return false;
    }
    return value->empty() || value->front() != '0';
}

} // namespace gb
