#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/gradient_commands.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace pikdraw::vector::tools {

enum class GradientHandleKind : uint8_t {
  None = 0,
  LinearStart,
  LinearEnd,
  RadialCenter,
  RadialRadius,
  RadialAspect,
  Stop,
  Midpoint
};

struct GradientHandleHit {
  GradientHandleKind kind{GradientHandleKind::None};
  size_t stop_index{0};
  geometry::Point2D position_object{0.0, 0.0};
};

enum class GradientToolState : uint8_t {
  Idle = 0,
  CreatingDrag,
  DraggingHandle
};

class GradientTool {
public:
  static constexpr double kMinDragDistanceMm = 0.5;
  static constexpr double kShiftSnapAngleDegrees = 15.0;

  explicit GradientTool(document::Document &document) noexcept;

  void set_target_objects(std::vector<document::ObjectId> ids);
  [[nodiscard]] const std::vector<document::ObjectId> &target_objects() const noexcept {
    return target_ids_;
  }

  void set_kind(document::GradientKind kind);
  [[nodiscard]] document::GradientKind kind() const noexcept { return kind_; }

  void set_spread(document::GradientSpread spread);
  [[nodiscard]] document::GradientSpread spread() const noexcept { return spread_; }

  [[nodiscard]] GradientToolState state() const noexcept { return state_; }
  [[nodiscard]] bool is_editing() const noexcept { return state_ != GradientToolState::Idle; }
  [[nodiscard]] uint64_t draft_generation() const noexcept { return draft_generation_; }
  [[nodiscard]] const document::GradientPaint *draft_paint() const noexcept {
    return draft_paint_.has_value() ? &*draft_paint_ : nullptr;
  }

  // Pointer gesture handlers (coordinates in target object-local mm)
  void on_pointer_down(geometry::Point2D object_pos, bool shift, bool ctrl, double hit_tolerance_mm = 3.0);
  void on_pointer_move(geometry::Point2D object_pos, bool shift, bool ctrl);
  bool on_pointer_up(geometry::Point2D object_pos);
  void on_cancel();

  // Handles hit testing and inspection
  [[nodiscard]] GradientHandleHit hit_test_handles(geometry::Point2D object_pos, double tolerance) const;
  [[nodiscard]] std::vector<GradientHandleHit> get_handles() const;

  // Direct edits to draft paint
  bool set_draft_paint(document::GradientPaint paint);
  bool reverse_stops();
  bool fit_to_object_bounds(const geometry::Rect2D &bounds);

  // Commit draft to document as atomic transaction
  bool commit();

private:
  void init_draft_from_targets();
  void update_creating_drag(geometry::Point2D current, bool shift, bool ctrl);
  void update_handle_drag(geometry::Point2D current, bool shift);
  static geometry::Point2D snap_angle_15_deg(geometry::Point2D start, geometry::Point2D current);

  document::Document &document_;
  std::vector<document::ObjectId> target_ids_;
  document::GradientKind kind_{document::GradientKind::Linear};
  document::GradientSpread spread_{document::GradientSpread::Pad};
  GradientToolState state_{GradientToolState::Idle};
  uint64_t draft_generation_{1};

  std::optional<document::GradientPaint> draft_paint_;
  geometry::Point2D drag_start_{0.0, 0.0};
  geometry::Point2D drag_current_{0.0, 0.0};
  GradientHandleHit active_handle_{};
  geometry::Rect2D initial_target_bounds_{0.0, 0.0, 100.0, 100.0};
};

} // namespace pikdraw::vector::tools
