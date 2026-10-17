#include "dimension_tool_controller.hpp"

namespace pikdraw::desktop::tools {

DimensionToolController::DimensionToolController(document::Document& doc)
    : doc_(doc) {}

void DimensionToolController::activate(document::DimensionKind kind, bool smart_mode) {
    active_ = true;
    smart_mode_ = smart_mode;
    current_kind_ = kind;
    state_ = DimensionSessionState::Idle;
    references_.clear();
    has_preview_ = false;
}

void DimensionToolController::deactivate() {
    active_ = false;
    state_ = DimensionSessionState::Idle;
    references_.clear();
    has_preview_ = false;
}

void DimensionToolController::set_linear_orientation(geometry::LinearOrientation orientation) noexcept {
    orientation_ = orientation;
    update_preview();
}

void DimensionToolController::set_current_style(document::DimensionStyle style) noexcept {
    style_ = std::move(style);
    update_preview();
}

bool DimensionToolController::handle_pointer_press(
    geometry::Point2D world_pos,
    std::optional<document::ObjectId> hit_object,
    std::optional<uint64_t> hit_element) {
    if (!active_) return false;

    if (state_ == DimensionSessionState::Idle) {
        document::SourceReference ref;
        ref.source_object_id = hit_object.value_or(document::kInvalidObjectId);
        ref.element_id = hit_element;
        ref.ref_kind = hit_element.has_value() ? document::SourceRefKind::Node : document::SourceRefKind::ExplicitPoint;
        ref.explicit_point = world_pos;

        references_.push_back(ref);
        first_point_ = world_pos;

        if (current_kind_ == document::DimensionKind::Radius ||
            current_kind_ == document::DimensionKind::Diameter ||
            current_kind_ == document::DimensionKind::Ordinate) {
            draft_dim_.placement_point = world_pos;
            state_ = DimensionSessionState::Placement;
            update_preview();
        } else {
            state_ = DimensionSessionState::PickSecond;
        }
        return true;
    }

    if (state_ == DimensionSessionState::PickSecond) {
        document::SourceReference ref;
        ref.source_object_id = hit_object.value_or(document::kInvalidObjectId);
        ref.element_id = hit_element;
        ref.ref_kind = hit_element.has_value() ? document::SourceRefKind::Node : document::SourceRefKind::ExplicitPoint;
        ref.explicit_point = world_pos;

        references_.push_back(ref);
        second_point_ = world_pos;

        draft_dim_.placement_point = world_pos;
        state_ = DimensionSessionState::Placement;
        update_preview();
        return true;
    }

    if (state_ == DimensionSessionState::Placement) {
        draft_dim_.placement_point = world_pos;
        update_preview();
        return commit();
    }

    return false;
}

void DimensionToolController::handle_pointer_move(geometry::Point2D world_pos) {
    if (!active_ || state_ != DimensionSessionState::Placement) return;

    draft_dim_.placement_point = world_pos;
    update_preview();
}

bool DimensionToolController::handle_pointer_release(geometry::Point2D /*world_pos*/) {
    return active_;
}

bool DimensionToolController::handle_escape() noexcept {
    if (!active_) return false;

    if (state_ == DimensionSessionState::Placement) {
        state_ = DimensionSessionState::PickSecond;
        if (references_.size() > 1) {
            references_.pop_back();
        }
        has_preview_ = false;
        return true;
    }

    if (state_ == DimensionSessionState::PickSecond) {
        state_ = DimensionSessionState::Idle;
        references_.clear();
        has_preview_ = false;
        return true;
    }

    return false;
}

bool DimensionToolController::commit() {
    if (!has_preview_ || !preview_layout_.valid) return false;

    draft_dim_.id = document::generate_object_id();
    draft_dim_.style = style_;
    draft_dim_.kind = current_kind_;
    draft_dim_.linear_orientation = orientation_;
    draft_dim_.references = references_;

    const auto res = doc_.execute_command(std::make_unique<document::AddDimensionCommand>(draft_dim_, 0));
    if (res.success) {
        state_ = DimensionSessionState::Idle;
        references_.clear();
        has_preview_ = false;
        return true;
    }
    return false;
}

void DimensionToolController::update_preview() {
    if (state_ != DimensionSessionState::Placement) return;

    draft_dim_.kind = current_kind_;
    draft_dim_.linear_orientation = orientation_;
    draft_dim_.style = style_;
    draft_dim_.references = references_;

    preview_layout_ = document::evaluate_dimension_layout(draft_dim_, doc_);
    has_preview_ = preview_layout_.valid;
}

} // namespace pikdraw::desktop::tools
