#include "pikdraw/vector/precision/numeric/numeric_input.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace numeric = pikdraw::vector::precision::numeric;
using pikdraw::geometry::Point2D;

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

void near(double actual, double expected, double tolerance, const char* description) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, description);
}

void expect_error(numeric::ErrorCode actual, numeric::ErrorCode expected, const char* description) {
    check(actual == expected, description);
}

void quantity_vectors() {
    auto inch = numeric::parse_length_mm("1in");
    auto points = numeric::parse_length_mm("72pt");
    auto meters = numeric::parse_length_mm("1m");
    auto feet = numeric::parse_length_mm("1ft");
    auto yards = numeric::parse_length_mm("1yd");
    auto picas = numeric::parse_length_mm("6pc");
    auto eighth_inch = numeric::parse_length_mm("1/8in");
    auto mixed_fraction = numeric::parse_length_mm("1 1/2in");
    auto mixed_units = numeric::parse_length_mm("5ft+6in");
    auto centimeters = numeric::parse_length_mm("1cm");
    auto expression = numeric::parse_length_mm("2*125mm");
    auto parenthesized = numeric::parse_length_mm("(100mm+20mm)/2");
    auto bare = numeric::parse_length_mm("12.5");
    auto angle = numeric::parse_angle_degrees("1.5707963267948966rad");

    check(inch && points && meters && feet && yards && picas && eighth_inch && mixed_fraction && mixed_units &&
          centimeters && expression && parenthesized && bare && angle,
          "valid unit and expression inputs parse");
    if (inch)
        near(*inch.value, 25.4, 1e-12, "one inch converts to 25.4 mm");
    if (points)
        near(*points.value, 25.4, 1e-12, "72 points convert to one inch");
    if (meters)
        near(*meters.value, 1000.0, 1e-12, "one meter converts to 1000 mm");
    if (feet)
        near(*feet.value, 304.8, 1e-12, "one international foot converts to 304.8 mm");
    if (yards)
        near(*yards.value, 914.4, 1e-12, "one yard converts to 914.4 mm");
    if (picas)
        near(*picas.value, 25.4, 1e-12, "six picas convert to one inch");
    if (eighth_inch)
        near(*eighth_inch.value, 3.175, 1e-12, "fractional inch parses");
    if (mixed_fraction)
        near(*mixed_fraction.value, 38.1, 1e-12, "mixed number fraction parses as one and a half inches");
    if (mixed_units)
        near(*mixed_units.value, 1676.4, 1e-12, "mixed feet and inch expression parses");
    if (centimeters)
        near(*centimeters.value, 10.0, 1e-12, "one centimeter converts to 10 mm");
    if (expression)
        near(*expression.value, 250.0, 1e-12, "scalar multiplication preserves length");
    if (parenthesized)
        near(*parenthesized.value, 60.0, 1e-12, "parentheses and division evaluate by precedence");
    if (bare)
        near(*bare.value, 12.5, 1e-12, "bare length uses default millimeters");
    if (angle)
        near(*angle.value, 90.0, 1e-12, "radians convert to degrees");

    auto typed = numeric::parse_quantity("10mm/2");
    check(typed && typed.value->dimension == numeric::Dimension::Length,
          "length divided by scalar stays a length");
    auto ratio = numeric::parse_quantity("10mm/2mm");
    check(ratio && ratio.value->dimension == numeric::Dimension::Scalar,
          "length divided by length becomes a scalar");
    auto mismatch = numeric::parse_length_mm("2deg");
    expect_error(mismatch.diagnostic.code, numeric::ErrorCode::WrongDimension,
                 "angle cannot be consumed as a length");

    numeric::ParseOptions inches;
    inches.default_length_unit = pikdraw::geometry::units::LengthUnit::Inch;
    const auto bare_in_inches = numeric::parse_length_mm("2.5", inches);
    const auto explicit_mm_in_inches = numeric::parse_length_mm("25.4mm", inches);
    check(bare_in_inches && explicit_mm_in_inches, "field unit context and explicit suffix parse");
    if (bare_in_inches)
        near(*bare_in_inches.value, 63.5, 1e-12, "bare value follows its inch field context");
    if (explicit_mm_in_inches)
        near(*explicit_mm_in_inches.value, 25.4, 1e-12, "explicit suffix overrides the field context");

    const auto pixel_without_dpi = numeric::parse_length_mm("1px");
    expect_error(pixel_without_dpi.diagnostic.code, numeric::ErrorCode::MissingUnitContext,
                 "pixel length requires explicit DPI");
    numeric::ParseOptions pixels;
    pixels.pixel_dpi = 96.0;
    const auto css_pixel = numeric::parse_length_mm("96px", pixels);
    check(static_cast<bool>(css_pixel), "pixel length parses with a DPI context");
    if (css_pixel)
        near(*css_pixel.value, 25.4, 1e-12, "96 pixels at 96 DPI convert to one inch");
}

void point_vectors() {
    auto polar = numeric::evaluate_point_text("@250mm<45deg", {10.0, 20.0});
    auto cartesian = numeric::evaluate_point_text("@20;15", {10.0, 20.0});
    auto absolute = numeric::evaluate_point_text(
        "#120;80", {std::numeric_limits<double>::infinity(), 0.0},
        numeric::ConstructionFrame{std::numeric_limits<double>::quiet_NaN()});
    auto polar_in_frame =
        numeric::evaluate_point_text("@10mm<0deg", {0.0, 0.0}, numeric::ConstructionFrame{90.0});
    auto cartesian_in_frame =
        numeric::evaluate_point_text("@20;15", {10.0, 20.0}, numeric::ConstructionFrame{90.0});

    check(polar && cartesian && absolute && polar_in_frame && cartesian_in_frame,
          "relative and absolute coordinate forms evaluate");
    if (polar) {
        near(polar.value->x, 186.7766952966369, 1e-10, "polar x matches drawing vector");
        near(polar.value->y, 196.7766952966369, 1e-10, "polar y matches drawing vector");
    }
    if (cartesian) {
        near(cartesian.value->x, 30.0, 1e-12, "relative Cartesian x matches drawing vector");
        near(cartesian.value->y, 35.0, 1e-12, "relative Cartesian y matches drawing vector");
    }
    if (absolute) {
        near(absolute.value->x, 120.0, 1e-12, "absolute world x ignores anchor and frame");
        near(absolute.value->y, 80.0, 1e-12, "absolute world y ignores anchor and frame");
    }
    if (polar_in_frame) {
        near(polar_in_frame.value->x, 0.0, 1e-12, "polar local angle rotates with construction frame");
        near(polar_in_frame.value->y, 10.0, 1e-12, "polar local length remains millimeters");
    }
    if (cartesian_in_frame) {
        near(cartesian_in_frame.value->x, -5.0, 1e-12, "local Cartesian x transforms as a vector");
        near(cartesian_in_frame.value->y, 40.0, 1e-12, "local Cartesian y transforms as a vector");
    }

    auto negative_angle = numeric::evaluate_point_text("@10mm<-90deg", {0.0, 0.0});
    auto wrapped_angle = numeric::evaluate_point_text("@10mm<450deg", {0.0, 0.0});
    auto large_coordinate = numeric::evaluate_point_text("@0.001;0", {20000.0, 0.0});
    check(negative_angle && wrapped_angle && large_coordinate,
          "negative and wrapped angles and large-coordinate detail evaluate");
    if (negative_angle) {
        near(negative_angle.value->x, 0.0, 1e-12, "negative polar angle points down");
        near(negative_angle.value->y, -10.0, 1e-12, "negative polar angle preserves length");
    }
    if (wrapped_angle) {
        near(wrapped_angle.value->x, 0.0, 1e-12, "wrapped angle has normalized x");
        near(wrapped_angle.value->y, 10.0, 1e-12, "450 degrees wraps to 90 degrees");
    }
    if (large_coordinate)
        near(large_coordinate.value->x - 20000.0, 0.001, 1e-9,
             "one-micron detail remains representable at twenty-meter coordinates");

    const numeric::ParseOptions comma{','};
    auto comma_length = numeric::parse_length_mm("0,5in", comma);
    auto comma_point = numeric::evaluate_point_text("@20,5;15,25", {10.0, 20.0}, std::nullopt, comma);
    check(comma_length && comma_point, "locale comma parses decimals beside unit and pair separator");
    if (comma_length)
        near(*comma_length.value, 12.7, 1e-12, "comma decimal converts inches");
    if (comma_point) {
        near(comma_point.value->x, 30.5, 1e-12, "comma decimal relative x");
        near(comma_point.value->y, 35.25, 1e-12, "comma decimal relative y");
    }
}

void rejects_invalid_or_unsafe_inputs() {
    auto nan = numeric::parse_quantity("NaN");
    auto infinity = numeric::parse_quantity("Inf");
    auto divide_zero = numeric::parse_quantity("10mm/0");
    auto mixed_fraction_zero = numeric::parse_length_mm("1 1/0in");
    auto mixed_add = numeric::parse_quantity("10mm+2deg");
    auto mixed_multiply = numeric::parse_quantity("2mm*3cm");
    auto overflow_literal = numeric::parse_quantity("1e309mm");
    auto overflow_conversion = numeric::parse_quantity("1e308in");
    auto overflow_expression = numeric::parse_quantity("1e308mm*10");
    auto bad_unit = numeric::parse_quantity("1kg");
    auto negative_polar = numeric::parse_point_input("@-1mm<45deg");
    auto thousands_in_comma_locale = numeric::parse_quantity("1.234,5mm", {','});
    auto thousands_in_dot_locale = numeric::parse_quantity("1,234.5mm");
    auto bad_locale = numeric::parse_quantity("1mm", {';'});
    auto bad_frame = numeric::evaluate_point_text("@1;2", {0.0, 0.0},
                                                  numeric::ConstructionFrame{
                                                      std::numeric_limits<double>::infinity()});
    auto bad_anchor = numeric::evaluate_point_text("@1;2",
                                                   {std::numeric_limits<double>::quiet_NaN(), 0.0});
    numeric::ParsedPointInput injected_non_finite;
    injected_non_finite.kind = numeric::PointInputKind::RelativeCartesian;
    injected_non_finite.first = {std::numeric_limits<double>::quiet_NaN(), numeric::Dimension::Length};
    injected_non_finite.second = {0.0, numeric::Dimension::Length};
    auto bad_evaluated_value = numeric::evaluate_point_input(injected_non_finite, {0.0, 0.0});
    auto anchor_overflow = numeric::evaluate_point_text("@1e308;0", {1e308, 0.0});
    auto rotation_overflow = numeric::evaluate_point_text("@1.7e308;1.7e308", {0.0, 0.0},
                                                         numeric::ConstructionFrame{45.0});

    expect_error(nan.diagnostic.code, numeric::ErrorCode::NonFiniteValue, "NaN is rejected explicitly");
    expect_error(infinity.diagnostic.code, numeric::ErrorCode::NonFiniteValue, "infinity is rejected explicitly");
    expect_error(divide_zero.diagnostic.code, numeric::ErrorCode::DivisionByZero, "division by zero is rejected");
    expect_error(mixed_fraction_zero.diagnostic.code, numeric::ErrorCode::DivisionByZero,
                 "mixed fraction with a zero denominator is rejected");
    expect_error(mixed_add.diagnostic.code, numeric::ErrorCode::WrongDimension,
                 "addition of length and angle is rejected");
    expect_error(mixed_multiply.diagnostic.code, numeric::ErrorCode::WrongDimension,
                 "multiplication of two dimensions is rejected");
    expect_error(overflow_literal.diagnostic.code, numeric::ErrorCode::Overflow, "overflowing literal is rejected");
    expect_error(overflow_conversion.diagnostic.code, numeric::ErrorCode::Overflow,
                 "overflowing unit conversion is rejected");
    expect_error(overflow_expression.diagnostic.code, numeric::ErrorCode::Overflow,
                 "overflowing expression is rejected");
    expect_error(bad_unit.diagnostic.code, numeric::ErrorCode::UnknownUnit, "unknown units are rejected");
    expect_error(negative_polar.diagnostic.code, numeric::ErrorCode::NegativePolarLength,
                 "negative relative polar endpoint length is rejected");
    check(!thousands_in_comma_locale && !thousands_in_dot_locale,
          "thousands separators are rejected in both locales");
    expect_error(bad_locale.diagnostic.code, numeric::ErrorCode::InvalidLocale, "unsupported decimal locale is rejected");
    expect_error(bad_frame.diagnostic.code, numeric::ErrorCode::InvalidFrame,
                 "non-finite construction frame is rejected");
    expect_error(bad_anchor.diagnostic.code, numeric::ErrorCode::InvalidAnchor,
                 "non-finite relative anchor is rejected");
    expect_error(bad_evaluated_value.diagnostic.code, numeric::ErrorCode::NonFiniteValue,
                 "non-finite typed values are rejected by the evaluator");
    expect_error(anchor_overflow.diagnostic.code, numeric::ErrorCode::Overflow,
                 "relative addition that overflows world coordinates is rejected");
    expect_error(rotation_overflow.diagnostic.code, numeric::ErrorCode::Overflow,
                 "construction-frame rotation that overflows is rejected");

    const std::string too_long(numeric::max_input_bytes + 1, '1');
    auto long_input = numeric::parse_quantity(too_long);
    expect_error(long_input.diagnostic.code, numeric::ErrorCode::InputTooLong, "input byte bound is enforced");

    const std::string too_deep(33, '(');
    auto deep_input = numeric::parse_quantity(too_deep + "1" + std::string(33, ')'));
    expect_error(deep_input.diagnostic.code, numeric::ErrorCode::ExpressionTooDeep,
                 "recursive expression depth is bounded");

    const std::string near_depth = std::string(16, '-') + std::string(16, '(') + "1" + std::string(16, ')');
    const std::string mixed_depth = std::string(17, '-') + std::string(16, '(') + "1" + std::string(16, ')');
    check(static_cast<bool>(numeric::parse_quantity(near_depth)),
          "mixed unary and parenthesis depth at the boundary is accepted");
    expect_error(numeric::parse_quantity(mixed_depth).diagnostic.code, numeric::ErrorCode::ExpressionTooDeep,
                 "mixed unary and parenthesis depth beyond the boundary is rejected");

    const auto repeated_sum = [](std::size_t terms) {
        std::string expression;
        for (std::size_t index = 0; index < terms; ++index) {
            if (index != 0)
                expression += "+";
            expression += "1";
        }
        return expression;
    };
    const std::string token_boundary = "+" + repeated_sum(64);
    const std::string too_many_tokens = repeated_sum(65);
    check(static_cast<bool>(numeric::parse_quantity(token_boundary)),
          "expression at the token-count boundary is accepted");
    expect_error(numeric::parse_quantity(too_many_tokens).diagnostic.code, numeric::ErrorCode::TooManyTokens,
                 "expression beyond the token-count boundary is rejected");

    auto malformed = numeric::parse_point_input("@1;2;3");
    expect_error(malformed.diagnostic.code, numeric::ErrorCode::InvalidSyntax,
                 "extra coordinate components are rejected");
}

} // namespace

int main() {
    quantity_vectors();
    point_vectors();
    rejects_invalid_or_unsafe_inputs();
    if (failures != 0)
        std::cerr << failures << " numeric input checks failed\n";
    return failures == 0 ? 0 : 1;
}
