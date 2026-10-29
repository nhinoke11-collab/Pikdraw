#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pikdraw::document {

/// Stable identity for immutable raster source content.
struct RasterAssetId {
    uint64_t high{0};
    uint64_t low{0};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return high != 0 || low != 0;
    }
    friend constexpr bool operator==(RasterAssetId, RasterAssetId) = default;
};

enum class RasterStorageMode : uint8_t { Embedded = 0, Linked = 1 };

/// Source identity and location. encoded_byte_length is 64-bit by design;
/// this descriptor does not imply that encoded bytes are resident in memory.
struct RasterSourceDescriptor {
    RasterAssetId asset_id;
    uint64_t revision{0};
    RasterStorageMode mode{RasterStorageMode::Embedded};
    uint64_t encoded_byte_length{0};
    std::string media_type;
    std::string locator;
    std::string fingerprint_sha256;
    int64_t source_modified_ticks{0};
};

struct RasterEditNodeId {
    uint64_t value{0};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(RasterEditNodeId, RasterEditNodeId) = default;
};

enum class RasterAdjustmentMaskKind : uint8_t {
    Bitmap = 0,
    Rectangle = 1,
    LinearGradient = 2,
    RadialGradient = 3
};

/// Immutable mask in the coordinate space immediately before one pointwise
/// adjustment node. Bitmap coverage supports paint/refine; geometric masks do
/// not allocate a full image plane.
struct RasterAdjustmentMask {
    uint64_t id{0};
    uint32_t width_px{0};
    uint32_t height_px{0};
    uint8_t density_percent{100};
    bool inverted{false};
    std::vector<uint8_t> coverage;
    RasterAdjustmentMaskKind kind{RasterAdjustmentMaskKind::Bitmap};
    double left{0.0};
    double top{0.0};
    double width{1.0};
    double height{1.0};
    double feather_px{0.0};
    /// Clockwise image-space direction of a linear fade: 0° is left to right.
    double gradient_angle_degrees{0.0};
    friend bool operator==(const RasterAdjustmentMask&,
                           const RasterAdjustmentMask&) = default;
};

/// Stable native-file operation IDs. Never serialize std::variant.index().
enum class RasterEditKind : uint32_t {
    Crop = 1,
    Flip = 2,
    Rotate = 3,
    Exposure = 4,
    Levels = 5,
    Hsl = 6,
    Curves = 7,
    WhiteBalance = 8,
    Vibrance = 9,
    SelectiveHsl = 10,
    Light = 11,
    ChannelMixer = 12,
    CloneStamp = 13,
    ColorBalance = 14,
    RasterEraser = 15
};

struct RasterCropParams {
    // Normalized source rectangle: [x, x + width] x [y, y + height].
    double x{0.0};
    double y{0.0};
    double width{1.0};
    double height{1.0};
};
struct RasterFlipParams { bool horizontal{false}; bool vertical{false}; };
struct RasterRotateParams { double degrees{0.0}; };
struct RasterExposureParams { double exposure_ev{0.0}; };
/// Basic tone controls in linear-light sRGB. Positive Highlights reduce the
/// upper tones; positive Shadows lift lower tones. Whites and Blacks shift
/// only their respective end ranges. Alpha is never adjusted.
struct RasterLightParams {
    double exposure_ev{0.0};
    double contrast_percent{0.0};
    double highlights_percent{0.0};
    double shadows_percent{0.0};
    double whites_percent{0.0};
    double blacks_percent{0.0};
};
enum class RasterTonalRange : uint8_t { Shadows = 0, Midtones = 1, Highlights = 2 };
/// Perceptual RGB correction weighted by a selected luminance band. Positive
/// axes move toward red, green and blue; negative axes move toward their
/// cyan, magenta and yellow complements. Alpha is preserved.
struct RasterColorBalanceParams {
    RasterTonalRange range{RasterTonalRange::Midtones};
    double red_cyan_percent{0.0};
    double green_magenta_percent{0.0};
    double blue_yellow_percent{0.0};
    bool preserve_luminosity{true};
};
/// Linear-light sRGB channel matrix. Rows are output R/G/B; columns are input
/// R/G/B. Coefficients are normalized gains in [-2, 2], then outputs clamp
/// to the display range. Alpha is preserved.
struct RasterChannelMixerParams {
    std::array<double, 9> coefficients{1.0, 0.0, 0.0,
                                       0.0, 1.0, 0.0,
                                       0.0, 0.0, 1.0};
};
/// One reproducible soft circular clone dab. Centers are normalized to the
/// current frame; radius is a percent of the shorter frame edge. The source
/// sample is frozen from the pixels immediately before this node is applied.
struct RasterCloneStampParams {
    double source_x{0.5};
    double source_y{0.5};
    double target_x{0.5};
    double target_y{0.5};
    double radius_percent{1.0};
    double hardness_percent{75.0};
    double opacity_percent{100.0};
};
struct RasterEraserPoint {
    double x{0.0};
    double y{0.0};
    friend bool operator==(const RasterEraserPoint&,
                           const RasterEraserPoint&) = default;
};
/// Non-destructive round-nib alpha eraser in the output coordinate space at
/// this graph position. Centers are normalized; diameter remains in pixels.
struct RasterEraserParams {
    static constexpr size_t kMaxPoints = 4096;
    std::vector<RasterEraserPoint> points;
    double diameter_px{12.0};
    friend bool operator==(const RasterEraserParams&,
                           const RasterEraserParams&) = default;
};
enum class RasterToneChannel : uint8_t { Composite = 0, Red = 1, Green = 2, Blue = 3 };
/// Encoded-sRGB Levels. Each node targets composite RGB or one RGB channel;
/// alpha is never adjusted. The normalized values are independent of sample depth.
struct RasterLevelsParams {
    RasterToneChannel channel{RasterToneChannel::Composite};
    double black_in{0.0};
    double white_in{1.0};
    double gamma{1.0};
    double black_out{0.0};
    double white_out{1.0};
};
/// HSL adjustment on encoded sRGB samples. Hue wraps cyclically; saturation
/// and lightness are signed percentage adjustments. Alpha is never adjusted.
struct RasterHslParams {
    double hue_degrees{0.0};
    double saturation_percent{0.0};
    double lightness_percent{0.0};
};
enum class RasterHueRange : uint8_t {
    Reds = 0, Yellows = 1, Greens = 2, Cyans = 3, Blues = 4, Magentas = 5
};
/// Hue-targeted adjustment over six fixed 60-degree ranges. Feather softens
/// each range edge from a hard boundary to the full 30-degree half-range.
struct RasterSelectiveHslParams {
    RasterHueRange range{RasterHueRange::Reds};
    double hue_degrees{0.0};
    double saturation_percent{0.0};
    double lightness_percent{0.0};
    double feather_percent{50.0};
};
/// Five-anchor, piecewise-linear tone curve on encoded sRGB values. Anchor X
/// positions are fixed at 0, 25, 50, 75 and 100 percent; RGB and alpha are
/// never reinterpreted as print separations by this adjustment.
struct RasterCurvesParams {
    RasterToneChannel channel{RasterToneChannel::Composite};
    std::array<double, 5> output_points{0.0, 0.25, 0.5, 0.75, 1.0};
};
/// Relative temperature/tint correction in linear-light sRGB. Values are
/// normalized sliders, not Kelvin readings or camera metadata.
struct RasterWhiteBalanceParams {
    double temperature_shift_percent{0.0};
    double tint_percent{0.0};
};
/// Saturation-aware adjustment on encoded sRGB. Positive values lift muted
/// colors toward full saturation; negative values reduce saturation uniformly.
struct RasterVibranceParams {
    double amount_percent{0.0};
};
using RasterEditParameters = std::variant<RasterCropParams, RasterFlipParams,
                                          RasterRotateParams, RasterExposureParams,
                                          RasterLevelsParams, RasterHslParams,
                                          RasterCurvesParams,
                                          RasterWhiteBalanceParams,
                                          RasterVibranceParams,
                                          RasterSelectiveHslParams,
                                          RasterLightParams,
                                          RasterChannelMixerParams,
                                          RasterCloneStampParams,
                                          RasterColorBalanceParams,
                                          RasterEraserParams>;

struct RasterEditNode {
    RasterEditNodeId id;
    bool enabled{true};
    RasterEditParameters parameters{RasterCropParams{}};
    uint32_t algorithm_version{1};
    std::optional<RasterAdjustmentMask> mask;
};

/// Ordered, non-destructive edit history attached to an image placement.
struct RasterEditGraph {
    static constexpr uint16_t kSchemaVersion = 18;
    static constexpr uint32_t kMaxNodes = 1024;
    uint16_t schema_version{kSchemaVersion};
    std::vector<RasterEditNode> nodes;
};

} // namespace pikdraw::document
