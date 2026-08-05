#include "gb/achievements/runtime/condition_evaluator.hpp"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace gb::achievements::runtime {
namespace {

struct OperandValue {
    std::uint32_t value = 0U;
    std::uint32_t mask = std::numeric_limits<std::uint32_t>::max();
};

bool isFloat(parser::MemorySize size) noexcept {
    return size == parser::MemorySize::Float || size == parser::MemorySize::FloatBigEndian
        || size == parser::MemorySize::Double32 || size == parser::MemorySize::Double32BigEndian
        || size == parser::MemorySize::Mbf32 || size == parser::MemorySize::Mbf32LittleEndian;
}

bool isAdvancedFlag(parser::ConditionFlag flag) noexcept {
    return flag == parser::ConditionFlag::Measured
        || flag == parser::ConditionFlag::MeasuredPercent || flag == parser::ConditionFlag::MeasuredIf
        || flag == parser::ConditionFlag::Remember;
}

std::uint32_t bcd(std::uint32_t value) noexcept {
    std::uint32_t result = 0U;
    std::uint32_t multiplier = 1U;
    while (value != 0U) {
        result += (value & 0xFU) * multiplier;
        multiplier *= 10U;
        value >>= 4U;
    }
    return result;
}

struct GroupEvaluation {
    bool passes = true;
    bool hasTrigger = false;
    bool triggerPasses = false;
    ConditionEvaluationStatus control = ConditionEvaluationStatus::Waiting;
};

} // namespace

class EvaluationFrame final {
public:
    explicit EvaluationFrame(ConditionEvaluator& owner) : owner_(owner) {}

    bool operand(const parser::Operand& operand, OperandValue& output, std::uint32_t offset = 0U) {
        if (operand.kind == parser::OperandKind::Constant) {
            output.value = operand.constant;
            output.mask = std::numeric_limits<std::uint32_t>::max();
            return true;
        }
        if (operand.kind == parser::OperandKind::Recall) {
            return fail(ConditionEvaluationStatus::Unsupported, operand.span, "recall is unsupported");
        }
        if (isFloat(operand.memory.size)) {
            return fail(ConditionEvaluationStatus::Unsupported, operand.span, "floating memory is unsupported");
        }

        if (operand.memory.address > std::numeric_limits<std::uint32_t>::max() - offset) return unsupported(operand.span, "indirect address overflow");
        const auto address = operand.memory.address + offset;
        RawMemory* raw = findRaw(address, operand.memory.size);
        if (raw == nullptr) {
            RawMemory decoded;
            decoded.address = address;
            decoded.size = operand.memory.size;
            if (!read(decoded)) {
                error_.sourceSpan = operand.span;
                return false;
            }
            reads_.push_back(decoded);
            raw = &reads_.back();
        }

        auto history = std::find_if(owner_.histories_.begin(), owner_.histories_.end(), [&operand, address](const auto& entry) {
            return entry.address == address && entry.size == operand.memory.size;
        });
        if (history == owner_.histories_.end()) {
            owner_.histories_.push_back({address, operand.memory.size});
            history = std::prev(owner_.histories_.end());
        }

        output.value = raw->value;
        output.mask = raw->mask;
        if (operand.modifier == parser::OperandModifier::Delta && history->hasLast) {
            output.value = history->last;
        } else if (operand.modifier == parser::OperandModifier::Prior && history->hasLast) {
            output.value = history->last != raw->value ? history->last : (history->hasPrior ? history->prior : raw->value);
        }
        if (operand.modifier == parser::OperandModifier::Bcd) output.value = bcd(output.value);
        if (operand.modifier == parser::OperandModifier::Invert) output.value ^= output.mask;
        return true;
    }

    void commit() noexcept {
        for (const auto& raw : reads_) {
            auto history = std::find_if(owner_.histories_.begin(), owner_.histories_.end(), [&raw](const auto& entry) {
                return entry.address == raw.address && entry.size == raw.size;
            });
            if (history == owner_.histories_.end()) continue;
            if (!history->hasLast) {
                history->last = raw.value;
                history->prior = raw.value;
                history->hasLast = true;
                history->hasPrior = true;
            } else if (history->last != raw.value) {
                history->prior = history->last;
                history->last = raw.value;
                history->hasPrior = true;
            }
        }
    }

    const ConditionEvaluation& error() const noexcept { return error_; }

    bool unsupported(parser::SourceSpan span, const char* reason) {
        return fail(ConditionEvaluationStatus::Unsupported, span, reason);
    }

private:
    struct RawMemory {
        std::uint32_t address = 0U;
        parser::MemorySize size = parser::MemorySize::EightBit;
        std::uint32_t value = 0U;
        std::uint32_t mask = 0U;
    };

    RawMemory* findRaw(std::uint32_t address, parser::MemorySize size) noexcept {
        const auto found = std::find_if(reads_.begin(), reads_.end(), [address, size](const auto& entry) {
            return entry.address == address && entry.size == size;
        });
        return found == reads_.end() ? nullptr : &*found;
    }

    bool fail(ConditionEvaluationStatus status, parser::SourceSpan span, const char* reason) {
        error_ = {status, span, reason};
        return false;
    }

    bool read(RawMemory& output) {
        std::size_t bytes = 1U;
        bool bigEndian = false;
        switch (output.size) {
        case parser::MemorySize::SixteenBit: bytes = 2U; output.mask = 0xFFFFU; break;
        case parser::MemorySize::TwentyFourBit: bytes = 3U; output.mask = 0xFFFFFFU; break;
        case parser::MemorySize::ThirtyTwoBit: bytes = 4U; output.mask = 0xFFFFFFFFU; break;
        case parser::MemorySize::SixteenBitBigEndian: bytes = 2U; output.mask = 0xFFFFU; bigEndian = true; break;
        case parser::MemorySize::TwentyFourBitBigEndian: bytes = 3U; output.mask = 0xFFFFFFU; bigEndian = true; break;
        case parser::MemorySize::ThirtyTwoBitBigEndian: bytes = 4U; output.mask = 0xFFFFFFFFU; bigEndian = true; break;
        default: output.mask = 0xFFU; break;
        }
        std::uint8_t buffer[4]{};
        if (!owner_.reader_ || owner_.reader_(output.address, buffer, bytes) != bytes) {
            return fail(ConditionEvaluationStatus::MemoryError, {}, "short memory read");
        }
        std::uint32_t value = 0U;
        if (bigEndian) {
            for (std::size_t index = 0U; index < bytes; ++index) value = (value << 8U) | buffer[index];
        } else {
            for (std::size_t index = 0U; index < bytes; ++index) value |= static_cast<std::uint32_t>(buffer[index]) << (index * 8U);
        }
        switch (output.size) {
        case parser::MemorySize::Bit0: output.value = value & 1U; output.mask = 1U; break;
        case parser::MemorySize::Bit1: output.value = (value >> 1U) & 1U; output.mask = 1U; break;
        case parser::MemorySize::Bit2: output.value = (value >> 2U) & 1U; output.mask = 1U; break;
        case parser::MemorySize::Bit3: output.value = (value >> 3U) & 1U; output.mask = 1U; break;
        case parser::MemorySize::Bit4: output.value = (value >> 4U) & 1U; output.mask = 1U; break;
        case parser::MemorySize::Bit5: output.value = (value >> 5U) & 1U; output.mask = 1U; break;
        case parser::MemorySize::Bit6: output.value = (value >> 6U) & 1U; output.mask = 1U; break;
        case parser::MemorySize::Bit7: output.value = (value >> 7U) & 1U; output.mask = 1U; break;
        case parser::MemorySize::LowerNibble: output.value = value & 0xFU; output.mask = 0xFU; break;
        case parser::MemorySize::UpperNibble: output.value = (value >> 4U) & 0xFU; output.mask = 0xFU; break;
        case parser::MemorySize::BitCount: {
            std::uint32_t bits = value & 0xFFU;
            std::uint32_t count = 0U;
            while (bits != 0U) { count += bits & 1U; bits >>= 1U; }
            output.value = count;
            output.mask = 1U;
            break;
        }
        default: output.value = value; break;
        }
        return true;
    }

    ConditionEvaluator& owner_;
    std::vector<RawMemory> reads_;
    ConditionEvaluation error_{};
};

namespace {

bool preflight(const parser::ConditionGroup& group, ConditionEvaluation& result) {
    for (const auto& condition : group.conditions) {
        if (isAdvancedFlag(condition.flag)) {
            result = {ConditionEvaluationStatus::Unsupported, condition.span, "advanced flag is unsupported"};
            return false;
        }
        const auto unsupportedOperand = [&result, &condition](const parser::Operand& operand) {
            if (operand.kind == parser::OperandKind::Recall || (operand.kind == parser::OperandKind::Memory && isFloat(operand.memory.size))) {
                result = {ConditionEvaluationStatus::Unsupported, condition.span, "operand is unsupported"};
                return true;
            }
            return false;
        };
        if (unsupportedOperand(condition.left) || (condition.right && unsupportedOperand(*condition.right))) return false;
    }
    return true;
}

bool expression(EvaluationFrame& frame, const parser::Condition& condition, bool& result, std::uint32_t offset = 0U, std::uint32_t source = 0U) {
    OperandValue left;
    if (!frame.operand(condition.left, left, offset)) return false;
    left.value += source;
    if (!condition.right || condition.op == parser::Operator::None) {
        result = left.value != 0U;
        return true;
    }
    OperandValue right;
    if (!frame.operand(*condition.right, right, offset)) return false;
    switch (condition.op) {
    case parser::Operator::Equal: result = left.value == right.value; return true;
    case parser::Operator::NotEqual: result = left.value != right.value; return true;
    case parser::Operator::Less: result = left.value < right.value; return true;
    case parser::Operator::LessOrEqual: result = left.value <= right.value; return true;
    case parser::Operator::Greater: result = left.value > right.value; return true;
    case parser::Operator::GreaterOrEqual: result = left.value >= right.value; return true;
    case parser::Operator::Divide:
        if (right.value == 0U) { result = false; return frame.unsupported(condition.span, "division by zero"); }
        result = left.value / right.value != 0U;
        return true;
    case parser::Operator::Modulo:
        if (right.value == 0U) { result = false; return frame.unsupported(condition.span, "modulo by zero"); }
        result = left.value % right.value != 0U;
        return true;
    case parser::Operator::Multiply: result = left.value * right.value != 0U; return true;
    case parser::Operator::Add: result = left.value + right.value != 0U; return true;
    case parser::Operator::Subtract: result = left.value - right.value != 0U; return true;
    case parser::Operator::BitwiseAnd: result = (left.value & right.value) != 0U; return true;
    case parser::Operator::BitwiseXor: result = (left.value ^ right.value) != 0U; return true;
    default: result = false; return false;
    }
}

} // namespace

ConditionEvaluator::ConditionEvaluator(memory::MemoryReader reader)
    : reader_(std::move(reader)) {}

ConditionEvaluation ConditionEvaluator::evaluate(const parser::ConditionTrigger& trigger) {
    ConditionEvaluation unsupported;
    if (!preflight(trigger.core, unsupported)) return unsupported;
    for (const auto& group : trigger.alt) if (!preflight(group, unsupported)) return unsupported;

    EvaluationFrame frame(*this);
    const auto evaluateGroup = [this, &frame, &trigger](const parser::ConditionGroup& group) {
        GroupEvaluation groupResult;
        bool resetNext = false;
        std::uint32_t source = 0U;
        std::uint32_t addressOffset = 0U;
        bool chainActive = false;
        bool chainOr = false;
        bool chainValue = false;
        std::uint32_t hitContribution = 0U;
        bool hitSubtract = false;
        for (const auto& condition : group.conditions) {
            if (condition.flag == parser::ConditionFlag::AddHits || condition.flag == parser::ConditionFlag::SubHits) {
                OperandValue contribution;
                if (!frame.operand(condition.left, contribution)) return GroupEvaluation{false, false, false, frame.error().status};
                if (condition.flag == parser::ConditionFlag::AddHits) {
                    hitContribution = std::numeric_limits<std::uint32_t>::max() - hitContribution < contribution.value
                        ? std::numeric_limits<std::uint32_t>::max() : hitContribution + contribution.value;
                } else {
                    hitContribution = contribution.value;
                    hitSubtract = true;
                }
                continue;
            }
            if (condition.flag == parser::ConditionFlag::AddSource || condition.flag == parser::ConditionFlag::SubSource
                || condition.flag == parser::ConditionFlag::AddAddress) {
                OperandValue value;
                if (!frame.operand(condition.left, value)) return GroupEvaluation{false, false, false, frame.error().status};
                if (condition.flag == parser::ConditionFlag::AddSource) source += value.value;
                else if (condition.flag == parser::ConditionFlag::SubSource) source -= value.value;
                else {
                    if (addressOffset > std::numeric_limits<std::uint32_t>::max() - value.value) {
                        frame.unsupported(condition.span, "indirect address overflow");
                        return GroupEvaluation{false, false, false, ConditionEvaluationStatus::Unsupported};
                    }
                    addressOffset += value.value;
                }
                continue;
            }
            bool conditionPasses = false;
            if (!expression(frame, condition, conditionPasses, addressOffset, source)) {
                if (frame.error().status != ConditionEvaluationStatus::Waiting) return GroupEvaluation{false, false, false, frame.error().status};
                return GroupEvaluation{false, false, false, ConditionEvaluationStatus::Unsupported};
            }
            if (condition.hitTarget) {
                auto hit = std::find_if(hitCounts_.begin(), hitCounts_.end(), [&trigger, &condition](const auto& entry) {
                    return entry.source == trigger.source && entry.span.begin == condition.span.begin && entry.span.end == condition.span.end;
                });
                if (hit == hitCounts_.end()) {
                    hitCounts_.push_back({trigger.source, condition.span, 0U});
                    hit = std::prev(hitCounts_.end());
                }
                if (conditionPasses) {
                    if (hitSubtract) hit->count = hit->count < hitContribution ? 0U : hit->count - hitContribution;
                    const auto increment = hitSubtract || hitContribution == 0U ? 1U : hitContribution;
                    hit->count = std::numeric_limits<std::uint32_t>::max() - hit->count < increment
                        ? std::numeric_limits<std::uint32_t>::max() : hit->count + increment;
                }
                hitContribution = 0U;
                hitSubtract = false;
                conditionPasses = conditionPasses && hit->count >= *condition.hitTarget;
            }
            if (condition.flag == parser::ConditionFlag::PauseIf && conditionPasses) return GroupEvaluation{false, false, false, ConditionEvaluationStatus::Paused};
            if (condition.flag == parser::ConditionFlag::ResetIf && conditionPasses) return GroupEvaluation{false, false, false, ConditionEvaluationStatus::Reset};
            if (condition.flag == parser::ConditionFlag::PauseIf || condition.flag == parser::ConditionFlag::ResetIf) continue;
            if (condition.flag == parser::ConditionFlag::ResetNextIf) {
                resetNext = conditionPasses;
                continue;
            }
            if (condition.flag == parser::ConditionFlag::AndNext || condition.flag == parser::ConditionFlag::OrNext) {
                chainActive = true;
                chainOr = condition.flag == parser::ConditionFlag::OrNext;
                chainValue = conditionPasses;
                continue;
            }
            if (resetNext && conditionPasses) return GroupEvaluation{false, false, false, ConditionEvaluationStatus::Reset};
            if (condition.flag == parser::ConditionFlag::Trigger) {
                groupResult.hasTrigger = true;
                groupResult.triggerPasses = groupResult.triggerPasses || conditionPasses;
                continue;
            }
            resetNext = false;
            if (chainActive) {
                conditionPasses = chainOr ? (chainValue || conditionPasses) : (chainValue && conditionPasses);
                chainActive = false;
            }
            if (!conditionPasses) groupResult.passes = false;
            source = 0U;
            addressOffset = 0U;
        }
        return groupResult;
    };

    auto core = evaluateGroup(trigger.core);
    if (core.control == ConditionEvaluationStatus::MemoryError || core.control == ConditionEvaluationStatus::Unsupported) return frame.error();
    if (core.control == ConditionEvaluationStatus::Paused) { frame.commit(); return {ConditionEvaluationStatus::Paused, {}, {}}; }
    if (core.control == ConditionEvaluationStatus::Reset) { reset(); return {ConditionEvaluationStatus::Reset, {}, {}}; }
    bool alternatePasses = trigger.alt.empty();
    bool hasTrigger = core.hasTrigger;
    bool triggerPasses = core.triggerPasses;
    for (const auto& group : trigger.alt) {
        const auto alternate = evaluateGroup(group);
        if (alternate.control == ConditionEvaluationStatus::MemoryError || alternate.control == ConditionEvaluationStatus::Unsupported) return frame.error();
        if (alternate.control == ConditionEvaluationStatus::Paused) { frame.commit(); return {ConditionEvaluationStatus::Paused, {}, {}}; }
        if (alternate.control == ConditionEvaluationStatus::Reset) { reset(); return {ConditionEvaluationStatus::Reset, {}, {}}; }
        if (alternate.passes) {
            alternatePasses = true;
            hasTrigger = hasTrigger || alternate.hasTrigger;
            triggerPasses = triggerPasses || alternate.triggerPasses;
        }
    }
    frame.commit();
    if (!core.passes) return {ConditionEvaluationStatus::Waiting, {}, {}};
    if (!alternatePasses) return {ConditionEvaluationStatus::Waiting, {}, {}};
    if (hasTrigger && !triggerPasses) return {ConditionEvaluationStatus::Primed, {}, {}};
    return {ConditionEvaluationStatus::Triggered, {}, {}};
}

void ConditionEvaluator::reset() noexcept {
    histories_.clear();
    hitCounts_.clear();
}

} // namespace gb::achievements::runtime
