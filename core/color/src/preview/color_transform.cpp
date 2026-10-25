#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pikdraw::color {

double calculate_tac(const CMYKColor& cmyk) noexcept {
    return (cmyk.c + cmyk.m + cmyk.y + cmyk.k) * 100.0;
}

CMYKColor clamp_tac(const CMYKColor& cmyk, double max_tac_percent) noexcept {
    if (!cmyk.is_valid() || !std::isfinite(max_tac_percent) ||
        max_tac_percent < 100.0 || max_tac_percent > 400.0)
        return cmyk;
    const double max_fraction = max_tac_percent / 100.0;
    const double total = cmyk.c + cmyk.m + cmyk.y + cmyk.k;
    if (total <= max_fraction) {
        return cmyk;
    }
    const double available_cmy = std::max(0.0, max_fraction - cmyk.k);
    const double cmy_sum = cmyk.c + cmyk.m + cmyk.y;
    if (cmy_sum > 1e-6) {
        const double scale = available_cmy / cmy_sum;
        return CMYKColor{
            std::clamp(cmyk.c * scale, 0.0, 1.0),
            std::clamp(cmyk.m * scale, 0.0, 1.0),
            std::clamp(cmyk.y * scale, 0.0, 1.0),
            std::clamp(cmyk.k, 0.0, 1.0),
            cmyk.a
        };
    }
    return CMYKColor{0.0, 0.0, 0.0, std::clamp(cmyk.k, 0.0, max_fraction), cmyk.a};
}

PreviewRGBA simulate_overprint(const PreviewRGBA& background,
                              const PreviewRGBA& foreground) noexcept {
    return PreviewRGBA{
        std::clamp(background.r * foreground.r, 0.0f, 1.0f),
        std::clamp(background.g * foreground.g, 0.0f, 1.0f),
        std::clamp(background.b * foreground.b, 0.0f, 1.0f),
        std::clamp(1.0f - (1.0f - background.a) * (1.0f - foreground.a), 0.0f, 1.0f)
    };
}

ColorConversionReport convert_color_to_mode_report(const ColorValue& source, DocumentColorMode mode,
                                const IccPreview* icc, BlackGeneration black_gen,
                                std::optional<double> max_tac_percent) {
    if (!source.is_valid() || !valid_document_color_mode(mode))
        throw std::invalid_argument("Invalid source color or document color mode");
    if (max_tac_percent && (!std::isfinite(*max_tac_percent) ||
        *max_tac_percent < 100.0 || *max_tac_percent > 400.0))
        throw std::invalid_argument("TAC limit must be an explicit value from 100% to 400%");
    if (source.color_space() == ColorSpace::Spot ||
        (mode == DocumentColorMode::Rgb && source.color_space() == ColorSpace::Rgb) ||
        (mode == DocumentColorMode::Cmyk && source.color_space() == ColorSpace::Cmyk))
        return {source, ColorConversionQuality::Preserved, false, false, false};
    ColorValue result;
    const auto* rgb = std::get_if<RGBColor>(&source.payload());
    bool is_pure_k_overprint = false;
    bool used_icc = false;
    bool black_policy_applied = false;
    bool tac_limited = false;
    auto apply_tac_limit = [&](const CMYKColor& source_cmyk) {
        const auto destination_profile = result.profile_resource();
        if (!max_tac_percent) {
            result = source_cmyk;
            if (destination_profile)
                (void)result.set_profile_resource(destination_profile);
            return;
        }
        const auto limited = clamp_tac(source_cmyk, *max_tac_percent);
        tac_limited = limited.c != source_cmyk.c || limited.m != source_cmyk.m ||
            limited.y != source_cmyk.y || limited.k != source_cmyk.k;
        result = limited;
        if (destination_profile)
            (void)result.set_profile_resource(destination_profile);
    };

    if (source.profile_resource()) {
        if (!icc || !icc->convert_process(source, mode, result))
            throw std::invalid_argument(
                "Bound paint ICC profile does not match an available conversion transform");
        used_icc = true;
        if (mode == DocumentColorMode::Cmyk) {
            if (const auto* c = std::get_if<CMYKColor>(&result.payload())) apply_tac_limit(*c);
        }
    } else if (mode == DocumentColorMode::Cmyk && rgb && black_gen != BlackGeneration::None &&
        rgb->r <= 0.02 && rgb->g <= 0.02 && rgb->b <= 0.02) {
        black_policy_applied = true;
        if (black_gen == BlackGeneration::PureK100) {
            result = CMYKColor{0, 0, 0, 1, rgb->a};
            is_pure_k_overprint = true;
        } else {
            apply_tac_limit(CMYKColor{0.40, 0.30, 0.30, 1.00, rgb->a});
        }
    } else if (icc && icc->convert_process(source, mode, result)) {
        used_icc = true;
        // The conversion uses real source/destination ICC files.
        if (mode == DocumentColorMode::Cmyk) {
            if (const auto* c = std::get_if<CMYKColor>(&result.payload())) apply_tac_limit(*c);
        }
    } else if (const auto* gray = std::get_if<GrayscaleColor>(&source.payload())) {
        result = mode == DocumentColorMode::Cmyk
            ? ColorValue{CMYKColor{0, 0, 0, 1 - gray->gray, gray->a}}
            : ColorValue{RGBColor{gray->gray, gray->gray, gray->gray, gray->a}};
    } else if (rgb) {
        const double maximum = std::max({rgb->r, rgb->g, rgb->b});
        if (maximum <= 1e-6) {
            result = CMYKColor{0, 0, 0, 1, rgb->a};
            if (black_gen == BlackGeneration::PureK100) is_pure_k_overprint = true;
        } else {
            auto raw_cmyk = CMYKColor{(maximum - rgb->r) / maximum, (maximum - rgb->g) / maximum,
                                      (maximum - rgb->b) / maximum, 1 - maximum, rgb->a};
            apply_tac_limit(raw_cmyk);
        }
    } else {
        const auto& c = std::get<CMYKColor>(source.payload());
        result = RGBColor{(1 - c.c) * (1 - c.k), (1 - c.m) * (1 - c.k),
                          (1 - c.y) * (1 - c.k), c.a};
    }
    if (is_pure_k_overprint) {
        result.set_overprint({true, true});
    } else {
        result.set_overprint(source.overprint());
    }
    const auto quality = black_policy_applied ? ColorConversionQuality::PresetPolicy
        : used_icc ? ColorConversionQuality::IccManaged : ColorConversionQuality::Approximate;
    return {std::move(result), quality, true, black_policy_applied, tac_limited};
}

ColorValue convert_color_to_mode(const ColorValue& source, DocumentColorMode mode,
                                const IccPreview* icc, BlackGeneration black_gen,
                                std::optional<double> max_tac_percent) {
    return convert_color_to_mode_report(source, mode, icc, black_gen, max_tac_percent).color;
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
