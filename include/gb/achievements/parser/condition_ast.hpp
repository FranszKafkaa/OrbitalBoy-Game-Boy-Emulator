#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gb::achievements::parser {

struct SourceSpan {
    std::size_t begin = 0U;
    std::size_t end = 0U;
};

enum class ConditionFlag : std::uint8_t {
    None,
    PauseIf,
    ResetIf,
    ResetNextIf,
    AddSource,
    SubSource,
    AddHits,
    SubHits,
    AddAddress,
    AndNext,
    OrNext,
    Measured,
    MeasuredPercent,
    MeasuredIf,
    Trigger,
    Remember,
};

enum class OperandKind : std::uint8_t {
    Constant,
    Memory,
    Recall,
};

enum class OperandModifier : std::uint8_t {
    None,
    Delta,
    Prior,
    Bcd,
    Invert,
};

enum class MemorySize : std::uint8_t {
    Bit0,
    Bit1,
    Bit2,
    Bit3,
    Bit4,
    Bit5,
    Bit6,
    Bit7,
    LowerNibble,
    UpperNibble,
    EightBit,
    SixteenBit,
    TwentyFourBit,
    ThirtyTwoBit,
    SixteenBitBigEndian,
    TwentyFourBitBigEndian,
    ThirtyTwoBitBigEndian,
    BitCount,
    Float,
    FloatBigEndian,
    Double32,
    Double32BigEndian,
    Mbf32,
    Mbf32LittleEndian,
};

enum class Operator : std::uint8_t {
    None,
    Equal,
    NotEqual,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    Multiply,
    Divide,
    Add,
    Subtract,
    Modulo,
    BitwiseAnd,
    BitwiseXor,
};

struct MemoryOperand {
    MemorySize size = MemorySize::SixteenBit;
    std::uint32_t address = 0U;
    SourceSpan sizeSpan{};
    SourceSpan addressSpan{};
};

struct Operand {
    OperandKind kind = OperandKind::Constant;
    OperandModifier modifier = OperandModifier::None;
    std::uint32_t constant = 0U;
    MemoryOperand memory{};
    SourceSpan span{};
    std::optional<SourceSpan> modifierSpan{};
};

struct Condition {
    ConditionFlag flag = ConditionFlag::None;
    Operand left{};
    Operator op = Operator::None;
    std::optional<Operand> right{};
    std::optional<std::uint32_t> hitTarget{};
    SourceSpan span{};
    std::optional<SourceSpan> flagSpan{};
    std::optional<SourceSpan> operatorSpan{};
    std::optional<SourceSpan> hitTargetSpan{};
};

struct ConditionGroup {
    std::vector<Condition> conditions{};
    SourceSpan span{};
};

struct ConditionTrigger {
    std::string source{};
    ConditionGroup core{};
    std::vector<ConditionGroup> alt{};
};

} // namespace gb::achievements::parser
