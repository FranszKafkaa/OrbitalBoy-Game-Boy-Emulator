#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <vector>

#include "gb/achievements/protocol/json_parser.hpp"

#include "../../test_framework.hpp"

namespace json_parser_allocation_failure_test {

thread_local bool failNextAllocation = false;

} // namespace json_parser_allocation_failure_test

void* operator new(std::size_t size) {
    using namespace json_parser_allocation_failure_test;
    if (failNextAllocation) {
        failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (void* storage = std::malloc(size == 0U ? 1U : size)) {
        return storage;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* storage) noexcept {
    std::free(storage);
}

void operator delete[](void* storage) noexcept {
    std::free(storage);
}

void operator delete(void* storage, std::size_t) noexcept {
    std::free(storage);
}

void operator delete[](void* storage, std::size_t) noexcept {
    std::free(storage);
}

namespace {

using gb::achievements::protocol::JsonErrorCategory;
using gb::achievements::protocol::JsonMemberLookupKind;
using gb::achievements::protocol::JsonParseOptions;
using gb::achievements::protocol::JsonParser;
using gb::achievements::protocol::JsonValue;
using gb::achievements::protocol::JsonValueType;
using gb::achievements::protocol::kJsonDefaultMaximumDecodedStringBytes;
using gb::achievements::protocol::kJsonDefaultMaximumInputBytes;
using gb::achievements::protocol::kJsonDefaultMaximumNestingDepth;
using gb::achievements::protocol::kJsonDefaultMaximumTotalValues;

JsonValue parseValue(const std::string& input) {
    const auto result = JsonParser::parse(input);
    T_REQUIRE(result.ok());
    return result.value();
}

void requireError(const std::string& input, JsonErrorCategory category) {
    const auto result = JsonParser::parse(input);
    T_REQUIRE(!result.ok());
    T_REQUIRE(result.error().category == category);
    T_REQUIRE(result.error().byteOffset <= input.size());
}

std::string zeroArray(std::size_t valueCount) {
    std::string input;
    input.reserve(valueCount == 0U ? 2U : valueCount * 2U + 1U);
    input.push_back('[');
    for (std::size_t index = 0U; index < valueCount; ++index) {
        if (index != 0U) {
            input.push_back(',');
        }
        input.push_back('0');
    }
    input.push_back(']');
    return input;
}

TEST_CASE("achievements_json_parser", "accepts_every_json_kind_as_a_top_level_value") {
    T_REQUIRE(parseValue("null").type() == JsonValueType::Null);
    const auto boolean = parseValue(" \ttrue\r\n");
    T_REQUIRE(boolean.type() == JsonValueType::Boolean);
    T_REQUIRE(*boolean.boolean() == true);
    T_EQ(*parseValue("-12.5e+2").numberLexeme(), "-12.5e+2");
    T_EQ(*parseValue("\"text\"").string(), "text");
    T_EQ(parseValue("[]").array()->size(), 0U);
    T_EQ(parseValue("{}").object()->size(), 0U);
}

TEST_CASE("achievements_json_parser", "preserves_array_object_and_duplicate_member_order") {
    const auto value = parseValue("[0,{\"first\":true,\"first\":false,\"last\":null},2]");
    const auto* array = value.array();
    T_EQ(array->size(), 3U);
    T_EQ(*array->at(0).numberLexeme(), "0");
    const auto* object = array->at(1).object();
    T_EQ(object->size(), 3U);
    T_EQ(object->at(0).name, "first");
    T_EQ(*object->at(0).value.boolean(), true);
    T_EQ(object->at(1).name, "first");
    T_EQ(*object->at(1).value.boolean(), false);
    const auto duplicate = array->at(1).findUniqueMember("first");
    T_REQUIRE(duplicate.kind == JsonMemberLookupKind::Ambiguous);
    T_REQUIRE(duplicate.value == nullptr);
    const auto unique = array->at(1).findUniqueMember("last");
    T_REQUIRE(unique.kind == JsonMemberLookupKind::Unique);
    T_REQUIRE(unique.value != nullptr && unique.value->type() == JsonValueType::Null);
    T_REQUIRE(array->at(1).findUniqueMember("missing").kind == JsonMemberLookupKind::Absent);
}

TEST_CASE("achievements_json_parser", "decodes_all_json_escapes_and_utf8_without_using_platform_conversion") {
    const auto escaped = parseValue("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0041\\uD83D\\uDE00\"");
    const std::string expected{"\"\\/\b\f\n\r\tA\xF0\x9F\x98\x80", 13U};
    T_EQ(*escaped.string(), expected);
    const auto bmp = parseValue("\"\\u20AC\"");
    const std::string bmpExpected{"\xE2\x82\xAC", 3U};
    T_EQ(*bmp.string(), bmpExpected);
    const auto direct = parseValue(std::string{"\"\xC2\xA2\xE2\x82\xAC\xF0\x90\x8D\x88\"", 11U});
    const std::string directExpected{"\xC2\xA2\xE2\x82\xAC\xF0\x90\x8D\x88", 9U};
    T_EQ(*direct.string(), directExpected);
}

TEST_CASE("achievements_json_parser", "preserves_exact_number_lexemes_and_converts_only_exact_integers") {
    for (const std::string& input : {"0", "-0", "10", "0.1", "1e+1", "1E-1"}) {
        T_EQ(*parseValue(input).numberLexeme(), input);
    }
    T_EQ(*parseValue("-9223372036854775808").numberLexeme(), "-9223372036854775808");
    T_EQ(*parseValue("18446744073709551615").numberLexeme(), "18446744073709551615");
    std::int64_t signedValue = 0;
    std::uint64_t unsignedValue = 0;
    T_REQUIRE(parseValue("-9223372036854775808").toInt64(signedValue));
    T_EQ(signedValue, std::numeric_limits<std::int64_t>::min());
    T_REQUIRE(parseValue("18446744073709551615").toUint64(unsignedValue));
    T_EQ(unsignedValue, std::numeric_limits<std::uint64_t>::max());
    T_REQUIRE(!parseValue("9223372036854775808").toInt64(signedValue));
    T_REQUIRE(!parseValue("18446744073709551616").toUint64(unsignedValue));
    T_REQUIRE(!parseValue("-1").toUint64(unsignedValue));
    T_REQUIRE(!parseValue("1.0").toInt64(signedValue));
    T_REQUIRE(!parseValue("1e0").toInt64(signedValue));
    T_REQUIRE(!parseValue("true").toInt64(signedValue));
    T_REQUIRE(!JsonValue::number("12x").toUint64(unsignedValue));
}

TEST_CASE("achievements_json_parser", "rejects_non_json_syntax_and_number_grammar") {
    for (const std::string input : {"null x", "[1,]", "[1 2]", "{\"a\" 1}", "TRUE", "//x", "'x'", "NaN", "Infinity", "+1", "01", "-01", "1.", "1e", "1e+"}) {
        requireError(input, JsonErrorCategory::Syntax);
    }
}

TEST_CASE("achievements_json_parser", "rejects_invalid_string_escapes_controls_bom_and_surrogates") {
    requireError("\"\\x\"", JsonErrorCategory::InvalidEscape);
    requireError("\"\\u12G4\"", JsonErrorCategory::InvalidEscape);
    requireError("\"\\uD800\"", JsonErrorCategory::InvalidSurrogate);
    requireError("\"\\uDC00\"", JsonErrorCategory::InvalidSurrogate);
    requireError("\"\\uD800\\u0041\"", JsonErrorCategory::InvalidSurrogate);
    requireError(std::string{"\"a\x01b\"", 5U}, JsonErrorCategory::Syntax);
    requireError(std::string{"\xEF\xBB\xBFnull", 7U}, JsonErrorCategory::InvalidUtf8);
}

TEST_CASE("achievements_json_parser", "rejects_malformed_overlong_surrogate_and_out_of_range_utf8") {
    for (const std::string& input : {
             std::string{"\"\xC2\"", 3U},
             std::string{"\"\xC0\x80\"", 4U},
             std::string{"\"\xED\xA0\x80\"", 5U},
             std::string{"\"\xF4\x90\x80\x80\"", 6U},
         }) {
        requireError(input, JsonErrorCategory::InvalidUtf8);
    }
    requireError(std::string{"\xC0\x80", 2U}, JsonErrorCategory::InvalidUtf8);
}

TEST_CASE("achievements_json_parser", "enforces_default_input_depth_value_and_string_limits") {
    const std::string fourMiB = "null" + std::string(4U * 1024U * 1024U - 4U, ' ');
    T_EQ(fourMiB.size(), 4U * 1024U * 1024U);
    T_REQUIRE(JsonParser::parse(fourMiB).ok());
    requireError(fourMiB + " ", JsonErrorCategory::InputTooLarge);

    const std::string oneMiB(kJsonDefaultMaximumDecodedStringBytes, 'a');
    T_REQUIRE(JsonParser::parse("\"" + oneMiB + "\"").ok());
    requireError("\"" + oneMiB + "a\"", JsonErrorCategory::StringLimit);

    const std::string depth64(64U, '[');
    T_REQUIRE(JsonParser::parse(depth64 + std::string("0") + std::string(64U, ']')).ok());
    requireError(std::string(65U, '[') + "0" + std::string(65U, ']'), JsonErrorCategory::DepthLimit);

    JsonParseOptions values;
    values.maximumTotalValues = 3U;
    T_REQUIRE(JsonParser::parse("[0,1]", values).ok());
    const auto tooManyValues = JsonParser::parse("[0,1,2]", values);
    T_REQUIRE(!tooManyValues.ok());
    T_REQUIRE(tooManyValues.error().category == JsonErrorCategory::ValueLimit);

    JsonParseOptions strings;
    strings.maximumDecodedStringBytes = 3U;
    T_REQUIRE(JsonParser::parse("\"abc\"", strings).ok());
    const auto tooLong = JsonParser::parse("\"abcd\"", strings);
    T_REQUIRE(!tooLong.ok());
    T_REQUIRE(tooLong.error().category == JsonErrorCategory::StringLimit);
}

TEST_CASE("achievements_json_parser", "enforces_the_default_total_value_boundary_without_rounding_or_recursion") {
    T_REQUIRE(JsonParser::parse(zeroArray(kJsonDefaultMaximumTotalValues - 1U)).ok());
    const auto overLimit = JsonParser::parse(zeroArray(kJsonDefaultMaximumTotalValues));
    T_REQUIRE(!overLimit.ok());
    T_REQUIRE(overLimit.error().category == JsonErrorCategory::ValueLimit);
}

TEST_CASE("achievements_json_parser", "counts_object_members_separately_from_their_values") {
    JsonParseOptions options;
    options.maximumTotalValues = 5U;
    T_REQUIRE(JsonParser::parse("{\"a\":0,\"b\":1}", options).ok());
    options.maximumTotalValues = 4U;
    const auto overLimit = JsonParser::parse("{\"a\":0,\"b\":1}", options);
    T_REQUIRE(!overLimit.ok());
    T_REQUIRE(overLimit.error().category == JsonErrorCategory::ValueLimit);
}

TEST_CASE("achievements_json_parser", "rejects_zero_options_and_provides_checked_type_accessors") {
    for (JsonParseOptions options : {
             JsonParseOptions{0U, 64U, 100000U, 1024U},
             JsonParseOptions{4U, 0U, 100000U, 1024U},
             JsonParseOptions{4U, 64U, 0U, 1024U},
             JsonParseOptions{4U, 64U, 100000U, 0U},
         }) {
        const auto invalid = JsonParser::parse("null", options);
        T_REQUIRE(!invalid.ok());
        T_REQUIRE(invalid.error().category == JsonErrorCategory::InvalidOptions);
    }

    const auto value = parseValue("null");
    T_REQUIRE(value.boolean() == nullptr);
    T_REQUIRE(value.numberLexeme() == nullptr);
    T_REQUIRE(value.string() == nullptr);
    T_REQUIRE(value.array() == nullptr);
    T_REQUIRE(value.object() == nullptr);
}

TEST_CASE("achievements_json_parser", "rejects_options_above_fixed_security_ceilings_before_parsing") {
    for (JsonParseOptions options : {
             JsonParseOptions{kJsonDefaultMaximumInputBytes + 1U, 64U, 100000U, 1024U},
             JsonParseOptions{4U, kJsonDefaultMaximumNestingDepth + 1U, 100000U, 1024U},
             JsonParseOptions{4U, 64U, kJsonDefaultMaximumTotalValues + 1U, 1024U},
             JsonParseOptions{4U, 64U, 100000U, kJsonDefaultMaximumDecodedStringBytes + 1U},
         }) {
        const auto invalid = JsonParser::parse("null", options);
        T_REQUIRE(!invalid.ok());
        T_REQUIRE(invalid.error().category == JsonErrorCategory::InvalidOptions);
        T_EQ(invalid.error().byteOffset, 0U);
    }

    const std::string deeplyNested = std::string(65U, '[') + "0" + std::string(65U, ']');
    const JsonParseOptions unsafeDepth{4U * 1024U * 1024U, 65U, 100000U, 1024U * 1024U};
    const auto invalidDepth = JsonParser::parse(deeplyNested, unsafeDepth);
    T_REQUIRE(!invalidDepth.ok());
    T_REQUIRE(invalidDepth.error().category == JsonErrorCategory::InvalidOptions);
    T_EQ(invalidDepth.error().byteOffset, 0U);
}

TEST_CASE("achievements_json_parser", "returns_a_resource_error_without_a_partial_value_when_allocation_fails") {
    json_parser_allocation_failure_test::failNextAllocation = true;
    const auto result = JsonParser::parse("[0]");
    T_REQUIRE(!result.ok());
    T_REQUIRE(result.error().category == JsonErrorCategory::OutOfMemory);
    T_REQUIRE(result.value().type() == JsonValueType::Null);
}

} // namespace
