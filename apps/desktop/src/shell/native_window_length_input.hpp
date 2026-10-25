#pragma once

#include "pikdraw/vector/precision/numeric/numeric_input.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pikdraw::desktop {

inline std::string_view display_length_unit_suffix(geometry::units::LengthUnit unit) noexcept {
    return geometry::units::length_unit_suffix(unit);
}

inline std::wstring display_length_unit_suffix_wide(geometry::units::LengthUnit unit) {
    std::wstring suffix;
    for (const char ch : display_length_unit_suffix(unit))
        suffix.push_back(static_cast<wchar_t>(ch));
    return suffix;
}

inline std::optional<double> image_source_axis_dpi(
    double dpi_x, double dpi_y, bool dpi_from_source, uint16_t exif_orientation,
    bool orientation_applied, bool horizontal_axis) noexcept {
    if (!dpi_from_source) return std::nullopt;
    const bool axes_swapped = orientation_applied && exif_orientation >= 5 && exif_orientation <= 8;
    const bool use_horizontal_dpi = horizontal_axis != axes_swapped;
    const double dpi = use_horizontal_dpi ? dpi_x : dpi_y;
    if (!std::isfinite(dpi) || dpi <= 0.0) return std::nullopt;
    return dpi;
}

inline std::optional<double> length_value_from_mm(
    double value_mm, geometry::units::LengthUnit unit) noexcept {
    if (!std::isfinite(value_mm)) return std::nullopt;
    const auto scale = geometry::units::millimeters_per_unit(unit);
    if (!scale) return std::nullopt;
    const double value = value_mm / *scale;
    if (!std::isfinite(value)) return std::nullopt;
    return value;
}

inline std::string format_mm_input(double value_mm, geometry::units::LengthUnit unit) {
    const auto value = length_value_from_mm(value_mm, unit);
    if (!value) unit = geometry::units::LengthUnit::Millimeter;
    const auto display_value = value.value_or(value_mm);
    std::ostringstream formatted;
    formatted << std::setprecision(std::numeric_limits<double>::max_digits10)
              << display_value << display_length_unit_suffix(unit);
    return formatted.str();
}

inline std::string format_mm_display_utf8(
    double value_mm, geometry::units::LengthUnit unit, int precision = 7) {
    auto value = length_value_from_mm(value_mm, unit);
    if (!value) {
        unit = geometry::units::LengthUnit::Millimeter;
        value = value_mm;
    }
    std::ostringstream formatted;
    formatted << std::setprecision(std::max(1, precision)) << *value << " "
              << display_length_unit_suffix(unit);
    return formatted.str();
}

inline std::string format_ruler_value_mm(double value_mm,
    geometry::units::LengthUnit unit, double major_step_in_unit) {
    auto value = length_value_from_mm(value_mm, unit);
    if (!value) value = value_mm;
    const int precision = !std::isfinite(major_step_in_unit) || major_step_in_unit <= 0.0
        ? 0 : std::clamp(static_cast<int>(std::ceil(-std::log10(major_step_in_unit))), 0, 6);
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(precision) << *value;
    return formatted.str();
}

inline std::wstring format_mm_display(double value_mm, geometry::units::LengthUnit unit,
                                      int precision = 7) {
    auto value = length_value_from_mm(value_mm, unit);
    if (!value) {
        unit = geometry::units::LengthUnit::Millimeter;
        value = value_mm;
    }
    std::wostringstream formatted;
    formatted << std::setprecision(std::max(1, precision)) << *value << L" "
              << display_length_unit_suffix_wide(unit);
    return formatted.str();
}

inline std::wstring format_length_pair_mm(double width_mm, double height_mm,
    geometry::units::LengthUnit unit, int precision = 4) {
    auto width = length_value_from_mm(width_mm, unit);
    auto height = length_value_from_mm(height_mm, unit);
    if (!width || !height) {
        unit = geometry::units::LengthUnit::Millimeter;
        width = width_mm;
        height = height_mm;
    }
    std::wostringstream formatted;
    formatted << std::setprecision(std::max(1, precision)) << *width << L" × " << *height
              << L" " << display_length_unit_suffix_wide(unit);
    return formatted.str();
}

inline double parse_length_input_mm(
    std::string text,
    geometry::units::LengthUnit default_unit = geometry::units::LengthUnit::Millimeter,
    std::optional<double> pixel_dpi = std::nullopt) {
    std::replace(text.begin(), text.end(), ',', '.');
    const auto parsed = vector::precision::numeric::parse_length_mm(
        text, vector::precision::numeric::ParseOptions{'.', default_unit, pixel_dpi});
    if (!parsed) throw std::invalid_argument("Invalid or unsupported length input");
    return *parsed.value;
}

struct QuickDimensionInputMm {
    std::optional<double> width_mm;
    std::optional<double> height_mm;
};

inline bool is_quick_dimension_input_character(wchar_t value) noexcept {
    return (value >= L'0' && value <= L'9') ||
           (value >= L'a' && value <= L'z') ||
           (value >= L'A' && value <= L'Z') ||
           value == L'.' || value == L',' || value == L';' || value == L'/' ||
           value == L'+' || value == L'x' || value == L'X' ||
           value == L' ' || value == L'\t' || value == L'-';
}

inline std::optional<QuickDimensionInputMm> parse_quick_dimension_input_mm(
    std::string_view input, geometry::units::LengthUnit default_unit) {
    const auto trim = [](std::string_view value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return std::string_view{};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    };
    const auto text = trim(input);
    if (text.empty()) return std::nullopt;

    const auto parse_part = [&](std::string_view part) -> std::optional<double> {
        part = trim(part);
        if (part.empty()) return std::nullopt;
        try {
            return parse_length_input_mm(std::string(part), default_unit);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };

    const auto explicit_separator = text.find_first_of(",;xX");
    if (explicit_separator == std::string_view::npos) {
        if (const auto single = parse_part(text))
            return QuickDimensionInputMm{single, std::nullopt};
    }

    const auto separator = explicit_separator == std::string_view::npos
        ? text.find_first_of(" \t\r\n") : explicit_separator;
    if (separator == std::string_view::npos) return std::nullopt;

    const auto width = parse_part(text.substr(0, separator));
    const auto height = parse_part(text.substr(separator + 1));
    if (!width && !height) return std::nullopt;
    if ((!trim(text.substr(0, separator)).empty() && !width) ||
        (!trim(text.substr(separator + 1)).empty() && !height))
        return std::nullopt;
    return QuickDimensionInputMm{width, height};
}

} // namespace pikdraw::desktop
