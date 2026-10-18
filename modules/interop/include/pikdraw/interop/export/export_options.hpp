#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace pikdraw::interop {

inline constexpr size_t kMaxExportOptionFields = 128;
inline constexpr size_t kMaxExportOptionStringBytes = 4096;

using ExportOptionValue = std::variant<bool, int64_t, double, std::string>;
using ExportOptionValues = std::map<std::string, ExportOptionValue, std::less<>>;

enum class ExportOptionKind {
    Boolean,
    Integer,
    Number,
    String
};

struct ExportOptionDefinition {
    std::string key;
    ExportOptionKind kind{ExportOptionKind::String};
    bool required{false};
    std::optional<ExportOptionValue> default_value;
    std::optional<int64_t> minimum_integer;
    std::optional<int64_t> maximum_integer;
    std::optional<double> minimum_number;
    std::optional<double> maximum_number;
    std::vector<std::string> allowed_strings;
};

struct ExportOptionsSchema {
    std::string adapter_id;
    std::string profile_id;
    uint32_t version{1};
    std::vector<ExportOptionDefinition> fields;
};

class ExportOptionsSnapshot;
struct ExportOptionsValidation;
[[nodiscard]] ExportOptionsValidation build_export_options(
    const ExportOptionsSchema& schema, ExportOptionValues values);

class ExportOptionsSnapshot final {
public:
    [[nodiscard]] const std::string& adapter_id() const noexcept { return adapter_id_; }
    [[nodiscard]] const std::string& profile_id() const noexcept { return profile_id_; }
    [[nodiscard]] uint32_t schema_version() const noexcept { return schema_version_; }
    [[nodiscard]] const ExportOptionValues& values() const noexcept { return values_; }
    [[nodiscard]] const ExportOptionValue* find(std::string_view key) const noexcept;

private:
    friend struct ExportOptionsValidation;
    friend ExportOptionsValidation build_export_options(
        const ExportOptionsSchema&, ExportOptionValues);

    ExportOptionsSnapshot(std::string adapter_id, std::string profile_id,
                          uint32_t schema_version,
                          ExportOptionValues values)
        : adapter_id_(std::move(adapter_id)), profile_id_(std::move(profile_id)),
          schema_version_(schema_version),
          values_(std::move(values)) {}

    std::string adapter_id_;
    std::string profile_id_;
    uint32_t schema_version_{0};
    ExportOptionValues values_;
};

enum class ExportOptionsError {
    None,
    InvalidSchema,
    UnknownField,
    MissingRequiredField,
    TypeMismatch,
    ValueOutOfRange
};

struct ExportOptionsValidation {
    std::shared_ptr<const ExportOptionsSnapshot> snapshot;
    ExportOptionsError error{ExportOptionsError::None};
    std::string field;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(snapshot);
    }
};

[[nodiscard]] ExportOptionsValidation build_export_options(
    const ExportOptionsSchema& schema, ExportOptionValues values);

} // namespace pikdraw::interop
