#include "pikdraw/automation/json_value.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pikdraw::automation {
namespace {

void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

bool valid_utf8(std::string_view value) {
    size_t i = 0;
    while (i < value.size()) {
        const auto lead = static_cast<uint8_t>(value[i]);
        if (lead <= 0x7f) { ++i; continue; }
        size_t count = 0;
        uint8_t second_min = 0x80, second_max = 0xbf;
        if (lead >= 0xc2 && lead <= 0xdf) count = 2;
        else if (lead == 0xe0) { count = 3; second_min = 0xa0; }
        else if ((lead >= 0xe1 && lead <= 0xec) || (lead >= 0xee && lead <= 0xef)) count = 3;
        else if (lead == 0xed) { count = 3; second_max = 0x9f; }
        else if (lead == 0xf0) { count = 4; second_min = 0x90; }
        else if (lead >= 0xf1 && lead <= 0xf3) count = 4;
        else if (lead == 0xf4) { count = 4; second_max = 0x8f; }
        else return false;
        if (i + count > value.size()) return false;
        const auto second = static_cast<uint8_t>(value[i + 1]);
        if (second < second_min || second > second_max) return false;
        for (size_t j = 2; j < count; ++j) {
            const auto continuation = static_cast<uint8_t>(value[i + j]);
            if (continuation < 0x80 || continuation > 0xbf) return false;
        }
        i += count;
    }
    return true;
}

class Parser {
public:
    Parser(std::string_view input, size_t max_depth, size_t max_nodes)
        : input_(input), max_depth_(max_depth), remaining_nodes_(max_nodes) {}

    bool run(JsonValue& result, std::string& error) {
        skip_ws();
        if (!parse_value(result, 0)) { error = error_; return false; }
        skip_ws();
        if (cursor_ != input_.size()) return fail("Trailing data after JSON value", error);
        return true;
    }

private:
    bool fail(std::string message, std::string& out) {
        error_ = std::move(message) + " at byte " + std::to_string(cursor_);
        out = error_;
        return false;
    }
    bool bad(std::string message) {
        error_ = std::move(message) + " at byte " + std::to_string(cursor_);
        return false;
    }
    void skip_ws() {
        while (cursor_ < input_.size() &&
               (input_[cursor_] == ' ' || input_[cursor_] == '\t' ||
                input_[cursor_] == '\r' || input_[cursor_] == '\n')) ++cursor_;
    }
    bool parse_value(JsonValue& out, size_t depth) {
        if (depth > max_depth_) return bad("JSON nesting limit exceeded");
        if (remaining_nodes_-- == 0) return bad("JSON node limit exceeded");
        skip_ws();
        if (cursor_ >= input_.size()) return bad("Unexpected end of JSON");
        switch (input_[cursor_]) {
        case 'n': if (literal("null")) { out = nullptr; return true; } break;
        case 't': if (literal("true")) { out = true; return true; } break;
        case 'f': if (literal("false")) { out = false; return true; } break;
        case '"': { std::string value; if (!parse_string(value)) return false; out = std::move(value); return true; }
        case '[': return parse_array(out, depth + 1);
        case '{': return parse_object(out, depth + 1);
        default: if (input_[cursor_] == '-' || (input_[cursor_] >= '0' && input_[cursor_] <= '9'))
                     return parse_number(out);
        }
        return bad("Invalid JSON value");
    }
    bool literal(std::string_view word) {
        if (input_.substr(cursor_, word.size()) != word) return false;
        cursor_ += word.size(); return true;
    }
    bool hex4(uint32_t& value) {
        if (cursor_ + 4 > input_.size()) return bad("Incomplete Unicode escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[cursor_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
            else return bad("Invalid Unicode escape");
        }
        return true;
    }
    bool parse_string(std::string& out) {
        if (input_[cursor_++] != '"') return bad("Expected string");
        while (cursor_ < input_.size()) {
            const auto c = static_cast<unsigned char>(input_[cursor_++]);
            if (c == '"') {
                if (!valid_utf8(out)) return bad("Invalid UTF-8 in JSON string");
                return true;
            }
            if (c < 0x20) return bad("Unescaped control character in string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (cursor_ >= input_.size()) return bad("Incomplete escape sequence");
            switch (input_[cursor_++]) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                uint32_t cp = 0; if (!hex4(cp)) return false;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (cursor_ + 2 > input_.size() || input_[cursor_] != '\\' || input_[cursor_ + 1] != 'u')
                        return bad("Unpaired high surrogate");
                    cursor_ += 2; uint32_t low = 0; if (!hex4(low)) return false;
                    if (low < 0xdc00 || low > 0xdfff) return bad("Invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) return bad("Unpaired low surrogate");
                append_utf8(out, cp); break;
            }
            default: return bad("Invalid string escape");
            }
        }
        return bad("Unterminated string");
    }
    bool parse_number(JsonValue& out) {
        const size_t begin = cursor_;
        if (input_[cursor_] == '-') ++cursor_;
        if (cursor_ >= input_.size()) return bad("Incomplete number");
        if (input_[cursor_] == '0') ++cursor_;
        else {
            if (input_[cursor_] < '1' || input_[cursor_] > '9') return bad("Invalid number");
            while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9') ++cursor_;
        }
        if (cursor_ < input_.size() && input_[cursor_] == '.') {
            ++cursor_; const auto start = cursor_;
            while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9') ++cursor_;
            if (cursor_ == start) return bad("Missing fractional digits");
        }
        if (cursor_ < input_.size() && (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
            ++cursor_; if (cursor_ < input_.size() && (input_[cursor_] == '+' || input_[cursor_] == '-')) ++cursor_;
            const auto start = cursor_;
            while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9') ++cursor_;
            if (cursor_ == start) return bad("Missing exponent digits");
        }
        double value = 0.0;
        const auto token = input_.substr(begin, cursor_ - begin);
        const auto converted = std::from_chars(token.data(), token.data() + token.size(), value);
        if (converted.ec != std::errc{} || converted.ptr != token.data() + token.size() || !std::isfinite(value))
            return bad("Number is outside the finite binary64 range");
        out = value; return true;
    }
    bool parse_array(JsonValue& out, size_t depth) {
        ++cursor_; skip_ws(); JsonValue::Array values;
        if (cursor_ < input_.size() && input_[cursor_] == ']') { ++cursor_; out = std::move(values); return true; }
        while (true) {
            JsonValue value; if (!parse_value(value, depth)) return false;
            values.push_back(std::move(value)); skip_ws();
            if (cursor_ >= input_.size()) return bad("Unterminated array");
            if (input_[cursor_++] == ']') break;
            if (input_[cursor_ - 1] != ',') return bad("Expected comma in array");
        }
        out = std::move(values); return true;
    }
    bool parse_object(JsonValue& out, size_t depth) {
        ++cursor_; skip_ws(); JsonValue::Object values;
        if (cursor_ < input_.size() && input_[cursor_] == '}') { ++cursor_; out = std::move(values); return true; }
        while (true) {
            skip_ws(); if (cursor_ >= input_.size() || input_[cursor_] != '"') return bad("Expected object key");
            std::string key; if (!parse_string(key)) return false;
            skip_ws(); if (cursor_ >= input_.size() || input_[cursor_++] != ':') return bad("Expected colon after key");
            JsonValue value; if (!parse_value(value, depth)) return false;
            if (!values.emplace(std::move(key), std::move(value)).second) return bad("Duplicate object key");
            skip_ws(); if (cursor_ >= input_.size()) return bad("Unterminated object");
            if (input_[cursor_++] == '}') break;
            if (input_[cursor_ - 1] != ',') return bad("Expected comma in object");
        }
        out = std::move(values); return true;
    }

    std::string_view input_;
    size_t cursor_{0};
    size_t max_depth_;
    size_t remaining_nodes_;
    std::string error_;
};

void serialize_string(std::string_view value, std::string& out) {
    constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 0xf]); }
            else out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
}

void serialize_value(const JsonValue& value, std::string& out) {
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) out += "null";
        else if constexpr (std::is_same_v<T, bool>) out += item ? "true" : "false";
        else if constexpr (std::is_same_v<T, double>) {
            char buffer[64]; const auto result = std::to_chars(buffer, buffer + sizeof(buffer), item);
            if (result.ec == std::errc{}) out.append(buffer, result.ptr);
            else out += "null";
        } else if constexpr (std::is_same_v<T, std::string>) serialize_string(item, out);
        else if constexpr (std::is_same_v<T, JsonValue::Array>) {
            out.push_back('['); bool first = true;
            for (const auto& element : item) { if (!first) out.push_back(','); first = false; serialize_value(element, out); }
            out.push_back(']');
        } else if constexpr (std::is_same_v<T, JsonValue::Object>) {
            out.push_back('{'); bool first = true;
            for (const auto& [key, element] : item) {
                if (!first) out.push_back(','); first = false; serialize_string(key, out); out.push_back(':'); serialize_value(element, out);
            }
            out.push_back('}');
        }
    }, value.storage());
}

} // namespace

bool JsonValue::is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(value_); }
const bool* JsonValue::boolean() const noexcept { return std::get_if<bool>(&value_); }
const double* JsonValue::number() const noexcept { return std::get_if<double>(&value_); }
const std::string* JsonValue::string() const noexcept { return std::get_if<std::string>(&value_); }
const JsonValue::Array* JsonValue::array() const noexcept { return std::get_if<Array>(&value_); }
const JsonValue::Object* JsonValue::object() const noexcept { return std::get_if<Object>(&value_); }
const JsonValue* JsonValue::get(std::string_view key) const noexcept {
    const auto* values = object(); if (!values) return nullptr;
    const auto it = values->find(key); return it == values->end() ? nullptr : &it->second;
}
std::string JsonValue::serialize() const { std::string output; serialize_value(*this, output); return output; }
bool JsonValue::parse(std::string_view input, JsonValue& output, std::string& error,
                      size_t max_depth, size_t max_nodes) {
    if (input.size() > 1024 * 1024) { error = "JSON message limit exceeded"; return false; }
    return Parser(input, max_depth, max_nodes).run(output, error);
}

} // namespace pikdraw::automation
