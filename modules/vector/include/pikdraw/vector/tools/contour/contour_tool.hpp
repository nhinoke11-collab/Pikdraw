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
    double arc_tolerance_mm_{0.005};
    geometry::ContourResult preview_result_;
};

} // namespace pikdraw::vector::tools::contour
