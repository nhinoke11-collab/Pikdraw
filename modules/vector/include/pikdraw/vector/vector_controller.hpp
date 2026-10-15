#pragma once
#include "pikdraw/vector/gestures/style_tool.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/vector/gestures/transform_gesture.hpp"
#include "pikdraw/vector/selection/selection_model.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
namespace pikdraw::vector {
enum class ToolMode { Select, Rectangle, Ellipse, Text };
class VectorController {
  public:
    explicit VectorController(ToolMode mode = ToolMode::Select) : mode_(mode) {}
    ToolMode mode() const noexcept {
        return mode_;
    }
    void set_mode(ToolMode mode) noexcept {
        on_escape();
        mode_ = mode;
    }
    void set_active_layer(size_t index) noexcept {
        on_escape();
        layer_ = index;
    }
    SelectionModel &selection() noexcept {
        return selection_;
    }
    const SelectionModel &selection() const noexcept {
        return selection_;
    }
    TransformGesture &gesture() noexcept {
        return gesture_;
    }
    const TransformGesture &gesture() const noexcept {
        return gesture_;
    }
    const HitDiagnostics &last_hit_diagnostics() const noexcept {
        return hit_diagnostics_;
    }
    void on_pointer_down(geometry::Point2D point, const document::DocumentSnapshot &snapshot,
                         SelectionMode selection_mode = SelectionMode::Replace, double tolerance_mm = 2.0);
    void set_selection(const std::vector<document::ObjectId> &ids, const document::DocumentSnapshot &snapshot,
                       SelectionMode mode = SelectionMode::Replace);
    void select_region(const geometry::Rect2D &region, RegionMode region_mode,
                       const document::DocumentSnapshot &snapshot,
                       SelectionMode mode = SelectionMode::Replace);
    geometry::Transform2D on_pointer_move(geometry::Point2D point);
    document::CommandResult on_pointer_up(geometry::Point2D point, document::Document &doc);
    std::optional<geometry::Rect2D> shape_preview() const noexcept;
    void on_escape() noexcept {
        gesture_.cancel();
        shape_active_ = false;
    }
    document::CommandResult delete_selected(document::Document &doc);
    document::CommandResult
    insert_text(document::Document &doc, geometry::Point2D point, std::string text,
                typography::FontIdentity font = typography::FontIdentity::make_default());

  private:
    void bind_selection(const document::DocumentSnapshot &snapshot);
    bool valid_layer(const document::DocumentSnapshot &snapshot) const;
    ToolMode mode_;
    size_t layer_{0};
    SelectionModel selection_;
    HitDiagnostics hit_diagnostics_;
    TransformGesture gesture_;
    GestureStamp selection_stamp_, shape_stamp_;
    bool shape_active_{false};
    geometry::Point2D start_, current_;
};
} // namespace pikdraw::vector
