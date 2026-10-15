#pragma once

#include "pikdraw/vector/selection/selection_model.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/transform_gesture.hpp"
#include "pikdraw/vector/gestures/style_tool.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/document/document.hpp"

namespace pikdraw::vector {

enum class ToolMode {
    Select,
    Rectangle,
    Ellipse,
    Text
};

/// High-level Vector Tool Controller coordinating selection, shape tools, and gestures
class VectorController {
public:
    explicit VectorController(ToolMode initial_mode = ToolMode::Select)
        : current_mode_(initial_mode) {}

    [[nodiscard]] ToolMode mode() const noexcept { return current_mode_; }
    void set_mode(ToolMode mode) noexcept {
        current_mode_ = mode;
        gesture_.cancel();
    }

    [[nodiscard]] SelectionModel& selection() noexcept { return selection_; }
    [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }

    [[nodiscard]] TransformGesture& gesture() noexcept { return gesture_; }
    [[nodiscard]] const TransformGesture& gesture() const noexcept { return gesture_; }

    /// Handles pointer down in world mm coordinates
    void on_pointer_down(geometry::Point2D pt_mm, const document::DocumentSnapshot& snapshot) {
        if (current_mode_ == ToolMode::Select) {
            auto hit_id = SelectionModel::hit_test(pt_mm, snapshot);
            if (hit_id) {
                if (!selection_.is_selected(*hit_id)) {
                    selection_.select(*hit_id, SelectionMode::Replace);
                }
                gesture_.begin(pt_mm, selection_.selected_ids());
            } else {
                selection_.clear();
            }
        }
    }

    /// Handles pointer move in world mm coordinates
    [[nodiscard]] geometry::Transform2D on_pointer_move(geometry::Point2D pt_mm) {
        if (gesture_.is_active()) {
            return gesture_.update(pt_mm);
        }
        return geometry::Transform2D::identity();
    }

    /// Handles pointer up in world mm coordinates, committing transactions
    document::CommandResult on_pointer_up(geometry::Point2D pt_mm, document::Document& doc) {
        if (gesture_.is_active()) {
            return gesture_.commit(doc);
        } else if (current_mode_ == ToolMode::Rectangle) {
            auto rect = geometry::Rect2D{pt_mm.x, pt_mm.y, 100.0, 60.0};
            auto obj = ShapeFactory::create_rectangle(rect);
            auto cmd = std::make_unique<document::AddObjectCommand>(obj);
            selection_.select(obj.id);
            return doc.execute_command(std::move(cmd));
        } else if (current_mode_ == ToolMode::Ellipse) {
            auto obj = ShapeFactory::create_ellipse(pt_mm, 50.0, 30.0);
            auto cmd = std::make_unique<document::AddObjectCommand>(obj);
            selection_.select(obj.id);
            return doc.execute_command(std::move(cmd));
        }
        return document::CommandResult::ok();
    }

    /// Cancels active gesture on Escape key press
    void on_escape() noexcept {
        gesture_.cancel();
    }

    /// Deletes currently selected objects in a single atomic transaction
    document::CommandResult delete_selected(document::Document& doc) {
        if (selection_.empty()) return document::CommandResult::ok();

        auto composite = std::make_unique<document::CompositeCommand>("Delete Selected");
        for (auto id : selection_.selected_ids()) {
            composite->add_command(std::make_unique<document::RemoveObjectCommand>(id));
        }
        selection_.clear();
        return doc.execute_command(std::move(composite));
    }

private:
    ToolMode current_mode_{ToolMode::Select};
    SelectionModel selection_;
    TransformGesture gesture_;
};

} // namespace pikdraw::vector
