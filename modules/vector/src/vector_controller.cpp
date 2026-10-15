#include "pikdraw/vector/vector_controller.hpp"
namespace pikdraw::vector {
bool VectorController::valid_layer(const document::DocumentSnapshot &snapshot) const {
    return layer_ < snapshot.page().layers.size() && snapshot.page().layers[layer_].visible &&
           !snapshot.page().layers[layer_].locked;
}
void VectorController::bind_selection(const document::DocumentSnapshot &snapshot) {
    if (selection_stamp_.document_id != snapshot.document_id() || selection_stamp_.epoch != snapshot.epoch())
        selection_.clear();
    selection_stamp_ = GestureStamp(snapshot);
}
void VectorController::on_pointer_down(geometry::Point2D point, const document::DocumentSnapshot &snapshot) {
    on_escape();
    if (!finite_point(point))
        return;
    bind_selection(snapshot);
    if (mode_ == ToolMode::Select) {
        const auto hit = SelectionModel::hit_test_detail(point, snapshot, 2.0, true, &hit_diagnostics_);
        if (!hit) {
            selection_.clear();
            return;
        }
        if (!selection_.is_selected(hit->id))
            selection_.select(hit->id);
        (void)gesture_.begin(point, selection_.selected_ids(), snapshot);
    } else if (mode_ == ToolMode::Rectangle || mode_ == ToolMode::Ellipse) {
        if (!valid_layer(snapshot))
            return;
        shape_stamp_ = GestureStamp(snapshot);
        start_ = current_ = point;
        shape_active_ = true;
    }
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
        const auto result = doc.execute_command(std::make_unique<document::AddObjectCommand>(object, layer_));
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
    for (auto id : selection_.selected_ids()) {
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
    const auto result = doc.execute_command(std::make_unique<document::AddObjectCommand>(object, layer_));
    if (result.success) {
        selection_.select(object.id);
        selection_stamp_ = GestureStamp(*doc.current_snapshot());
    }
    return result;
}
} // namespace pikdraw::vector
