#pragma once
#include "pikdraw/vector/gestures/style_tool.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/vector/gestures/transform_gesture.hpp"
#include "pikdraw/vector/selection/selection_model.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/geometry/queries/aabb_index.hpp"
#include <cstdint>
#include <string>
#include <vector>
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
    std::optional<HitResult> pick(geometry::Point2D point, const document::DocumentSnapshot& snapshot, double tolerance_mm=2.0);
    void on_resolved_pointer_down(geometry::Point2D point,
        const document::DocumentSnapshot& snapshot, document::ObjectId id,
        SelectionMode mode = SelectionMode::Replace);
    void set_selection_scope(document::ObjectId id, bool frame = false) { on_escape(); selection_scope_=id; selection_frame_=frame; selection_index_attempted_=false; }
    document::ObjectId selection_scope() const noexcept { return selection_scope_; }
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
    [[nodiscard]] bool ensure_selection_index(const document::DocumentSnapshot &snapshot);
    [[nodiscard]] std::vector<document::ObjectId>
    selection_candidates(const geometry::Rect2D &query) const;
    bool valid_layer(const document::DocumentSnapshot &snapshot) const;
    document::ObjectId selection_scope_{0};
    bool selection_frame_{false};
    ToolMode mode_;
    size_t layer_{0};
    SelectionModel selection_;
    HitDiagnostics hit_diagnostics_;
    TransformGesture gesture_;
    GestureStamp selection_stamp_, shape_stamp_;
    geometry::AabbIndex selection_index_;
    const document::DocumentSnapshot *indexed_snapshot_{nullptr};
    std::string indexed_document_id_;
    uint64_t indexed_epoch_{0};
    uint64_t indexed_revision_{0};
    document::PageId indexed_page_id_{document::kInvalidPageId};
    double max_indexed_text_scale_{0.0};
    bool selection_index_attempted_{false};
    bool selection_index_valid_{false};
    bool shape_active_{false};
    geometry::Point2D start_, current_;
};
} // namespace pikdraw::vector
