#include "gb/achievements/protocol/json_parser.hpp"

#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace gb::achievements::protocol {
namespace {

bool isWhitespace(std::uint8_t byte) noexcept {
    return byte == 0x20U || byte == 0x09U || byte == 0x0AU || byte == 0x0DU;
}

bool isDigit(std::uint8_t byte) noexcept {
    return byte >= static_cast<std::uint8_t>('0') && byte <= static_cast<std::uint8_t>('9');
}

bool isHex(std::uint8_t byte) noexcept {
    return (byte >= static_cast<std::uint8_t>('0') && byte <= static_cast<std::uint8_t>('9'))
        || (byte >= static_cast<std::uint8_t>('a') && byte <= static_cast<std::uint8_t>('f'))
        || (byte >= static_cast<std::uint8_t>('A') && byte <= static_cast<std::uint8_t>('F'));
}

std::uint32_t hexValue(std::uint8_t byte) noexcept {
    if (byte >= static_cast<std::uint8_t>('0') && byte <= static_cast<std::uint8_t>('9')) {
        return byte - static_cast<std::uint8_t>('0');
    }
    if (byte >= static_cast<std::uint8_t>('a') && byte <= static_cast<std::uint8_t>('f')) {
        return 10U + byte - static_cast<std::uint8_t>('a');
    }
    return 10U + byte - static_cast<std::uint8_t>('A');
}

bool validateUtf8(const std::uint8_t* input, std::size_t size, std::size_t& errorOffset) noexcept {
    for (std::size_t position = 0U; position < size;) {
        const auto first = input[position];
        if (first < 0x80U) {
            ++position;
            continue;
        }
        std::size_t length = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
        } else {
            errorOffset = position;
            return false;
        }
        if (size - position < length) {
            errorOffset = position;
            return false;
        }
        const auto second = input[position + 1U];
        if ((first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second > 0x9FU)
            || (first == 0xF0U && second < 0x90U) || (first == 0xF4U && second > 0x8FU)) {
            errorOffset = position;
            return false;
        }
        for (std::size_t index = 1U; index < length; ++index) {
            if ((input[position + index] & 0xC0U) != 0x80U) {
                errorOffset = position + index;
                return false;
            }
        }
        position += length;
    }
    return true;
}

class Reader {
public:
    Reader(const std::uint8_t* input, std::size_t size, const JsonParseOptions& options) noexcept
        : input_(input), size_(size), options_(options) {
    }

    JsonParseResult run() {
        if (input_ == nullptr && size_ != 0U) {
            return JsonParseResult::failure(JsonErrorCategory::Syntax, 0U);
        }
        if (size_ >= 3U && input_[0] == 0xEFU && input_[1] == 0xBBU && input_[2] == 0xBFU) {
            return JsonParseResult::failure(JsonErrorCategory::InvalidUtf8, 0U);
        }
        std::size_t utf8ErrorOffset = 0U;
        if (!validateUtf8(input_, size_, utf8ErrorOffset)) {
            return JsonParseResult::failure(JsonErrorCategory::InvalidUtf8, utf8ErrorOffset);
        }
        JsonValue value;
        if (!parseValue(value, 0U)) {
            return JsonParseResult::failure(error_, errorOffset_);
        }
        skipWhitespace();
        if (position_ != size_) {
            return JsonParseResult::failure(JsonErrorCategory::Syntax, position_);
        }
        return JsonParseResult::success(std::move(value));
    }

private:
    bool fail(JsonErrorCategory category, std::size_t offset) noexcept {
        error_ = category;
        errorOffset_ = offset;
        return false;
    }

    void skipWhitespace() noexcept {
        while (position_ < size_ && isWhitespace(input_[position_])) {
            ++position_;
        }
    }

    bool takeSlot() noexcept {
        if (valueCount_ >= options_.maximumTotalValues) {
            return fail(JsonErrorCategory::ValueLimit, position_);
        }
        ++valueCount_;
        return true;
    }

    bool parseValue(JsonValue& value, std::size_t depth) {
        skipWhitespace();
        if (position_ == size_) {
            return fail(JsonErrorCategory::Syntax, position_);
        }
        if (!takeSlot()) {
            return false;
        }
        switch (input_[position_]) {
        case static_cast<std::uint8_t>('n'):
            return parseLiteral("null", JsonValue::null(), value);
        case static_cast<std::uint8_t>('t'):
            return parseLiteral("true", JsonValue::boolean(true), value);
        case static_cast<std::uint8_t>('f'):
            return parseLiteral("false", JsonValue::boolean(false), value);
        case static_cast<std::uint8_t>('"'):
            return parseStringValue(value);
        case static_cast<std::uint8_t>('['):
            if (depth >= options_.maximumNestingDepth) {
                return fail(JsonErrorCategory::DepthLimit, position_);
            }
            return parseArray(value, depth + 1U);
        case static_cast<std::uint8_t>('{'):
            if (depth >= options_.maximumNestingDepth) {
                return fail(JsonErrorCategory::DepthLimit, position_);
            }
            return parseObject(value, depth + 1U);
        default:
            if (input_[position_] == static_cast<std::uint8_t>('-') || isDigit(input_[position_])) {
                return parseNumber(value);
            }
            return fail(JsonErrorCategory::Syntax, position_);
        }
    }

    bool parseLiteral(const char* literal, JsonValue literalValue, JsonValue& value) noexcept {
        std::size_t length = 0U;
        while (literal[length] != '\0') {
            ++length;
        }
        if (size_ - position_ < length) {
            return fail(JsonErrorCategory::Syntax, position_);
        }
        for (std::size_t index = 0U; index < length; ++index) {
            if (input_[position_ + index] != static_cast<std::uint8_t>(literal[index])) {
                return fail(JsonErrorCategory::Syntax, position_ + index);
            }
        }
        position_ += length;
        value = std::move(literalValue);
        return true;
    }

    bool append(std::string& output, const char* bytes, std::size_t count) {
        if (count > options_.maximumDecodedStringBytes - output.size()) {
            return fail(JsonErrorCategory::StringLimit, position_);
        }
        output.append(bytes, count);
        return true;
    }

    bool appendCodePoint(std::string& output, std::uint32_t codePoint) {
        char bytes[4]{};
        std::size_t count = 0U;
        if (codePoint <= 0x7FU) {
            bytes[count++] = static_cast<char>(codePoint);
        } else if (codePoint <= 0x7FFU) {
            bytes[count++] = static_cast<char>(0xC0U | (codePoint >> 6U));
            bytes[count++] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        } else if (codePoint <= 0xFFFFU) {
            bytes[count++] = static_cast<char>(0xE0U | (codePoint >> 12U));
            bytes[count++] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
            bytes[count++] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        } else {
            bytes[count++] = static_cast<char>(0xF0U | (codePoint >> 18U));
            bytes[count++] = static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU));
            bytes[count++] = static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
            bytes[count++] = static_cast<char>(0x80U | (codePoint & 0x3FU));
        }
        return append(output, bytes, count);
    }

    bool parseHexEscape(std::uint32_t& value) noexcept {
        if (size_ - position_ < 4U) {
            return fail(JsonErrorCategory::InvalidEscape, position_);
        }
        value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const auto byte = input_[position_ + index];
            if (!isHex(byte)) {
                return fail(JsonErrorCategory::InvalidEscape, position_ + index);
            }
            value = (value << 4U) | hexValue(byte);
        }
        position_ += 4U;
        return true;
    }

    bool appendUtf8Sequence(std::string& output) {
        const auto begin = position_;
        const auto first = input_[position_++];
        std::size_t length = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
        } else {
            return fail(JsonErrorCategory::InvalidUtf8, begin);
        }
        if (size_ - begin < length) {
            return fail(JsonErrorCategory::InvalidUtf8, begin);
        }
        const auto second = input_[begin + 1U];
        if ((first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second > 0x9FU)
            || (first == 0xF0U && second < 0x90U) || (first == 0xF4U && second > 0x8FU)) {
            return fail(JsonErrorCategory::InvalidUtf8, begin);
        }
        for (std::size_t index = 1U; index < length; ++index) {
            if ((input_[begin + index] & 0xC0U) != 0x80U) {
                return fail(JsonErrorCategory::InvalidUtf8, begin + index);
            }
        }
        position_ = begin + length;
        return append(output, reinterpret_cast<const char*>(input_ + begin), length);
    }

    bool parseString(std::string& output) {
        ++position_;
        while (position_ < size_) {
            const auto byte = input_[position_++];
            if (byte == static_cast<std::uint8_t>('"')) {
                return true;
            }
            if (byte < 0x20U) {
                return fail(JsonErrorCategory::Syntax, position_ - 1U);
            }
            if (byte != static_cast<std::uint8_t>('\\')) {
                if (byte < 0x80U) {
                    const char character = static_cast<char>(byte);
                    if (!append(output, &character, 1U)) {
                        return false;
                    }
                } else {
                    --position_;
                    if (!appendUtf8Sequence(output)) {
                        return false;
                    }
                }
                continue;
            }
            if (position_ == size_) {
                return fail(JsonErrorCategory::InvalidEscape, position_ - 1U);
            }
            const auto escape = input_[position_++];
            switch (escape) {
            case static_cast<std::uint8_t>('"'):
            case static_cast<std::uint8_t>('\\'):
            case static_cast<std::uint8_t>('/'):
                if (!append(output, reinterpret_cast<const char*>(&escape), 1U)) return false;
                break;
            case static_cast<std::uint8_t>('b'): { const char c = '\b'; if (!append(output, &c, 1U)) return false; break; }
            case static_cast<std::uint8_t>('f'): { const char c = '\f'; if (!append(output, &c, 1U)) return false; break; }
            case static_cast<std::uint8_t>('n'): { const char c = '\n'; if (!append(output, &c, 1U)) return false; break; }
            case static_cast<std::uint8_t>('r'): { const char c = '\r'; if (!append(output, &c, 1U)) return false; break; }
            case static_cast<std::uint8_t>('t'): { const char c = '\t'; if (!append(output, &c, 1U)) return false; break; }
            case static_cast<std::uint8_t>('u'): {
                std::uint32_t codePoint = 0U;
                if (!parseHexEscape(codePoint)) return false;
                if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
                    if (size_ - position_ < 6U || input_[position_] != static_cast<std::uint8_t>('\\')
                        || input_[position_ + 1U] != static_cast<std::uint8_t>('u')) {
                        return fail(JsonErrorCategory::InvalidSurrogate, position_);
                    }
                    position_ += 2U;
                    std::uint32_t low = 0U;
                    if (!parseHexEscape(low)) return false;
                    if (low < 0xDC00U || low > 0xDFFFU) return fail(JsonErrorCategory::InvalidSurrogate, position_ - 4U);
                    codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) + (low - 0xDC00U);
                } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
                    return fail(JsonErrorCategory::InvalidSurrogate, position_ - 4U);
                }
                if (!appendCodePoint(output, codePoint)) return false;
                break;
            }
            default:
                return fail(JsonErrorCategory::InvalidEscape, position_ - 1U);
            }
        }
        return fail(JsonErrorCategory::Syntax, position_);
    }

    bool parseStringValue(JsonValue& value) {
        std::string string;
        if (!parseString(string)) return false;
        value = JsonValue::stringValue(std::move(string));
        return true;
    }

    bool parseNumber(JsonValue& value) {
        const auto begin = position_;
        if (input_[position_] == static_cast<std::uint8_t>('-')) {
            ++position_;
            if (position_ == size_) return fail(JsonErrorCategory::Syntax, position_);
        }
        if (position_ == size_ || !isDigit(input_[position_])) return fail(JsonErrorCategory::Syntax, position_);
        if (input_[position_] == static_cast<std::uint8_t>('0')) {
            ++position_;
            if (position_ < size_ && isDigit(input_[position_])) return fail(JsonErrorCategory::Syntax, position_);
        } else {
            do { ++position_; } while (position_ < size_ && isDigit(input_[position_]));
        }
        if (position_ < size_ && input_[position_] == static_cast<std::uint8_t>('.')) {
            ++position_;
            if (position_ == size_ || !isDigit(input_[position_])) return fail(JsonErrorCategory::Syntax, position_);
            do { ++position_; } while (position_ < size_ && isDigit(input_[position_]));
        }
        if (position_ < size_ && (input_[position_] == static_cast<std::uint8_t>('e') || input_[position_] == static_cast<std::uint8_t>('E'))) {
            ++position_;
            if (position_ < size_ && (input_[position_] == static_cast<std::uint8_t>('+') || input_[position_] == static_cast<std::uint8_t>('-'))) ++position_;
            if (position_ == size_ || !isDigit(input_[position_])) return fail(JsonErrorCategory::Syntax, position_);
            do { ++position_; } while (position_ < size_ && isDigit(input_[position_]));
        }
        value = JsonValue::number(std::string(reinterpret_cast<const char*>(input_ + begin), position_ - begin));
        return true;
    }

    bool parseArray(JsonValue& value, std::size_t depth) {
        ++position_;
        skipWhitespace();
        std::vector<JsonValue> values;
        if (position_ < size_ && input_[position_] == static_cast<std::uint8_t>(']')) {
            ++position_;
            value = JsonValue::arrayValue(std::move(values));
            return true;
        }
        for (;;) {
            JsonValue element;
            if (!parseValue(element, depth)) return false;
            values.push_back(std::move(element));
            skipWhitespace();
            if (position_ == size_) return fail(JsonErrorCategory::Syntax, position_);
            if (input_[position_] == static_cast<std::uint8_t>(']')) {
                ++position_;
                value = JsonValue::arrayValue(std::move(values));
                return true;
            }
            if (input_[position_] != static_cast<std::uint8_t>(',')) return fail(JsonErrorCategory::Syntax, position_);
            ++position_;
            skipWhitespace();
            if (position_ < size_ && input_[position_] == static_cast<std::uint8_t>(']')) return fail(JsonErrorCategory::Syntax, position_);
        }
    }

    bool parseObject(JsonValue& value, std::size_t depth) {
        ++position_;
        skipWhitespace();
        std::vector<JsonObjectMember> members;
        if (position_ < size_ && input_[position_] == static_cast<std::uint8_t>('}')) {
            ++position_;
            value = JsonValue::objectValue(std::move(members));
            return true;
        }
        for (;;) {
            if (position_ == size_ || input_[position_] != static_cast<std::uint8_t>('"')) return fail(JsonErrorCategory::Syntax, position_);
            std::string name;
            if (!parseString(name)) return false;
            skipWhitespace();
            if (position_ == size_ || input_[position_] != static_cast<std::uint8_t>(':')) return fail(JsonErrorCategory::Syntax, position_);
            ++position_;
            skipWhitespace();
            if (!takeSlot()) return false;
            JsonValue memberValue;
            if (!parseValue(memberValue, depth)) return false;
            members.push_back({std::move(name), std::move(memberValue)});
            skipWhitespace();
            if (position_ == size_) return fail(JsonErrorCategory::Syntax, position_);
            if (input_[position_] == static_cast<std::uint8_t>('}')) {
                ++position_;
                value = JsonValue::objectValue(std::move(members));
                return true;
            }
            if (input_[position_] != static_cast<std::uint8_t>(',')) return fail(JsonErrorCategory::Syntax, position_);
            ++position_;
            skipWhitespace();
            if (position_ < size_ && input_[position_] == static_cast<std::uint8_t>('}')) return fail(JsonErrorCategory::Syntax, position_);
        }
    }

    const std::uint8_t* input_ = nullptr;
    std::size_t size_ = 0U;
    const JsonParseOptions& options_;
    std::size_t position_ = 0U;
    std::size_t valueCount_ = 0U;
    JsonErrorCategory error_ = JsonErrorCategory::Syntax;
    std::size_t errorOffset_ = 0U;
};

bool optionsAreValid(const JsonParseOptions& options) noexcept {
    return options.maximumInputBytes != 0U && options.maximumNestingDepth != 0U
        && options.maximumTotalValues != 0U && options.maximumDecodedStringBytes != 0U;
}

} // namespace

bool JsonParseResult::ok() const noexcept { return error_.category == JsonErrorCategory::None; }
const JsonValue& JsonParseResult::value() const noexcept { return value_; }
const JsonParseError& JsonParseResult::error() const noexcept { return error_; }
JsonParseResult JsonParseResult::success(JsonValue value) noexcept {
    JsonParseResult result;
    result.value_ = std::move(value);
    return result;
}
JsonParseResult JsonParseResult::failure(JsonErrorCategory category, std::size_t byteOffset) noexcept {
    JsonParseResult result;
    result.error_ = {category, byteOffset};
    return result;
}

JsonParseResult JsonParser::parse(std::string_view input, const JsonParseOptions& options) noexcept {
    return parse(reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), options);
}

JsonParseResult JsonParser::parse(const std::uint8_t* input, std::size_t size, const JsonParseOptions& options) noexcept {
    if (!optionsAreValid(options)) return JsonParseResult::failure(JsonErrorCategory::InvalidOptions, 0U);
    if (size > options.maximumInputBytes) return JsonParseResult::failure(JsonErrorCategory::InputTooLarge, 0U);
    try {
        return Reader(input, size, options).run();
    } catch (const std::bad_alloc&) {
        return JsonParseResult::failure(JsonErrorCategory::OutOfMemory, 0U);
    } catch (...) {
        return JsonParseResult::failure(JsonErrorCategory::OutOfMemory, 0U);
    }
}

} // namespace gb::achievements::protocol
