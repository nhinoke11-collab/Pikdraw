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

    test_dimension_object_and_layout();
    std::cout << "[PASS] DimensionObject evaluation & note templates" << std::endl;

    std::cout << "All Dimension Model Tests passed successfully!" << std::endl;
    return 0;
}
