#pragma once
#include "pikdraw/desktop/shell/native_window.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>

namespace pikdraw::desktop::eyedropper_detail {

struct BitmapPixelAddress {
    int32_t x{0};
    int32_t y{0};
};

// ImageObject row zero is rendered at the top of bounds_rect. Keep sampling in
// the same Y-up document convention as SoftwareRasterizer::render_image.
[[nodiscard]] inline std::optional<BitmapPixelAddress> bitmap_pixel_address(
    const document::ImageObject& image,
    const geometry::Transform2D& world_transform,
    geometry::Point2D world_point) noexcept {
    if (image.width_px <= 0 || image.height_px <= 0 ||
        image.bounds_rect.width <= 1e-9 || image.bounds_rect.height <= 1e-9)
        return std::nullopt;
    const auto inverse = world_transform.inverted();
    if (!inverse) return std::nullopt;
    const auto local = inverse->map_point(world_point);
    const auto& bounds = image.bounds_rect;
    if (local.x < bounds.x || local.x >= bounds.right() ||
        local.y < bounds.y || local.y >= bounds.bottom())
        return std::nullopt;
    const double u = (local.x - bounds.x) / bounds.width;
    const double v = (bounds.bottom() - local.y) / bounds.height;
    const auto x = static_cast<int32_t>(std::clamp(
        std::floor(u * image.width_px), 0.0,
        static_cast<double>(image.width_px - 1)));
    const auto y = static_cast<int32_t>(std::clamp(
        std::floor(v * image.height_px), 0.0,
        static_cast<double>(image.height_px - 1)));
    return BitmapPixelAddress{x, y};
}

[[nodiscard]] inline std::string format_sample_color(
    const CanvasColorSample& sample) {
    const auto format_percent = [](double normalized) {
        const double bounded = std::isfinite(normalized)
            ? std::clamp(normalized, 0.0, 1.0)
            : 0.0;
        std::array<char, 64> buffer{};
        const auto result = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), bounded * 100.0);
        if (result.ec != std::errc{}) return std::string{"0"};
        return std::string(buffer.data(), result.ptr);
    };
    return std::visit([&](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        std::ostringstream text;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            text << "RGB "
                 << static_cast<int>(std::lround(std::clamp(value.r, 0.0, 1.0) * 255.0)) << ","
                 << static_cast<int>(std::lround(std::clamp(value.g, 0.0, 1.0) * 255.0)) << ","
                 << static_cast<int>(std::lround(std::clamp(value.b, 0.0, 1.0) * 255.0));
        } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
            text << "CMYK C" << format_percent(value.c)
                 << " M" << format_percent(value.m)
                 << " Y" << format_percent(value.y)
                 << " K" << format_percent(value.k);
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
            text << "Spot " << value.spot_name << " " << format_percent(value.tint) << "%";
        } else {
            text << "Gray " << format_percent(value.gray) << "%";
        }
        return text.str();
    }, sample.color_value.payload());
}

} // namespace pikdraw::desktop::eyedropper_detail
