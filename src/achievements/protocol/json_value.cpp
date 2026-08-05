#include "gb/achievements/protocol/json_value.hpp"

#include <limits>
#include <utility>

namespace gb::achievements::protocol {

class JsonValueStorage {
public:
    std::vector<JsonValue> array;
    std::vector<JsonObjectMember> object;
};

namespace {

bool isDigit(char character) noexcept {
    return character >= '0' && character <= '9';
}

bool parseMagnitude(std::string_view digits, std::uint64_t limit, std::uint64_t& value) noexcept {
    if (digits.empty()) {
        return false;
    }
    std::uint64_t parsed = 0U;
    for (const char character : digits) {
        if (!isDigit(character)) {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (limit - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    value = parsed;
    return true;
}

} // namespace

JsonValue::JsonValue() = default;
JsonValue::~JsonValue() = default;
JsonValue::JsonValue(const JsonValue& other) = default;
JsonValue::JsonValue(JsonValue&& other) noexcept = default;
JsonValue& JsonValue::operator=(const JsonValue& other) = default;
JsonValue& JsonValue::operator=(JsonValue&& other) noexcept = default;

JsonValue::JsonValue(JsonValueType type, bool boolean, std::string scalar,
                     std::shared_ptr<JsonValueStorage> storage) noexcept
    : type_(type), boolean_(boolean), scalar_(std::move(scalar)), storage_(std::move(storage)) {
}

JsonValue JsonValue::null() {
    return {};
}

JsonValue JsonValue::boolean(bool value) {
    return {JsonValueType::Boolean, value, {}, {}};
}

JsonValue JsonValue::number(std::string lexeme) {
    return {JsonValueType::Number, false, std::move(lexeme), {}};
}

JsonValue JsonValue::stringValue(std::string value) {
    return {JsonValueType::String, false, std::move(value), {}};
}

JsonValue JsonValue::arrayValue(std::vector<JsonValue> values) {
    auto storage = std::make_shared<JsonValueStorage>();
    storage->array = std::move(values);
    return {JsonValueType::Array, false, {}, std::move(storage)};
}

JsonValue JsonValue::objectValue(std::vector<JsonObjectMember> members) {
    auto storage = std::make_shared<JsonValueStorage>();
    storage->object = std::move(members);
    return {JsonValueType::Object, false, {}, std::move(storage)};
}

JsonValueType JsonValue::type() const noexcept {
    return type_;
}

const bool* JsonValue::boolean() const noexcept {
    return type_ == JsonValueType::Boolean ? &boolean_ : nullptr;
}

const std::string* JsonValue::numberLexeme() const noexcept {
    return type_ == JsonValueType::Number ? &scalar_ : nullptr;
}

const std::string* JsonValue::string() const noexcept {
    return type_ == JsonValueType::String ? &scalar_ : nullptr;
}

const std::vector<JsonValue>* JsonValue::array() const noexcept {
    return type_ == JsonValueType::Array && storage_ ? &storage_->array : nullptr;
}

const std::vector<JsonObjectMember>* JsonValue::object() const noexcept {
    return type_ == JsonValueType::Object && storage_ ? &storage_->object : nullptr;
}

bool JsonValue::toInt64(std::int64_t& value) const noexcept {
    const auto* lexeme = numberLexeme();
    if (lexeme == nullptr || lexeme->empty()) {
        return false;
    }
    const bool negative = lexeme->front() == '-';
    const std::string_view digits = negative
        ? std::string_view(*lexeme).substr(1U)
        : std::string_view(*lexeme);
    const auto limit = negative
        ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U
        : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    std::uint64_t magnitude = 0U;
    if (!parseMagnitude(digits, limit, magnitude)) {
        return false;
    }
    if (!negative) {
        value = static_cast<std::int64_t>(magnitude);
    } else if (magnitude == limit) {
        value = std::numeric_limits<std::int64_t>::min();
    } else {
        value = -static_cast<std::int64_t>(magnitude);
    }
    return true;
}

bool JsonValue::toUint64(std::uint64_t& value) const noexcept {
    const auto* lexeme = numberLexeme();
    if (lexeme == nullptr || lexeme->empty() || lexeme->front() == '-') {
        return false;
    }
    return parseMagnitude(*lexeme, std::numeric_limits<std::uint64_t>::max(), value);
}

JsonMemberLookup JsonValue::findUniqueMember(std::string_view name) const noexcept {
    const auto* members = object();
    if (members == nullptr) {
        return {};
    }
    const JsonValue* found = nullptr;
    for (const auto& member : *members) {
        if (member.name == name) {
            if (found != nullptr) {
                return {JsonMemberLookupKind::Ambiguous, nullptr};
            }
            found = &member.value;
        }
    }
    return found == nullptr ? JsonMemberLookup{} : JsonMemberLookup{JsonMemberLookupKind::Unique, found};
}

} // namespace gb::achievements::protocol
