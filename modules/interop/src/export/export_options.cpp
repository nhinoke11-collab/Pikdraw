#include "pikdraw/interop/export/export_options.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace pikdraw::interop {
namespace {

ExportOptionsValidation reject(ExportOptionsError error, std::string field,
                                const char* message) {
    return {nullptr, error, std::move(field), message};
}

bool valid_kind(ExportOptionKind kind) noexcept {
    switch (kind) {
        case ExportOptionKind::Boolean:
        case ExportOptionKind::Integer:
        case ExportOptionKind::Number:
        case ExportOptionKind::String:
            return true;
    }
    return false;
}

bool kind_matches(ExportOptionKind kind, const ExportOptionValue& value) noexcept {
    switch (kind) {
        case ExportOptionKind::Boolean: return std::holds_alternative<bool>(value);
        case ExportOptionKind::Integer: return std::holds_alternative<int64_t>(value);
        case ExportOptionKind::Number: return std::holds_alternative<double>(value);
        case ExportOptionKind::String: return std::holds_alternative<std::string>(value);
    }
    return false;
}

bool valid_value(const ExportOptionDefinition& field, const ExportOptionValue& value) {
    if (!kind_matches(field.kind, value)) return false;
    if (const auto* integer = std::get_if<int64_t>(&value)) {
        if (field.minimum_integer && *integer < *field.minimum_integer) return false;
        if (field.maximum_integer && *integer > *field.maximum_integer) return false;
    } else if (const auto* number = std::get_if<double>(&value)) {
        if (!std::isfinite(*number)) return false;
        if (field.minimum_number && *number < *field.minimum_number) return false;
        if (field.maximum_number && *number > *field.maximum_number) return false;
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        if (text->size() > kMaxExportOptionStringBytes) return false;
        if (!field.allowed_strings.empty() &&
            std::find(field.allowed_strings.begin(), field.allowed_strings.end(), *text) ==
                field.allowed_strings.end())
            return false;
    }
    return true;
}

bool valid_definition(const ExportOptionDefinition& field) {
    if (field.key.empty() || field.key.size() > 64 || !valid_kind(field.kind) ||
        (field.required && field.default_value))
        return false;
    if (field.kind == ExportOptionKind::Integer) {
        if (field.minimum_number || field.maximum_number || !field.allowed_strings.empty() ||
            (field.minimum_integer && field.maximum_integer &&
             *field.minimum_integer > *field.maximum_integer))
            return false;
    } else if (field.kind == ExportOptionKind::Number) {
        if (field.minimum_integer || field.maximum_integer || !field.allowed_strings.empty() ||
            (field.minimum_number && !std::isfinite(*field.minimum_number)) ||
            (field.maximum_number && !std::isfinite(*field.maximum_number)) ||
            (field.minimum_number && field.maximum_number &&
             *field.minimum_number > *field.maximum_number))
            return false;
    } else if (field.kind == ExportOptionKind::String) {
        if (field.minimum_integer || field.maximum_integer || field.minimum_number ||
            field.maximum_number)
            return false;
        std::unordered_set<std::string> allowed;
        if (field.allowed_strings.size() > 256) return false;
        for (const auto& value : field.allowed_strings)
            if (value.empty() || value.size() > 256 || !allowed.insert(value).second) return false;
    } else if (field.minimum_integer || field.maximum_integer || field.minimum_number ||
               field.maximum_number || !field.allowed_strings.empty()) {
        return false;
    }
    return !field.default_value || valid_value(field, *field.default_value);
}

} // namespace

const ExportOptionValue* ExportOptionsSnapshot::find(std::string_view key) const noexcept {
    const auto found = values_.find(key);
    return found == values_.end() ? nullptr : &found->second;
}

ExportOptionsValidation build_export_options(
    const ExportOptionsSchema& schema, ExportOptionValues values) {
    if (schema.adapter_id.empty() || schema.adapter_id.size() > 128 ||
        schema.profile_id.empty() || schema.profile_id.size() > 128 || schema.version == 0 ||
        schema.fields.size() > kMaxExportOptionFields)
        return reject(ExportOptionsError::InvalidSchema, {},
                      "An options schema needs adapter/profile IDs and a nonzero version.");

    std::unordered_map<std::string, const ExportOptionDefinition*> definitions;
    definitions.reserve(schema.fields.size());
    for (const auto& field : schema.fields) {
        if (!valid_definition(field) || !definitions.emplace(field.key, &field).second)
            return reject(ExportOptionsError::InvalidSchema, field.key,
                          "The options schema contains an invalid or duplicate field definition.");
    }
    for (const auto& [key, value] : values) {
        (void)value;
        if (!definitions.contains(key))
            return reject(ExportOptionsError::UnknownField, key,
                          "An option is not defined by this adapter schema.");
    }

    for (const auto& field : schema.fields) {
        auto found = values.find(field.key);
        if (found == values.end()) {
            if (field.required)
                return reject(ExportOptionsError::MissingRequiredField, field.key,
                              "A required export option is missing.");
            if (field.default_value) {
                found = values.emplace(field.key, *field.default_value).first;
            } else {
                continue;
            }
        }
        if (!kind_matches(field.kind, found->second))
            return reject(ExportOptionsError::TypeMismatch, field.key,
                          "The export option value has the wrong type for this profile.");
        if (!valid_value(field, found->second))
            return reject(ExportOptionsError::ValueOutOfRange, field.key,
                          "The export option value is outside the allowed profile range.");
    }

    return {std::shared_ptr<const ExportOptionsSnapshot>(new ExportOptionsSnapshot(
                schema.adapter_id, schema.profile_id, schema.version, std::move(values))),
            ExportOptionsError::None, {}, {}};
}

} // namespace pikdraw::interop
