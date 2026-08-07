#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gb::achievements::protocol {

enum class JsonValueType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

class JsonValue;
class JsonValueStorage;
struct JsonObjectMember;
struct JsonMemberLookup;

enum class JsonMemberLookupKind {
    Absent,
    Unique,
    Ambiguous,
};

class JsonValue {
public:
    JsonValue();
    ~JsonValue();
    JsonValue(const JsonValue& other);
    JsonValue(JsonValue&& other) noexcept;
    JsonValue& operator=(const JsonValue& other);
    JsonValue& operator=(JsonValue&& other) noexcept;

    [[nodiscard]] static JsonValue null();
    [[nodiscard]] static JsonValue boolean(bool value);
    [[nodiscard]] static JsonValue number(std::string lexeme);
    [[nodiscard]] static JsonValue stringValue(std::string value);
    [[nodiscard]] static JsonValue arrayValue(std::vector<JsonValue> values);
    [[nodiscard]] static JsonValue objectValue(std::vector<JsonObjectMember> members);

    [[nodiscard]] JsonValueType type() const noexcept;
    [[nodiscard]] const bool* boolean() const noexcept;
    [[nodiscard]] const std::string* numberLexeme() const noexcept;
    [[nodiscard]] const std::string* string() const noexcept;
    [[nodiscard]] const std::vector<JsonValue>* array() const noexcept;
    [[nodiscard]] const std::vector<JsonObjectMember>* object() const noexcept;
    [[nodiscard]] bool toInt64(std::int64_t& value) const noexcept;
    [[nodiscard]] bool toUint64(std::uint64_t& value) const noexcept;
    [[nodiscard]] JsonMemberLookup findUniqueMember(std::string_view name) const noexcept;

private:
    JsonValue(JsonValueType type, bool boolean, std::string scalar,
              std::shared_ptr<JsonValueStorage> storage) noexcept;

    JsonValueType type_ = JsonValueType::Null;
    bool boolean_ = false;
    std::string scalar_;
    std::shared_ptr<JsonValueStorage> storage_;
};

struct JsonObjectMember {
    std::string name;
    JsonValue value;
};

struct JsonMemberLookup {
    JsonMemberLookupKind kind = JsonMemberLookupKind::Absent;
    const JsonValue* value = nullptr;
};

} // namespace gb::achievements::protocol
