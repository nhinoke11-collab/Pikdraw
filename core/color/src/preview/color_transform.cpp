#include "pikdraw/color/preview/color_transform.hpp"

namespace pikdraw::color {

PreviewRGBA to_preview_rgba(const ColorValue& color) noexcept {
    return std::visit([](const auto& c) noexcept -> PreviewRGBA {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, RGBColor>) {
            return PreviewRGBA{
                static_cast<float>(c.r),
                static_cast<float>(c.g),
                static_cast<float>(c.b),
                static_cast<float>(c.a)
            };
        } else if constexpr (std::is_same_v<T, CMYKColor>) {
            // High-fidelity standard CMYK to sRGB subtractive simulation
            const double r = (1.0 - c.c) * (1.0 - c.k);
            const double g = (1.0 - c.m) * (1.0 - c.k);
            const double b = (1.0 - c.y) * (1.0 - c.k);
            return PreviewRGBA{
                static_cast<float>(std::clamp(r, 0.0, 1.0)),
                static_cast<float>(std::clamp(g, 0.0, 1.0)),
                static_cast<float>(std::clamp(b, 0.0, 1.0)),
                static_cast<float>(c.a)
            };
        } else if constexpr (std::is_same_v<T, SpotColor>) {
            // Blend white (1.0) towards alternate RGB by tint
            const double r = 1.0 - c.tint * (1.0 - c.alternate_rgb.r);
            const double g = 1.0 - c.tint * (1.0 - c.alternate_rgb.g);
            const double b = 1.0 - c.tint * (1.0 - c.alternate_rgb.b);
            return PreviewRGBA{
                static_cast<float>(std::clamp(r, 0.0, 1.0)),
                static_cast<float>(std::clamp(g, 0.0, 1.0)),
                static_cast<float>(std::clamp(b, 0.0, 1.0)),
                static_cast<float>(c.alternate_rgb.a)
            };
        } else if constexpr (std::is_same_v<T, GrayscaleColor>) {
            const float val = static_cast<float>(c.gray);
            return PreviewRGBA{val, val, val, static_cast<float>(c.a)};
        }
    }, color.payload());
}

} // namespace pikdraw::color
