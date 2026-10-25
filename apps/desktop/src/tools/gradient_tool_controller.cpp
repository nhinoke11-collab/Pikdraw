#include "gradient_tool_controller.hpp"
#include "pikdraw/document/commands/gradient_commands.hpp"
#include "pikdraw/document/model/object.hpp"
#include <algorithm>
#include <cmath>

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

std::optional<geometry::Transform2D> GradientToolController::active_object_transform() const {
    if (active_targets_.size() != 1) return std::nullopt;
    const auto snapshot = document_.current_snapshot();
    if (!snapshot) return std::nullopt;
    const auto* entry = snapshot->hierarchy().find(active_targets_.front());
    if (!entry || !entry->world.is_finite() || !entry->world.is_invertible())
        return std::nullopt;
    return entry->world;
}

std::optional<geometry::Point2D> GradientToolController::world_to_object(
    geometry::Point2D world_point) const {
    const auto transform = active_object_transform();
    const auto inverse = transform ? transform->inverted() : std::nullopt;
    if (!inverse) return std::nullopt;
    const auto point = inverse->map_point(world_point);
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
    return point;
}

std::optional<geometry::Point2D> GradientToolController::object_to_world(
    geometry::Point2D object_point) const {
    const auto transform = active_object_transform();
    if (!transform) return std::nullopt;
    const auto point = transform->map_point(object_point);
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
    return point;
}

void GradientToolController::sync_presenter_from_document() {
    const auto snapshot = document_.current_snapshot();
    if (snapshot) {
        const auto summary = summarize_gradient_selection(*snapshot, active_targets_);
        presenter_.load_selection(summary, compute_selection_bounds());
        const auto shared_draft = presenter_.shared_draft_paint();
        if (shared_draft)
            (void)tool_->set_shared_draft_paint(shared_draft);
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
    preview_source_.reset();
    preview_snapshot_.reset();
}

void GradientToolController::on_selection_changed(std::vector<document::ObjectId> new_targets) {
    if (has_unapplied_changes()) {
        status_message_ = "Đã hủy thay đổi Gradient chưa áp dụng";
        cancel();
    }
    active_targets_ = std::move(new_targets);
    selection_generation_++;
    tool_->set_target_objects(active_targets_);
    preview_source_.reset();
    preview_snapshot_.reset();
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
    const auto source = document_.current_snapshot();
    return preview_snapshot_for_canvas(source) != source;
}

bool GradientToolController::apply_with_token(const GradientSessionToken& token) {
    if (!validate_token(token)) {
        return false;
    }
    return apply();
}

void GradientToolController::on_pointer_down(geometry::Point2D object_pos, bool shift, bool ctrl, double hit_tolerance_mm) {
    if (presenter_.draft_paint())
        (void)tool_->set_draft_paint(*presenter_.draft_paint());
    const auto hit = tool_->hit_test_handles(object_pos, hit_tolerance_mm);
    on_pointer_down_with_hit(object_pos, hit, shift, ctrl);
}

void GradientToolController::on_pointer_down_with_hit(
    geometry::Point2D object_pos, vector::tools::GradientHandleHit hit,
    bool shift, bool ctrl) {
    status_message_.clear();
    if (presenter_.draft_paint()) {
        tool_->set_draft_paint(*presenter_.draft_paint());
    }
    tool_->on_pointer_down_with_hit(object_pos, hit, shift, ctrl);
    const auto active_hit = tool_->active_handle();
    if ((active_hit.kind == vector::tools::GradientHandleKind::Stop ||
         active_hit.kind == vector::tools::GradientHandleKind::Midpoint) && tool_->draft_paint() &&
        active_hit.stop_index < tool_->draft_paint()->stops.size()) {
        (void)presenter_.select_stop(tool_->draft_paint()->stops[active_hit.stop_index].id);
    }
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
    } else if (tool_->last_gesture_below_threshold()) {
        cancel();
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
    if (const auto shared_draft = presenter_.shared_draft_paint())
        (void)tool_->set_shared_draft_paint(shared_draft);
    preview_source_.reset();
    preview_snapshot_.reset();
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
    size_t selected_stop_index = 0;
    const auto selected_stop = std::find_if(draft->stops.begin(), draft->stops.end(),
        [&](const auto& stop) { return stop.id == presenter_.selected_stop_id(); });
    if (selected_stop != draft->stops.end())
        selected_stop_index = static_cast<size_t>(selected_stop - draft->stops.begin());

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
        if (const auto* refreshed = presenter_.draft_paint(); refreshed && !refreshed->stops.empty())
            (void)presenter_.select_stop(refreshed->stops[
                std::min(selected_stop_index, refreshed->stops.size() - 1)].id);
        preview_source_.reset();
        preview_snapshot_.reset();
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

document::SnapshotPtr GradientToolController::preview_snapshot_for_canvas(
    document::SnapshotPtr source) {
    const auto current = document_.current_snapshot();
    if (!source || !current || source->document_id() != current->document_id() ||
        source->epoch() != current->epoch() || source->revision() != current->revision())
        return source;

    const auto* draft = active_draft_paint();
    if (!draft || (!presenter_.is_dirty() && !tool_->is_editing())) {
        preview_source_.reset();
        preview_snapshot_.reset();
        return source;
    }
    const auto presenter_generation = presenter_.draft_generation();
    const auto tool_generation = tool_->draft_generation();
    if (preview_source_ == source && preview_snapshot_ &&
        preview_presenter_generation_ == presenter_generation &&
        preview_tool_generation_ == tool_generation)
        return preview_snapshot_;

    try {
        auto pages = source->pages();
        auto page_it = std::find_if(pages.begin(), pages.end(), [&](const auto& page) {
            return page.id == source->active_page_id();
        });
        if (page_it == pages.end()) return source;
        auto objects = page_it->objects;
        size_t replaced = 0;
        for (const auto id : active_targets_) {
            auto object = objects.get(id);
            auto* path = object ? std::get_if<document::PathObject>(&*object) : nullptr;
            if (!path) continue;

            document::GradientPaint paint = *draft;
            if (path->fill_gradient) {
                if (!presenter_.stops_modified())
                    paint.stops = path->fill_gradient->stops;
                if (!presenter_.geometry_modified())
                    paint.gradient_to_object = path->fill_gradient->gradient_to_object;
                if (!presenter_.kind_modified())
                    paint.kind = path->fill_gradient->kind;
                if (!presenter_.spread_modified())
                    paint.spread = path->fill_gradient->spread;
            }
            std::vector<uint64_t> stop_ids;
            stop_ids.reserve(paint.stops.size());
            for (size_t i = 0; i < paint.stops.size(); ++i)
                stop_ids.push_back(document::allocate_gradient_stop_id());
            paint = paint.clone_with_new_ids(document::allocate_gradient_paint_id(), stop_ids);
            path->has_fill = true;
            path->fill = paint.stops.front().color;
            path->fill_gradient = std::make_shared<const document::GradientPaint>(std::move(paint));
            objects = objects.set(id, std::move(*object));
            ++replaced;
        }
        if (replaced == 0) return source;
        page_it->objects = std::move(objects);
        preview_snapshot_ = std::make_shared<const document::DocumentSnapshot>(
            source->epoch(), source->revision(), std::move(pages), source->active_page_id(),
            source->document_id(), source->content_mode());
        preview_source_ = std::move(source);
        preview_presenter_generation_ = presenter_generation;
        preview_tool_generation_ = tool_generation;
        return preview_snapshot_;
    } catch (const std::exception&) {
        preview_source_.reset();
        preview_snapshot_.reset();
        return source;
    }
}

} // namespace pikdraw::desktop::tools
