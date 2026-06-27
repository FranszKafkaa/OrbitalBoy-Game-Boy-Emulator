#include "json_rpc.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace orbitalboy::mcp {

namespace {

const Json::Array kEmptyArray{};
const Json::Object kEmptyObject{};
const std::string kEmptyString{};

std::string escapeJson(const std::string& text) {
    std::ostringstream out;
    for (char ch : text) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(ch));
            } else {
                out << ch;
            }
            break;
        }
    }
    return out.str();
}

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    std::optional<Json> parse(std::string* error) {
        skipWs();
        auto value = parseValue(error);
        if (!value.has_value()) {
            return std::nullopt;
        }
        skipWs();
        if (pos_ != text_.size()) {
            setError(error, "trailing characters");
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<Json> parseValue(std::string* error) {
        skipWs();
        if (pos_ >= text_.size()) {
            setError(error, "unexpected end");
            return std::nullopt;
        }
        const char ch = text_[pos_];
        if (ch == 'n') return parseLiteral("null", Json(nullptr), error);
        if (ch == 't') return parseLiteral("true", Json(true), error);
        if (ch == 'f') return parseLiteral("false", Json(false), error);
        if (ch == '"') return parseString(error);
        if (ch == '[') return parseArray(error);
        if (ch == '{') return parseObject(error);
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parseNumber(error);
        setError(error, "unexpected token");
        return std::nullopt;
    }

    std::optional<Json> parseLiteral(const char* literal, Json value, std::string* error) {
        const std::string lit(literal);
        if (text_.compare(pos_, lit.size(), lit) != 0) {
            setError(error, "invalid literal");
            return std::nullopt;
        }
        pos_ += lit.size();
        return value;
    }

    std::optional<Json> parseString(std::string* error) {
        if (text_[pos_] != '"') {
            setError(error, "expected string");
            return std::nullopt;
        }
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            char ch = text_[pos_++];
            if (ch == '"') {
                return Json(out);
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (pos_ >= text_.size()) {
                setError(error, "bad escape");
                return std::nullopt;
            }
            const char esc = text_[pos_++];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                if (pos_ + 4 > text_.size()) {
                    setError(error, "bad unicode escape");
                    return std::nullopt;
                }
                pos_ += 4;
                out.push_back('?');
                break;
            default:
                setError(error, "bad escape");
                return std::nullopt;
            }
        }
        setError(error, "unterminated string");
        return std::nullopt;
    }

    std::optional<Json> parseNumber(std::string* error) {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        try {
            return Json(std::stod(text_.substr(start, pos_ - start)));
        } catch (...) {
            setError(error, "invalid number");
            return std::nullopt;
        }
    }

    std::optional<Json> parseArray(std::string* error) {
        ++pos_;
        Json::Array arr;
        skipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return Json(arr);
        }
        while (true) {
            auto value = parseValue(error);
            if (!value.has_value()) return std::nullopt;
            arr.push_back(*value);
            skipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return Json(arr);
            }
            setError(error, "expected array separator");
            return std::nullopt;
        }
    }

    std::optional<Json> parseObject(std::string* error) {
        ++pos_;
        Json::Object obj;
        skipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return Json(obj);
        }
        while (true) {
            auto key = parseString(error);
            if (!key.has_value()) return std::nullopt;
            skipWs();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                setError(error, "expected object colon");
                return std::nullopt;
            }
            ++pos_;
            auto value = parseValue(error);
            if (!value.has_value()) return std::nullopt;
            obj[key->asString()] = *value;
            skipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                skipWs();
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return Json(obj);
            }
            setError(error, "expected object separator");
            return std::nullopt;
        }
    }

    void skipWs() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    static void setError(std::string* error, const std::string& message) {
        if (error) *error = message;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

void dumpJson(const Json& value, std::ostringstream& out, int indent, int depth) {
    const auto newline = [&]() {
        if (indent >= 0) out << '\n' << std::string(static_cast<std::size_t>((depth + 1) * indent), ' ');
    };
    if (value.isNull()) {
        out << "null";
    } else if (value.isBool()) {
        out << (value.asBool() ? "true" : "false");
    } else if (value.isNumber()) {
        const double n = value.asNumber();
        if (std::floor(n) == n) out << static_cast<long long>(n);
        else out << n;
    } else if (value.isString()) {
        out << '"' << escapeJson(value.asString()) << '"';
    } else if (value.isArray()) {
        out << '[';
        const auto& arr = value.asArray();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i != 0) out << ',';
            newline();
            dumpJson(arr[i], out, indent, depth + 1);
        }
        if (!arr.empty() && indent >= 0) out << '\n' << std::string(static_cast<std::size_t>(depth * indent), ' ');
        out << ']';
    } else {
        out << '{';
        const auto& obj = value.asObject();
        std::size_t i = 0;
        for (const auto& entry : obj) {
            if (i++ != 0) out << ',';
            newline();
            out << '"' << escapeJson(entry.first) << '"' << (indent >= 0 ? ": " : ":");
            dumpJson(entry.second, out, indent, depth + 1);
        }
        if (!obj.empty() && indent >= 0) out << '\n' << std::string(static_cast<std::size_t>(depth * indent), ' ');
        out << '}';
    }
}

} // namespace

Json::Json() : value_(nullptr) {}
Json::Json(std::nullptr_t) : value_(nullptr) {}
Json::Json(bool value) : value_(value) {}
Json::Json(double value) : value_(value) {}
Json::Json(int value) : value_(static_cast<double>(value)) {}
Json::Json(std::size_t value) : value_(static_cast<double>(value)) {}
Json::Json(const char* value) : value_(std::string(value)) {}
Json::Json(std::string value) : value_(std::move(value)) {}
Json::Json(Array value) : value_(std::move(value)) {}
Json::Json(Object value) : value_(std::move(value)) {}

bool Json::isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
bool Json::isBool() const { return std::holds_alternative<bool>(value_); }
bool Json::isNumber() const { return std::holds_alternative<double>(value_); }
bool Json::isString() const { return std::holds_alternative<std::string>(value_); }
bool Json::isArray() const { return std::holds_alternative<Array>(value_); }
bool Json::isObject() const { return std::holds_alternative<Object>(value_); }
bool Json::asBool(bool fallback) const { return isBool() ? std::get<bool>(value_) : fallback; }
double Json::asNumber(double fallback) const { return isNumber() ? std::get<double>(value_) : fallback; }
int Json::asInt(int fallback) const { return isNumber() ? static_cast<int>(std::get<double>(value_)) : fallback; }
const std::string& Json::asString() const { return isString() ? std::get<std::string>(value_) : kEmptyString; }
const Json::Array& Json::asArray() const { return isArray() ? std::get<Array>(value_) : kEmptyArray; }
const Json::Object& Json::asObject() const { return isObject() ? std::get<Object>(value_) : kEmptyObject; }
Json::Array& Json::array() { if (!isArray()) value_ = Array{}; return std::get<Array>(value_); }
Json::Object& Json::object() { if (!isObject()) value_ = Object{}; return std::get<Object>(value_); }

const Json* Json::find(const std::string& key) const {
    if (!isObject()) return nullptr;
    const auto& obj = std::get<Object>(value_);
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

Json* Json::find(const std::string& key) {
    if (!isObject()) return nullptr;
    auto& obj = std::get<Object>(value_);
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

Json& Json::operator[](const std::string& key) {
    return object()[key];
}

std::string Json::dump(int indent) const {
    std::ostringstream out;
    dumpJson(*this, out, indent, 0);
    return out.str();
}

std::optional<Json> Json::parse(const std::string& text, std::string* error) {
    return Parser(text).parse(error);
}

Json jsonRpcResult(const Json& id, const Json& result) {
    Json::Object obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = id;
    obj["result"] = result;
    return Json(obj);
}

Json jsonRpcError(const Json& id, int code, const std::string& message) {
    Json::Object err;
    err["code"] = code;
    err["message"] = message;
    Json::Object obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = id;
    obj["error"] = Json(err);
    return Json(obj);
}

Json textToolResult(const std::string& text) {
    Json::Object item;
    item["type"] = "text";
    item["text"] = text;
    Json::Object result;
    result["content"] = Json::Array{Json(item)};
    return Json(result);
}

} // namespace orbitalboy::mcp
