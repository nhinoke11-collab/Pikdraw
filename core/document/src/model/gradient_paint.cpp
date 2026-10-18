#include "pikdraw/document/model/gradient_paint.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_set>

namespace pikdraw::document {

namespace {

bool same_color_values(const color::ColorValue& lhs, const color::ColorValue& rhs) {
    if (lhs.payload().index() != rhs.payload().index()) {
        return false;
    }
    return std::visit([&](const auto& left) {
        using T = std::decay_t<decltype(left)>;
        const auto* right = std::get_if<T>(&rhs.payload());
        if (!right) return false;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            return left.r == right->r && left.g == right->g &&
                   left.b == right->b && left.a == right->a;
        } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
            return left.c == right->c && left.m == right->m &&
                   left.y == right->y && left.k == right->k && left.a == right->a;
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
            return left.spot_name == right->spot_name && left.tint == right->tint &&
                   left.alternate_rgb.r == right->alternate_rgb.r &&
                   left.alternate_rgb.g == right->alternate_rgb.g &&
                   left.alternate_rgb.b == right->alternate_rgb.b &&
                   left.alternate_rgb.a == right->alternate_rgb.a &&
                   left.overprint == right->overprint;
        } else {
            return left.gray == right->gray && left.a == right->a;
        }
    }, lhs.payload()) &&
    lhs.overprint().fill == rhs.overprint().fill &&
    lhs.overprint().stroke == rhs.overprint().stroke;
}

} // namespace

bool GradientStop::operator==(const GradientStop& other) const noexcept {
    return id == other.id &&
           std::abs(offset - other.offset) <= 1e-12 &&
           std::abs(opacity - other.opacity) <= 1e-12 &&
           std::abs(midpoint_to_next - other.midpoint_to_next) <= 1e-12 &&
           same_color_values(color, other.color);
}

bool validate_gradient_matrix(const geometry::Transform2D& matrix) {
    if (!matrix.is_finite()) {
        return false;
    }
    if (std::abs(matrix.a) > kMaxCoordinateMm ||
        std::abs(matrix.b) > kMaxCoordinateMm ||
        std::abs(matrix.c) > kMaxCoordinateMm ||
        std::abs(matrix.d) > kMaxCoordinateMm ||
        std::abs(matrix.tx) > kMaxCoordinateMm ||
        std::abs(matrix.ty) > kMaxCoordinateMm) {
        return false;
    }

    const double n = std::max({
        std::abs(matrix.a),
        std::abs(matrix.b),
        std::abs(matrix.c),
        std::abs(matrix.d)
    });
    if (n <= 0.0 || !std::isfinite(n)) {
        return false;
    }

    const double det_scaled = (matrix.a / n) * (matrix.d / n) - (matrix.b / n) * (matrix.c / n);
    if (!std::isfinite(det_scaled) || std::abs(det_scaled) <= kRelativeInverseThreshold) {
        return false;
    }

    const auto inv = matrix.inverted(0.0);
    if (!inv.has_value() || !inv->is_finite()) {
        return false;
    }

    return true;
}

std::optional<std::string> GradientPaint::validation_error() const {
    if (schema_major != kGradientSchemaMajor || schema_minor != kGradientSchemaMinor) {
        return "Unsupported gradient schema version";
    }
    if (id == 0) {
        return "Gradient paint ID must be nonzero";
    }
    if (stops.size() < kMinGradientStops || stops.size() > kMaxGradientStops) {
        return "Gradient stops count must be between 2 and 256";
    }
    if (!validate_gradient_matrix(gradient_to_object)) {
        return "Gradient transform matrix is invalid, out of bounds, or non-invertible";
    }

    std::unordered_set<uint64_t> stop_ids;
    stop_ids.reserve(stops.size());

    for (size_t i = 0; i < stops.size(); ++i) {
        const auto& stop = stops[i];
        if (stop.id == 0) {
            return "Gradient stop ID must be nonzero";
        }
        if (!stop_ids.insert(stop.id).second) {
            return "Duplicate gradient stop ID within paint";
        }
        if (!std::isfinite(stop.offset) || stop.offset < 0.0 || stop.offset > 1.0) {
            return "Gradient stop offset must be finite and within [0, 1]";
        }
        if (i > 0 && stop.offset < stops[i - 1].offset) {
            return "Gradient stop offsets must be monotonic non-decreasing";
        }
        if (!std::isfinite(stop.opacity) || stop.opacity < 0.0 || stop.opacity > 1.0) {
            return "Gradient stop opacity must be finite and within [0, 1]";
        }
        if (!std::isfinite(stop.midpoint_to_next) ||
            stop.midpoint_to_next < kMinMidpoint ||
            stop.midpoint_to_next > kMaxMidpoint) {
            return "Gradient stop midpoint must be finite and within [0.01, 0.99]";
        }
        if (!stop.color.is_valid()) {
            return "Gradient stop color value is invalid";
        }
    }

    if (std::abs(stops.back().midpoint_to_next - 0.5) > 1e-6) {
        return "Terminal gradient stop midpoint must be exactly 0.5";
    }

    return std::nullopt;
}

bool GradientPaint::same_payload(const GradientPaint& other) const noexcept {
    if (schema_major != other.schema_major ||
        schema_minor != other.schema_minor ||
        id != other.id ||
        kind != other.kind ||
        spread != other.spread ||
        interpolation != other.interpolation ||
        stops.size() != other.stops.size()) {
        return false;
    }
    if (std::abs(gradient_to_object.a - other.gradient_to_object.a) > 1e-12 ||
        std::abs(gradient_to_object.b - other.gradient_to_object.b) > 1e-12 ||
        std::abs(gradient_to_object.c - other.gradient_to_object.c) > 1e-12 ||
        std::abs(gradient_to_object.d - other.gradient_to_object.d) > 1e-12 ||
        std::abs(gradient_to_object.tx - other.gradient_to_object.tx) > 1e-12 ||
        std::abs(gradient_to_object.ty - other.gradient_to_object.ty) > 1e-12) {
        return false;
    }
    for (size_t i = 0; i < stops.size(); ++i) {
        if (stops[i] != other.stops[i]) {
            return false;
        }
    }
    return true;
}

GradientPaint GradientPaint::with_reversed_stops() const {
    GradientPaint result = *this;
    const size_t n = stops.size();
    result.stops.resize(n);

    for (size_t i = 0; i < n; ++i) {
        const size_t j = n - 1 - i;
        result.stops[i] = stops[j];
        result.stops[i].offset = 1.0 - stops[j].offset;
        if (i + 1 < n) {
            // In the original array, interval between j-1 and j had midpoint stops[j-1].midpoint_to_next.
            // In the reversed array, that interval is between i and i+1.
            // Its midpoint relative to new left stop is (1.0 - original_midpoint).
            result.stops[i].midpoint_to_next = 1.0 - stops[j - 1].midpoint_to_next;
        } else {
            result.stops[i].midpoint_to_next = 0.5;
        }
    }
    return result;
}

GradientPaint GradientPaint::clone_with_new_ids(
    uint64_t new_paint_id,
    const std::vector<uint64_t>& new_stop_ids) const {
    GradientPaint copy = *this;
    copy.id = new_paint_id;
    for (size_t i = 0; i < copy.stops.size() && i < new_stop_ids.size(); ++i) {
        copy.stops[i].id = new_stop_ids[i];
    }
    return copy;
}

GradientPaint GradientPaint::make_default_linear(
    uint64_t id,
    const geometry::Rect2D& bounds,
    const color::ColorValue& start_color,
    const color::ColorValue& end_color) {
    GradientPaint paint;
    paint.id = id;
    paint.kind = GradientKind::Linear;
    paint.spread = GradientSpread::Pad;
    paint.interpolation = GradientInterpolation::EncodedSRGBStraightAlpha;

    const double w = (bounds.width > 0.0 && std::isfinite(bounds.width)) ? bounds.width : 100.0;
    const double h = (bounds.height > 0.0 && std::isfinite(bounds.height)) ? bounds.height : 100.0;
    const double x = std::isfinite(bounds.x) ? bounds.x : 0.0;
    const double y = std::isfinite(bounds.y) ? bounds.y : 0.0;

    // Linear default: axis from middle of left edge to middle of right edge
    // q=(0,0) -> left center (x, y + h*0.5)
    // q=(1,0) -> right center (x + w, y + h*0.5)
    // perpendicular: +90 rotation of (w, 0) is (0, w)
    paint.gradient_to_object = geometry::Transform2D{
        w, 0.0,
        0.0, w,
        x, y + h * 0.5
    };

    paint.stops = {
        GradientStop{allocate_gradient_stop_id(), 0.0, start_color, 1.0, 0.5},
        GradientStop{allocate_gradient_stop_id(), 1.0, end_color, 1.0, 0.5}
    };
    return paint;
}

GradientPaint GradientPaint::make_default_radial(
    uint64_t id,
    const geometry::Rect2D& bounds,
    const color::ColorValue& center_color,
    const color::ColorValue& edge_color) {
    GradientPaint paint;
    paint.id = id;
    paint.kind = GradientKind::Radial;
    paint.spread = GradientSpread::Pad;
    paint.interpolation = GradientInterpolation::EncodedSRGBStraightAlpha;

    const double w = (bounds.width > 0.0 && std::isfinite(bounds.width)) ? bounds.width : 100.0;
    const double h = (bounds.height > 0.0 && std::isfinite(bounds.height)) ? bounds.height : 100.0;
    const double x = std::isfinite(bounds.x) ? bounds.x : 0.0;
    const double y = std::isfinite(bounds.y) ? bounds.y : 0.0;

    const double cx = x + w * 0.5;
    const double cy = y + h * 0.5;
    const double rx = w * 0.5;
    const double ry = h * 0.5;

    paint.gradient_to_object = geometry::Transform2D{
        rx, 0.0,
        0.0, ry,
        cx, cy
    };

    paint.stops = {
        GradientStop{allocate_gradient_stop_id(), 0.0, center_color, 1.0, 0.5},
        GradientStop{allocate_gradient_stop_id(), 1.0, edge_color, 1.0, 0.5}
    };
    return paint;
}

uint64_t allocate_gradient_paint_id() {
    return generate_object_id();
}

uint64_t allocate_gradient_stop_id() {
    return generate_object_id();
}

void reserve_gradient_paint_id(uint64_t id) {
    reserve_object_id(id);
}

void reserve_gradient_stop_id(uint64_t id) {
    reserve_object_id(id);
}

void reset_gradient_id_allocators_for_tests() {
    // IDs share the global object/path/effect allocator. Never rewind it while
    // immutable snapshots may still own IDs, including in another test fixture.
}

} // namespace pikdraw::document
