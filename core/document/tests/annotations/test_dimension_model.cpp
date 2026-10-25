#include "pikdraw/document/annotations/dimension_style.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/document.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace pikdraw;
using namespace pikdraw::document;

static void test_style_formatting_ac_d07() {
    DimensionStyle style = DimensionStyle::make_default();
    style.units = UnitType::Millimeter;
    style.decimals = 2;
    style.show_unit = true;
    style.suppress_trailing_zeros = true;

    // 50.0 mm
    std::string s1 = format_measurement_text(50.0, style, false);
    assert(s1 == "50 mm");

    // 50.25 mm
    std::string s2 = format_measurement_text(50.25, style, false);
    assert(s2 == "50.25 mm");

    // Exact inch conversion: 25.4 mm -> 1 in
    style.units = UnitType::Inch;
    std::string s_inch = format_measurement_text(25.4, style, false);
    assert(s_inch == "1 in");

    style.units = UnitType::Centimeter;
    assert(format_measurement_text(25.4, style, false) == "2.54 cm");
    style.units = UnitType::Meter;
    assert(format_measurement_text(1000.0, style, false) == "1 m");

    style.units = UnitType::Foot;
    assert(format_measurement_text(304.8, style, false) == "1 ft");
    style.units = UnitType::Yard;
    assert(format_measurement_text(914.4, style, false) == "1 yd");
    style.units = UnitType::Point;
    assert(format_measurement_text(25.4, style, false) == "72 pt");
    style.units = UnitType::Pica;
    assert(format_measurement_text(25.4, style, false) == "6 pc");

    // Dual units: mm with inch dual [1 in]
    style.units = UnitType::Millimeter;
    style.dual_enabled = true;
    style.dual_units = UnitType::Inch;
    style.dual_decimals = 2;
    std::string s_dual = format_measurement_text(25.4, style, false);
    assert(s_dual == "25.4 mm [1 in]");

    // Symmetric tolerance
    style.dual_enabled = false;
    style.tolerance_mode = ToleranceMode::Symmetric;
    style.tolerance_magnitude = 0.05;
    std::string s_sym = format_measurement_text(100.0, style, false);
    assert(s_sym == "100 mm ±0.05");

    // Deviation tolerance
    style.tolerance_mode = ToleranceMode::Deviation;
    style.tolerance_upper = 0.05;
    style.tolerance_lower = -0.05;
    std::string s_dev = format_measurement_text(100.0, style, false);
    assert(s_dev == "100 mm +0.05/-0.05");

    // Angular formatting: Degrees
    constexpr double kPi = 3.14159265358979323846;
    style.angular_units = AngularUnit::Degrees;
    style.angular_decimals = 1;
    std::string s_deg = format_measurement_text(kPi * 0.5, style, true);
    assert(s_deg == "90°");

    // Angular formatting: Radians
    style.angular_units = AngularUnit::Radians;
    style.angular_decimals = 2;
    std::string s_rad = format_measurement_text(kPi * 0.5, style, true);
    assert(s_rad.find("1.57 rad") != std::string::npos);
}

static void test_dimension_unit_enum_roundtrip_and_validation() {
    static_assert(static_cast<uint8_t>(UnitType::Millimeter) == 0);
    static_assert(static_cast<uint8_t>(UnitType::Centimeter) == 1);
    static_assert(static_cast<uint8_t>(UnitType::Meter) == 2);
    static_assert(static_cast<uint8_t>(UnitType::Inch) == 3);

    DimensionObject source;
    source.style.units = UnitType::Foot;
    source.style.dual_units = UnitType::Pica;
    source.style.angular_units = AngularUnit::Radians;
    const auto serialized = serialize_dimension_spec(source);
    std::string error_reason = "stale diagnostic";
    const auto restored = deserialize_dimension_spec(serialized, &error_reason);
    assert(restored.has_value());
    assert(restored->style.units == UnitType::Foot);
    assert(restored->style.dual_units == UnitType::Pica);
    assert(restored->style.angular_units == AngularUnit::Radians);
    assert(error_reason.empty());

    const auto legacy = deserialize_dimension_spec("dimension:v1");
    assert(legacy.has_value());
    assert(legacy->style.units == UnitType::Millimeter);
    assert(legacy->style.dual_units == UnitType::Inch);
    assert(legacy->style.angular_units == AngularUnit::Degrees);

    auto invalid_primary = serialized;
    const auto primary_position = invalid_primary.find(";unit=4");
    assert(primary_position != std::string::npos);
    invalid_primary.replace(primary_position, 7, ";unit=8");
    assert(!deserialize_dimension_spec(invalid_primary, &error_reason).has_value());
    assert(error_reason == "Invalid or unsupported primary dimension unit");

    auto invalid_dual = serialized;
    const auto dual_position = invalid_dual.find(";dual_u=7");
    assert(dual_position != std::string::npos);
    invalid_dual.replace(dual_position, 9, ";dual_u=8");
    assert(!deserialize_dimension_spec(invalid_dual, &error_reason).has_value());
    assert(error_reason == "Invalid or unsupported dual dimension unit");

    auto malformed_unit = serialized;
    const auto malformed_position = malformed_unit.find(";unit=4");
    assert(malformed_position != std::string::npos);
    malformed_unit.replace(malformed_position, 7, ";unit=4junk");
    assert(!deserialize_dimension_spec(malformed_unit, &error_reason).has_value());
    assert(error_reason == "Invalid or unsupported primary dimension unit");

    auto invalid_angle = serialized;
    const auto angle_position = invalid_angle.find(";ang_u=1");
    assert(angle_position != std::string::npos);
    invalid_angle.replace(angle_position, 8, ";ang_u=2");
    assert(!deserialize_dimension_spec(invalid_angle, &error_reason).has_value());
    assert(error_reason == "Invalid or unsupported angular dimension unit");
    assert(!deserialize_dimension_spec("dimension:v10;unit=4", &error_reason).has_value());
    assert(error_reason == "Unsupported dimension specification version");
}

static void test_dimension_object_and_layout() {
    Document doc("test-dim-model");

    DimensionObject dim;
    dim.id = generate_object_id();
    dim.kind = DimensionKind::Aligned;
    dim.linear_orientation = geometry::LinearOrientation::Aligned;
    dim.placement_point = {10.0, 30.0};

    SourceReference ref1;
    ref1.ref_kind = SourceRefKind::ExplicitPoint;
    ref1.explicit_point = {0.0, 0.0};

    SourceReference ref2;
    ref2.ref_kind = SourceRefKind::ExplicitPoint;
    ref2.explicit_point = {30.0, 40.0};

    dim.references = {ref1, ref2};

    auto layout = evaluate_dimension_layout(dim, doc);
    assert(layout.valid);
    assert(std::abs(layout.raw_value - 50.0) < 1e-6);
    assert(layout.lines.size() == 3); // dim line + 2 ext lines
    assert(layout.arrows.size() == 2);
    assert(layout.formatted_text == "50 mm");

    // With note template "<> (TYP)"
    dim.note_template = "<> (TYP)";
    layout = evaluate_dimension_layout(dim, doc);
    assert(layout.valid);
    assert(layout.formatted_text == "50 mm (TYP)");
}

int main() {
    std::cout << "=== Running PikDraw Dimension Model Tests ===" << std::endl;

    test_style_formatting_ac_d07();
    std::cout << "[PASS] AC-D07: DimensionStyle unit conversion, tolerances, dual units & zero suppression" << std::endl;

    test_dimension_unit_enum_roundtrip_and_validation();
    std::cout << "[PASS] Dimension unit enum roundtrip and unknown-value rejection" << std::endl;

    test_dimension_object_and_layout();
    std::cout << "[PASS] DimensionObject evaluation & note templates" << std::endl;

    std::cout << "All Dimension Model Tests passed successfully!" << std::endl;
    return 0;
}
