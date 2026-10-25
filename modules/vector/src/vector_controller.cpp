#include "pikdraw/document/commands/clip_commands.hpp"
#include "pikdraw/vector/vector_controller.hpp"
#include <cmath>
#include <limits>
#include <type_traits>
namespace pikdraw::vector {
namespace {
bool expand_bounds(geometry::Rect2D &bounds, double x_margin, double y_margin) {
    if (!bounds.is_finite() || bounds.width < 0.0 || bounds.height < 0.0 ||
        !std::isfinite(bounds.right()) || !std::isfinite(bounds.bottom()) ||
        !std::isfinite(x_margin) || !std::isfinite(y_margin) ||
        x_margin < 0.0 || y_margin < 0.0)
        return false;
    const double left = bounds.x - x_margin;
    const double top = bounds.y - y_margin;
    const double right = bounds.right() + x_margin;
    const double bottom = bounds.bottom() + y_margin;
    const double width = right - left;
    const double height = bottom - top;
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(width) ||
        !std::isfinite(height) || width < 0.0 || height < 0.0)
        return false;
    bounds = {left, top, width, height};
    return true;
}

geometry::Rect2D conservative_hit_bounds(const document::ObjectVariant &object,
                                        double &text_scale) {
    return std::visit(
        [&](const auto &value) -> geometry::Rect2D {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                auto bounds = value.bounds();
                const auto &transform = value.transform;
                const double scale = std::hypot(std::hypot(transform.a, transform.b),
                                                std::hypot(transform.c, transform.d));
                const double stroke_margin = value.has_stroke
                    ? value.stroke_width_mm * scale * 0.5
                    : 0.0;
                if (!expand_bounds(bounds, stroke_margin, stroke_margin))
                    return {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0};
                return bounds;
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                const auto &transform = value.transform;
                const double scale = std::hypot(std::hypot(transform.a, transform.b),
                                                std::hypot(transform.c, transform.d));
                if (std::isfinite(scale))
                    text_scale = std::max(text_scale, scale);
                else
                    text_scale = std::numeric_limits<double>::infinity();
                return value.bounds();
            } else if constexpr (std::is_same_v<T, document::ImageObject>) {
                return value.bounds();
            } else {
                // Groups have no complete picking/selection geometry yet.
                return {};
            }
        },
        object);
}
} // namespace

bool VectorController::valid_layer(const document::DocumentSnapshot &snapshot) const {
    return layer_ < snapshot.page().layers.size() && snapshot.page().layers[layer_].visible &&
           !snapshot.page().layers[layer_].locked;
}
void VectorController::bind_selection(const document::DocumentSnapshot &snapshot) {
    if (selection_stamp_.document_id != snapshot.document_id() || selection_stamp_.epoch != snapshot.epoch() || selection_stamp_.page_id != snapshot.active_page_id())
        { selection_.clear(); selection_scope_=0; }
    selection_stamp_ = GestureStamp(snapshot);
}
bool VectorController::ensure_selection_index(const document::DocumentSnapshot &snapshot) {
    const bool same_snapshot = indexed_document_id_ == snapshot.document_id() &&
        indexed_snapshot_ == &snapshot &&
        indexed_epoch_ == snapshot.epoch() && indexed_revision_ == snapshot.revision() &&
        indexed_page_id_ == snapshot.active_page_id();
    if (same_snapshot && selection_index_attempted_)
        return selection_index_valid_;

    selection_index_.clear();
    indexed_document_id_ = snapshot.document_id();
    indexed_snapshot_ = &snapshot;
    indexed_epoch_ = snapshot.epoch();
    indexed_revision_ = snapshot.revision();
    indexed_page_id_ = snapshot.active_page_id();
    max_indexed_text_scale_ = 0.0;
    selection_index_attempted_ = true;
    selection_index_valid_ = false;

    std::vector<geometry::AabbIndexEntry> entries;
    entries.reserve(std::min(snapshot.object_count(), geometry::AabbIndex::kMaximumEntries));
    uint64_t z_order = 0;
    for (auto id : SelectionModel::scope_roots(snapshot,selection_scope_,selection_frame_)) {
        if (z_order > std::numeric_limits<uint32_t>::max()) return false;
        const auto z=static_cast<uint32_t>(z_order++);
        const auto* object=snapshot.objects().lookup_ref(id);
        const auto* hierarchy=snapshot.hierarchy().find(id);
        if(!object||!hierarchy||!editable_target(snapshot,id)) continue;
        double text_scale=0;
        geometry::Rect2D bounds;
        if (std::holds_alternative<document::GroupObject>(*object) && hierarchy->bounds) {
            bounds = *hierarchy->bounds;
        } else if (const auto* path = std::get_if<document::PathObject>(object);
                   path && hierarchy->bounds) {
            // SceneHierarchy computed these world bounds while publishing the immutable snapshot.
            // Reusing them avoids walking every path's nodes again during the first interactive pick.
            bounds = *hierarchy->bounds;
            const auto& transform = hierarchy->world;
            const double scale = std::hypot(std::hypot(transform.a, transform.b),
                                            std::hypot(transform.c, transform.d));
            const double stroke_margin = path->has_stroke
                ? path->stroke_width_mm * scale * 0.5
                : 0.0;
            if (!expand_bounds(bounds, stroke_margin, stroke_margin))
                bounds = {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0};
        } else {
            bounds = hierarchy->parent_world.map_rect(conservative_hit_bounds(*object,text_scale));
        }
        max_indexed_text_scale_=std::max(max_indexed_text_scale_,text_scale*std::hypot(
            std::hypot(hierarchy->parent_world.a,hierarchy->parent_world.b),
            std::hypot(hierarchy->parent_world.c,hierarchy->parent_world.d)));
        if(entries.size()>=geometry::AabbIndex::kMaximumEntries) return false;
        entries.push_back({static_cast<uint64_t>(id),bounds,z});
    }
    selection_index_valid_ = selection_index_.rebuild(std::move(entries));
    return selection_index_valid_;
}

std::vector<document::ObjectId>
VectorController::selection_candidates(const geometry::Rect2D &query) const {
    std::vector<document::ObjectId> ids;
    if (!selection_index_valid_)
        return ids;
    const auto entries = selection_index_.query(query);
    ids.reserve(entries.size());
    for (const auto &entry : entries)
        ids.push_back(static_cast<document::ObjectId>(entry.id));
    return ids;
}
void VectorController::set_selection(const std::vector<document::ObjectId> &ids,
                                     const document::DocumentSnapshot &snapshot, SelectionMode mode) {
    bind_selection(snapshot);
    if (mode == SelectionMode::Replace)
        selection_.clear();
    for (const auto id : ids)
        if (editable_target(snapshot, id))
            selection_.select(id, mode == SelectionMode::Replace ? SelectionMode::Add : mode);
}
void VectorController::select_region(const geometry::Rect2D &region, RegionMode region_mode,
                                     const document::DocumentSnapshot &snapshot, SelectionMode mode) {
    bind_selection(snapshot);
    if (selection_scope_) {
        set_selection(SelectionModel::query_region_candidates(region, region_mode, snapshot,
            SelectionModel::scope_roots(snapshot, selection_scope_, selection_frame_)), snapshot, mode);
        return;
    }
    if (ensure_selection_index(snapshot)) {
        const auto candidates = selection_candidates(region);
        set_selection(SelectionModel::query_region_candidates(region, region_mode, snapshot,
                                                               candidates),
                      snapshot, mode);
    } else {
        set_selection(SelectionModel::query_region(region, region_mode, snapshot), snapshot, mode);
    }
}
std::optional<HitResult> VectorController::pick(geometry::Point2D point, const document::DocumentSnapshot& snapshot, double tolerance_mm) {
        std::optional<HitResult> hit;
        bool indexed_query = false;
        if (ensure_selection_index(snapshot) && std::isfinite(tolerance_mm) && tolerance_mm >= 0.0) {
            const double world_margin = tolerance_mm * std::max(1.0, max_indexed_text_scale_);
            const geometry::Rect2D query{point.x - world_margin, point.y - world_margin,
                                         2.0 * world_margin, 2.0 * world_margin};
            if (query.is_finite() && query.width >= 0.0 && query.height >= 0.0 &&
                std::isfinite(query.right()) && std::isfinite(query.bottom())) {
                const auto candidates = selection_candidates(query);
                hit = SelectionModel::hit_test_detail_candidates(
                    point, snapshot, candidates, tolerance_mm, true, &hit_diagnostics_);
                indexed_query = true;
            }
        }
        if (!indexed_query)
            hit = SelectionModel::hit_test_detail_candidates(point, snapshot,
                SelectionModel::scope_roots(snapshot, selection_scope_, selection_frame_), tolerance_mm, true, &hit_diagnostics_);
    return hit;
}
void VectorController::on_pointer_down(geometry::Point2D point, const document::DocumentSnapshot &snapshot,
                                       SelectionMode selection_mode, double tolerance_mm) {
    on_escape();
    if (!finite_point(point))
        return;
    bind_selection(snapshot);
    if (mode_ == ToolMode::Select) {
        const auto hit=pick(point,snapshot,tolerance_mm);
        if (!hit) {
            if (selection_mode == SelectionMode::Replace)
                selection_.clear();
            return;
        }
        if (selection_mode != SelectionMode::Replace || !selection_.is_selected(hit->id))
            selection_.select(hit->id, selection_mode);
        if (selection_mode != SelectionMode::Replace)
            return;
        (void)gesture_.begin(point, selection_.selected_ids(), snapshot);
    } else if (mode_ == ToolMode::Rectangle || mode_ == ToolMode::Ellipse) {
        if (!valid_layer(snapshot))
            return;
        shape_stamp_ = GestureStamp(snapshot);
        start_ = current_ = point;
        shape_active_ = true;
    }
}
void VectorController::on_resolved_pointer_down(geometry::Point2D point,
    const document::DocumentSnapshot& snapshot,document::ObjectId id,SelectionMode mode) {
    on_escape(); bind_selection(snapshot);
    if(!finite_point(point)||!editable_target(snapshot,id)) return;
    if(selection_scope_ && (!snapshot.hierarchy().contains(selection_scope_,id) || id==selection_scope_))
        return;
    if(mode!=SelectionMode::Replace || !selection_.is_selected(id)) selection_.select(id,mode);
    if(mode==SelectionMode::Replace) (void)gesture_.begin(point,selection_.selected_ids(),snapshot);
}
geometry::Transform2D VectorController::on_pointer_move(geometry::Point2D point) {
    if (!finite_point(point)) {
        on_escape();
        return {};
    }
    if (shape_active_)
        current_ = point;
    if (gesture_.is_active())
        return gesture_.update(point);
    return {};
}
std::optional<geometry::Rect2D> VectorController::shape_preview() const noexcept {
    if (!shape_active_)
        return std::nullopt;
    return geometry::Rect2D{std::min(start_.x, current_.x), std::min(start_.y, current_.y),
                            std::abs(start_.x - current_.x), std::abs(start_.y - current_.y)};
}
document::CommandResult VectorController::on_pointer_up(geometry::Point2D point, document::Document &doc) {
    if (!finite_point(point)) {
        on_escape();
        return document::CommandResult::fail("Invalid release point");
    }
    if (gesture_.is_active()) {
        (void)gesture_.update(point); // release may arrive without a final move event
        return gesture_.commit(doc);
    }
    if (shape_active_) {
        current_ = point;
        const auto rect = *shape_preview();
        shape_active_ = false;
        if (!rect.is_finite())
            return document::CommandResult::fail("Shape gesture overflow");
        const auto snapshot = doc.current_snapshot();
        if (!shape_stamp_.matches(*snapshot) || !valid_layer(*snapshot))
            return document::CommandResult::fail("Stale or locked shape gesture");
        if (rect.width == 0 || rect.height == 0)
            return document::CommandResult::ok();
        const auto object =
            mode_ == ToolMode::Rectangle
                ? ShapeFactory::create_rectangle(rect)
                : ShapeFactory::create_ellipse(rect.center(), rect.width * .5, rect.height * .5);
        const auto result = doc.execute_command(std::make_unique<document::AddScopedObjectCommand>(object, selection_scope_, layer_));
        if (result.success) {
            selection_.select(object.id);
            selection_stamp_ = GestureStamp(*doc.current_snapshot());
        }
        return result;
    }
    if (mode_ == ToolMode::Text)
        return document::CommandResult::fail("Text insertion requires editable content via insert_text");
    return document::CommandResult::ok();
}
document::CommandResult VectorController::delete_selected(document::Document &doc) {
    if (selection_.empty())
        return document::CommandResult::ok();
    const auto snapshot = doc.current_snapshot();
    if (selection_stamp_.document_id.empty() || selection_stamp_.document_id != snapshot->document_id() ||
        selection_stamp_.epoch != snapshot->epoch())
        return document::CommandResult::fail("Selection belongs to a different document lifecycle");
    auto command = std::make_unique<document::CompositeCommand>("Delete Selected");
    for (auto id : snapshot->hierarchy().roots(selection_.selected_ids())) {
        if (!editable_target(*snapshot, id))
            return document::CommandResult::fail("Delete target missing, hidden or locked");
        command->add_command(std::make_unique<document::RemoveObjectCommand>(id));
    }
    const auto result = doc.execute_command(std::move(command));
    if (result.success) {
        selection_.clear();
        on_escape();
    }
    return result;
}
document::CommandResult VectorController::insert_text(document::Document &doc, geometry::Point2D point,
                                                      std::string text, typography::FontIdentity font) {
    const auto snapshot = doc.current_snapshot();
    if (!finite_point(point) || !valid_layer(*snapshot))
        return document::CommandResult::fail("Text point/layer invalid");
    document::TextObject object;
    object.id = document::generate_object_id();
    object.name = "Text " + std::to_string(object.id);
    object.position = point;
    object.text = std::move(text);
    object.font = std::move(font);
    const auto result = doc.execute_command(std::make_unique<document::AddScopedObjectCommand>(object, selection_scope_, layer_));
    if (result.success) {
        selection_.select(object.id);
        selection_stamp_ = GestureStamp(*doc.current_snapshot());
    }
    return result;
}
} // namespace pikdraw::vector
