#include "pikdraw/geometry/units/length_unit.hpp"

#include <cmath>

namespace pikdraw::geometry::units {

std::string_view length_unit_suffix(LengthUnit unit) noexcept {
    switch (unit) {
    case LengthUnit::Millimeter: return "mm";
    case LengthUnit::Centimeter: return "cm";
    case LengthUnit::Meter: return "m";
    case LengthUnit::Inch: return "in";
    case LengthUnit::Foot: return "ft";
    case LengthUnit::Yard: return "yd";
    case LengthUnit::Point: return "pt";
    case LengthUnit::Pica: return "pc";
    case LengthUnit::Pixel: return "px";
    }
    return "mm";
}

std::optional<LengthUnit> parse_length_unit_suffix(std::string_view suffix) noexcept {
    for (uint8_t value = 0; value <= static_cast<uint8_t>(LengthUnit::Pixel); ++value) {
        const auto unit = static_cast<LengthUnit>(value);
        if (length_unit_suffix(unit) == suffix)
            return unit;
    }
    return std::nullopt;
}

std::optional<double> millimeters_per_unit(LengthUnit unit, std::optional<double> dpi) noexcept {
    switch (unit) {
    case LengthUnit::Millimeter: return 1.0;
    case LengthUnit::Centimeter: return 10.0;
    case LengthUnit::Meter: return 1000.0;
    case LengthUnit::Inch: return 25.4;
    case LengthUnit::Foot: return 304.8;
    case LengthUnit::Yard: return 914.4;
    case LengthUnit::Point: return 25.4 / 72.0;
    case LengthUnit::Pica: return 25.4 / 6.0;
    case LengthUnit::Pixel:
        if (!dpi || !std::isfinite(*dpi) || *dpi <= 0.0)
            return std::nullopt;
        return 25.4 / *dpi;
    }
    return std::nullopt;
}

bool convert_length_to_mm(double value, LengthUnit unit, double& result_mm,
                          std::optional<double> dpi) noexcept {
    if (!std::isfinite(value))
        return false;
    const auto scale = millimeters_per_unit(unit, dpi);
    if (!scale)
        return false;
    const double converted = value * *scale;
    if (!std::isfinite(converted))
        return false;
    result_mm = converted;
    return true;
}

} // namespace pikdraw::geometry::units
