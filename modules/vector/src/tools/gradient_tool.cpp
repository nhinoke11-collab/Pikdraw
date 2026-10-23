#include "pikdraw/vector/tools/gradient_tool.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::vector::tools {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

GradientTool::GradientTool(document::Document &document) noexcept
    : document_(document) {}

void GradientTool::set_target_objects(std::vector<document::ObjectId> ids) {
  target_ids_ = std::move(ids);
  init_draft_from_targets();
  draft_generation_++;
}

void GradientTool::set_kind(document::GradientKind kind) {
  if (kind_ == kind) return;
  kind_ = kind;
  if (draft_paint_) {
    draft_paint_->kind = kind;
    // Reset geometry to default for the new kind over initial target bounds
    if (kind == document::GradientKind::Linear) {
      const auto default_linear = document::GradientPaint::make_default_linear(
          draft_paint_->id, initial_target_bounds_);
      draft_paint_->gradient_to_object = default_linear.gradient_to_object;
    } else {
      const auto default_radial = document::GradientPaint::make_default_radial(
          draft_paint_->id, initial_target_bounds_);
      draft_paint_->gradient_to_object = default_radial.gradient_to_object;
    }
  }
  draft_generation_++;
}

void GradientTool::set_spread(document::GradientSpread spread) {
  spread_ = spread;
  if (draft_paint_) {
    draft_paint_->spread = spread;
  }
  draft_generation_++;
}

void GradientTool::init_draft_from_targets() {
  if (target_ids_.empty()) {
    draft_paint_ = std::nullopt;
    return;
  }

  const auto snapshot = document_.current_snapshot();
  const auto obj_variant = snapshot->get_object(target_ids_.front());
  if (!obj_variant || !std::holds_alternative<document::PathObject>(*obj_variant)) {
    draft_paint_ = std::nullopt;
    return;
  }

  const auto &po = std::get<document::PathObject>(*obj_variant);
  initial_target_bounds_ = po.bounds();
  if (initial_target_bounds_.width <= 0.0 || initial_target_bounds_.height <= 0.0) {
    initial_target_bounds_ = geometry::Rect2D{0.0, 0.0, 100.0, 100.0};
  }

  if (po.fill_gradient) {
    draft_paint_ = *po.fill_gradient;
    kind_ = draft_paint_->kind;
    spread_ = draft_paint_->spread;
  } else {
    if (kind_ == document::GradientKind::Linear) {
      draft_paint_ = document::GradientPaint::make_default_linear(
          document::allocate_gradient_paint_id(), initial_target_bounds_);
    } else {
      draft_paint_ = document::GradientPaint::make_default_radial(
          document::allocate_gradient_paint_id(), initial_target_bounds_);
    }
    draft_paint_->spread = spread_;
  }
}

geometry::Point2D GradientTool::snap_angle_15_deg(geometry::Point2D start, geometry::Point2D current) {
  const double dx = current.x - start.x;
  const double dy = current.y - start.y;
  const double len = std::hypot(dx, dy);
  if (len <= 1e-9) return current;

  const double step_rad = kShiftSnapAngleDegrees * (kPi / 180.0);
  const double angle = std::atan2(dy, dx);
  const double snapped = std::round(angle / step_rad) * step_rad;
  return geometry::Point2D{start.x + len * std::cos(snapped), start.y + len * std::sin(snapped)};
}

std::vector<GradientHandleHit> GradientTool::get_handles() const {
  std::vector<GradientHandleHit> handles;
  if (!draft_paint_) return handles;

  const auto &mat = draft_paint_->gradient_to_object;
  if (kind_ == document::GradientKind::Linear) {
    handles.push_back({GradientHandleKind::LinearStart, 0, mat.map_point({0.0, 0.0})});
    handles.push_back({GradientHandleKind::LinearEnd, 0, mat.map_point({1.0, 0.0})});
    for (size_t i = 0; i < draft_paint_->stops.size(); ++i) {
      handles.push_back({GradientHandleKind::Stop, i, mat.map_point({draft_paint_->stops[i].offset, 0.0})});
    }
    for (size_t i = 0; i + 1 < draft_paint_->stops.size(); ++i) {
      const double s0 = draft_paint_->stops[i].offset;
      const double s1 = draft_paint_->stops[i + 1].offset;
      const double mid_t = s0 + (s1 - s0) * draft_paint_->stops[i].midpoint_to_next;
      handles.push_back({GradientHandleKind::Midpoint, i, mat.map_point({mid_t, 0.0})});
    }
  } else {
    handles.push_back({GradientHandleKind::RadialCenter, 0, mat.map_point({0.0, 0.0})});
    handles.push_back({GradientHandleKind::RadialRadius, 0, mat.map_point({1.0, 0.0})});
    handles.push_back({GradientHandleKind::RadialAspect, 0, mat.map_point({0.0, 1.0})});
    for (size_t i = 0; i < draft_paint_->stops.size(); ++i) {
      handles.push_back({GradientHandleKind::Stop, i, mat.map_point({draft_paint_->stops[i].offset, 0.0})});
    }
    for (size_t i = 0; i + 1 < draft_paint_->stops.size(); ++i) {
      const double s0 = draft_paint_->stops[i].offset;
      const double s1 = draft_paint_->stops[i + 1].offset;
      const double mid_t = s0 + (s1 - s0) * draft_paint_->stops[i].midpoint_to_next;
      handles.push_back({GradientHandleKind::Midpoint, i, mat.map_point({mid_t, 0.0})});
    }
  }
  return handles;
}

GradientHandleHit GradientTool::hit_test_handles(geometry::Point2D object_pos, double tolerance) const {
  const auto handles = get_handles();
  GradientHandleHit best_hit{};
  double best_dist_sq = tolerance * tolerance;

  for (const auto &h : handles) {
    const double dx = h.position_object.x - object_pos.x;
    const double dy = h.position_object.y - object_pos.y;
    const double dist_sq = dx * dx + dy * dy;
    if (dist_sq < best_dist_sq) {
      best_dist_sq = dist_sq;
      best_hit = h;
    }
  }
  return best_hit;
}

void GradientTool::on_pointer_down(geometry::Point2D object_pos, bool shift, bool ctrl, double hit_tolerance_mm) {
  (void)shift;
  (void)ctrl;
  const auto hit = hit_test_handles(object_pos, hit_tolerance_mm);
  if (hit.kind != GradientHandleKind::None) {
    state_ = GradientToolState::DraggingHandle;
    active_handle_ = hit;
    drag_start_ = object_pos;
    drag_current_ = object_pos;
  } else {
    state_ = GradientToolState::CreatingDrag;
    drag_start_ = object_pos;
    drag_current_ = object_pos;
    if (!draft_paint_) {
      init_draft_from_targets();
    }
  }
  draft_generation_++;
}

void GradientTool::on_pointer_move(geometry::Point2D object_pos, bool shift, bool ctrl) {
  if (state_ == GradientToolState::CreatingDrag) {
    drag_current_ = object_pos;
    update_creating_drag(object_pos, shift, ctrl);
    draft_generation_++;
  } else if (state_ == GradientToolState::DraggingHandle) {
    drag_current_ = object_pos;
    update_handle_drag(object_pos, shift);
    draft_generation_++;
  }
}

void GradientTool::update_creating_drag(geometry::Point2D current, bool shift, bool ctrl) {
  (void)ctrl;
  if (!draft_paint_) return;

  const auto target_pt = shift ? snap_angle_15_deg(drag_start_, current) : current;
  const double dx = target_pt.x - drag_start_.x;
  const double dy = target_pt.y - drag_start_.y;
  const double len = std::hypot(dx, dy);
  if (len < 1e-4) return;

  if (kind_ == document::GradientKind::Linear) {
    draft_paint_->gradient_to_object = geometry::Transform2D(
        dx, dy, -dy, dx, drag_start_.x, drag_start_.y);
  } else {
    const double aspect = (initial_target_bounds_.width > 0.0 && initial_target_bounds_.height > 0.0)
                              ? (initial_target_bounds_.height / initial_target_bounds_.width)
                              : 1.0;
    draft_paint_->gradient_to_object = geometry::Transform2D(
        dx, dy, -dy * aspect, dx * aspect, drag_start_.x, drag_start_.y);
  }
}

void GradientTool::update_handle_drag(geometry::Point2D current, bool shift) {
  if (!draft_paint_) return;
  auto &mat = draft_paint_->gradient_to_object;

  switch (active_handle_.kind) {
  case GradientHandleKind::LinearStart: {
    // Start handle moves, end handle stays fixed
    const auto end_pt = mat.map_point({1.0, 0.0});
    const auto start_pt = shift ? snap_angle_15_deg(end_pt, current) : current;
    const double dx = end_pt.x - start_pt.x;
    const double dy = end_pt.y - start_pt.y;
    if (std::hypot(dx, dy) >= 1e-4) {
      mat = geometry::Transform2D(dx, dy, -dy, dx, start_pt.x, start_pt.y);
    }
    break;
  }
  case GradientHandleKind::LinearEnd: {
    // End handle moves, start handle stays fixed
    const auto start_pt = mat.map_point({0.0, 0.0});
    const auto end_pt = shift ? snap_angle_15_deg(start_pt, current) : current;
    const double dx = end_pt.x - start_pt.x;
    const double dy = end_pt.y - start_pt.y;
    if (std::hypot(dx, dy) >= 1e-4) {
      mat = geometry::Transform2D(dx, dy, -dy, dx, start_pt.x, start_pt.y);
    }
    break;
  }
  case GradientHandleKind::RadialCenter: {
    // Center handle translates gradient
    mat.tx = current.x;
    mat.ty = current.y;
    break;
  }
  case GradientHandleKind::RadialRadius: {
    const auto center_pt = geometry::Point2D{mat.tx, mat.ty};
    const auto rad_pt = shift ? snap_angle_15_deg(center_pt, current) : current;
    const double dx = rad_pt.x - center_pt.x;
    const double dy = rad_pt.y - center_pt.y;
    if (std::hypot(dx, dy) >= 1e-4) {
      const double aspect = (initial_target_bounds_.width > 0.0 && initial_target_bounds_.height > 0.0)
                                ? (initial_target_bounds_.height / initial_target_bounds_.width)
                                : 1.0;
      mat = geometry::Transform2D(dx, dy, -dy * aspect, dx * aspect, center_pt.x, center_pt.y);
    }
    break;
  }
  case GradientHandleKind::RadialAspect: {
    const auto center_pt = geometry::Point2D{mat.tx, mat.ty};
    const double dy_aspect = std::abs(current.y - center_pt.y);
    if (dy_aspect >= 1e-4) {
      mat.c = -mat.b * (dy_aspect / std::max(1e-4, std::abs(mat.a)));
      mat.d = mat.a * (dy_aspect / std::max(1e-4, std::abs(mat.a)));
    }
    break;
  }
  case GradientHandleKind::Stop: {
    const auto inv = mat.inverted();
    if (inv && active_handle_.stop_index < draft_paint_->stops.size()) {
      const auto q = inv->map_point(current);
      const double new_offset = std::clamp(q.x, 0.0, 1.0);
      draft_paint_->stops[active_handle_.stop_index].offset = new_offset;
      // Keep sorted by offset while preserving stable IDs
      std::stable_sort(draft_paint_->stops.begin(), draft_paint_->stops.end(),
                       [](const auto &a, const auto &b) { return a.offset < b.offset; });
    }
    break;
  }
  case GradientHandleKind::Midpoint: {
    const auto inv = mat.inverted();
    if (inv && active_handle_.stop_index + 1 < draft_paint_->stops.size()) {
      const auto q = inv->map_point(current);
      const double s0 = draft_paint_->stops[active_handle_.stop_index].offset;
      const double s1 = draft_paint_->stops[active_handle_.stop_index + 1].offset;
      const double seg_len = s1 - s0;
      if (seg_len > 1e-6) {
        const double t = (q.x - s0) / seg_len;
        draft_paint_->stops[active_handle_.stop_index].midpoint_to_next = std::clamp(t, 0.05, 0.95);
      }
    }
    break;
  }
  case GradientHandleKind::None:
    break;
  }
}

bool GradientTool::on_pointer_up(geometry::Point2D object_pos) {
  if (state_ == GradientToolState::CreatingDrag) {
    const double dist = std::hypot(object_pos.x - drag_start_.x, object_pos.y - drag_start_.y);
    if (dist < kMinDragDistanceMm) {
      // Under drag threshold: do not commit gradient implicitly (AC-G08)
      state_ = GradientToolState::Idle;
      draft_generation_++;
      return false;
    }
    const bool ok = commit();
    state_ = GradientToolState::Idle;
    draft_generation_++;
    return ok;
  }

  if (state_ == GradientToolState::DraggingHandle) {
    const bool ok = commit();
    state_ = GradientToolState::Idle;
    draft_generation_++;
    return ok;
  }

  state_ = GradientToolState::Idle;
  return false;
}

void GradientTool::on_cancel() {
  state_ = GradientToolState::Idle;
  draft_generation_++;
  init_draft_from_targets();
}

bool GradientTool::set_draft_paint(document::GradientPaint paint) {
  if (const auto err = paint.validation_error()) {
    return false;
  }
  draft_paint_ = std::move(paint);
  kind_ = draft_paint_->kind;
  spread_ = draft_paint_->spread;
  draft_generation_++;
  return true;
}

bool GradientTool::reverse_stops() {
  if (!draft_paint_) return false;
  draft_paint_ = draft_paint_->with_reversed_stops();
  draft_generation_++;
  return true;
}

bool GradientTool::fit_to_object_bounds(const geometry::Rect2D &bounds) {
  if (!draft_paint_) return false;
  if (!bounds.is_finite() || bounds.width <= 0.0 || bounds.height <= 0.0) return false;

  initial_target_bounds_ = bounds;
  if (kind_ == document::GradientKind::Linear) {
    const auto def = document::GradientPaint::make_default_linear(draft_paint_->id, bounds);
    draft_paint_->gradient_to_object = def.gradient_to_object;
  } else {
    const auto def = document::GradientPaint::make_default_radial(draft_paint_->id, bounds);
    draft_paint_->gradient_to_object = def.gradient_to_object;
  }
  draft_generation_++;
  return true;
}

bool GradientTool::commit() {
  if (!draft_paint_ || target_ids_.empty()) return false;
  if (const auto err = draft_paint_->validation_error()) return false;

  std::vector<document::FillPaintReplacement> replacements;
  replacements.reserve(target_ids_.size());
  auto grad_ptr = std::make_shared<const document::GradientPaint>(*draft_paint_);
  for (const auto id : target_ids_) {
    replacements.push_back(document::FillPaintReplacement{
        id,
        true,
        color::RGBColor{0.0, 0.0, 0.0, 1.0},
        grad_ptr
    });
  }

  auto cmd = std::make_unique<document::SetFillPaintCommand>(std::move(replacements));
  const auto res = document_.execute_command(std::move(cmd));
  draft_generation_++;
  return res.success;
}

} // namespace pikdraw::vector::tools
