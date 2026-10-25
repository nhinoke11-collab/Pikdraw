#include "annotation_tool_controller.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::desktop::tools {

AnnotationToolController::AnnotationToolController(document::Document& doc)
    : doc_(doc) {}

void AnnotationToolController::activate(AnnotationToolMode mode) {
    active_ = true;
    mode_ = mode;
    state_ = AnnotationSessionState::Idle;
    has_preview_ = false;
}

void AnnotationToolController::deactivate() {
    active_ = false;
    state_ = AnnotationSessionState::Idle;
    has_preview_ = false;
}

void AnnotationToolController::set_mode(AnnotationToolMode mode) noexcept {
    mode_ = mode;
    state_ = AnnotationSessionState::Idle;
    has_preview_ = false;
}

void AnnotationToolController::set_scale_ratio(double ratio) {
    if (ratio >= 0.1 && ratio <= 50.0) {
        scale_ratio_ = ratio;
        draft_detail_view_.scale_ratio = ratio;
        if (has_preview_ && mode_ == AnnotationToolMode::DetailView) {
            update_preview();
        }
    }
}

void AnnotationToolController::set_detail_shape(document::DetailViewShape shape) {
    detail_shape_ = shape;
    draft_detail_view_.shape = shape;
    if (has_preview_ && mode_ == AnnotationToolMode::DetailView) {
        update_preview();
    }
}

void AnnotationToolController::set_detail_label(std::string label) {
    detail_label_ = std::move(label);
    draft_detail_view_.label = detail_label_;
    if (has_preview_ && mode_ == AnnotationToolMode::DetailView) {
        update_preview();
    }
}

void AnnotationToolController::set_note_text(std::string text) {
    note_text_ = std::move(text);
    draft_annotation_.text = note_text_;
    if (has_preview_) {
        update_preview();
    }
}

bool AnnotationToolController::handle_pointer_press(
    geometry::Point2D world_pos,
    std::optional<document::ObjectId> hit_object,
    std::optional<uint64_t> hit_element) {
    if (!active_) return false;

    press_point_ = world_pos;
    current_point_ = world_pos;

    if (mode_ == AnnotationToolMode::DetailView) {
        if (state_ == AnnotationSessionState::Idle) {
            state_ = AnnotationSessionState::DetailSourceDragging;
            draft_detail_view_ = document::DetailViewObject{};
            draft_detail_view_.scale_ratio = scale_ratio_;
            draft_detail_view_.shape = detail_shape_;
            draft_detail_view_.label = detail_label_;
            constexpr double kInitR = 10.0;
            draft_detail_view_.source_region = {world_pos.x - kInitR, world_pos.y - kInitR, 2 * kInitR, 2 * kInitR};
            update_preview();
            return true;
        } else if (state_ == AnnotationSessionState::DetailFramePlacing) {
            return commit();
        }
    } else if (mode_ == AnnotationToolMode::LeaderNote) {
        if (state_ == AnnotationSessionState::Idle) {
            state_ = AnnotationSessionState::LeaderAnchorPicked;
            draft_annotation_ = document::AnnotationObject{};
            draft_annotation_.kind = document::AnnotationKind::LeaderNote;
            draft_annotation_.anchor.anchor_pt = world_pos;
            if (hit_object.has_value()) {
                draft_annotation_.anchor.source_object_id = *hit_object;
                draft_annotation_.anchor.state = document::AssociationState::Valid;
                draft_annotation_.anchor.element_id = hit_element;
            } else {
                draft_annotation_.anchor.state = document::AssociationState::Fixed;
            }
            draft_annotation_.leader_points.push_back(world_pos);
            draft_annotation_.text = note_text_;
            update_preview();
            return true;
        } else if (state_ == AnnotationSessionState::LeaderAnchorPicked) {
            state_ = AnnotationSessionState::LeaderElbowPicked;
            draft_annotation_.leader_points.push_back(world_pos);
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            update_preview();
            return true;
        } else if (state_ == AnnotationSessionState::LeaderElbowPicked) {
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            draft_annotation_.text = note_text_;
            return commit();
        }
    } else if (mode_ == AnnotationToolMode::PlainNote) {
        draft_annotation_ = document::AnnotationObject{};
        draft_annotation_.kind = document::AnnotationKind::PlainNote;
        draft_annotation_.text_point = world_pos;
        draft_annotation_.text = note_text_;
        return commit();
    }

    return false;
}

void AnnotationToolController::handle_pointer_move(geometry::Point2D world_pos) {
    if (!active_) return;
    current_point_ = world_pos;

    if (mode_ == AnnotationToolMode::DetailView) {
        if (state_ == AnnotationSessionState::DetailSourceDragging) {
            double r = std::hypot(world_pos.x - press_point_.x, world_pos.y - press_point_.y);
            if (r < 2.0) r = 10.0;
            draft_detail_view_.source_region = {press_point_.x - r, press_point_.y - r, 2 * r, 2 * r};
            update_preview();
        } else if (state_ == AnnotationSessionState::DetailFramePlacing) {
            const double sr = (std::min)(draft_detail_view_.source_region.width, draft_detail_view_.source_region.height) * 0.5;
            const double fr = sr * scale_ratio_;
            draft_detail_view_.frame_rect = {world_pos.x - fr, world_pos.y - fr, 2 * fr, 2 * fr};
            update_preview();
        }
    } else if (mode_ == AnnotationToolMode::LeaderNote) {
        if (state_ == AnnotationSessionState::LeaderAnchorPicked) {
            if (!draft_annotation_.leader_points.empty()) {
                draft_annotation_.leader_points.back() = world_pos;
            } else {
                draft_annotation_.leader_points.push_back(world_pos);
            }
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            update_preview();
        } else if (state_ == AnnotationSessionState::LeaderElbowPicked) {
            draft_annotation_.landing_point = world_pos;
            draft_annotation_.text_point = world_pos;
            update_preview();
        }
    }
}

bool AnnotationToolController::handle_pointer_release(geometry::Point2D world_pos) {
    if (!active_) return false;

    if (mode_ == AnnotationToolMode::DetailView) {
        if (state_ == AnnotationSessionState::DetailSourceDragging) {
            state_ = AnnotationSessionState::DetailFramePlacing;
            const double sr = (std::min)(draft_detail_view_.source_region.width, draft_detail_view_.source_region.height) * 0.5;
            const double fr = sr * scale_ratio_;
            // Position frame slightly offset from mouse release
            draft_detail_view_.frame_rect = {world_pos.x + 10.0, world_pos.y - fr, 2 * fr, 2 * fr};
            update_preview();
            return true;
        }
    }

    return false;
}

bool AnnotationToolController::handle_escape() noexcept {
    if (!active_) return false;
    if (state_ != AnnotationSessionState::Idle) {
        state_ = AnnotationSessionState::Idle;
        has_preview_ = false;
        return true;
    }
    return false;
}

void AnnotationToolController::update_preview() {
    if (mode_ == AnnotationToolMode::DetailView) {
        preview_detail_view_layout_ = document::evaluate_detail_view_layout(draft_detail_view_);
        has_preview_ = true;
    } else {
        preview_annotation_layout_ = document::evaluate_annotation_layout(draft_annotation_);
        has_preview_ = true;
    }
}

bool AnnotationToolController::commit() {
    if (!active_) return false;

    if (mode_ == AnnotationToolMode::DetailView) {
        auto cmd = std::make_unique<document::AddDetailViewCommand>(draft_detail_view_);
        const auto res = doc_.execute_command(std::move(cmd));
        state_ = AnnotationSessionState::Idle;
        has_preview_ = false;
        return res.success;
    } else {
        auto cmd = std::make_unique<document::AddAnnotationCommand>(draft_annotation_);
        const auto res = doc_.execute_command(std::move(cmd));
        state_ = AnnotationSessionState::Idle;
        has_preview_ = false;
        return res.success;
    }
}

} // namespace pikdraw::desktop::tools
