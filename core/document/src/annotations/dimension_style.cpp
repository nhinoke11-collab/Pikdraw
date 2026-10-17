#include "pikdraw/document/annotations/dimension_style.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace pikdraw::document {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kInchToMm = 25.4;

std::string format_number(double val, int decimals, DecimalSeparator sep, bool suppress_trailing_zeros) {
    if (std::abs(val) < 1e-12) val = 0.0; // avoid -0.0

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(std::max(0, decimals)) << val;
    std::string s = ss.str();

    if (suppress_trailing_zeros && decimals > 0) {
        const auto dot_pos = s.find('.');
        if (dot_pos != std::string::npos) {
            while (s.size() > dot_pos + 1 && s.back() == '0') {
                s.pop_back();
            }
            if (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
    }

    if (sep == DecimalSeparator::Comma) {
        const auto dot = s.find('.');
        if (dot != std::string::npos) s[dot] = ',';
    }

    return s;
}

double convert_linear_from_mm(double mm, UnitType unit) {
    switch (unit) {
    case UnitType::Millimeter: return mm;
    case UnitType::Centimeter: return mm / 10.0;
    case UnitType::Meter: return mm / 1000.0;
    case UnitType::Inch: return mm / kInchToMm;
    }
    return mm;
}

const char* unit_symbol(UnitType unit) {
    switch (unit) {
    case UnitType::Millimeter: return " mm";
    case UnitType::Centimeter: return " cm";
    case UnitType::Meter: return " m";
    case UnitType::Inch: return " in";
    }
    return "";
}

} // namespace

DimensionStyle DimensionStyle::make_default() {
    return DimensionStyle{};
}

std::string format_measurement_text(double raw_val, const DimensionStyle& style, bool is_angle) {
    std::string result;
    if (!style.prefix.empty()) result += style.prefix;

    if (is_angle) {
        double val = raw_val;
        std::string sym = "°";
        if (style.angular_units == AngularUnit::Degrees) {
            val = raw_val * (180.0 / kPi);
        } else {
            sym = " rad";
        }
        result += format_number(val, style.angular_decimals, style.decimal_separator, style.suppress_trailing_zeros);
        if (style.show_unit) result += sym;
    } else {
        const double primary_val = convert_linear_from_mm(raw_val, style.units);
        result += format_number(primary_val, style.decimals, style.decimal_separator, style.suppress_trailing_zeros);
        if (style.show_unit) result += unit_symbol(style.units);

        // Tolerances
        if (style.tolerance_mode == ToleranceMode::Symmetric) {
            result += " ±";
            result += format_number(style.tolerance_magnitude, style.decimals, style.decimal_separator, style.suppress_trailing_zeros);
        } else if (style.tolerance_mode == ToleranceMode::Deviation) {
            result += " +";
            result += format_number(style.tolerance_upper, style.decimals, style.decimal_separator, style.suppress_trailing_zeros);
            result += "/-";
            result += format_number(std::abs(style.tolerance_lower), style.decimals, style.decimal_separator, style.suppress_trailing_zeros);
        }

        // Dual units
        if (style.dual_enabled) {
            const double dual_val = convert_linear_from_mm(raw_val, style.dual_units);
            result += " [";
            result += format_number(dual_val, style.dual_decimals, style.decimal_separator, style.suppress_trailing_zeros);
            if (style.show_unit) result += unit_symbol(style.dual_units);
            result += "]";
        }
    }

    if (!style.suffix.empty()) result += style.suffix;
    return result;
}

} // namespace pikdraw::document
