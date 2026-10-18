#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::desktop {

/// Multi-selection summary of gradient state across selected objects.
/// Complies with AC-G09: does not zero out mixed values or blindly overwrite
/// objects with object 0's properties.
struct GradientSelectionSummary {
    size_t selected_count{0};
    size_t supported_count{0};
    bool all_gradients{false};
    bool any_gradient{false};

    // Mixed flags
    bool mixed_kind{false};
    bool mixed_spread{false};
    bool mixed_stops{false};
    bool mixed_angle{false};
    bool mixed_length{false};
    bool mixed_center{false};
    bool mixed_aspect_ratio{false};

    // Representative values when uniform (or initial defaults)
    document::GradientKind kind{document::GradientKind::Linear};
    document::GradientSpread spread{document::GradientSpread::Pad};
    std::vector<document::GradientStop> stops;
    double angle_degrees{0.0};
    double length_mm{100.0};
    geometry::Point2D center_mm{0.0, 0.0};
    double aspect_ratio{1.0}; // for radial

    std::optional<document::GradientPaint> representative_paint;

    [[nodiscard]] bool can_edit_gradient() const noexcept {
        return supported_count > 0;
    }
};

/// 2D geometric projection of GradientPaint affine matrix for UI fields
/// (Angle, Length, Center, Aspect Ratio).
struct GradientGeometryProjection {
    double angle_degrees{0.0};
    double length_mm{100.0};
    geometry::Point2D center_mm{0.0, 0.0};
    double aspect_ratio{1.0};
};

/// Projects affine matrix to user-friendly angle/length/center parameters
[[nodiscard]] GradientGeometryProjection project_gradient_geometry(
    document::GradientKind kind,
    const geometry::Transform2D& matrix);

/// Composes affine matrix from angle/length/center parameters
[[nodiscard]] geometry::Transform2D compose_gradient_geometry(
    document::GradientKind kind,
    const GradientGeometryProjection& proj);

/// Summarizes gradient fill state from DocumentSnapshot
[[nodiscard]] GradientSelectionSummary summarize_gradient_selection(
    const document::DocumentSnapshot& snapshot,
    const std::vector<document::ObjectId>& target_ids);

/// Presenter coordinating the Properties dock, contextual property bar,
/// and local draft edits. Supports Enter confirmation without commit,
/// and Apply / Cancel lifecycle.
class GradientPropertiesPresenter {
public:
    GradientPropertiesPresenter() = default;

    void load_selection(const GradientSelectionSummary& summary, const geometry::Rect2D& combined_bounds);

    [[nodiscard]] bool has_draft() const noexcept { return draft_paint_.has_value(); }
    [[nodiscard]] const document::GradientPaint* draft_paint() const noexcept {
        return draft_paint_.has_value() ? &*draft_paint_ : nullptr;
    }
    [[nodiscard]] bool is_dirty() const noexcept { return is_dirty_; }
    [[nodiscard]] bool preview_enabled() const noexcept { return preview_enabled_; }
    void set_preview_enabled(bool enabled) noexcept { preview_enabled_ = enabled; }
    [[nodiscard]] uint64_t draft_generation() const noexcept { return draft_generation_; }

    [[nodiscard]] const GradientSelectionSummary& summary() const noexcept { return summary_; }
    [[nodiscard]] GradientGeometryProjection geometry_projection() const;
    [[nodiscard]] uint64_t selected_stop_id() const noexcept { return selected_stop_id_; }
    bool select_stop(uint64_t stop_id) noexcept;

    // Field modifications
    void set_kind(document::GradientKind kind);
    void set_spread(document::GradientSpread spread);
    void set_angle_degrees(double degrees);
    void set_length_mm(double length);
    void set_center_mm(geometry::Point2D center);

    // Stop modifications
    bool add_stop(double offset, const color::ColorValue& color, double opacity = 1.0, double midpoint = 0.5);
    bool delete_stop(size_t index, std::string* out_reason = nullptr);
    [[nodiscard]] bool can_delete_stop(size_t index, std::string* out_reason = nullptr) const;
    bool set_stop_offset(size_t index, double offset);
    bool set_stop_color(size_t index, const color::ColorValue& color);
    bool set_stop_opacity(size_t index, double opacity);
    bool set_segment_midpoint(size_t segment_index, double midpoint);
    bool reverse_stops();
    bool fit_to_bounds(const geometry::Rect2D& bounds);

    // Enter confirmation for field editing (confirms value into draft without committing document)
    void confirm_field() noexcept;

    // Transaction lifecycle
    void mark_clean() noexcept { is_dirty_ = false; }
    void cancel();

    void set_draft_paint(document::GradientPaint paint);

    // Query which aspects were modified in this draft session
    [[nodiscard]] bool kind_modified() const noexcept { return kind_modified_; }
    [[nodiscard]] bool spread_modified() const noexcept { return spread_modified_; }
    [[nodiscard]] bool geometry_modified() const noexcept { return geometry_modified_; }
    [[nodiscard]] bool stops_modified() const noexcept { return stops_modified_; }

private:
    void ensure_draft();
    void bump_generation();

    GradientSelectionSummary summary_;
    geometry::Rect2D bounds_{0.0, 0.0, 100.0, 100.0};
    std::optional<document::GradientPaint> draft_paint_;
    uint64_t selected_stop_id_{0};

    bool is_dirty_{false};
    bool preview_enabled_{true};
    uint64_t draft_generation_{1};

    bool kind_modified_{false};
    bool spread_modified_{false};
    bool geometry_modified_{false};
    bool stops_modified_{false};
};

} // namespace pikdraw::desktop
