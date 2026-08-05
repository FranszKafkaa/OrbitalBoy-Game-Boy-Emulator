#include <array>
#include <vector>

#include "gb/achievements/runtime/condition_evaluator.hpp"

#include "../../test_framework.hpp"

namespace {

using gb::achievements::memory::MemoryReader;
using gb::achievements::parser::Condition;
using gb::achievements::parser::ConditionTrigger;
using gb::achievements::parser::MemorySize;
using gb::achievements::parser::OperandKind;
using gb::achievements::parser::Operator;
using gb::achievements::runtime::ConditionEvaluationStatus;
using gb::achievements::runtime::ConditionEvaluator;

Condition memoryEquals(std::uint32_t address, std::uint32_t value) {
    Condition condition;
    condition.left.kind = OperandKind::Memory;
    condition.left.memory.size = MemorySize::EightBit;
    condition.left.memory.address = address;
    condition.op = Operator::Equal;
    condition.right = gb::achievements::parser::Operand{};
    condition.right->kind = OperandKind::Constant;
    condition.right->constant = value;
    return condition;
}

Condition memoryCondition(std::uint32_t address, MemorySize size, Operator op, std::uint32_t value) {
    auto condition = memoryEquals(address, value);
    condition.left.memory.size = size;
    condition.op = op;
    return condition;
}

Condition constantCondition(std::uint32_t left, Operator op, std::uint32_t right) {
    Condition condition;
    condition.left.kind = OperandKind::Constant;
    condition.left.constant = left;
    condition.op = op;
    condition.right = gb::achievements::parser::Operand{};
    condition.right->kind = OperandKind::Constant;
    condition.right->constant = right;
    return condition;
}

struct TestMemory {
    std::vector<std::uint8_t> bytes;
    std::size_t maximumRead = static_cast<std::size_t>(-1);

    MemoryReader reader() {
        return [this](std::uint32_t address, std::uint8_t* destination, std::size_t count) {
            if (address >= bytes.size()) return std::size_t{0U};
            const auto available = bytes.size() - address;
            const auto readable = count < available ? count : available;
            const auto actual = readable < maximumRead ? readable : maximumRead;
            for (std::size_t index = 0U; index < actual; ++index) destination[index] = bytes.at(address + index);
            return actual;
        };
    }
};

TEST_CASE("achievements_condition_evaluator", "evaluates_current_eight_bit_memory_against_a_constant") {
    const std::array<std::uint8_t, 2U> memory{7U, 0U};
    MemoryReader reader = [&memory](std::uint32_t address, std::uint8_t* destination, std::size_t count) {
        if (address >= memory.size() || count > memory.size() - address) return std::size_t{0U};
        for (std::size_t index = 0U; index < count; ++index) destination[index] = memory.at(address + index);
        return count;
    };
    ConditionTrigger trigger;
    trigger.core.conditions.push_back(memoryEquals(0U, 7U));
    ConditionEvaluator evaluator(std::move(reader));

    const auto evaluation = evaluator.evaluate(trigger);

    T_REQUIRE(evaluation.status == ConditionEvaluationStatus::Triggered);
}

TEST_CASE("achievements_condition_evaluator", "decodes_every_integer_memory_family") {
    TestMemory memory{{0x96U, 0x34U, 0x12U, 0x56U, 0x78U}};
    struct Example { MemorySize size; std::uint32_t address; std::uint32_t expected; };
    const Example examples[] = {
        {MemorySize::Bit0, 0U, 0U}, {MemorySize::Bit1, 0U, 1U}, {MemorySize::Bit2, 0U, 1U},
        {MemorySize::Bit3, 0U, 0U}, {MemorySize::Bit4, 0U, 1U}, {MemorySize::Bit5, 0U, 0U},
        {MemorySize::Bit6, 0U, 0U}, {MemorySize::Bit7, 0U, 1U}, {MemorySize::LowerNibble, 0U, 6U},
        {MemorySize::UpperNibble, 0U, 9U}, {MemorySize::EightBit, 0U, 0x96U},
        {MemorySize::SixteenBit, 1U, 0x1234U}, {MemorySize::TwentyFourBit, 1U, 0x561234U},
        {MemorySize::ThirtyTwoBit, 1U, 0x78561234U}, {MemorySize::SixteenBitBigEndian, 1U, 0x3412U},
        {MemorySize::TwentyFourBitBigEndian, 1U, 0x341256U}, {MemorySize::ThirtyTwoBitBigEndian, 1U, 0x34125678U},
        {MemorySize::BitCount, 0U, 4U},
    };
    for (const auto& example : examples) {
        ConditionTrigger trigger;
        trigger.core.conditions.push_back(memoryCondition(example.address, example.size, Operator::Equal, example.expected));
        ConditionEvaluator evaluator(memory.reader());
        T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Triggered);
    }
}

TEST_CASE("achievements_condition_evaluator", "requires_core_and_one_complete_alternate_group") {
    TestMemory memory{{1U, 2U, 3U}};
    ConditionTrigger trigger;
    trigger.core.conditions.push_back(memoryEquals(0U, 1U));
    trigger.alt.push_back({{memoryEquals(1U, 9U)}, {}});
    trigger.alt.push_back({{memoryEquals(2U, 3U)}, {}});
    ConditionEvaluator evaluator(memory.reader());
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Triggered);

    trigger.core.conditions.front().right->constant = 9U;
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Waiting);
}

TEST_CASE("achievements_condition_evaluator", "tracks_hit_targets_and_reset") {
    TestMemory memory{{1U}};
    ConditionTrigger trigger;
    auto condition = memoryEquals(0U, 1U);
    condition.hitTarget = 2U;
    trigger.core.conditions.push_back(condition);
    ConditionEvaluator evaluator(memory.reader());
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Waiting);
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Triggered);
    memory.bytes[0] = 0U;
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Waiting);
    memory.bytes[0] = 1U;
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Triggered);
    evaluator.reset();
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Waiting);
}

TEST_CASE("achievements_condition_evaluator", "honors_pause_reset_reset_next_and_trigger_controls") {
    TestMemory memory{{1U, 1U}};
    ConditionEvaluator evaluator(memory.reader());
    ConditionTrigger pause;
    auto pauseCondition = memoryEquals(0U, 1U);
    pauseCondition.flag = gb::achievements::parser::ConditionFlag::PauseIf;
    pause.core.conditions.push_back(pauseCondition);
    pause.core.conditions.push_back(memoryEquals(1U, 1U));
    T_REQUIRE(evaluator.evaluate(pause).status == ConditionEvaluationStatus::Paused);
    memory.bytes[0] = 0U;
    T_REQUIRE(evaluator.evaluate(pause).status == ConditionEvaluationStatus::Triggered);
    memory.bytes[0] = 1U;

    ConditionTrigger reset;
    auto resetCondition = memoryEquals(0U, 1U);
    resetCondition.flag = gb::achievements::parser::ConditionFlag::ResetIf;
    reset.core.conditions.push_back(resetCondition);
    T_REQUIRE(evaluator.evaluate(reset).status == ConditionEvaluationStatus::Reset);
    memory.bytes[0] = 0U;
    reset.core.conditions.push_back(memoryEquals(1U, 1U));
    T_REQUIRE(evaluator.evaluate(reset).status == ConditionEvaluationStatus::Triggered);
    memory.bytes[0] = 1U;

    ConditionTrigger resetNext;
    auto resetNextCondition = memoryEquals(0U, 1U);
    resetNextCondition.flag = gb::achievements::parser::ConditionFlag::ResetNextIf;
    resetNext.core.conditions.push_back(resetNextCondition);
    resetNext.core.conditions.push_back(memoryEquals(1U, 1U));
    T_REQUIRE(evaluator.evaluate(resetNext).status == ConditionEvaluationStatus::Reset);

    ConditionTrigger resetNextTrigger;
    resetNextTrigger.core.conditions.push_back(resetNextCondition);
    auto followingTrigger = memoryEquals(1U, 1U);
    followingTrigger.flag = gb::achievements::parser::ConditionFlag::Trigger;
    resetNextTrigger.core.conditions.push_back(followingTrigger);
    T_REQUIRE(evaluator.evaluate(resetNextTrigger).status == ConditionEvaluationStatus::Reset);

    ConditionTrigger trigger;
    auto triggerCondition = memoryEquals(0U, 2U);
    triggerCondition.flag = gb::achievements::parser::ConditionFlag::Trigger;
    trigger.core.conditions.push_back(memoryEquals(1U, 1U));
    trigger.core.conditions.push_back(triggerCondition);
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Primed);
    memory.bytes[0] = 2U;
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Triggered);
}

TEST_CASE("achievements_condition_evaluator", "uses_previous_and_most_recent_distinct_values_for_delta_and_prior") {
    TestMemory memory{{1U}};
    ConditionTrigger delta;
    auto deltaCondition = memoryEquals(0U, 1U);
    deltaCondition.left.modifier = gb::achievements::parser::OperandModifier::Delta;
    delta.core.conditions.push_back(deltaCondition);
    ConditionEvaluator deltaEvaluator(memory.reader());
    T_REQUIRE(deltaEvaluator.evaluate(delta).status == ConditionEvaluationStatus::Triggered);
    memory.bytes[0] = 2U;
    T_REQUIRE(deltaEvaluator.evaluate(delta).status == ConditionEvaluationStatus::Triggered);

    memory.bytes[0] = 1U;
    ConditionTrigger prior;
    auto priorCondition = memoryEquals(0U, 1U);
    priorCondition.left.modifier = gb::achievements::parser::OperandModifier::Prior;
    prior.core.conditions.push_back(priorCondition);
    ConditionEvaluator priorEvaluator(memory.reader());
    T_REQUIRE(priorEvaluator.evaluate(prior).status == ConditionEvaluationStatus::Triggered);
    memory.bytes[0] = 2U;
    T_REQUIRE(priorEvaluator.evaluate(prior).status == ConditionEvaluationStatus::Triggered);
    memory.bytes[0] = 3U;
    priorCondition.right->constant = 2U;
    prior.core.conditions.front() = priorCondition;
    T_REQUIRE(priorEvaluator.evaluate(prior).status == ConditionEvaluationStatus::Triggered);
}

TEST_CASE("achievements_condition_evaluator", "applies_bcd_and_invert_modifiers") {
    TestMemory memory{{0x42U, 0x0FU}};
    ConditionTrigger bcd;
    auto bcdCondition = memoryEquals(0U, 42U);
    bcdCondition.left.modifier = gb::achievements::parser::OperandModifier::Bcd;
    bcd.core.conditions.push_back(bcdCondition);
    ConditionEvaluator evaluator(memory.reader());
    T_REQUIRE(evaluator.evaluate(bcd).status == ConditionEvaluationStatus::Triggered);

    ConditionTrigger inverted;
    auto invertCondition = memoryEquals(1U, 240U);
    invertCondition.left.modifier = gb::achievements::parser::OperandModifier::Invert;
    inverted.core.conditions.push_back(invertCondition);
    T_REQUIRE(evaluator.evaluate(inverted).status == ConditionEvaluationStatus::Triggered);

    ConditionTrigger bitCountInverted;
    memory.bytes[0] = 0x96U;
    auto bitCountInvert = memoryCondition(0U, MemorySize::BitCount, Operator::Equal, 5U);
    bitCountInvert.left.modifier = gb::achievements::parser::OperandModifier::Invert;
    bitCountInverted.core.conditions.push_back(bitCountInvert);
    T_REQUIRE(evaluator.evaluate(bitCountInverted).status == ConditionEvaluationStatus::Triggered);
}

TEST_CASE("achievements_condition_evaluator", "reports_checked_arithmetic_memory_and_unsupported_errors") {
    TestMemory memory{{1U}};
    ConditionEvaluator evaluator(memory.reader());
    ConditionTrigger divideByZero;
    divideByZero.core.conditions.push_back(constantCondition(5U, Operator::Divide, 0U));
    const auto divide = evaluator.evaluate(divideByZero);
    T_REQUIRE(divide.status == ConditionEvaluationStatus::Unsupported);
    T_REQUIRE(!divide.reason.empty());

    ConditionTrigger moduloByZero;
    moduloByZero.core.conditions.push_back(constantCondition(5U, Operator::Modulo, 0U));
    T_REQUIRE(evaluator.evaluate(moduloByZero).status == ConditionEvaluationStatus::Unsupported);

    ConditionTrigger shortRead;
    shortRead.core.conditions.push_back(memoryCondition(0U, MemorySize::SixteenBit, Operator::Equal, 1U));
    const auto shortResult = evaluator.evaluate(shortRead);
    T_REQUIRE(shortResult.status == ConditionEvaluationStatus::MemoryError);

    ConditionTrigger floating;
    auto floatCondition = memoryEquals(0U, 1U);
    floatCondition.left.memory.size = MemorySize::Float;
    floatCondition.span = {9U, 14U};
    floating.core.conditions.push_back(floatCondition);
    const auto floatResult = evaluator.evaluate(floating);
    T_REQUIRE(floatResult.status == ConditionEvaluationStatus::Unsupported);
    T_REQUIRE(floatResult.sourceSpan.has_value());
    T_EQ(floatResult.sourceSpan->begin, 9U);

    ConditionTrigger advanced;
    auto advancedCondition = memoryEquals(0U, 1U);
    advancedCondition.flag = gb::achievements::parser::ConditionFlag::Measured;
    advanced.core.conditions.push_back(advancedCondition);
    T_REQUIRE(evaluator.evaluate(advanced).status == ConditionEvaluationStatus::Unsupported);
}

TEST_CASE("achievements_condition_evaluator", "updates_alternate_state_and_honors_alternate_reset_when_core_fails") {
    TestMemory memory{{1U, 0U}};
    ConditionEvaluator evaluator(memory.reader());
    ConditionTrigger trigger;
    trigger.core.conditions.push_back(memoryEquals(1U, 1U));
    auto delta = memoryEquals(0U, 2U);
    delta.left.modifier = gb::achievements::parser::OperandModifier::Delta;
    trigger.alt.push_back({{delta}, {}});
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Waiting);
    memory.bytes[0] = 2U;
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Waiting);
    memory.bytes[0] = 3U;
    memory.bytes[1] = 1U;
    T_REQUIRE(evaluator.evaluate(trigger).status == ConditionEvaluationStatus::Triggered);

    ConditionTrigger alternateReset;
    alternateReset.core.conditions.push_back(memoryEquals(1U, 9U));
    auto reset = memoryEquals(0U, 3U);
    reset.flag = gb::achievements::parser::ConditionFlag::ResetIf;
    alternateReset.alt.push_back({{reset}, {}});
    T_REQUIRE(evaluator.evaluate(alternateReset).status == ConditionEvaluationStatus::Reset);
}

TEST_CASE("achievements_condition_evaluator", "combines_sources_and_indirect_addresses") {
    TestMemory memory{{4U, 2U, 0U, 0U, 7U}};
    ConditionEvaluator evaluator(memory.reader());
    ConditionTrigger source;
    auto addSource = memoryEquals(1U, 0U);
    addSource.flag = gb::achievements::parser::ConditionFlag::AddSource;
    addSource.op = Operator::None;
    addSource.right.reset();
    source.core.conditions.push_back(addSource);
    source.core.conditions.push_back(memoryEquals(4U, 9U));
    T_REQUIRE(evaluator.evaluate(source).status == ConditionEvaluationStatus::Triggered);

    ConditionTrigger indirect;
    auto addAddress = memoryEquals(0U, 0U);
    addAddress.flag = gb::achievements::parser::ConditionFlag::AddAddress;
    addAddress.op = Operator::None;
    addAddress.right.reset();
    indirect.core.conditions.push_back(addAddress);
    indirect.core.conditions.push_back(memoryEquals(0U, 7U));
    T_REQUIRE(evaluator.evaluate(indirect).status == ConditionEvaluationStatus::Triggered);
}

TEST_CASE("achievements_condition_evaluator", "rejects_unimplemented_chain_and_hit_combinations") {
    TestMemory memory{{1U}};
    for (const auto flag : {gb::achievements::parser::ConditionFlag::AndNext,
             gb::achievements::parser::ConditionFlag::OrNext,
             gb::achievements::parser::ConditionFlag::AddHits,
             gb::achievements::parser::ConditionFlag::SubHits}) {
        ConditionTrigger trigger;
        auto condition = memoryEquals(0U, 1U);
        condition.flag = flag;
        trigger.core.conditions.push_back(condition);
        T_REQUIRE(ConditionEvaluator(memory.reader()).evaluate(trigger).status == ConditionEvaluationStatus::Unsupported);
    }
}

} // namespace
