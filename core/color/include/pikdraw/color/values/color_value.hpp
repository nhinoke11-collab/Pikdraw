#pragma once

#include "pikdraw/color/values/color_space.hpp"
#include <string>
#include <variant>
#include <cmath>
#include <algorithm>

namespace pikdraw::color {

/// RGB Color with normalized components [0.0, 1.0]
struct RGBColor {
    double r{0.0};
    double g{0.0};
    double b{0.0};
    double a{1.0};

    [[nodiscard]] bool is_valid() const noexcept {
        return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a) &&
               r >= 0.0 && r <= 1.0 && g >= 0.0 && g <= 1.0 && b >= 0.0 && b <= 1.0 &&
               a >= 0.0 && a <= 1.0;
    }

    [[nodiscard]] bool approx_equal(const RGBColor& o, double eps = 1e-4) const noexcept {
        return std::abs(r - o.r) <= eps && std::abs(g - o.g) <= eps &&
               std::abs(b - o.b) <= eps && std::abs(a - o.a) <= eps;
    }
};

/// CMYK Color for print and manufacturing [0.0, 1.0] (0% to 100%)
struct CMYKColor {
    double c{0.0};
    double m{0.0};
    double y{0.0};
    double k{0.0};
    double a{1.0};

    [[nodiscard]] bool is_valid() const noexcept {
        return std::isfinite(c) && std::isfinite(m) && std::isfinite(y) && std::isfinite(k) && std::isfinite(a) &&
               c >= 0.0 && c <= 1.0 && m >= 0.0 && m <= 1.0 && y >= 0.0 && y <= 1.0 &&
               k >= 0.0 && k <= 1.0 && a >= 0.0 && a <= 1.0;
    }

    [[nodiscard]] bool approx_equal(const CMYKColor& o, double eps = 1e-4) const noexcept {
        return std::abs(c - o.c) <= eps && std::abs(m - o.m) <= eps &&
               std::abs(y - o.y) <= eps && std::abs(k - o.k) <= eps &&
               std::abs(a - o.a) <= eps;
    }
};

/// Spot color separation with name and tint
struct SpotColor {
    std::string spot_name;
    double tint{1.0}; // [0.0, 1.0]
    RGBColor alternate_rgb{1.0, 0.0, 0.0, 1.0}; // Fallback for display
    bool overprint{false};

    [[nodiscard]] bool is_valid() const noexcept {
        return !spot_name.empty() && std::isfinite(tint) && tint >= 0.0 && tint <= 1.0 && alternate_rgb.is_valid();
    }
};

/// Grayscale color [0.0, 1.0]
struct GrayscaleColor {
    double gray{0.0};
    double a{1.0};

    [[nodiscard]] bool is_valid() const noexcept {
        return std::isfinite(gray) && std::isfinite(a) && gray >= 0.0 && gray <= 1.0 && a >= 0.0 && a <= 1.0;
    }
};

/// Overprint metadata for graphic elements
struct OverprintConfig {
    bool fill{false};
    bool stroke{false};
};

/// Comprehensive typed ColorValue holding any supported source model
using ColorPayload = std::variant<RGBColor, CMYKColor, SpotColor, GrayscaleColor>;

class ColorValue {
public:
    ColorValue() : payload_(RGBColor{0.0, 0.0, 0.0, 1.0}) {}
    ColorValue(RGBColor rgb) : payload_(rgb) {}
    ColorValue(CMYKColor cmyk) : payload_(cmyk) {}
    ColorValue(SpotColor spot) : payload_(std::move(spot)) {}
    ColorValue(GrayscaleColor gray) : payload_(gray) {}

    [[nodiscard]] ColorSpace color_space() const noexcept {
        return std::visit([](const auto& c) -> ColorSpace {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, RGBColor>) return ColorSpace::Rgb;
            else if constexpr (std::is_same_v<T, CMYKColor>) return ColorSpace::Cmyk;
            else if constexpr (std::is_same_v<T, SpotColor>) return ColorSpace::Spot;
            else return ColorSpace::Grayscale;
        }, payload_);
    }

    [[nodiscard]] const ColorPayload& payload() const noexcept { return payload_; }
    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] const OverprintConfig& overprint() const noexcept { return overprint_; }
    void set_overprint(OverprintConfig op) noexcept { overprint_ = op; }

    static ColorValue make_rgb(double r, double g, double b, double a = 1.0) {
        return ColorValue{RGBColor{r, g, b, a}};
    }

    static ColorValue make_cmyk(double c, double m, double y, double k, double a = 1.0) {
        return ColorValue{CMYKColor{c, m, y, k, a}};
    }

    static ColorValue make_spot(std::string name, double tint, RGBColor alt = {1.0, 0.0, 0.0, 1.0}) {
        return ColorValue{SpotColor{std::move(name), tint, alt, false}};
    }

private:
    ColorPayload payload_;
    OverprintConfig overprint_{};
};

} // namespace pikdraw::color
