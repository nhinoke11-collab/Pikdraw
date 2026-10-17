#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::document {

inline constexpr uint32_t kGradientSchemaMajor = 1;
inline constexpr uint32_t kGradientSchemaMinor = 0;
inline constexpr size_t kMinGradientStops = 2;
inline constexpr size_t kMaxGradientStops = 256;
inline constexpr size_t kMaxDocumentGradientStops = 1000000;
inline constexpr double kMinMidpoint = 0.01;
inline constexpr double kMaxMidpoint = 0.99;
inline constexpr double kRelativeInverseThreshold = 1e-12;
inline constexpr double kMaxCoordinateMm = 1e9;

enum class GradientKind : uint8_t {
    Linear = 0,
    Radial = 1
};

enum class GradientSpread : uint8_t {
    Pad = 0,
    Repeat = 1,
    Reflect = 2
};

enum class GradientInterpolation : uint8_t {
    EncodedSRGBStraightAlpha = 0
};

struct GradientStop {
    uint64_t id{0};               // stable, nonzero; ordered tie identity
    double offset{0.0};           // [0, 1]
    color::ColorValue color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double opacity{1.0};          // [0, 1], separate from color alpha
    double midpoint_to_next{0.5}; // [0.01, 0.99], terminal stop always 0.5

    [[nodiscard]] bool operator==(const GradientStop& other) const noexcept;
    [[nodiscard]] bool operator!=(const GradientStop& other) const noexcept {
        return !(*this == other);
    }
};

struct GradientPaint {
    uint32_t schema_major{kGradientSchemaMajor};
    uint32_t schema_minor{kGradientSchemaMinor};
    uint64_t id{0}; // paint instance; shared immutable snapshots
    GradientKind kind{GradientKind::Linear};
    GradientSpread spread{GradientSpread::Pad};
    GradientInterpolation interpolation{GradientInterpolation::EncodedSRGBStraightAlpha};
    geometry::Transform2D gradient_to_object{geometry::Transform2D::identity()};
    std::vector<GradientStop> stops;

    [[nodiscard]] std::optional<std::string> validation_error() const;
    [[nodiscard]] bool is_valid() const {
        return !validation_error().has_value();
    }

    [[nodiscard]] bool same_payload(const GradientPaint& other) const noexcept;

    [[nodiscard]] GradientPaint with_reversed_stops() const;
    [[nodiscard]] GradientPaint clone_with_new_ids(
        uint64_t new_paint_id,
        const std::vector<uint64_t>& new_stop_ids) const;

    static GradientPaint make_default_linear(
        uint64_t id,
        const geometry::Rect2D& bounds,
        const color::ColorValue& start_color = color::RGBColor{0.2, 0.4, 0.8, 1.0},
        const color::ColorValue& end_color = color::RGBColor{1.0, 1.0, 1.0, 1.0});

    static GradientPaint make_default_radial(
        uint64_t id,
        const geometry::Rect2D& bounds,
        const color::ColorValue& center_color = color::RGBColor{1.0, 1.0, 1.0, 1.0},
        const color::ColorValue& edge_color = color::RGBColor{0.2, 0.4, 0.8, 1.0});
};

[[nodiscard]] bool validate_gradient_matrix(const geometry::Transform2D& matrix);

uint64_t allocate_gradient_paint_id();
uint64_t allocate_gradient_stop_id();
void reserve_gradient_paint_id(uint64_t id);
void reserve_gradient_stop_id(uint64_t id);
void reset_gradient_id_allocators_for_tests();

enum class FillPaintKind : uint8_t {
    None = 0,
    Solid = 1,
    Gradient = 2
};

struct EffectiveFillPaint {
    FillPaintKind kind{FillPaintKind::None};
    color::ColorValue solid_color{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    std::shared_ptr<const GradientPaint> gradient{nullptr};
};

} // namespace pikdraw::document
