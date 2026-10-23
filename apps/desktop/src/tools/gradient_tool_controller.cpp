#include "gradient_tool_controller.hpp"
#include "pikdraw/document/commands/gradient_commands.hpp"
#include "pikdraw/document/model/object.hpp"

namespace pikdraw::desktop::tools {

GradientToolController::GradientToolController(document::Document& document)
    : document_(document),
      tool_(std::make_unique<vector::tools::GradientTool>(document)),
      last_document_revision_(document.revision()) {}

geometry::Rect2D GradientToolController::compute_selection_bounds() const {
    geometry::Rect2D bounds{};
    bool first = true;

    for (const auto id : active_targets_) {
        const auto* obj_variant = document_.objects().lookup_ref(id);
        if (!obj_variant) {
            continue;
        }
        const auto* path_obj = std::get_if<document::PathObject>(obj_variant);
        if (!path_obj) {
            continue;
        }
        const auto obj_bounds = path_obj->path.bounds();
        if (first) {
            bounds = obj_bounds;
            first = false;
        } else {
            bounds = bounds.united(obj_bounds);
        }
    }

    if (first || !bounds.is_finite() || bounds.width <= 0.0 || bounds.height <= 0.0) {
        return geometry::Rect2D{0.0, 0.0, 100.0, 100.0};
    }
    return bounds;
}

void GradientToolController::sync_presenter_from_document() {
    const auto snapshot = document_.current_snapshot();
    if (snapshot) {
        const auto summary = summarize_gradient_selection(*snapshot, active_targets_);
        presenter_.load_selection(summary, compute_selection_bounds());
    }
}

bool GradientToolController::activate(std::vector<document::ObjectId> target_ids) {
    is_active_ = true;
    active_targets_ = std::move(target_ids);
    tool_->set_target_objects(active_targets_);
    selection_generation_++;
    last_document_revision_ = document_.revision();
    status_message_.clear();
    sync_presenter_from_document();
    return true;
}

void GradientToolController::deactivate() {
    if (has_unapplied_changes()) {
        cancel();
    }
    is_active_ = false;
    active_targets_.clear();
    tool_->set_target_objects({});
}

void GradientToolController::on_selection_changed(std::vector<document::ObjectId> new_targets) {
    if (has_unapplied_changes()) {
        status_message_ = "Đã hủy thay đổi Gradient chưa áp dụng";
        cancel();
    }
    active_targets_ = std::move(new_targets);
    selection_generation_++;
    tool_->set_target_objects(active_targets_);
    sync_presenter_from_document();
}

void GradientToolController::on_document_changed() {
    if (document_.revision() != last_document_revision_) {
        if (has_unapplied_changes()) {
            status_message_ = "Đã hủy thay đổi Gradient chưa áp dụng";
            cancel();
        }
        last_document_revision_ = document_.revision();
        sync_presenter_from_document();
    }
}

GradientSessionToken GradientToolController::current_token() const noexcept {
    return GradientSessionToken{
        document_.revision(),
        selection_generation_,
        presenter_.draft_generation()
    };
}

bool GradientToolController::validate_token(const GradientSessionToken& token) const noexcept {
    return token.matches(
        document_.revision(),
        selection_generation_,
        presenter_.draft_generation()
    );
}

bool GradientToolController::publish_preview(const GradientSessionToken& token) {
    if (!validate_token(token)) {
        return false;
    }
    return true;
}

bool GradientToolController::apply_with_token(const GradientSessionToken& token) {
    if (!validate_token(token)) {
        return false;
    }
    return apply();
}

void GradientToolController::on_pointer_down(geometry::Point2D object_pos, bool shift, bool ctrl, double hit_tolerance_mm) {
    status_message_.clear();
    if (presenter_.draft_paint()) {
        tool_->set_draft_paint(*presenter_.draft_paint());
    }
    tool_->on_pointer_down(object_pos, shift, ctrl, hit_tolerance_mm);
}

void GradientToolController::on_pointer_move(geometry::Point2D object_pos, bool shift, bool ctrl) {
    tool_->on_pointer_move(object_pos, shift, ctrl);
    if (tool_->is_editing() && tool_->draft_paint()) {
        presenter_.set_draft_paint(*tool_->draft_paint());
    }
}

bool GradientToolController::on_pointer_up(geometry::Point2D object_pos) {
    const bool committed = tool_->on_pointer_up(object_pos);
    if (committed) {
        last_document_revision_ = document_.revision();
        presenter_.mark_clean();
        sync_presenter_from_document();
    } else if (tool_->draft_paint()) {
        presenter_.set_draft_paint(*tool_->draft_paint());
    }
    return committed;
}

void GradientToolController::on_cancel() {
    cancel();
}

bool GradientToolController::has_unapplied_changes() const noexcept {
    return presenter_.is_dirty() || tool_->state() != vector::tools::GradientToolState::Idle;
}

void GradientToolController::cancel() {
    if (has_unapplied_changes()) {
        status_message_ = "Đã hủy thay đổi Gradient chưa áp dụng";
    }
    tool_->on_cancel();
    presenter_.cancel();
}

bool GradientToolController::apply() {
    if (active_targets_.empty()) {
        return false;
    }

    if (!presenter_.is_dirty()) {
        return true;
    }

    const auto* draft = presenter_.draft_paint();
    if (!draft) {
        return false;
    }

    std::vector<document::FillPaintReplacement> replacements;
    replacements.reserve(active_targets_.size());

    for (const auto id : active_targets_) {
        const auto* obj_variant = document_.objects().lookup_ref(id);
        if (!obj_variant) {
            continue;
        }
        const auto* path_obj = std::get_if<document::PathObject>(obj_variant);
        if (!path_obj) {
            continue;
        }

        document::GradientPaint paint_to_apply = *draft;

        // Preserve individual properties for mixed selections where only specific fields changed
        if (path_obj->fill_gradient) {
            if (!presenter_.stops_modified()) {
                paint_to_apply.stops = path_obj->fill_gradient->stops;
            }
            if (!presenter_.geometry_modified()) {
                paint_to_apply.gradient_to_object = path_obj->fill_gradient->gradient_to_object;
            }
            if (!presenter_.kind_modified()) {
                paint_to_apply.kind = path_obj->fill_gradient->kind;
            }
            if (!presenter_.spread_modified()) {
                paint_to_apply.spread = path_obj->fill_gradient->spread;
            }
        }

        const uint64_t new_paint_id = document::allocate_gradient_paint_id();
        std::vector<uint64_t> new_stop_ids;
        new_stop_ids.reserve(paint_to_apply.stops.size());
        for (size_t s = 0; s < paint_to_apply.stops.size(); ++s) {
            new_stop_ids.push_back(document::allocate_gradient_stop_id());
        }
        paint_to_apply = paint_to_apply.clone_with_new_ids(new_paint_id, new_stop_ids);

        document::FillPaintReplacement rep;
        rep.object_id = id;
        rep.has_fill = true;
        rep.fill = paint_to_apply.stops.front().color;
        rep.fill_gradient = std::make_shared<const document::GradientPaint>(std::move(paint_to_apply));
        replacements.push_back(std::move(rep));
    }

    if (replacements.empty()) {
        return false;
    }

    auto cmd = std::make_unique<document::SetFillPaintCommand>(std::move(replacements));
    auto res = document_.execute_command(std::move(cmd));
    if (res.success) {
        last_document_revision_ = document_.revision();
        presenter_.mark_clean();
        tool_->set_target_objects(active_targets_);
        sync_presenter_from_document();
        return true;
    }
    return false;
}

const document::GradientPaint* GradientToolController::active_draft_paint() const noexcept {
    if (tool_->is_editing() && tool_->draft_paint()) {
        return tool_->draft_paint();
    }
    return presenter_.draft_paint();
}

} // namespace pikdraw::desktop::tools
