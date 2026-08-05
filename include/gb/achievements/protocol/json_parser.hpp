#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gb/achievements/protocol/json_value.hpp"

namespace gb::achievements::protocol {

inline constexpr std::size_t kJsonDefaultMaximumInputBytes = 4U * 1024U * 1024U;
inline constexpr std::size_t kJsonDefaultMaximumNestingDepth = 64U;
inline constexpr std::size_t kJsonDefaultMaximumTotalValues = 100000U;
inline constexpr std::size_t kJsonDefaultMaximumDecodedStringBytes = 1U * 1024U * 1024U;

struct JsonParseOptions {
    std::size_t maximumInputBytes = kJsonDefaultMaximumInputBytes;
    std::size_t maximumNestingDepth = kJsonDefaultMaximumNestingDepth;
    std::size_t maximumTotalValues = kJsonDefaultMaximumTotalValues;
    std::size_t maximumDecodedStringBytes = kJsonDefaultMaximumDecodedStringBytes;
};

enum class JsonErrorCategory {
    None,
    InvalidOptions,
    InputTooLarge,
    Syntax,
    InvalidEscape,
    InvalidSurrogate,
    InvalidUtf8,
    DepthLimit,
    ValueLimit,
    StringLimit,
    OutOfMemory,
};

struct JsonParseError {
    JsonErrorCategory category = JsonErrorCategory::None;
    std::size_t byteOffset = 0U;
};

class JsonParseResult {
public:
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const JsonValue& value() const noexcept;
    [[nodiscard]] const JsonParseError& error() const noexcept;

    [[nodiscard]] static JsonParseResult success(JsonValue value) noexcept;
    [[nodiscard]] static JsonParseResult failure(JsonErrorCategory category, std::size_t byteOffset) noexcept;

private:
    JsonValue value_;
    JsonParseError error_;
};

class JsonParser {
public:
    [[nodiscard]] static JsonParseResult parse(std::string_view input,
                                               const JsonParseOptions& options = {} ) noexcept;
    [[nodiscard]] static JsonParseResult parse(const std::uint8_t* input, std::size_t size,
                                               const JsonParseOptions& options = {}) noexcept;
};

} // namespace gb::achievements::protocol
