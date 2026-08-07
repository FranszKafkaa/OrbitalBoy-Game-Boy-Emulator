#include "gb/achievements/parser/condition_parser.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace gb::achievements::parser {

ConditionParseResult ConditionParseResult::success(ConditionTrigger trigger) {
    ConditionParseResult result;
    result.value_ = std::move(trigger);
    return result;
}

ConditionParseResult ConditionParseResult::failure(ConditionParseErrorCategory category, std::size_t byteOffset) {
    ConditionParseResult result;
    result.error_ = ConditionParseError{category, byteOffset};
    return result;
}

bool ConditionParseResult::ok() const noexcept {
    return value_.has_value();
}

const ConditionTrigger& ConditionParseResult::value() const {
    if (!value_) {
        throw std::logic_error("condition parse result has no value");
    }
    return *value_;
}

const ConditionParseError& ConditionParseResult::error() const {
    if (value_) {
        throw std::logic_error("condition parse result has no error");
    }
    return error_;
}

namespace {

bool isDecimal(char character) noexcept {
    return character >= '0' && character <= '9';
}

unsigned int hexValue(char character) noexcept {
    if (character >= '0' && character <= '9') return static_cast<unsigned int>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<unsigned int>(character - 'a') + 10U;
    if (character >= 'A' && character <= 'F') return static_cast<unsigned int>(character - 'A') + 10U;
    return 16U;
}

bool isWhitespace(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

bool isComparison(Operator op) noexcept {
    return op == Operator::Equal || op == Operator::NotEqual || op == Operator::Less
        || op == Operator::LessOrEqual || op == Operator::Greater || op == Operator::GreaterOrEqual;
}

bool isValueOperator(Operator op) noexcept {
    return op == Operator::Multiply || op == Operator::Divide || op == Operator::Add
        || op == Operator::Subtract || op == Operator::Modulo || op == Operator::BitwiseAnd
        || op == Operator::BitwiseXor;
}

bool memorySizeForPrefix(char prefix, MemorySize& size) noexcept {
    switch (prefix) {
    case 'M': size = MemorySize::Bit0; return true;
    case 'N': size = MemorySize::Bit1; return true;
    case 'O': size = MemorySize::Bit2; return true;
    case 'P': size = MemorySize::Bit3; return true;
    case 'Q': size = MemorySize::Bit4; return true;
    case 'R': size = MemorySize::Bit5; return true;
    case 'S': size = MemorySize::Bit6; return true;
    case 'T': size = MemorySize::Bit7; return true;
    case 'L': size = MemorySize::LowerNibble; return true;
    case 'U': size = MemorySize::UpperNibble; return true;
    case 'H': size = MemorySize::EightBit; return true;
    case 'W': size = MemorySize::TwentyFourBit; return true;
    case 'X': size = MemorySize::ThirtyTwoBit; return true;
    case 'I': size = MemorySize::SixteenBitBigEndian; return true;
    case 'J': size = MemorySize::TwentyFourBitBigEndian; return true;
    case 'G': size = MemorySize::ThirtyTwoBitBigEndian; return true;
    case 'K': size = MemorySize::BitCount; return true;
    default: return false;
    }
}

bool floatMemorySizeForPrefix(char prefix, MemorySize& size) noexcept {
    switch (prefix) {
    case 'F': size = MemorySize::Float; return true;
    case 'B': size = MemorySize::FloatBigEndian; return true;
    case 'H': size = MemorySize::Double32; return true;
    case 'I': size = MemorySize::Double32BigEndian; return true;
    case 'M': size = MemorySize::Mbf32; return true;
    case 'L': size = MemorySize::Mbf32LittleEndian; return true;
    default: return false;
    }
}

bool flagForPrefix(char prefix, ConditionFlag& flag) noexcept {
    switch (prefix) {
    case 'P': flag = ConditionFlag::PauseIf; return true;
    case 'R': flag = ConditionFlag::ResetIf; return true;
    case 'Z': flag = ConditionFlag::ResetNextIf; return true;
    case 'A': flag = ConditionFlag::AddSource; return true;
    case 'B': flag = ConditionFlag::SubSource; return true;
    case 'C': flag = ConditionFlag::AddHits; return true;
    case 'D': flag = ConditionFlag::SubHits; return true;
    case 'I': flag = ConditionFlag::AddAddress; return true;
    case 'N': flag = ConditionFlag::AndNext; return true;
    case 'O': flag = ConditionFlag::OrNext; return true;
    case 'M': flag = ConditionFlag::Measured; return true;
    case 'G': flag = ConditionFlag::MeasuredPercent; return true;
    case 'Q': flag = ConditionFlag::MeasuredIf; return true;
    case 'T': flag = ConditionFlag::Trigger; return true;
    case 'K': flag = ConditionFlag::Remember; return true;
    default: return false;
    }
}

class Parser final {
public:
    Parser(const std::string& input, const ConditionParseOptions& options) noexcept
        : input_(input), options_(options), size_(input.size()) {}

    ConditionParseResult parse() {
        if (!hasValidOptions()) return ConditionParseResult::failure(ConditionParseErrorCategory::InvalidOptions, 0U);
        if (size_ > options_.maximumInputBytes) return ConditionParseResult::failure(ConditionParseErrorCategory::InputTooLarge, options_.maximumInputBytes);
        if (size_ == 0U) return ConditionParseResult::failure(ConditionParseErrorCategory::EmptyInput, 0U);

        ConditionTrigger trigger;
        trigger.source = input_;
        if (input_[position_] == 'S') return ConditionParseResult::failure(ConditionParseErrorCategory::EmptyGroup, position_);
        if (!parseGroup(trigger.core)) return failureResult();

        while (position_ < size_) {
            if (input_[position_] != 'S') return ConditionParseResult::failure(ConditionParseErrorCategory::InvalidOperand, position_);
            if (groupCount_ >= options_.maximumGroups) return ConditionParseResult::failure(ConditionParseErrorCategory::GroupLimit, position_);
            ++position_;
            if (position_ == size_ || input_[position_] == 'S') return ConditionParseResult::failure(ConditionParseErrorCategory::EmptyGroup, position_);
            ConditionGroup group;
            if (!parseGroup(group)) return failureResult();
            trigger.alt.push_back(std::move(group));
        }
        return ConditionParseResult::success(std::move(trigger));
    }

private:
    bool hasValidOptions() const noexcept {
        return options_.maximumInputBytes > 0U && options_.maximumInputBytes <= kConditionMaximumInputBytes
            && options_.maximumGroups > 0U && options_.maximumGroups <= kConditionMaximumGroups
            && options_.maximumConditions > 0U && options_.maximumConditions <= kConditionMaximumConditions;
    }

    ConditionParseResult failureResult() const {
        return ConditionParseResult::failure(errorCategory_, errorOffset_);
    }

    bool fail(ConditionParseErrorCategory category, std::size_t offset) noexcept {
        errorCategory_ = category;
        errorOffset_ = offset;
        return false;
    }

    bool parseGroup(ConditionGroup& group) {
        const auto groupBegin = position_;
        if (groupCount_ >= options_.maximumGroups) return fail(ConditionParseErrorCategory::GroupLimit, position_);
        ++groupCount_;
        while (true) {
            if (position_ == size_ || input_[position_] == 'S') return fail(ConditionParseErrorCategory::EmptyGroup, position_);
            if (input_[position_] == '_') return fail(ConditionParseErrorCategory::EmptyCondition, position_);
            if (conditionCount_ >= options_.maximumConditions) return fail(ConditionParseErrorCategory::ConditionLimit, position_);
            Condition condition;
            if (!parseCondition(condition)) return false;
            ++conditionCount_;
            group.conditions.push_back(std::move(condition));
            if (position_ == size_ || input_[position_] == 'S') {
                group.span = SourceSpan{groupBegin, position_};
                return true;
            }
            if (input_[position_] != '_') return fail(ConditionParseErrorCategory::InvalidOperand, position_);
            ++position_;
            if (position_ == size_ || input_[position_] == '_' || input_[position_] == 'S') {
                return fail(ConditionParseErrorCategory::EmptyCondition, position_);
            }
        }
    }

    bool parseCondition(Condition& condition) {
        const auto begin = position_;
        if (position_ + 1U < size_ && input_[position_ + 1U] == ':') {
            if (!flagForPrefix(input_[position_], condition.flag)) return fail(ConditionParseErrorCategory::UnknownFlag, position_);
            condition.flagSpan = SourceSpan{position_, position_ + 1U};
            position_ += 2U;
        } else if (position_ < size_ && input_[position_] >= 'A' && input_[position_] <= 'Z') {
            return fail(ConditionParseErrorCategory::UnknownFlag, position_);
        }

        if (position_ < size_ && isWhitespace(input_[position_])) return fail(ConditionParseErrorCategory::Whitespace, position_);
        if (position_ < size_ && (input_[position_] == '=' || input_[position_] == '!' || input_[position_] == '<'
            || input_[position_] == '>' || input_[position_] == '*' || input_[position_] == '/' || input_[position_] == '+'
            || input_[position_] == '-' || input_[position_] == '%' || input_[position_] == '&' || input_[position_] == '^')) {
            return fail(ConditionParseErrorCategory::MissingOperand, position_);
        }

        if (!parseOperand(condition.left)) return false;
        if (position_ == size_ || input_[position_] == '_' || input_[position_] == 'S' || input_[position_] == '.') {
            if (!allowsMissingOperator(condition.flag)) return fail(ConditionParseErrorCategory::MissingOperator, position_);
            condition.span = SourceSpan{begin, position_};
            return parseHitTarget(condition);
        }
        if (!parseOperator(condition)) return false;
        Operand right;
        if (!parseOperand(right)) {
            if (position_ == size_ || input_[position_] == '_' || input_[position_] == 'S' || input_[position_] == '.') {
                return fail(ConditionParseErrorCategory::MissingOperand, position_);
            }
            return false;
        }
        condition.right = std::move(right);
        if (!operatorAllowed(condition.flag, condition.op)) return fail(ConditionParseErrorCategory::InvalidFlagOperator, condition.operatorSpan->begin);
        condition.span = SourceSpan{begin, position_};
        return parseHitTarget(condition);
    }

    bool parseOperand(Operand& operand) {
        const auto begin = position_;
        if (position_ == size_) return fail(ConditionParseErrorCategory::MissingOperand, position_);
        if (isWhitespace(input_[position_])) return fail(ConditionParseErrorCategory::Whitespace, position_);
        if (input_[position_] == 'd' || input_[position_] == 'p' || input_[position_] == 'b' || input_[position_] == '~') {
            const char modifier = input_[position_++];
            operand.modifierSpan = SourceSpan{begin, position_};
            switch (modifier) {
            case 'd': operand.modifier = OperandModifier::Delta; break;
            case 'p': operand.modifier = OperandModifier::Prior; break;
            case 'b': operand.modifier = OperandModifier::Bcd; break;
            default: operand.modifier = OperandModifier::Invert; break;
            }
            if (position_ == size_) return fail(ConditionParseErrorCategory::MissingOperand, position_);
        }
        if (position_ + 8U <= size_ && input_.compare(position_, 8U, "{recall}") == 0) {
            if (operand.modifier != OperandModifier::None) return fail(ConditionParseErrorCategory::InvalidOperand, begin);
            operand.kind = OperandKind::Recall;
            position_ += 8U;
            operand.span = SourceSpan{begin, position_};
            return true;
        }
        if (position_ + 2U <= size_ && input_[position_] == '0' && input_[position_ + 1U] == 'x') {
            return parseZeroXOperand(operand, begin);
        }
        if (input_[position_] == 'f') return parseFloatMemoryOperand(operand, begin);
        return parseConstant(operand, begin, false);
    }

    bool parseZeroXOperand(Operand& operand, std::size_t begin) {
        const auto prefixOffset = position_;
        position_ += 2U;
        if (position_ == size_) return fail(ConditionParseErrorCategory::InvalidConstant, position_);
        MemorySize size{};
        if (input_[position_] == ' ') {
            ++position_;
            return parseMemoryAddress(operand, begin, size = MemorySize::SixteenBit, SourceSpan{prefixOffset, position_});
        }
        if (memorySizeForPrefix(input_[position_], size)) {
            ++position_;
            return parseMemoryAddress(operand, begin, size, SourceSpan{prefixOffset, position_});
        }
        if (hexValue(input_[position_]) < 16U) {
            position_ = prefixOffset;
            return parseConstant(operand, begin, true);
        }
        return fail(ConditionParseErrorCategory::InvalidMemorySize, position_);
    }

    bool parseFloatMemoryOperand(Operand& operand, std::size_t begin) {
        const auto prefixOffset = position_;
        ++position_;
        if (position_ == size_) return fail(ConditionParseErrorCategory::InvalidMemorySize, position_);
        MemorySize size{};
        if (!floatMemorySizeForPrefix(input_[position_], size)) return fail(ConditionParseErrorCategory::InvalidMemorySize, position_);
        ++position_;
        return parseMemoryAddress(operand, begin, size, SourceSpan{prefixOffset, position_});
    }

    bool parseMemoryAddress(Operand& operand, std::size_t begin, MemorySize size, SourceSpan sizeSpan) {
        const auto addressBegin = position_;
        std::uint32_t address = 0U;
        bool hasDigit = false;
        while (position_ < size_) {
            const auto digit = hexValue(input_[position_]);
            if (digit >= 16U) break;
            hasDigit = true;
            if (address > (std::numeric_limits<std::uint32_t>::max() - digit) / 16U) return fail(ConditionParseErrorCategory::NumericOverflow, position_);
            address = address * 16U + digit;
            ++position_;
        }
        if (!hasDigit) return fail(ConditionParseErrorCategory::InvalidAddress, addressBegin);
        if (position_ < size_ && isWhitespace(input_[position_])) return fail(ConditionParseErrorCategory::Whitespace, position_);
        operand.kind = OperandKind::Memory;
        operand.memory.size = size;
        operand.memory.address = address;
        operand.memory.sizeSpan = sizeSpan;
        operand.memory.addressSpan = SourceSpan{addressBegin, position_};
        operand.span = SourceSpan{begin, position_};
        return true;
    }

    bool parseConstant(Operand& operand, std::size_t begin, bool hexadecimal) {
        if (hexadecimal) position_ += 2U;
        const auto digitsBegin = position_;
        std::uint32_t value = 0U;
        bool hasDigit = false;
        while (position_ < size_) {
            const auto digit = hexadecimal ? hexValue(input_[position_]) : (isDecimal(input_[position_]) ? static_cast<unsigned int>(input_[position_] - '0') : 10U);
            const auto base = hexadecimal ? 16U : 10U;
            if (digit >= base) break;
            hasDigit = true;
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / base) return fail(ConditionParseErrorCategory::NumericOverflow, position_);
            value = value * base + digit;
            ++position_;
        }
        if (!hasDigit) return fail(ConditionParseErrorCategory::InvalidConstant, digitsBegin);
        if (position_ < size_ && isWhitespace(input_[position_])) return fail(ConditionParseErrorCategory::Whitespace, position_);
        operand.kind = OperandKind::Constant;
        operand.constant = value;
        operand.span = SourceSpan{begin, position_};
        return true;
    }

    bool parseOperator(Condition& condition) {
        const auto begin = position_;
        if (isWhitespace(input_[position_])) return fail(ConditionParseErrorCategory::Whitespace, position_);
        switch (input_[position_++]) {
        case '=': condition.op = Operator::Equal; break;
        case '!':
            if (position_ == size_ || input_[position_++] != '=') return fail(ConditionParseErrorCategory::InvalidOperator, begin);
            condition.op = Operator::NotEqual;
            break;
        case '<':
            condition.op = Operator::Less;
            if (position_ < size_ && input_[position_] == '=') { ++position_; condition.op = Operator::LessOrEqual; }
            break;
        case '>':
            condition.op = Operator::Greater;
            if (position_ < size_ && input_[position_] == '=') { ++position_; condition.op = Operator::GreaterOrEqual; }
            break;
        case '*': condition.op = Operator::Multiply; break;
        case '/': condition.op = Operator::Divide; break;
        case '+': condition.op = Operator::Add; break;
        case '-': condition.op = Operator::Subtract; break;
        case '%': condition.op = Operator::Modulo; break;
        case '&': condition.op = Operator::BitwiseAnd; break;
        case '^': condition.op = Operator::BitwiseXor; break;
        default: return fail(ConditionParseErrorCategory::InvalidOperator, begin);
        }
        condition.operatorSpan = SourceSpan{begin, position_};
        return true;
    }

    bool parseHitTarget(Condition& condition) {
        if (position_ >= size_ || input_[position_] != '.') return true;
        const auto begin = position_++;
        const auto digitsBegin = position_;
        std::uint32_t target = 0U;
        bool hasDigit = false;
        while (position_ < size_ && isDecimal(input_[position_])) {
            hasDigit = true;
            const auto digit = static_cast<unsigned int>(input_[position_] - '0');
            if (target > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) return fail(ConditionParseErrorCategory::NumericOverflow, position_);
            target = target * 10U + digit;
            ++position_;
        }
        if (!hasDigit || position_ == size_ || input_[position_] != '.') return fail(ConditionParseErrorCategory::InvalidHitTarget, position_);
        ++position_;
        condition.hitTarget = target;
        condition.hitTargetSpan = SourceSpan{begin, position_};
        condition.span.end = position_;
        (void)digitsBegin;
        return true;
    }

    bool operatorAllowed(ConditionFlag flag, Operator op) const noexcept {
        switch (flag) {
        case ConditionFlag::AddSource:
        case ConditionFlag::SubSource:
        case ConditionFlag::Remember:
            return isValueOperator(op);
        case ConditionFlag::AddAddress:
            return isComparison(op) || isValueOperator(op);
        default:
            return isComparison(op);
        }
    }

    bool allowsMissingOperator(ConditionFlag flag) const noexcept {
        return flag == ConditionFlag::AddSource || flag == ConditionFlag::SubSource
            || flag == ConditionFlag::AddAddress || flag == ConditionFlag::Remember;
    }

    const std::string& input_;
    const ConditionParseOptions& options_;
    std::size_t size_ = 0U;
    std::size_t position_ = 0U;
    std::size_t groupCount_ = 0U;
    std::size_t conditionCount_ = 0U;
    ConditionParseErrorCategory errorCategory_ = ConditionParseErrorCategory::InvalidOperand;
    std::size_t errorOffset_ = 0U;
};

} // namespace

ConditionParseResult ConditionParser::parse(const std::string& input, const ConditionParseOptions& options) {
    return Parser(input, options).parse();
}

} // namespace gb::achievements::parser
