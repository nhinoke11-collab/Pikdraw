#pragma once

#include <string_view>

namespace pikdraw::color {

/// Supported color spaces according to Contract C4
enum class ColorSpace {
    Rgb,
    Cmyk,
    Spot,
    Grayscale
};

[[nodiscard]] constexpr std::string_view color_space_to_string(ColorSpace space) noexcept {
    switch (space) {
        case ColorSpace::Rgb: return "RGB";
        case ColorSpace::Cmyk: return "CMYK";
        case ColorSpace::Spot: return "Spot";
        case ColorSpace::Grayscale: return "Grayscale";
    }
    return "Unknown";
}

} // namespace pikdraw::color
