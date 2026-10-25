#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace pikdraw::color {

class IccPreview;
enum class BlackGeneration {
    None,      /// Use the selected ICC or standard conversion without a black-preservation shortcut.
    PureK100,  /// C=0, M=0, Y=0, K=1 with overprint=true for small text and line art
    RichBlack  /// C=0.40, M=0.30, Y=0.30, K=1.00 for large solid dark fills
};

enum class ColorConversionQuality {
    Preserved,
    IccManaged,
    Approximate,
    PresetPolicy
};

struct ColorConversionReport {
    ColorValue color;
    ColorConversionQuality quality{ColorConversionQuality::Preserved};
    bool changed{false};
    bool black_policy_applied{false};
    bool tac_limited{false};
};

struct ColorConversionSummary {
    std::size_t icc_managed{0};
    std::size_t approximate{0};
    std::size_t preset_policy{0};
    std::size_t tac_limited{0};

    void record(const ColorConversionReport& report) noexcept {
        switch (report.quality) {
        case ColorConversionQuality::IccManaged: ++icc_managed; break;
        case ColorConversionQuality::Approximate: ++approximate; break;
        case ColorConversionQuality::PresetPolicy: ++preset_policy; break;
        case ColorConversionQuality::Preserved: break;
        }
        if (report.tac_limited) ++tac_limited;
    }
};

/// Explicit print/output condition supplied by a preflight or export workflow.
struct TACOutputCondition {
    std::string identity;
    double maximum_tac_percent{0.0};

    [[nodiscard]] bool is_valid() const noexcept {
        return !identity.empty() && identity.size() <= 256 &&
            std::isfinite(maximum_tac_percent) &&
            maximum_tac_percent >= 100.0 && maximum_tac_percent <= 400.0;
    }
};

/// TAC validation/fix summary. TAC covers process CMYK only; spot inks and raster
/// pixels need separate separation/profile-aware preflight.
struct TACSummary {
    std::string output_condition_id;
    std::string scope;
    std::string fix_method;
    double maximum_tac_percent{0.0};
    double maximum_before_percent{0.0};
    double maximum_after_percent{0.0};
    std::size_t process_colors_examined{0};
    std::size_t process_colors_over_limit{0};
    std::size_t process_colors_changed{0};
    std::size_t spot_colors_not_assessed{0};
    std::size_t raster_objects_not_assessed{0};
};

/// Device-ready RGBA color (8-bit per channel)
struct DeviceRGBA {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    [[nodiscard]] uint32_t to_u32() const noexcept {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(r);
    }
};

/// High-precision preview floating-point RGBA [0.0, 1.0]
struct PreviewRGBA {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};

    [[nodiscard]] DeviceRGBA to_device() const noexcept {
        return DeviceRGBA{
            static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f)),
            static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f))
        };
    }
};

/// Calculate Total Area Coverage (TAC / TIC) percentage [0.0%, 400.0%]
[[nodiscard]] double calculate_tac(const CMYKColor& cmyk) noexcept;

/// Clamp CMYK ink coverage to an explicit output-condition TAC limit using a K-preserving policy.
/// Invalid limits or source colors leave the source unchanged; limits must be 100%..400%.
[[nodiscard]] CMYKColor clamp_tac(const CMYKColor& cmyk, double max_tac_percent) noexcept;

// Explicit source edit. Spot identity/alternate and all overprint metadata survive.
// A missing ICC transform uses deterministic subtractive conversion, not print proofing.
// Black-generation policy defaults to None; role-specific Pure K/Rich Black must be explicit.
[[nodiscard]] ColorValue convert_color_to_mode(const ColorValue& source,
    DocumentColorMode target_mode, const IccPreview* icc = nullptr,
    BlackGeneration black_gen = BlackGeneration::None,
    std::optional<double> max_tac_percent = std::nullopt);

// Same channel conversion with a truthful indication of whether the result
// used a real ICC transform, a formula fallback, a preset policy or no change.
[[nodiscard]] ColorConversionReport convert_color_to_mode_report(
    const ColorValue& source, DocumentColorMode target_mode,
    const IccPreview* icc = nullptr,
    BlackGeneration black_gen = BlackGeneration::None,
    std::optional<double> max_tac_percent = std::nullopt);

/// Subtractive ink blend for overprint simulation in linear display RGB
[[nodiscard]] PreviewRGBA simulate_overprint(const PreviewRGBA& background,
                                            const PreviewRGBA& foreground) noexcept;

/// Transforms any source ColorValue into PreviewRGBA for screen viewport display
[[nodiscard]] PreviewRGBA to_preview_rgba(const ColorValue& color) noexcept;

} // namespace pikdraw::color
