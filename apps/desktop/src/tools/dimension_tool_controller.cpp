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
    snap_active_ = false;
    snap_type_ = DimensionSnapType::None;
    ortho_active_ = false;
}

void DimensionToolController::deactivate() {
    active_ = false;
    state_ = DimensionSessionState::Idle;
    references_.clear();
    has_preview_ = false;
    snap_active_ = false;
    snap_type_ = DimensionSnapType::None;
    ortho_active_ = false;
}

void DimensionToolController::set_linear_orientation(geometry::LinearOrientation orientation) noexcept {
    orientation_ = orientation;
    update_preview();
}

void DimensionToolController::set_current_style(document::DimensionStyle style) noexcept {
    style_ = std::move(style);
    update_preview();
}

void DimensionToolController::update_snap(geometry::Point2D world_pos, double zoom, bool ortho_constraint) {
    if (!active_) {
        snap_active_ = false;
        return;
    }

    ortho_active_ = ortho_constraint;
    geometry::Point2D query_pt = world_pos;

    // If picking second point and ORTHO constraint is active (Ctrl or Shift held):
    if (state_ == DimensionSessionState::PickSecond && ortho_constraint) {
        const double dx = query_pt.x - first_point_.x;
        const double dy = query_pt.y - first_point_.y;
        if (std::abs(dx) >= std::abs(dy)) {
            query_pt.y = first_point_.y;
        } else {
            query_pt.x = first_point_.x;
        }
    }

    snap_active_ = false;
    snap_type_ = DimensionSnapType::None;
    snap_point_ = query_pt;
    snapped_object_id_ = document::kInvalidObjectId;
    snapped_element_id_ = std::nullopt;

    const auto snap = doc_.current_snapshot();
    if (!snap) return;

    double best_dist_px = 12.0; // 12 screen pixels snap aperture

    auto test_candidate = [&](geometry::Point2D pt, DimensionSnapType type, document::ObjectId obj_id, std::optional<uint64_t> el_id) {
        const double dist_world = std::hypot(pt.x - query_pt.x, pt.y - query_pt.y);
        const double dist_px = dist_world * zoom;
        if (dist_px < best_dist_px) {
            best_dist_px = dist_px;
            snap_active_ = true;
            snap_type_ = type;
            snap_point_ = pt;
            snapped_object_id_ = obj_id;
            snapped_element_id_ = el_id;
        }
    };

    for (const auto& [id, obj] : snap->objects().all()) {
        if (const auto* path_obj = std::get_if<document::PathObject>(&obj)) {
            if (!path_obj->visible) continue;
            // Never snap to dimension lines themselves
            if (path_obj->contour_spec.starts_with("dimension:v1")) continue;

            const auto b = path_obj->bounds();
            // 4 corners of bounding box
            test_candidate({b.x, b.y}, DimensionSnapType::BoundingBoxCorner, id, std::nullopt);
            test_candidate({b.right(), b.y}, DimensionSnapType::BoundingBoxCorner, id, std::nullopt);
            test_candidate({b.right(), b.bottom()}, DimensionSnapType::BoundingBoxCorner, id, std::nullopt);
            test_candidate({b.x, b.bottom()}, DimensionSnapType::BoundingBoxCorner, id, std::nullopt);

            // 4 midpoints of edges
            test_candidate({b.x + b.width * 0.5, b.y}, DimensionSnapType::Midpoint, id, std::nullopt);
            test_candidate({b.x + b.width * 0.5, b.bottom()}, DimensionSnapType::Midpoint, id, std::nullopt);
            test_candidate({b.x, b.y + b.height * 0.5}, DimensionSnapType::Midpoint, id, std::nullopt);
            test_candidate({b.right(), b.y + b.height * 0.5}, DimensionSnapType::Midpoint, id, std::nullopt);

            // Center
            test_candidate({b.x + b.width * 0.5, b.y + b.height * 0.5}, DimensionSnapType::Center, id, std::nullopt);

            // Path nodes
            if (path_obj->authoring_source) {
                for (const auto& sp : path_obj->authoring_source->subpaths) {
                    for (const auto& node : sp.nodes) {
                        const auto world_anchor = path_obj->transform.map_point(node.anchor);
                        test_candidate(world_anchor, DimensionSnapType::Node, id, node.id.value());
                    }
                }
            } else {
                for (const auto& el : path_obj->path.elements()) {
                    if (el.verb == geometry::PathVerb::MoveTo || el.verb == geometry::PathVerb::LineTo) {
                        const auto world_pt = path_obj->transform.map_point(el.pt);
                        test_candidate(world_pt, DimensionSnapType::Node, id, std::nullopt);
                    }
                }
            }
        }
    }
}

bool DimensionToolController::handle_pointer_press(
    geometry::Point2D world_pos,
    std::optional<document::ObjectId> hit_object,
    std::optional<uint64_t> hit_element) {
    if (!active_) return false;

    const geometry::Point2D effective_pos = snap_active_ ? snap_point_ : world_pos;
    const document::ObjectId effective_obj = snap_active_ && snapped_object_id_ != document::kInvalidObjectId
        ? snapped_object_id_
        : hit_object.value_or(document::kInvalidObjectId);
    const auto effective_el = snap_active_ ? snapped_element_id_ : hit_element;

    if (state_ == DimensionSessionState::Idle) {
        document::SourceReference ref;
        ref.source_object_id = effective_obj;
        ref.element_id = effective_el;
        ref.ref_kind = effective_el.has_value() ? document::SourceRefKind::Node : document::SourceRefKind::ExplicitPoint;
        ref.explicit_point = effective_pos;

        references_.push_back(ref);
        first_point_ = effective_pos;

        if (current_kind_ == document::DimensionKind::Radius ||
            current_kind_ == document::DimensionKind::Diameter ||
            current_kind_ == document::DimensionKind::Ordinate) {
            draft_dim_.placement_point = effective_pos;
            state_ = DimensionSessionState::Placement;
            update_preview();
        } else {
            state_ = DimensionSessionState::PickSecond;
        }
        return true;
    }

    if (state_ == DimensionSessionState::PickSecond) {
        document::SourceReference ref;
        ref.source_object_id = effective_obj;
        ref.element_id = effective_el;
        ref.ref_kind = effective_el.has_value() ? document::SourceRefKind::Node : document::SourceRefKind::ExplicitPoint;
        ref.explicit_point = effective_pos;

        references_.push_back(ref);
        second_point_ = effective_pos;

        draft_dim_.placement_point = effective_pos;
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
    if (!active_) return;

    if (state_ == DimensionSessionState::Placement) {
        geometry::Point2D place_pt = world_pos;
        if (ortho_active_) {
            const geometry::Point2D v = second_point_ - first_point_;
            const double len = std::hypot(v.x, v.y);
            if (len > 1e-6) {
                const geometry::Point2D n{-v.y / len, v.x / len};
                const double dist = (world_pos.x - first_point_.x) * n.x + (world_pos.y - first_point_.y) * n.y;
                place_pt = first_point_ + (second_point_ - first_point_) * 0.5 + n * dist;
            }
        }
        draft_dim_.placement_point = place_pt;
        update_preview();
    }
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
