#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include <algorithm>
#include <cstdint>

namespace pikdraw::color {

class IccPreview;
enum class BlackGeneration {
    PureK100,  /// C=0, M=0, Y=0, K=1 with overprint=true for small text and line art
    RichBlack  /// C=0.40, M=0.30, Y=0.30, K=1.00 for large solid dark fills
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

/// Clamp CMYK ink coverage to maximum TAC (default 320% ISO 12647-2) using UCR/GCR balance
[[nodiscard]] CMYKColor clamp_tac(const CMYKColor& cmyk, double max_tac_percent = 320.0) noexcept;

// Explicit source edit. Spot identity/alternate and all overprint metadata survive.
// A missing ICC transform uses deterministic subtractive conversion, not print proofing.
[[nodiscard]] ColorValue convert_color_to_mode(const ColorValue& source,
    DocumentColorMode target_mode, const IccPreview* icc = nullptr,
    BlackGeneration black_gen = BlackGeneration::PureK100,
    double max_tac_percent = 320.0);

/// Subtractive ink blend for overprint simulation in linear display RGB
[[nodiscard]] PreviewRGBA simulate_overprint(const PreviewRGBA& background,
                                            const PreviewRGBA& foreground) noexcept;

/// Transforms any source ColorValue into PreviewRGBA for screen viewport display
[[nodiscard]] PreviewRGBA to_preview_rgba(const ColorValue& color) noexcept;

} // namespace pikdraw::color
