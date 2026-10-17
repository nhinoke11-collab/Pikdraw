#include "pikdraw/desktop/presenters/gradient_properties.hpp"
#include "pikdraw/document/model/object.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pikdraw::desktop {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool are_colors_equal(const color::ColorValue& a, const color::ColorValue& b) {
    if (a.color_space() != b.color_space()) {
        return false;
    }
    return std::visit(
        [&](const auto& val_a) -> bool {
            using T = std::decay_t<decltype(val_a)>;
            const auto& val_b = std::get<T>(b.payload());
            if constexpr (std::is_same_v<T, color::SpotColor>) {
                return val_a.spot_name == val_b.spot_name &&
                       std::abs(val_a.tint - val_b.tint) < 1e-4;
            } else if constexpr (std::is_same_v<T, color::GrayscaleColor>) {
                return std::abs(val_a.gray - val_b.gray) < 1e-4 &&
                       std::abs(val_a.a - val_b.a) < 1e-4;
            } else {
                return val_a.approx_equal(val_b, 1e-4);
            }
        },
        a.payload());
}

bool are_stops_equal(const std::vector<document::GradientStop>& a,
                     const std::vector<document::GradientStop>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].offset - b[i].offset) > 1e-4 ||
            std::abs(a[i].opacity - b[i].opacity) > 1e-4 ||
            std::abs(a[i].midpoint_to_next - b[i].midpoint_to_next) > 1e-4 ||
            !are_colors_equal(a[i].color, b[i].color)) {
            return false;
        }
    }
    return true;
}

} // namespace

GradientGeometryProjection project_gradient_geometry(
    document::GradientKind kind,
    const geometry::Transform2D& matrix) {
    GradientGeometryProjection proj{};

    if (kind == document::GradientKind::Linear) {
        // (0, 0) maps to start: (tx, ty)
        const double start_x = matrix.tx;
        const double start_y = matrix.ty;
        // Direction vector along X column
        const double dx = matrix.a;
        const double dy = matrix.b;
        proj.length_mm = std::hypot(dx, dy);
        if (proj.length_mm < 1e-9) {
            proj.length_mm = 1e-9;
        }
        proj.angle_degrees = std::atan2(dy, dx) * 180.0 / kPi;
        proj.center_mm = {start_x + 0.5 * dx, start_y + 0.5 * dy};
        proj.aspect_ratio = 1.0;
    } else {
        // Radial center is at (tx, ty)
        proj.center_mm = {matrix.tx, matrix.ty};
        const double rx = std::hypot(matrix.a, matrix.b);
        const double ry = std::hypot(matrix.c, matrix.d);
        proj.length_mm = (rx > 1e-9) ? rx : 1e-9;
        proj.aspect_ratio = (rx > 1e-9) ? (ry / rx) : 1.0;
        proj.angle_degrees = std::atan2(matrix.b, matrix.a) * 180.0 / kPi;
    }

    return proj;
}

geometry::Transform2D compose_gradient_geometry(
    document::GradientKind kind,
    const GradientGeometryProjection& proj) {
    const double rad = proj.angle_degrees * kPi / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);

    if (kind == document::GradientKind::Linear) {
        const double dx = proj.length_mm * c;
        const double dy = proj.length_mm * s;
        const double start_x = proj.center_mm.x - 0.5 * dx;
        const double start_y = proj.center_mm.y - 0.5 * dy;

        return geometry::Transform2D{
            dx, dy,
            -dy, dx,
            start_x, start_y
        };
    } else {
        const double rx = proj.length_mm;
        const double ry = proj.length_mm * proj.aspect_ratio;

        return geometry::Transform2D{
            rx * c, rx * s,
            -ry * s, ry * c,
            proj.center_mm.x, proj.center_mm.y
        };
    }
}

GradientSelectionSummary summarize_gradient_selection(
    const document::DocumentSnapshot& snapshot,
    const std::vector<document::ObjectId>& target_ids) {
    GradientSelectionSummary summary{};
    summary.selected_count = target_ids.size();

    size_t gradient_count = 0;
    bool first_gradient = true;

    for (const auto id : target_ids) {
        const auto* obj_variant = snapshot.objects().lookup_ref(id);
        if (!obj_variant) {
            continue;
        }

        const auto* path_obj = std::get_if<document::PathObject>(obj_variant);
        if (!path_obj) {
            continue;
        }

        summary.supported_count++;

        if (path_obj->fill_gradient) {
            gradient_count++;
            const auto& grad = *path_obj->fill_gradient;
            const auto proj = project_gradient_geometry(grad.kind, grad.gradient_to_object);

            if (first_gradient) {
                first_gradient = false;
                summary.kind = grad.kind;
                summary.spread = grad.spread;
                summary.stops = grad.stops;
                summary.angle_degrees = proj.angle_degrees;
                summary.length_mm = proj.length_mm;
                summary.center_mm = proj.center_mm;
                summary.aspect_ratio = proj.aspect_ratio;
                summary.representative_paint = grad;
            } else {
                if (grad.kind != summary.kind) {
                    summary.mixed_kind = true;
                }
                if (grad.spread != summary.spread) {
                    summary.mixed_spread = true;
                }
                if (!are_stops_equal(grad.stops, summary.stops)) {
                    summary.mixed_stops = true;
                }
                if (std::abs(proj.angle_degrees - summary.angle_degrees) > 0.05) {
                    summary.mixed_angle = true;
                }
                if (std::abs(proj.length_mm - summary.length_mm) > 0.05) {
                    summary.mixed_length = true;
                }
                if (std::hypot(proj.center_mm.x - summary.center_mm.x,
                               proj.center_mm.y - summary.center_mm.y) > 0.05) {
                    summary.mixed_center = true;
                }
            }
        }
    }

    summary.all_gradients = (summary.supported_count > 0 && gradient_count == summary.supported_count);
    summary.any_gradient = (gradient_count > 0);

    return summary;
}

void GradientPropertiesPresenter::load_selection(
    const GradientSelectionSummary& summary,
    const geometry::Rect2D& combined_bounds) {
    summary_ = summary;
    bounds_ = combined_bounds;
    is_dirty_ = false;
    kind_modified_ = false;
    spread_modified_ = false;
    geometry_modified_ = false;
    stops_modified_ = false;

    if (summary_.representative_paint.has_value()) {
        draft_paint_ = summary_.representative_paint;
    } else if (summary_.can_edit_gradient()) {
        const uint64_t paint_id = document::allocate_gradient_paint_id();
        draft_paint_ = document::GradientPaint::make_default_linear(paint_id, bounds_);
    } else {
        draft_paint_.reset();
    }

    bump_generation();
}

void GradientPropertiesPresenter::ensure_draft() {
    if (!draft_paint_.has_value()) {
        const uint64_t paint_id = document::allocate_gradient_paint_id();
        draft_paint_ = document::GradientPaint::make_default_linear(paint_id, bounds_);
        bump_generation();
    }
}

void GradientPropertiesPresenter::bump_generation() {
    draft_generation_++;
}

GradientGeometryProjection GradientPropertiesPresenter::geometry_projection() const {
    if (draft_paint_.has_value()) {
        return project_gradient_geometry(draft_paint_->kind, draft_paint_->gradient_to_object);
    }
    return GradientGeometryProjection{};
}

void GradientPropertiesPresenter::set_kind(document::GradientKind kind) {
    ensure_draft();
    if (draft_paint_->kind == kind) {
        return;
    }
    draft_paint_->kind = kind;
    // When changing type, reset geometry to default on bounds per section 5.3
    if (kind == document::GradientKind::Linear) {
        const auto def = document::GradientPaint::make_default_linear(draft_paint_->id, bounds_);
        draft_paint_->gradient_to_object = def.gradient_to_object;
    } else {
        const auto def = document::GradientPaint::make_default_radial(draft_paint_->id, bounds_);
        draft_paint_->gradient_to_object = def.gradient_to_object;
    }
    kind_modified_ = true;
    geometry_modified_ = true;
    is_dirty_ = true;
    bump_generation();
}

void GradientPropertiesPresenter::set_spread(document::GradientSpread spread) {
    ensure_draft();
    if (draft_paint_->spread == spread) {
        return;
    }
    draft_paint_->spread = spread;
    spread_modified_ = true;
    is_dirty_ = true;
    bump_generation();
}

void GradientPropertiesPresenter::set_angle_degrees(double degrees) {
    ensure_draft();
    auto proj = project_gradient_geometry(draft_paint_->kind, draft_paint_->gradient_to_object);
    proj.angle_degrees = degrees;
    draft_paint_->gradient_to_object = compose_gradient_geometry(draft_paint_->kind, proj);
    geometry_modified_ = true;
    is_dirty_ = true;
    bump_generation();
}

void GradientPropertiesPresenter::set_length_mm(double length) {
    ensure_draft();
    if (length < 1e-6) {
        length = 1e-6;
    }
    auto proj = project_gradient_geometry(draft_paint_->kind, draft_paint_->gradient_to_object);
    proj.length_mm = length;
    draft_paint_->gradient_to_object = compose_gradient_geometry(draft_paint_->kind, proj);
    geometry_modified_ = true;
    is_dirty_ = true;
    bump_generation();
}

void GradientPropertiesPresenter::set_center_mm(geometry::Point2D center) {
    ensure_draft();
    auto proj = project_gradient_geometry(draft_paint_->kind, draft_paint_->gradient_to_object);
    proj.center_mm = center;
    draft_paint_->gradient_to_object = compose_gradient_geometry(draft_paint_->kind, proj);
    geometry_modified_ = true;
    is_dirty_ = true;
    bump_generation();
}

bool GradientPropertiesPresenter::add_stop(double offset, const color::ColorValue& color, double opacity, double midpoint) {
    ensure_draft();
    if (draft_paint_->stops.size() >= document::kMaxGradientStops) {
        return false;
    }
    (void)midpoint;

    document::GradientStop new_stop{};
    new_stop.id = document::allocate_gradient_stop_id();
    new_stop.offset = std::clamp(offset, 0.0, 1.0);
    new_stop.color = color;
    new_stop.opacity = std::clamp(opacity, 0.0, 1.0);
    new_stop.midpoint_to_next = 0.5; // per spec: two new intervals midpoint 50%

    // Find insertion index
    auto& stops = draft_paint_->stops;
    auto it = std::lower_bound(stops.begin(), stops.end(), new_stop.offset,
                               [](const document::GradientStop& s, double val) {
                                   return s.offset < val;
                               });
    size_t insert_idx = std::distance(stops.begin(), it);
    if (insert_idx > 0) {
        stops[insert_idx - 1].midpoint_to_next = 0.5;
    }
    stops.insert(it, new_stop);

    stops_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

bool GradientPropertiesPresenter::can_delete_stop(size_t index, std::string* out_reason) const {
    if (!draft_paint_.has_value() || draft_paint_->stops.size() <= 2) {
        if (out_reason) {
            *out_reason = "Gradient phải có tối thiểu 2 stop.";
        }
        return false;
    }
    if (index >= draft_paint_->stops.size()) {
        if (out_reason) {
            *out_reason = "Chỉ số stop không hợp lệ.";
        }
        return false;
    }
    return true;
}

bool GradientPropertiesPresenter::delete_stop(size_t index, std::string* out_reason) {
    if (!can_delete_stop(index, out_reason)) {
        return false;
    }
    auto& stops = draft_paint_->stops;
    stops.erase(stops.begin() + index);

    // Connecting interval has midpoint 50% per spec
    if (index > 0 && index < stops.size()) {
        stops[index - 1].midpoint_to_next = 0.5;
    }

    stops_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

bool GradientPropertiesPresenter::set_stop_offset(size_t index, double offset) {
    ensure_draft();
    auto& stops = draft_paint_->stops;
    if (index >= stops.size()) {
        return false;
    }
    stops[index].offset = std::clamp(offset, 0.0, 1.0);
    std::stable_sort(stops.begin(), stops.end(),
                     [](const auto& a, const auto& b) { return a.offset < b.offset; });
    stops_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

bool GradientPropertiesPresenter::set_stop_color(size_t index, const color::ColorValue& color) {
    ensure_draft();
    auto& stops = draft_paint_->stops;
    if (index >= stops.size()) {
        return false;
    }
    stops[index].color = color;
    stops_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

bool GradientPropertiesPresenter::set_stop_opacity(size_t index, double opacity) {
    ensure_draft();
    auto& stops = draft_paint_->stops;
    if (index >= stops.size()) {
        return false;
    }
    stops[index].opacity = std::clamp(opacity, 0.0, 1.0);
    stops_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

bool GradientPropertiesPresenter::set_segment_midpoint(size_t segment_index, double midpoint) {
    ensure_draft();
    auto& stops = draft_paint_->stops;
    if (segment_index >= stops.size() - 1) {
        return false;
    }
    // Midpoint bounded [0.05, 0.95] per spec
    stops[segment_index].midpoint_to_next = std::clamp(midpoint, 0.05, 0.95);
    stops_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

bool GradientPropertiesPresenter::reverse_stops() {
    ensure_draft();
    draft_paint_ = draft_paint_->with_reversed_stops();
    stops_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

bool GradientPropertiesPresenter::fit_to_bounds(const geometry::Rect2D& bounds) {
    ensure_draft();
    if (!bounds.is_finite() || bounds.width <= 0.0 || bounds.height <= 0.0) {
        return false;
    }
    bounds_ = bounds;
    if (draft_paint_->kind == document::GradientKind::Linear) {
        const auto def = document::GradientPaint::make_default_linear(draft_paint_->id, bounds);
        draft_paint_->gradient_to_object = def.gradient_to_object;
    } else {
        const auto def = document::GradientPaint::make_default_radial(draft_paint_->id, bounds);
        draft_paint_->gradient_to_object = def.gradient_to_object;
    }
    geometry_modified_ = true;
    is_dirty_ = true;
    bump_generation();
    return true;
}

void GradientPropertiesPresenter::confirm_field() noexcept {
    // Enter key confirms field into draft without committing document
    // Keeps current draft state active
}

void GradientPropertiesPresenter::cancel() {
    // Revert draft changes back to initial summary state
    is_dirty_ = false;
    kind_modified_ = false;
    spread_modified_ = false;
    geometry_modified_ = false;
    stops_modified_ = false;

    if (summary_.representative_paint.has_value()) {
        draft_paint_ = summary_.representative_paint;
    } else if (summary_.can_edit_gradient()) {
        const uint64_t paint_id = document::allocate_gradient_paint_id();
        draft_paint_ = document::GradientPaint::make_default_linear(paint_id, bounds_);
    } else {
        draft_paint_.reset();
    }
    bump_generation();
}

void GradientPropertiesPresenter::set_draft_paint(document::GradientPaint paint) {
    if (paint.validation_error().has_value()) {
        return;
    }
    draft_paint_ = std::move(paint);
    is_dirty_ = true;
    kind_modified_ = true;
    spread_modified_ = true;
    geometry_modified_ = true;
    stops_modified_ = true;
    bump_generation();
}

} // namespace pikdraw::desktop
