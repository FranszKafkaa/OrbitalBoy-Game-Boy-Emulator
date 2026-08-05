#include "gb/achievements/parser/condition_parser.hpp"

#include "../../test_framework.hpp"

namespace {

using gb::achievements::parser::ConditionParser;
using gb::achievements::parser::ConditionFlag;
using gb::achievements::parser::ConditionParseErrorCategory;
using gb::achievements::parser::ConditionParseOptions;
using gb::achievements::parser::MemorySize;
using gb::achievements::parser::OperandKind;
using gb::achievements::parser::OperandModifier;
using gb::achievements::parser::Operator;

gb::achievements::parser::Condition onlyCondition(const std::string& input) {
    const auto result = ConditionParser::parse(input);
    T_REQUIRE(result.ok());
    T_EQ(result.value().core.conditions.size(), 1U);
    return result.value().core.conditions.at(0U);
}

void requireError(const std::string& input, ConditionParseErrorCategory category, std::size_t offset) {
    const auto result = ConditionParser::parse(input);
    T_REQUIRE(!result.ok());
    T_REQUIRE(result.error().category == category);
    T_EQ(result.error().byteOffset, offset);
}

TEST_CASE("achievements_condition_parser", "parses_a_documented_memory_comparison_without_losing_lexical_structure") {
    const auto result = ConditionParser::parse("0xH1234=1");

    T_REQUIRE(result.ok());
    const auto& condition = result.value().core.conditions.at(0U);
    T_REQUIRE(condition.left.kind == OperandKind::Memory);
    T_REQUIRE(condition.left.memory.size == MemorySize::EightBit);
    T_EQ(condition.left.memory.address, 0x1234U);
    T_EQ(condition.left.span.begin, 0U);
    T_EQ(condition.left.span.end, 7U);
    T_EQ(condition.left.memory.sizeSpan.begin, 0U);
    T_EQ(condition.left.memory.sizeSpan.end, 3U);
    T_EQ(condition.left.memory.addressSpan.begin, 3U);
    T_EQ(condition.left.memory.addressSpan.end, 7U);
    T_REQUIRE(condition.op == Operator::Equal);
    T_REQUIRE(condition.operatorSpan.has_value());
    T_EQ(condition.operatorSpan->begin, 7U);
    T_EQ(condition.operatorSpan->end, 8U);
    T_REQUIRE(condition.right.has_value());
    T_REQUIRE(condition.right->kind == OperandKind::Constant);
    T_EQ(condition.right->constant, 1U);
    T_EQ(condition.right->span.begin, 8U);
    T_EQ(condition.right->span.end, 9U);
    T_EQ(condition.span.begin, 0U);
    T_EQ(condition.span.end, 9U);
}

TEST_CASE("achievements_condition_parser", "preserves_every_documented_flag_and_value_operator") {
    struct Example { const char* input; ConditionFlag flag; Operator op; };
    const Example examples[] = {
        {"P:0xH1=1", ConditionFlag::PauseIf, Operator::Equal},
        {"R:0xH1=1", ConditionFlag::ResetIf, Operator::Equal},
        {"Z:0xH1=1", ConditionFlag::ResetNextIf, Operator::Equal},
        {"A:0xH1+1", ConditionFlag::AddSource, Operator::Add},
        {"B:0xH1-1", ConditionFlag::SubSource, Operator::Subtract},
        {"I:0xH1=1", ConditionFlag::AddAddress, Operator::Equal},
        {"C:0xH1=1", ConditionFlag::AddHits, Operator::Equal},
        {"D:0xH1=1", ConditionFlag::SubHits, Operator::Equal},
        {"I:0xH1+1", ConditionFlag::AddAddress, Operator::Add},
        {"N:0xH1=1", ConditionFlag::AndNext, Operator::Equal},
        {"O:0xH1=1", ConditionFlag::OrNext, Operator::Equal},
        {"M:0xH1=1", ConditionFlag::Measured, Operator::Equal},
        {"G:0xH1=1", ConditionFlag::MeasuredPercent, Operator::Equal},
        {"Q:0xH1=1", ConditionFlag::MeasuredIf, Operator::Equal},
        {"T:0xH1=1", ConditionFlag::Trigger, Operator::Equal},
        {"K:0xH1*2", ConditionFlag::Remember, Operator::Multiply},
    };

    for (const auto& example : examples) {
        const auto& condition = onlyCondition(example.input);
        T_REQUIRE(condition.flag == example.flag);
        T_REQUIRE(condition.op == example.op);
        T_REQUIRE(condition.flagSpan.has_value());
        T_EQ(condition.flagSpan->begin, 0U);
        T_EQ(condition.flagSpan->end, 1U);
    }

    for (const char* input : {"A:0xH1", "B:0xFF", "I:3", "K:0xH1"}) {
        const auto result = ConditionParser::parse(input);
        T_REQUIRE(result.ok());
        T_REQUIRE(result.value().core.conditions.at(0U).op == Operator::None);
    }
}

TEST_CASE("achievements_condition_parser", "preserves_operand_modifiers_recall_and_constants") {
    struct ModifierExample { const char* input; OperandModifier modifier; };
    const ModifierExample examples[] = {
        {"d0xH1=1", OperandModifier::Delta},
        {"p0xH1=1", OperandModifier::Prior},
        {"b0xH1=1", OperandModifier::Bcd},
        {"~0xH1=1", OperandModifier::Invert},
    };
    for (const auto& example : examples) {
        const auto& condition = onlyCondition(example.input);
        T_REQUIRE(condition.left.kind == OperandKind::Memory);
        T_REQUIRE(condition.left.modifier == example.modifier);
        T_REQUIRE(condition.left.modifierSpan.has_value());
        T_EQ(condition.left.modifierSpan->begin, 0U);
        T_EQ(condition.left.modifierSpan->end, 1U);
    }

    const auto& recall = onlyCondition("{recall}=0xFF");
    T_REQUIRE(recall.left.kind == OperandKind::Recall);
    T_REQUIRE(recall.right.has_value());
    T_REQUIRE(recall.right->kind == OperandKind::Constant);
    T_EQ(recall.right->constant, 255U);

    const auto& decimal = onlyCondition("15=2");
    T_EQ(decimal.left.constant, 15U);
    T_REQUIRE(decimal.right.has_value());
    T_EQ(decimal.right->constant, 2U);
}

TEST_CASE("achievements_condition_parser", "preserves_every_documented_memory_size") {
    struct SizeExample { const char* input; MemorySize size; };
    const SizeExample examples[] = {
        {"0xM1=1", MemorySize::Bit0}, {"0xN1=1", MemorySize::Bit1},
        {"0xO1=1", MemorySize::Bit2}, {"0xP1=1", MemorySize::Bit3},
        {"0xQ1=1", MemorySize::Bit4}, {"0xR1=1", MemorySize::Bit5},
        {"0xS1=1", MemorySize::Bit6}, {"0xT1=1", MemorySize::Bit7},
        {"0xL1=1", MemorySize::LowerNibble}, {"0xU1=1", MemorySize::UpperNibble},
        {"0xH1=1", MemorySize::EightBit}, {"0x 1=1", MemorySize::SixteenBit},
        {"0xW1=1", MemorySize::TwentyFourBit}, {"0xX1=1", MemorySize::ThirtyTwoBit},
        {"0xI1=1", MemorySize::SixteenBitBigEndian}, {"0xJ1=1", MemorySize::TwentyFourBitBigEndian},
        {"0xG1=1", MemorySize::ThirtyTwoBitBigEndian}, {"0xK1=1", MemorySize::BitCount},
        {"fF1=1", MemorySize::Float}, {"fB1=1", MemorySize::FloatBigEndian},
        {"fH1=1", MemorySize::Double32}, {"fI1=1", MemorySize::Double32BigEndian},
        {"fM1=1", MemorySize::Mbf32}, {"fL1=1", MemorySize::Mbf32LittleEndian},
    };
    for (const auto& example : examples) {
        const auto& condition = onlyCondition(example.input);
        T_REQUIRE(condition.left.kind == OperandKind::Memory);
        T_REQUIRE(condition.left.memory.size == example.size);
        T_EQ(condition.left.memory.address, 1U);
    }
}

TEST_CASE("achievements_condition_parser", "parses_all_comparison_and_value_operators") {
    struct OperatorExample { const char* input; Operator op; };
    const OperatorExample examples[] = {
        {"1=1", Operator::Equal}, {"1!=1", Operator::NotEqual}, {"1<1", Operator::Less},
        {"1<=1", Operator::LessOrEqual}, {"1>1", Operator::Greater}, {"1>=1", Operator::GreaterOrEqual},
        {"K:1*1", Operator::Multiply}, {"K:1/1", Operator::Divide}, {"K:1+1", Operator::Add},
        {"K:1-1", Operator::Subtract}, {"K:1%1", Operator::Modulo}, {"K:1&1", Operator::BitwiseAnd},
        {"K:1^1", Operator::BitwiseXor},
    };
    for (const auto& example : examples) {
        T_REQUIRE(onlyCondition(example.input).op == example.op);
    }
}

TEST_CASE("achievements_condition_parser", "keeps_core_and_ordered_alt_groups_with_hit_targets") {
    const std::string input = "0xH1=1.0._0xH2=2S0xH3=3.4294967295.S0xH4=4";
    const auto result = ConditionParser::parse(input);

    T_REQUIRE(result.ok());
    T_EQ(result.value().source, input);
    T_EQ(result.value().core.conditions.size(), 2U);
    T_EQ(result.value().alt.size(), 2U);
    T_EQ(result.value().alt.at(0U).conditions.size(), 1U);
    T_EQ(result.value().alt.at(1U).conditions.size(), 1U);
    T_REQUIRE(result.value().core.conditions.at(0U).hitTarget.has_value());
    T_EQ(*result.value().core.conditions.at(0U).hitTarget, 0U);
    T_EQ(result.value().core.span.begin, 0U);
    T_EQ(result.value().core.span.end, 16U);
    T_REQUIRE(result.value().core.conditions.at(0U).hitTargetSpan.has_value());
    T_EQ(result.value().core.conditions.at(0U).hitTargetSpan->begin, 6U);
    T_EQ(result.value().core.conditions.at(0U).hitTargetSpan->end, 9U);
    T_EQ(result.value().alt.at(0U).span.begin, 17U);
    T_EQ(result.value().alt.at(0U).span.end, 35U);
    T_REQUIRE(result.value().alt.at(0U).conditions.at(0U).hitTarget.has_value());
    T_EQ(*result.value().alt.at(0U).conditions.at(0U).hitTarget, 4294967295U);
    T_REQUIRE(result.value().alt.at(0U).conditions.at(0U).hitTargetSpan.has_value());
    T_EQ(result.value().alt.at(0U).conditions.at(0U).hitTargetSpan->begin, 23U);
    T_EQ(result.value().alt.at(0U).conditions.at(0U).hitTargetSpan->end, 35U);
}

TEST_CASE("achievements_condition_parser", "rejects_malformed_syntax_with_exact_representative_offsets") {
    requireError("", ConditionParseErrorCategory::EmptyInput, 0U);
    requireError("0xH1=1__0xH2=2", ConditionParseErrorCategory::EmptyCondition, 7U);
    requireError("0xH1=1_", ConditionParseErrorCategory::EmptyCondition, 7U);
    requireError("S0xH1=1", ConditionParseErrorCategory::EmptyGroup, 0U);
    requireError("E:0xH1=1", ConditionParseErrorCategory::UnknownFlag, 0U);
    requireError("0xY1=1", ConditionParseErrorCategory::InvalidMemorySize, 2U);
    requireError("0xH=1", ConditionParseErrorCategory::InvalidAddress, 3U);
    requireError("0xH1?1", ConditionParseErrorCategory::InvalidOperator, 4U);
    requireError("0xH1=", ConditionParseErrorCategory::MissingOperand, 5U);
    requireError("0xH1=1.12", ConditionParseErrorCategory::InvalidHitTarget, 9U);
    requireError("0xH1=4294967296", ConditionParseErrorCategory::NumericOverflow, 14U);
    requireError("K:0xH1=1", ConditionParseErrorCategory::InvalidFlagOperator, 6U);
    requireError("0xH1 =1", ConditionParseErrorCategory::Whitespace, 4U);
    requireError(" 0xH1=1", ConditionParseErrorCategory::Whitespace, 0U);
    requireError("=1", ConditionParseErrorCategory::MissingOperand, 0U);
    requireError("0xH1=1.4294967296.", ConditionParseErrorCategory::NumericOverflow, 16U);
}

TEST_CASE("achievements_condition_parser", "enforces_input_group_and_condition_limits_with_downward_options") {
    ConditionParseOptions groups;
    groups.maximumGroups = 1U;
    T_REQUIRE(ConditionParser::parse("0xH1=1", groups).ok());
    const auto tooManyGroups = ConditionParser::parse("0xH1=1S0xH2=2", groups);
    T_REQUIRE(!tooManyGroups.ok());
    T_REQUIRE(tooManyGroups.error().category == ConditionParseErrorCategory::GroupLimit);

    ConditionParseOptions conditions;
    conditions.maximumConditions = 1U;
    T_REQUIRE(ConditionParser::parse("0xH1=1", conditions).ok());
    const auto tooManyConditions = ConditionParser::parse("0xH1=1_0xH2=2", conditions);
    T_REQUIRE(!tooManyConditions.ok());
    T_REQUIRE(tooManyConditions.error().category == ConditionParseErrorCategory::ConditionLimit);

    ConditionParseOptions zero;
    zero.maximumGroups = 0U;
    const auto invalidZero = ConditionParser::parse("0xH1=1", zero);
    T_REQUIRE(!invalidZero.ok());
    T_REQUIRE(invalidZero.error().category == ConditionParseErrorCategory::InvalidOptions);

    ConditionParseOptions zeroInput;
    zeroInput.maximumInputBytes = 0U;
    T_REQUIRE(!ConditionParser::parse("0xH1=1", zeroInput).ok());

    ConditionParseOptions zeroConditions;
    zeroConditions.maximumConditions = 0U;
    T_REQUIRE(!ConditionParser::parse("0xH1=1", zeroConditions).ok());

    ConditionParseOptions reducedInput;
    reducedInput.maximumInputBytes = 5U;
    const auto overReducedInput = ConditionParser::parse("0xH1=1", reducedInput);
    T_REQUIRE(!overReducedInput.ok());
    T_REQUIRE(overReducedInput.error().category == ConditionParseErrorCategory::InputTooLarge);

    ConditionParseOptions tooLarge;
    tooLarge.maximumInputBytes = gb::achievements::parser::kConditionMaximumInputBytes + 1U;
    const auto invalidCeiling = ConditionParser::parse("0xH1=1", tooLarge);
    T_REQUIRE(!invalidCeiling.ok());
    T_REQUIRE(invalidCeiling.error().category == ConditionParseErrorCategory::InvalidOptions);

    ConditionParseOptions tooManyAllowedGroups;
    tooManyAllowedGroups.maximumGroups = gb::achievements::parser::kConditionMaximumGroups + 1U;
    T_REQUIRE(!ConditionParser::parse("0xH1=1", tooManyAllowedGroups).ok());

    ConditionParseOptions tooManyAllowedConditions;
    tooManyAllowedConditions.maximumConditions = gb::achievements::parser::kConditionMaximumConditions + 1U;
    T_REQUIRE(!ConditionParser::parse("0xH1=1", tooManyAllowedConditions).ok());

    const std::string oneMiB(gb::achievements::parser::kConditionMaximumInputBytes, '1');
    const auto inputLimit = ConditionParser::parse(oneMiB + "1");
    T_REQUIRE(!inputLimit.ok());
    T_REQUIRE(inputLimit.error().category == ConditionParseErrorCategory::InputTooLarge);
}

} // namespace
