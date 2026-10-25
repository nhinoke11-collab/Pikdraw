#pragma once

#include <optional>
#include <cstdint>
#include <string_view>

namespace pikdraw::geometry::units {

enum class LengthUnit : uint8_t {
    Millimeter = 0,
    Centimeter = 1,
    Meter = 2,
    Inch = 3,
    Foot = 4,
    Yard = 5,
    Point = 6,
    Pica = 7,
    Pixel = 8
};

/// Document/page display units are physical units. Pixel requires an explicit
/// DPI basis and is therefore valid only in a field or imported asset context.
[[nodiscard]] constexpr bool is_physical_length_unit(LengthUnit unit) noexcept {
    return static_cast<uint8_t>(unit) <= static_cast<uint8_t>(LengthUnit::Pica);
}

/// Stable ASCII suffix used in numeric input and editor labels.
[[nodiscard]] std::string_view length_unit_suffix(LengthUnit unit) noexcept;

/// Resolves a canonical ASCII length suffix (mm/cm/m/in/ft/yd/pt/pc/px).
[[nodiscard]] std::optional<LengthUnit> parse_length_unit_suffix(
    std::string_view suffix) noexcept;

// Returns the number of millimeters represented by one unit. Pixel conversion
// requires a finite, positive source DPI; it never infers DPI from view zoom.
[[nodiscard]] std::optional<double> millimeters_per_unit(
    LengthUnit unit, std::optional<double> dpi = std::nullopt) noexcept;

[[nodiscard]] bool convert_length_to_mm(double value, LengthUnit unit, double& result_mm,
                                        std::optional<double> dpi = std::nullopt) noexcept;

} // namespace pikdraw::geometry::units
