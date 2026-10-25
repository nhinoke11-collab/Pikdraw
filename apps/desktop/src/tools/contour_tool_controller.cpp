#include "contour_tool_controller.hpp"

namespace pikdraw::desktop::tools {

bool ContourToolController::activate(document::ObjectId target_id) {
    if (target_id != document::kInvalidObjectId) {
        tool_->set_target_object(target_id);
    } else if (tool_->target_object() == document::kInvalidObjectId) {
        // Pick first available PathObject if not set
        const auto snap = document_.current_snapshot();
        if (snap) {
            for (const auto& [id, obj] : snap->objects().all()) {
                if (std::holds_alternative<document::PathObject>(obj)) {
                    tool_->set_target_object(id);
                    break;
                }
            }
        }
    }
    is_active_ = true;
    return true;
}

void ContourToolController::deactivate() {
    is_active_ = false;
}

void ContourToolController::set_direction(geometry::ContourDirection dir) {
    tool_->set_direction(dir);
}

geometry::ContourDirection ContourToolController::direction() const noexcept {
    return tool_->direction();
}

void ContourToolController::set_spacing_mm(double spacing) {
    tool_->set_spacing_mm(spacing);
}

double ContourToolController::spacing_mm() const noexcept {
    return tool_->spacing_mm();
}

void ContourToolController::set_steps(int steps) {
    tool_->set_steps(steps);
}

int ContourToolController::steps() const noexcept {
    return tool_->steps();
}

void ContourToolController::set_join_type(geometry::JoinType join) {
    tool_->set_join_type(join);
}

geometry::JoinType ContourToolController::join_type() const noexcept {
    return tool_->join_type();
}

void ContourToolController::set_miter_limit(double limit) {
    tool_->set_miter_limit(limit);
}

double ContourToolController::miter_limit() const noexcept {
    return tool_->miter_limit();
}

bool ContourToolController::set_arc_tolerance_mm(double tolerance) {
    return tool_->set_arc_tolerance_mm(tolerance);
}

double ContourToolController::arc_tolerance_mm() const noexcept {
    return tool_->arc_tolerance_mm();
}

bool ContourToolController::apply() {
    return tool_->apply();
}

bool ContourToolController::break_apart() {
    return tool_->break_apart();
}

bool ContourToolController::remove() {
    return tool_->remove();
}

void ContourToolController::cancel() {
    tool_->cancel();
}

bool ContourToolController::has_preview() const noexcept {
    return tool_->has_preview();
}

const geometry::ContourResult& ContourToolController::preview_result() const noexcept {
    return tool_->preview_result();
}

document::ObjectId ContourToolController::target_object() const noexcept {
    return tool_->target_object();
}

} // namespace pikdraw::desktop::tools
