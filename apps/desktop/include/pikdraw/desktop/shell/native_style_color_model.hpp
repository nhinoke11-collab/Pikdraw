#pragma once

#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"

namespace pikdraw::desktop::source_color {

enum class Model { Rgb, Cmyk, Spot, Grayscale };

inline Model model_of(const color::ColorValue &value) noexcept {
    switch (value.color_space()) {
    case color::ColorSpace::Rgb: return Model::Rgb;
    case color::ColorSpace::Cmyk: return Model::Cmyk;
    case color::ColorSpace::Spot: return Model::Spot;
    case color::ColorSpace::Grayscale: return Model::Grayscale;
    }
    return Model::Rgb;
}

inline color::ColorValue convert(const color::ColorValue &source, Model target,
                                 const color::IccPreview* icc = nullptr) {
    if (model_of(source) == target) return source;

    if (target == Model::Rgb)
        return color::convert_color_to_mode(source, color::DocumentColorMode::Rgb, icc,
                                            color::BlackGeneration::None);
    if (target == Model::Cmyk)
        return color::convert_color_to_mode(source, color::DocumentColorMode::Cmyk, icc,
                                            color::BlackGeneration::None);

    const auto preview = icc
        ? icc->convert(source, color::PreviewOptions{}) : color::to_preview_rgba(source);
    const double r = preview.r, g = preview.g, b = preview.b, a = preview.a;
    color::ColorValue result;
    switch (target) {
    case Model::Rgb:
        result = color::ColorValue::make_rgb(r, g, b, a);
        break;
    case Model::Cmyk: {
        const double k = 1.0 - std::max({r, g, b});
        const double divisor = 1.0 - k;
        if (divisor <= 1e-9)
            result = color::ColorValue::make_cmyk(0, 0, 0, 1, a);
        else
            result = color::ColorValue::make_cmyk((1 - r - k) / divisor,
                                                  (1 - g - k) / divisor,
                                                  (1 - b - k) / divisor, k, a);
        break;
    }
    case Model::Spot: {
        const auto *spot = std::get_if<color::SpotColor>(&source.payload());
        const std::string name = spot ? spot->spot_name : "Spot Color";
        result = color::ColorValue{color::SpotColor{name, 1.0, {r, g, b, a}, false}};
        break;
    }
    case Model::Grayscale: {
        const double gray = std::clamp(0.2126 * r + 0.7152 * g + 0.0722 * b, 0.0, 1.0);
        result = color::ColorValue{color::GrayscaleColor{gray, a}};
        break;
    }
    }
    result.set_overprint(source.overprint());
    return result;
}

inline color::ColorValue with_overprint(color::ColorValue value,
                                        bool stroke, bool enabled) {
    auto overprint = value.overprint();
    if (stroke) overprint.stroke = enabled;
    else overprint.fill = enabled;
    value.set_overprint(overprint);
    return value;
}

} // namespace pikdraw::desktop::source_color
