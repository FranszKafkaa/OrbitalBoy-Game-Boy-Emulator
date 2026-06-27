#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace orbitalboy::mcp {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json();
    Json(std::nullptr_t);
    Json(bool value);
    Json(double value);
    Json(int value);
    Json(std::size_t value);
    Json(const char* value);
    Json(std::string value);
    Json(Array value);
    Json(Object value);

    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isBool() const;
    [[nodiscard]] bool isNumber() const;
    [[nodiscard]] bool isString() const;
    [[nodiscard]] bool isArray() const;
    [[nodiscard]] bool isObject() const;

    [[nodiscard]] bool asBool(bool fallback = false) const;
    [[nodiscard]] double asNumber(double fallback = 0.0) const;
    [[nodiscard]] int asInt(int fallback = 0) const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array& asArray() const;
    [[nodiscard]] const Object& asObject() const;
    [[nodiscard]] Array& array();
    [[nodiscard]] Object& object();

    [[nodiscard]] const Json* find(const std::string& key) const;
    [[nodiscard]] Json* find(const std::string& key);
    Json& operator[](const std::string& key);

    [[nodiscard]] std::string dump(int indent = -1) const;
    static std::optional<Json> parse(const std::string& text, std::string* error = nullptr);

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_;
};

struct JsonRpcError {
    int code = 0;
    std::string message;
};

Json jsonRpcResult(const Json& id, const Json& result);
Json jsonRpcError(const Json& id, int code, const std::string& message);
Json textToolResult(const std::string& text);

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;

} // namespace orbitalboy::mcp
