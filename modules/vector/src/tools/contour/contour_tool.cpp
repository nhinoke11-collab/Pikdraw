#include "pikdraw/vector/tools/contour/contour_tool.hpp"
#include <algorithm>

namespace pikdraw::vector::tools::contour {

void ContourTool::set_target_object(document::ObjectId object_id) {
    if (target_object_id_ == object_id) return;
    target_object_id_ = object_id;

    if (target_object_id_ != document::kInvalidObjectId) {
        const auto feat = document::get_object_contour(document_, target_object_id_);
        if (feat) {
            direction_ = feat->direction;
            spacing_mm_ = feat->spacing_mm;
            steps_ = feat->steps;
            join_type_ = feat->join_type;
            miter_limit_ = feat->miter_limit;
            arc_tolerance_mm_ = feat->arc_tolerance_mm;
            outline_color_ = feat->outline_color;
            fill_color_ = feat->fill_color;
            has_outline_ = feat->has_outline;
            has_fill_ = feat->has_fill;
            stroke_width_mm_ = feat->stroke_width_mm;
        }
    }
    update_preview();
}

void ContourTool::set_direction(geometry::ContourDirection dir) {
    direction_ = dir;
    update_preview();
}

void ContourTool::set_spacing_mm(double spacing) {
    spacing_mm_ = std::max(0.01, spacing);
    update_preview();
}

void ContourTool::set_steps(int steps) {
    steps_ = std::clamp(steps, 1, 99);
    update_preview();
}

void ContourTool::set_join_type(geometry::JoinType join) {
    join_type_ = join;
    update_preview();
}

void ContourTool::set_miter_limit(double limit) {
    miter_limit_ = std::clamp(limit, 1.0, 100.0);
    update_preview();
}

void ContourTool::update_preview() {
    if (target_object_id_ == document::kInvalidObjectId) {
        preview_result_ = geometry::ContourResult{};
        return;
    }

    const auto opt = document_.objects().get(target_object_id_);
    if (!opt) {
        preview_result_ = geometry::ContourResult{};
        return;
    }

    const auto* path_obj = std::get_if<document::PathObject>(&(*opt));
    if (!path_obj) {
        preview_result_ = geometry::ContourResult{};
        return;
    }

    geometry::ContourRequest req;
    req.path = path_obj->path;
    req.direction = direction_;
    req.spacing_mm = spacing_mm_;
    req.steps = steps_;
    req.join_type = join_type_;
    req.miter_limit = miter_limit_;
    req.arc_tolerance_mm = arc_tolerance_mm_;

    preview_result_ = geometry::compute_contour(req);
}

bool ContourTool::apply() {
    if (target_object_id_ == document::kInvalidObjectId) return false;

    document::ContourFeature feat;
    feat.target_id = target_object_id_;
    feat.direction = direction_;
    feat.spacing_mm = spacing_mm_;
    feat.steps = steps_;
    feat.join_type = join_type_;
    feat.miter_limit = miter_limit_;
    feat.arc_tolerance_mm = arc_tolerance_mm_;
    feat.outline_color = outline_color_;
    feat.fill_color = fill_color_;
    feat.has_outline = has_outline_;
    feat.has_fill = has_fill_;
    feat.stroke_width_mm = stroke_width_mm_;

    auto cmd = std::make_unique<document::ApplyContourCommand>(target_object_id_, feat);
    auto res = document_.execute_command(std::move(cmd));
    if (res.success) {
        update_preview();
        return true;
    }
    return false;
}

bool ContourTool::break_apart() {
    if (target_object_id_ == document::kInvalidObjectId) return false;

    auto cmd = std::make_unique<document::BreakContourApartCommand>(target_object_id_);
    auto res = document_.execute_command(std::move(cmd));
    if (res.success) {
        preview_result_ = geometry::ContourResult{};
        return true;
    }
    return false;
}

bool ContourTool::remove() {
    if (target_object_id_ == document::kInvalidObjectId) return false;

    auto cmd = std::make_unique<document::RemoveContourCommand>(target_object_id_);
    auto res = document_.execute_command(std::move(cmd));
    if (res.success) {
        preview_result_ = geometry::ContourResult{};
        return true;
    }
    return false;
}

void ContourTool::cancel() {
    if (target_object_id_ != document::kInvalidObjectId) {
        const auto feat = document::get_object_contour(document_, target_object_id_);
        if (feat) {
            direction_ = feat->direction;
            spacing_mm_ = feat->spacing_mm;
            steps_ = feat->steps;
            join_type_ = feat->join_type;
            miter_limit_ = feat->miter_limit;
            arc_tolerance_mm_ = feat->arc_tolerance_mm;
            outline_color_ = feat->outline_color;
            fill_color_ = feat->fill_color;
            has_outline_ = feat->has_outline;
            has_fill_ = feat->has_fill;
            stroke_width_mm_ = feat->stroke_width_mm;
        }
    }
    update_preview();
}

} // namespace pikdraw::vector::tools::contour
