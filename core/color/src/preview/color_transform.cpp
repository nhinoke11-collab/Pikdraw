#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include <stdexcept>

namespace pikdraw::color {

ColorValue convert_color_to_mode(const ColorValue& source, DocumentColorMode mode,
                                const IccPreview* icc) {
    if (!source.is_valid() || !valid_document_color_mode(mode))
        throw std::invalid_argument("Invalid source color or document color mode");
    if (source.color_space() == ColorSpace::Spot ||
        (mode == DocumentColorMode::Rgb && source.color_space() == ColorSpace::Rgb) ||
        (mode == DocumentColorMode::Cmyk && source.color_space() == ColorSpace::Cmyk))
        return source;
    ColorValue result;
    const auto* rgb = std::get_if<RGBColor>(&source.payload());
    // Preserve neutral black as a single K plate, including with ICC enabled.
    if (mode == DocumentColorMode::Cmyk && rgb && rgb->r == 0 && rgb->g == 0 && rgb->b == 0)
        result = CMYKColor{0, 0, 0, 1, rgb->a};
    else if (icc && icc->convert_process(source, mode, result)) {
        // The conversion uses real source/destination ICC files.
    } else if (const auto* gray = std::get_if<GrayscaleColor>(&source.payload())) {
        result = mode == DocumentColorMode::Cmyk
            ? ColorValue{CMYKColor{0, 0, 0, 1 - gray->gray, gray->a}}
            : ColorValue{RGBColor{gray->gray, gray->gray, gray->gray, gray->a}};
    } else if (rgb) {
        const double maximum = std::max({rgb->r, rgb->g, rgb->b});
        result = CMYKColor{(maximum - rgb->r) / maximum, (maximum - rgb->g) / maximum,
                           (maximum - rgb->b) / maximum, 1 - maximum, rgb->a};
    } else {
        const auto& c = std::get<CMYKColor>(source.payload());
        result = RGBColor{(1 - c.c) * (1 - c.k), (1 - c.m) * (1 - c.k),
                          (1 - c.y) * (1 - c.k), c.a};
    }
    result.set_overprint(source.overprint());
    return result;
}

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
