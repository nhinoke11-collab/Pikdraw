#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/contour_commands.hpp"
#include "pikdraw/geometry/operations/offset.hpp"
#include <vector>

namespace pikdraw::vector::tools::contour {

class ContourTool {
public:
    explicit ContourTool(document::Document& document) noexcept
        : document_(document) {}

    void set_target_object(document::ObjectId object_id);
    [[nodiscard]] document::ObjectId target_object() const noexcept { return target_object_id_; }

    void set_direction(geometry::ContourDirection dir);
    [[nodiscard]] geometry::ContourDirection direction() const noexcept { return direction_; }

    void set_spacing_mm(double spacing);
    [[nodiscard]] double spacing_mm() const noexcept { return spacing_mm_; }

    void set_steps(int steps);
    [[nodiscard]] int steps() const noexcept { return steps_; }

    void set_join_type(geometry::JoinType join);
    [[nodiscard]] geometry::JoinType join_type() const noexcept { return join_type_; }

    void set_miter_limit(double limit);
    [[nodiscard]] double miter_limit() const noexcept { return miter_limit_; }

    bool set_arc_tolerance_mm(double tolerance);
    /// Returns the effective supported tolerance for UI/status consumers.
    /// A legacy serialized value below the floor remains stored until Apply.
    [[nodiscard]] double arc_tolerance_mm() const noexcept;

    void set_outline_color(color::ColorValue col) noexcept { outline_color_ = std::move(col); }
    [[nodiscard]] const color::ColorValue& outline_color() const noexcept { return outline_color_; }

    void set_fill_color(color::ColorValue col) noexcept { fill_color_ = std::move(col); }
    [[nodiscard]] const color::ColorValue& fill_color() const noexcept { return fill_color_; }

    void set_has_outline(bool has) noexcept { has_outline_ = has; }
    [[nodiscard]] bool has_outline() const noexcept { return has_outline_; }

    void set_has_fill(bool has) noexcept { has_fill_ = has; }
    [[nodiscard]] bool has_fill() const noexcept { return has_fill_; }

    void set_stroke_width_mm(double width) noexcept { stroke_width_mm_ = width; }
    [[nodiscard]] double stroke_width_mm() const noexcept { return stroke_width_mm_; }

    void update_preview();
    [[nodiscard]] const geometry::ContourResult& preview_result() const noexcept { return preview_result_; }
    [[nodiscard]] bool has_preview() const noexcept {
        return preview_result_.status == geometry::OffsetStatus::Success;
    }

    bool apply();
    bool break_apart();
    bool remove();
    void cancel();

private:
    document::Document& document_;
    document::ObjectId target_object_id_{document::kInvalidObjectId};
    geometry::ContourDirection direction_{geometry::ContourDirection::Outside};
    double spacing_mm_{1.0};
    int steps_{1};
    geometry::JoinType join_type_{geometry::JoinType::Miter};
    double miter_limit_{4.0};
    double arc_tolerance_mm_{geometry::kContourDefaultArcToleranceMm};
    color::ColorValue outline_color_{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    color::ColorValue fill_color_{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    bool has_outline_{true};
    bool has_fill_{false};
    double stroke_width_mm_{0.2};
    geometry::ContourResult preview_result_;
};

} // namespace pikdraw::vector::tools::contour
