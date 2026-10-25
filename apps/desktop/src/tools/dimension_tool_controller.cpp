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
    baseline_step_count_ = 0;
}

void DimensionToolController::deactivate() {
    active_ = false;
    state_ = DimensionSessionState::Idle;
    references_.clear();
    has_preview_ = false;
    snap_active_ = false;
    snap_type_ = DimensionSnapType::None;
    ortho_active_ = false;
    has_last_placement_ = false;
    baseline_mode_ = false;
    baseline_step_count_ = 0;
}

void DimensionToolController::set_axis_lock(DimensionAxisLock lock) noexcept {
    axis_lock_ = lock;
    if (lock == DimensionAxisLock::Horizontal) {
        orientation_ = geometry::LinearOrientation::Horizontal;
    } else if (lock == DimensionAxisLock::Vertical) {
        orientation_ = geometry::LinearOrientation::Vertical;
    } else if (lock == DimensionAxisLock::Aligned) {
        orientation_ = geometry::LinearOrientation::Aligned;
    }
    update_preview();
}

static geometry::Point2D compute_continuous_placement(
    geometry::Point2D p1,
    geometry::Point2D p2,
    geometry::Point2D last_placement,
    geometry::LinearOrientation orientation,
    DimensionAxisLock axis_lock,
    geometry::Point2D collinear_dir) {
    if (orientation == geometry::LinearOrientation::Horizontal || axis_lock == DimensionAxisLock::Horizontal) {
        return { (p1.x + p2.x) * 0.5, last_placement.y };
    }
    if (orientation == geometry::LinearOrientation::Vertical || axis_lock == DimensionAxisLock::Vertical) {
        return { last_placement.x, (p1.y + p2.y) * 0.5 };
    }
    const geometry::Point2D mid = (p1 + p2) * 0.5;
    const double u_len = std::hypot(collinear_dir.x, collinear_dir.y);
    const geometry::Point2D u = (u_len > 1e-6) ? (collinear_dir / u_len) : geometry::Point2D{1.0, 0.0};
    const double proj_t = (mid.x - last_placement.x) * u.x + (mid.y - last_placement.y) * u.y;
    return last_placement + u * proj_t;
}

void DimensionToolController::start_continue_from(
    geometry::Point2D base_pt,
    geometry::Point2D placement_pt,
    document::DimensionKind kind,
    geometry::LinearOrientation ori) {
    activate(kind, false);
    set_linear_orientation(ori);
    continuous_mode_ = true;
    baseline_mode_ = false;
    first_point_ = base_pt;
    last_placement_point_ = placement_pt;
    has_last_placement_ = true;
    if (ori == geometry::LinearOrientation::Horizontal) {
        collinear_dir_ = {1.0, 0.0};
    } else if (ori == geometry::LinearOrientation::Vertical) {
        collinear_dir_ = {0.0, 1.0};
    } else {
        const double dx = placement_pt.x - base_pt.x;
        const double dy = placement_pt.y - base_pt.y;
        collinear_dir_ = {-dy, dx};
        const double d_len = std::hypot(collinear_dir_.x, collinear_dir_.y);
        if (d_len > 1e-6) collinear_dir_ = collinear_dir_ / d_len;
        else collinear_dir_ = {1.0, 0.0};
    }
    references_.clear();
    document::SourceReference ref;
    ref.ref_kind = document::SourceRefKind::ExplicitPoint;
    ref.explicit_point = base_pt;
    references_.push_back(ref);
    state_ = DimensionSessionState::PickSecond;
    has_preview_ = false;
}

void DimensionToolController::start_baseline_from(
    geometry::Point2D base_pt,
    geometry::Point2D placement_pt,
    document::DimensionKind kind,
    geometry::LinearOrientation ori) {
    activate(kind, false);
    set_linear_orientation(ori);
    baseline_mode_ = true;
    continuous_mode_ = false;
    first_point_ = base_pt;
    baseline_origin_ = base_pt;
    last_placement_point_ = placement_pt;
    has_last_placement_ = true;
    baseline_step_count_ = 1;
    references_.clear();
    document::SourceReference ref;
    ref.ref_kind = document::SourceRefKind::ExplicitPoint;
    ref.explicit_point = base_pt;
    references_.push_back(ref);
    state_ = DimensionSessionState::PickSecond;
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

void DimensionToolController::update_snap(geometry::Point2D world_pos, double zoom, bool ortho_constraint) {
    if (!active_) {
        snap_active_ = false;
        return;
    }

    ortho_active_ = ortho_constraint || (axis_lock_ != DimensionAxisLock::Auto && axis_lock_ != DimensionAxisLock::Aligned);
    geometry::Point2D query_pt = world_pos;

    // If picking second point or placement and ORTHO constraint or axis lock is active:
    if (state_ == DimensionSessionState::PickSecond) {
        if (axis_lock_ == DimensionAxisLock::Horizontal) {
            query_pt.y = first_point_.y;
        } else if (axis_lock_ == DimensionAxisLock::Vertical) {
            query_pt.x = first_point_.x;
        } else if (axis_lock_ == DimensionAxisLock::Angle45) {
            const double dx = query_pt.x - first_point_.x;
            const double dy = query_pt.y - first_point_.y;
            const double d = (std::abs(dx) + std::abs(dy)) * 0.5;
            query_pt.x = first_point_.x + (dx >= 0 ? d : -d);
            query_pt.y = first_point_.y + (dy >= 0 ? d : -d);
        } else if (ortho_constraint) {
            const double dx = query_pt.x - first_point_.x;
            const double dy = query_pt.y - first_point_.y;
            const double angle = std::atan2(dy, dx);
            constexpr double kPi = 3.14159265358979323846;
            constexpr double kStep = kPi / 4.0; // 45 deg snap
            const double snapped_angle = std::round(angle / kStep) * kStep;
            const double dist = std::hypot(dx, dy);
            query_pt.x = first_point_.x + dist * std::cos(snapped_angle);
            query_pt.y = first_point_.y + dist * std::sin(snapped_angle);
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

            // Center of bounding box
            test_candidate({b.x + b.width * 0.5, b.y + b.height * 0.5}, DimensionSnapType::Center, id, std::nullopt);

            // Circle / ellipse quadrant points
            if (std::abs(b.width - b.height) < 1.0) {
                const auto center_pt = geometry::Point2D{b.x + b.width * 0.5, b.y + b.height * 0.5};
                test_candidate({center_pt.x, b.y}, DimensionSnapType::Quadrant, id, std::nullopt);
                test_candidate({center_pt.x, b.bottom()}, DimensionSnapType::Quadrant, id, std::nullopt);
                test_candidate({b.x, center_pt.y}, DimensionSnapType::Quadrant, id, std::nullopt);
                test_candidate({b.right(), center_pt.y}, DimensionSnapType::Quadrant, id, std::nullopt);
            }

            // Path nodes and segment midpoints & perpendiculars
            if (path_obj->authoring_source) {
                for (const auto& sp : path_obj->authoring_source->subpaths) {
                    const size_t n_cnt = sp.nodes.size();
                    for (size_t i = 0; i < n_cnt; ++i) {
                        const auto world_anchor = path_obj->transform.map_point(sp.nodes[i].anchor);
                        test_candidate(world_anchor, DimensionSnapType::Node, id, sp.nodes[i].id.value());

                        if (i + 1 < n_cnt || sp.closed) {
                            const size_t next_i = (i + 1) % n_cnt;
                            const auto pt_b = path_obj->transform.map_point(sp.nodes[next_i].anchor);
                            const auto mid = (world_anchor + pt_b) * 0.5;
                            test_candidate(mid, DimensionSnapType::Midpoint, id, std::nullopt);

                            // Perpendicular snap from first_point_
                            if (state_ == DimensionSessionState::PickSecond) {
                                const auto v = pt_b - world_anchor;
                                const double lsq = v.x * v.x + v.y * v.y;
                                if (lsq > 1e-6) {
                                    const double t = ((first_point_.x - world_anchor.x) * v.x + (first_point_.y - world_anchor.y) * v.y) / lsq;
                                    if (t > 0.05 && t < 0.95) {
                                        test_candidate(world_anchor + v * t, DimensionSnapType::Perpendicular, id, std::nullopt);
                                    }
                                }
                            }
                        }
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
        baseline_origin_ = effective_pos;

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

        if (continuous_mode_ && has_last_placement_) {
            draft_dim_.placement_point = compute_continuous_placement(
                first_point_, second_point_, last_placement_point_, orientation_, axis_lock_, collinear_dir_);
            state_ = DimensionSessionState::Placement;
            update_preview();
            return commit();
        }

        if (baseline_mode_ && has_last_placement_) {
            const double spacing = style_.baseline_spacing_mm > 0.0 ? style_.baseline_spacing_mm : 8.0;
            geometry::Point2D place = last_placement_point_;
            if (orientation_ == geometry::LinearOrientation::Horizontal || axis_lock_ == DimensionAxisLock::Horizontal) {
                const double sign = (last_placement_point_.y >= first_point_.y) ? 1.0 : -1.0;
                place.y += sign * spacing * baseline_step_count_;
            } else if (orientation_ == geometry::LinearOrientation::Vertical || axis_lock_ == DimensionAxisLock::Vertical) {
                const double sign = (last_placement_point_.x >= first_point_.x) ? 1.0 : -1.0;
                place.x += sign * spacing * baseline_step_count_;
            } else {
                const geometry::Point2D v = second_point_ - first_point_;
                const double len = std::hypot(v.x, v.y);
                const geometry::Point2D n = (len > 1e-6) ? geometry::Point2D{-v.y / len, v.x / len} : geometry::Point2D{0.0, 1.0};
                place = place + n * (spacing * baseline_step_count_);
            }
            draft_dim_.placement_point = place;
            state_ = DimensionSessionState::Placement;
            update_preview();
            return commit();
        }

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

    if (state_ == DimensionSessionState::PickSecond && (continuous_mode_ || baseline_mode_) && has_last_placement_) {
        const geometry::Point2D cur_pt = snap_active_ ? snap_point_ : world_pos;
        second_point_ = cur_pt;
        if (continuous_mode_) {
            draft_dim_.placement_point = compute_continuous_placement(
                first_point_, second_point_, last_placement_point_, orientation_, axis_lock_, collinear_dir_);
        } else if (baseline_mode_) {
            const double spacing = style_.baseline_spacing_mm > 0.0 ? style_.baseline_spacing_mm : 8.0;
            geometry::Point2D place = last_placement_point_;
            if (orientation_ == geometry::LinearOrientation::Horizontal || axis_lock_ == DimensionAxisLock::Horizontal) {
                const double sign = (last_placement_point_.y >= first_point_.y) ? 1.0 : -1.0;
                place.y += sign * spacing * baseline_step_count_;
            } else if (orientation_ == geometry::LinearOrientation::Vertical || axis_lock_ == DimensionAxisLock::Vertical) {
                const double sign = (last_placement_point_.x >= first_point_.x) ? 1.0 : -1.0;
                place.x += sign * spacing * baseline_step_count_;
            } else {
                const geometry::Point2D v = second_point_ - first_point_;
                const double len = std::hypot(v.x, v.y);
                const geometry::Point2D n = (len > 1e-6) ? geometry::Point2D{-v.y / len, v.x / len} : geometry::Point2D{0.0, 1.0};
                place = place + n * (spacing * baseline_step_count_);
            }
            draft_dim_.placement_point = place;
        }
        if (references_.size() == 1) {
            document::SourceReference ref;
            ref.explicit_point = cur_pt;
            references_.push_back(ref);
            state_ = DimensionSessionState::Placement;
            update_preview();
            state_ = DimensionSessionState::PickSecond;
            references_.pop_back();
        }
    } else if (state_ == DimensionSessionState::Placement) {
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

    if (continuous_mode_ || baseline_mode_) {
        state_ = DimensionSessionState::Idle;
        references_.clear();
        has_preview_ = false;
        has_last_placement_ = false;
        baseline_step_count_ = 0;
        return true;
    }

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

    const auto res = doc_.execute_command(std::make_unique<document::AddDimensionCommand>(draft_dim_, active_layer_index_));
    if (res.success) {
        if (continuous_mode_) {
            last_placement_point_ = draft_dim_.placement_point;
            has_last_placement_ = true;
            first_point_ = second_point_;
            references_.clear();
            document::SourceReference ref;
            ref.source_object_id = snapped_object_id_;
            ref.element_id = snapped_element_id_;
            ref.ref_kind = snapped_element_id_.has_value() ? document::SourceRefKind::Node : document::SourceRefKind::ExplicitPoint;
            ref.explicit_point = first_point_;
            references_.push_back(ref);
            state_ = DimensionSessionState::PickSecond;
            has_preview_ = false;
        } else if (baseline_mode_) {
            baseline_step_count_++;
            first_point_ = baseline_origin_;
            references_.clear();
            document::SourceReference ref;
            ref.ref_kind = document::SourceRefKind::ExplicitPoint;
            ref.explicit_point = baseline_origin_;
            references_.push_back(ref);
            state_ = DimensionSessionState::PickSecond;
            has_preview_ = false;
        } else {
            state_ = DimensionSessionState::Idle;
            references_.clear();
            has_preview_ = false;
        }
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
