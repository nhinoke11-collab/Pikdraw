#pragma once
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/desktop/shell/native_style_color_model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace pikdraw::desktop::style_dialog_math {

enum class ViewerMode : int { Hsb, Hsl, Rgb, CMYK };

struct Hsv {
    double h{0.0};
    double s{0.0};
    double v{0.0};
};

struct Hsl {
    double h{0.0};
    double s{0.0};
    double l{0.0};
};

inline bool is_hsl(ViewerMode mode) noexcept { return mode == ViewerMode::Hsl; }
inline bool uses_picker(source_color::Model model) noexcept {
    return model == source_color::Model::Rgb || model == source_color::Model::Cmyk;
}

inline color::RGBColor hsv_to_rgb(double hue, double saturation, double value, double alpha = 1.0) {
    hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    saturation = std::clamp(saturation, 0.0, 1.0);
    value = std::clamp(value, 0.0, 1.0);
    const double chroma = value * saturation;
    const double sector = hue / 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if (sector < 1) { r = chroma; g = x; }
    else if (sector < 2) { r = x; g = chroma; }
    else if (sector < 3) { g = chroma; b = x; }
    else if (sector < 4) { g = x; b = chroma; }
    else if (sector < 5) { r = x; b = chroma; }
    else { r = chroma; b = x; }
    const double m = value - chroma;
    return {r + m, g + m, b + m, alpha};
}

inline color::RGBColor hsl_to_rgb(double hue, double saturation, double lightness, double alpha = 1.0) {
    hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    saturation = std::clamp(saturation, 0.0, 1.0);
    lightness = std::clamp(lightness, 0.0, 1.0);
    const double chroma = (1.0 - std::abs(2.0 * lightness - 1.0)) * saturation;
    const double sector = hue / 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if (sector < 1) { r = chroma; g = x; }
    else if (sector < 2) { r = x; g = chroma; }
    else if (sector < 3) { g = chroma; b = x; }
    else if (sector < 4) { g = x; b = chroma; }
    else if (sector < 5) { r = x; b = chroma; }
    else { r = chroma; b = x; }
    const double m = lightness - chroma / 2.0;
    return {r + m, g + m, b + m, alpha};
}

inline color::RGBColor rgb_from(const color::ColorValue &value,
                                const color::IccPreview* icc = nullptr) {
    const auto preview = icc ? icc->convert(value, color::PreviewOptions{})
                             : color::to_preview_rgba(value);
    return {preview.r, preview.g, preview.b, preview.a};
}

inline Hsv to_hsv(color::RGBColor rgb, double fallback_hue = 0.0) {
    const double maximum = std::max({rgb.r, rgb.g, rgb.b});
    const double minimum = std::min({rgb.r, rgb.g, rgb.b});
    const double delta = maximum - minimum;
    double hue = fallback_hue;
    if (delta > 1e-12) {
        if (maximum == rgb.r) hue = 60.0 * std::fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (maximum == rgb.g) hue = 60.0 * ((rgb.b - rgb.r) / delta + 2.0);
        else hue = 60.0 * ((rgb.r - rgb.g) / delta + 4.0);
        hue = std::fmod(hue + 360.0, 360.0);
    }
    return {hue, maximum > 0 ? delta / maximum : 0.0, maximum};
}

inline Hsl to_hsl(color::RGBColor rgb, double fallback_hue = 0.0) {
    const double maximum = std::max({rgb.r, rgb.g, rgb.b});
    const double minimum = std::min({rgb.r, rgb.g, rgb.b});
    const double delta = maximum - minimum;
    const double lightness = (maximum + minimum) / 2.0;
    double hue = fallback_hue, saturation = 0;
    if (delta > 1e-12) {
        if (maximum == rgb.r) hue = 60.0 * std::fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (maximum == rgb.g) hue = 60.0 * ((rgb.b - rgb.r) / delta + 2.0);
        else hue = 60.0 * ((rgb.r - rgb.g) / delta + 4.0);
        hue = std::fmod(hue + 360.0, 360.0);
        saturation = delta / (1.0 - std::abs(2.0 * lightness - 1.0));
    }
    return {hue, saturation, lightness};
}

inline std::array<double, 4> rgb_to_cmyk(color::RGBColor rgb) {
    const double k = 1.0 - std::max({rgb.r, rgb.g, rgb.b});
    const double divisor = 1.0 - k;
    if (divisor <= 1e-9) return {0.0, 0.0, 0.0, 1.0};
    return {(1.0 - rgb.r - k) / divisor,
            (1.0 - rgb.g - k) / divisor,
            (1.0 - rgb.b - k) / divisor,
            k};
}

inline color::RGBColor cmyk_to_rgb(double c, double m, double y, double k, double alpha = 1.0) {
    c = std::clamp(c, 0.0, 1.0);
    m = std::clamp(m, 0.0, 1.0);
    y = std::clamp(y, 0.0, 1.0);
    k = std::clamp(k, 0.0, 1.0);
    return {(1.0 - c) * (1.0 - k),
            (1.0 - m) * (1.0 - k),
            (1.0 - y) * (1.0 - k),
            alpha};
}

struct Lab {
    double l{0.0};
    double a{0.0};
    double b{0.0};
};

inline Lab rgb_to_lab(color::RGBColor rgb) {
    const auto to_linear = [](double c) {
        return c > 0.04045 ? std::pow((c + 0.055) / 1.055, 2.4) : (c / 12.92);
    };
    const double r = to_linear(rgb.r);
    const double g = to_linear(rgb.g);
    const double b = to_linear(rgb.b);

    const double x = (r * 0.4124564 + g * 0.3575761 + b * 0.1804375) / 0.95047;
    const double y = (r * 0.2126729 + g * 0.7151522 + b * 0.0721750) / 1.00000;
    const double z = (r * 0.0193339 + g * 0.1191920 + b * 0.9503041) / 1.08883;

    const auto f = [](double t) {
        return t > 0.008856 ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0);
    };
    const double fx = f(x), fy = f(y), fz = f(z);
    return {std::clamp(116.0 * fy - 16.0, 0.0, 100.0),
            std::clamp(500.0 * (fx - fy), -128.0, 127.0),
            std::clamp(200.0 * (fy - fz), -128.0, 127.0)};
}

inline color::RGBColor lab_to_rgb(double l, double a, double b, double alpha = 1.0) {
    const double fy = (l + 16.0) / 116.0;
    const double fx = a / 500.0 + fy;
    const double fz = fy - b / 200.0;

    const auto finv = [](double t) {
        const double t3 = t * t * t;
        return t3 > 0.008856 ? t3 : (t - 16.0 / 116.0) / 7.787;
    };
    const double x = finv(fx) * 0.95047;
    const double y = finv(fy) * 1.00000;
    const double z = finv(fz) * 1.08883;

    const double r_lin =  x * 3.2404542 - y * 1.5371385 - z * 0.4985314;
    const double g_lin = -x * 0.9692660 + y * 1.8760108 + z * 0.0415560;
    const double b_lin =  x * 0.0556434 - y * 0.2040259 + z * 1.0572252;

    const auto to_srgb = [](double c) {
        return c <= 0.0031308 ? (12.92 * c) : (1.055 * std::pow(std::max(0.0, c), 1.0 / 2.4) - 0.055);
    };
    return {std::clamp(to_srgb(r_lin), 0.0, 1.0),
            std::clamp(to_srgb(g_lin), 0.0, 1.0),
            std::clamp(to_srgb(b_lin), 0.0, 1.0),
            alpha};
}

inline uint32_t dib_pixel(color::RGBColor rgb) {
    const auto channel = [](double value) -> uint32_t {
        return static_cast<uint32_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
    };
    return (channel(rgb.r) << 16) | (channel(rgb.g) << 8) | channel(rgb.b);
}

inline std::wstring format_channel_number(double value, int max_decimals = 1) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(max_decimals) << value;
    std::wstring s = text.str();
    if (max_decimals > 0) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    if (s.empty()) s = L"0";
    return s;
}

inline std::wstring format_hex_color(color::RGBColor rgb) {
    wchar_t buf[16]{};
    const int r = std::clamp(static_cast<int>(std::lround(rgb.r * 255.0)), 0, 255);
    const int g = std::clamp(static_cast<int>(std::lround(rgb.g * 255.0)), 0, 255);
    const int b = std::clamp(static_cast<int>(std::lround(rgb.b * 255.0)), 0, 255);
    swprintf_s(buf, L"#%02X%02X%02X", r, g, b);
    return buf;
}

inline std::optional<color::RGBColor> parse_hex_color(const std::wstring &input) {
    std::wstring s = input;
    while (!s.empty() && std::iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && std::iswspace(s.back())) s.pop_back();
    if (!s.empty() && s.front() == L'#') s.erase(s.begin());
    if (s.size() != 6) return std::nullopt;
    for (wchar_t ch : s) {
        if (!std::iswxdigit(ch)) return std::nullopt;
    }
    wchar_t *end = nullptr;
    const unsigned long val = std::wcstoul(s.c_str(), &end, 16);
    if (end != s.c_str() + s.size()) return std::nullopt;
    const double r = ((val >> 16) & 0xFF) / 255.0;
    const double g = ((val >> 8) & 0xFF) / 255.0;
    const double b = (val & 0xFF) / 255.0;
    return color::RGBColor{r, g, b, 1.0};
}

inline std::optional<double> parse_number(const std::wstring &input, double maximum) {
    std::wstring text = input;
    std::replace(text.begin(), text.end(), L',', L'.');
    wchar_t *end = nullptr;
    const double result = std::wcstod(text.c_str(), &end);
    while (end && std::iswspace(*end)) ++end;
    if (end == text.c_str() || !end || *end || !std::isfinite(result) || result < 0.0 || result > maximum)
        return std::nullopt;
    return result;
}


inline ViewerMode viewer_from_index(int index) noexcept {
    return static_cast<ViewerMode>(std::clamp(index, 0, 3));
}

inline int model_index(source_color::Model model) noexcept { return static_cast<int>(model); }
inline source_color::Model model_from_index(int index) noexcept {
    return static_cast<source_color::Model>(std::clamp(index, 0, 3));
}

} // namespace pikdraw::desktop::style_dialog_math
