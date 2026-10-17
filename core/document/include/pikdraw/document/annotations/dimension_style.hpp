#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/geometry/dimensions/dimension_geometry.hpp"
#include "pikdraw/typography/shaping/font_identity.hpp"

#include <cstdint>
#include <string>

namespace pikdraw::document {

enum class FitMode : uint8_t {
    BestFit,
    ArrowsFirst,
    TextFirst,
    BothOutside
};

enum class UnitType : uint8_t {
    Millimeter,
    Centimeter,
    Meter,
    Inch
};

enum class DecimalSeparator : uint8_t {
    Dot,
    Comma
};

enum class AngularUnit : uint8_t {
    Degrees,
    Radians
};

enum class ToleranceMode : uint8_t {
    None,
    Symmetric,
    Deviation
};

enum class DimensionTextPosition : uint8_t {
    Above,
    Inline
};

enum class DimensionTextAlignment : uint8_t {
    Aligned,
    Horizontal
};

struct DimensionStyle {
    uint64_t style_id{1};
    std::string name{"PikDraw mm"};

    // 1. Lines and Arrows
    double line_width_mm{0.25};
    double ext_width_mm{0.25};
    double ext_offset_mm{1.5};
    double ext_overhang_mm{2.0};
    color::ColorValue line_color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    color::ColorValue ext_color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    geometry::ArrowType arrow_type{geometry::ArrowType::ClosedFilled};
    double arrow_size_mm{2.5};
    bool flip_first_arrow{false};
    bool flip_second_arrow{false};

    // 2. Text
    color::ColorValue text_color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    typography::FontIdentity font{typography::FontIdentity::make_default()};
    double text_size_mm{3.0};
    DimensionTextPosition text_position{DimensionTextPosition::Above};
    DimensionTextAlignment text_alignment{DimensionTextAlignment::Aligned};
    double text_gap_mm{0.75};
    bool text_mask{true};
    bool basic_frame{false};

    // 3. Fit
    FitMode fit_mode{FitMode::BestFit};
    double baseline_spacing_mm{8.0};

    // 4. Primary Units
    UnitType units{UnitType::Millimeter};
    int decimals{2};
    double round_increment_mm{0.0};
    DecimalSeparator decimal_separator{DecimalSeparator::Dot};
    std::string prefix;
    std::string suffix;
    bool show_unit{true};
    bool suppress_leading_zeros{false};
    bool suppress_trailing_zeros{true};
    AngularUnit angular_units{AngularUnit::Degrees};
    int angular_decimals{2};

    // 5. Dual Units
    bool dual_enabled{false};
    UnitType dual_units{UnitType::Inch};
    int dual_decimals{2};

    // 6. Tolerances
    ToleranceMode tolerance_mode{ToleranceMode::None};
    double tolerance_magnitude{0.05};
    double tolerance_upper{0.05};
    double tolerance_lower{-0.05};
    double tolerance_text_ratio{0.7};

    static DimensionStyle make_default();
};

std::string format_measurement_text(double raw_val, const DimensionStyle& style, bool is_angle = false);

} // namespace pikdraw::document
