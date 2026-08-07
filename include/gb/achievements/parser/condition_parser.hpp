#pragma once

#include <cstddef>
#include <optional>

#include "gb/achievements/parser/condition_ast.hpp"

namespace gb::achievements::parser {

constexpr std::size_t kConditionMaximumInputBytes = 1024U * 1024U;
constexpr std::size_t kConditionMaximumGroups = 256U;
constexpr std::size_t kConditionMaximumConditions = 100000U;

enum class ConditionParseErrorCategory : unsigned char {
    EmptyInput,
    InputTooLarge,
    InvalidOptions,
    EmptyGroup,
    EmptyCondition,
    UnexpectedSeparator,
    Whitespace,
    UnknownFlag,
    InvalidOperand,
    InvalidMemorySize,
    InvalidAddress,
    InvalidConstant,
    MissingOperator,
    InvalidOperator,
    MissingOperand,
    InvalidHitTarget,
    NumericOverflow,
    InvalidFlagOperator,
    GroupLimit,
    ConditionLimit,
};

struct ConditionParseError {
    ConditionParseErrorCategory category = ConditionParseErrorCategory::EmptyInput;
    std::size_t byteOffset = 0U;
};

struct ConditionParseOptions {
    std::size_t maximumInputBytes = kConditionMaximumInputBytes;
    std::size_t maximumGroups = kConditionMaximumGroups;
    std::size_t maximumConditions = kConditionMaximumConditions;
};

class ConditionParseResult {
public:
    static ConditionParseResult success(ConditionTrigger trigger);
    static ConditionParseResult failure(ConditionParseErrorCategory category, std::size_t byteOffset);

    bool ok() const noexcept;
    const ConditionTrigger& value() const;
    const ConditionParseError& error() const;

private:
    std::optional<ConditionTrigger> value_{};
    ConditionParseError error_{};
};

class ConditionParser {
public:
    static ConditionParseResult parse(const std::string& input, const ConditionParseOptions& options = {});
};

} // namespace gb::achievements::parser
