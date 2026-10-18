#include "pikdraw/interop/export/export_options.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>

using namespace pikdraw::interop;

namespace {

ExportOptionsSchema jpeg_schema() {
    ExportOptionsSchema schema;
    schema.adapter_id = "raster-jpeg";
    schema.profile_id = "design-jpeg";
    schema.version = 3;

    ExportOptionDefinition quality;
    quality.key = "quality";
    quality.kind = ExportOptionKind::Integer;
    quality.required = true;
    quality.minimum_integer = 1;
    quality.maximum_integer = 100;
    schema.fields.push_back(std::move(quality));

    ExportOptionDefinition dpi;
    dpi.key = "dpi";
    dpi.kind = ExportOptionKind::Number;
    dpi.default_value = ExportOptionValue{300.0};
    dpi.minimum_number = 1.0;
    dpi.maximum_number = 2400.0;
    schema.fields.push_back(std::move(dpi));

    ExportOptionDefinition progressive;
    progressive.key = "progressive";
    progressive.kind = ExportOptionKind::Boolean;
    progressive.default_value = ExportOptionValue{true};
    schema.fields.push_back(std::move(progressive));

    ExportOptionDefinition subsampling;
    subsampling.key = "subsampling";
    subsampling.kind = ExportOptionKind::String;
    subsampling.default_value = ExportOptionValue{std::string("4:2:0")};
    subsampling.allowed_strings = {"4:4:4", "4:2:2", "4:2:0"};
    schema.fields.push_back(std::move(subsampling));
    return schema;
}

void test_builds_versioned_snapshot_and_applies_defaults() {
    ExportOptionValues values;
    values.emplace("quality", int64_t{92});
    const auto built = build_export_options(jpeg_schema(), std::move(values));
    assert(built);
    assert(built.snapshot->adapter_id() == "raster-jpeg");
    assert(built.snapshot->profile_id() == "design-jpeg");
    assert(built.snapshot->schema_version() == 3);
    assert(std::get<int64_t>(*built.snapshot->find("quality")) == 92);
    assert(std::get<double>(*built.snapshot->find("dpi")) == 300.0);
    assert(std::get<bool>(*built.snapshot->find("progressive")));
    assert(std::get<std::string>(*built.snapshot->find("subsampling")) == "4:2:0");
}

void test_rejects_missing_unknown_wrong_type_and_out_of_range_values() {
    auto result = build_export_options(jpeg_schema(), {});
    assert(!result && result.error == ExportOptionsError::MissingRequiredField);

    ExportOptionValues values;
    values.emplace("quality", int64_t{92});
    values.emplace("mystery", true);
    result = build_export_options(jpeg_schema(), std::move(values));
    assert(!result && result.error == ExportOptionsError::UnknownField);

    values.clear();
    values.emplace("quality", 92.0);
    result = build_export_options(jpeg_schema(), std::move(values));
    assert(!result && result.error == ExportOptionsError::TypeMismatch);

    values.clear();
    values.emplace("quality", int64_t{101});
    result = build_export_options(jpeg_schema(), std::move(values));
    assert(!result && result.error == ExportOptionsError::ValueOutOfRange);
}

void test_rejects_non_finite_and_invalid_schema_ranges() {
    auto schema = jpeg_schema();
    ExportOptionValues values;
    values.emplace("quality", int64_t{90});
    values.emplace("dpi", std::numeric_limits<double>::infinity());
    auto result = build_export_options(schema, std::move(values));
    assert(!result && result.error == ExportOptionsError::ValueOutOfRange);

    schema.fields[0].minimum_integer = 120;
    schema.fields[0].maximum_integer = 10;
    values.clear();
    values.emplace("quality", int64_t{90});
    result = build_export_options(schema, std::move(values));
    assert(!result && result.error == ExportOptionsError::InvalidSchema);

    ExportOptionsSchema bounded;
    bounded.adapter_id = "bounded-options";
    bounded.profile_id = "test";
    ExportOptionDefinition label;
    label.key = "label";
    label.kind = ExportOptionKind::String;
    bounded.fields.push_back(label);
    values.clear();
    values.emplace("label", std::string(kMaxExportOptionStringBytes + 1, 'x'));
    result = build_export_options(bounded, std::move(values));
    assert(!result && result.error == ExportOptionsError::ValueOutOfRange);

    bounded.fields.resize(kMaxExportOptionFields + 1, label);
    result = build_export_options(bounded, {});
    assert(!result && result.error == ExportOptionsError::InvalidSchema);
}

} // namespace

int main() {
    test_builds_versioned_snapshot_and_applies_defaults();
    test_rejects_missing_unknown_wrong_type_and_out_of_range_values();
    test_rejects_non_finite_and_invalid_schema_ranges();
    std::cout << "All export options tests passed.\n";
    return 0;
}
