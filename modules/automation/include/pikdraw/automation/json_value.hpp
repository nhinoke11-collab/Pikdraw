#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pikdraw::automation {

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    JsonValue() : value_(nullptr) {}
    JsonValue(std::nullptr_t) : value_(nullptr) {}
    JsonValue(bool value) : value_(value) {}
    JsonValue(double value) : value_(value) {}
    JsonValue(int value) : value_(static_cast<double>(value)) {}
    JsonValue(uint64_t value) : value_(static_cast<double>(value)) {}
    JsonValue(std::string value) : value_(std::move(value)) {}
    JsonValue(const char* value) : value_(std::string(value)) {}
    JsonValue(Array value) : value_(std::move(value)) {}
    JsonValue(Object value) : value_(std::move(value)) {}

    [[nodiscard]] const Storage& storage() const noexcept { return value_; }
    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] const bool* boolean() const noexcept;
    [[nodiscard]] const double* number() const noexcept;
    [[nodiscard]] const std::string* string() const noexcept;
    [[nodiscard]] const Array* array() const noexcept;
    [[nodiscard]] const Object* object() const noexcept;
    [[nodiscard]] const JsonValue* get(std::string_view key) const noexcept;
    [[nodiscard]] std::string serialize() const;

    static bool parse(std::string_view input, JsonValue& output, std::string& error,
                      size_t max_depth = 64, size_t max_nodes = 65536);

private:
    Storage value_;
};

} // namespace pikdraw::automation
