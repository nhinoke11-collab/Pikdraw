#include "pikdraw/rendering/damage_tracker.hpp"
#include "pikdraw/rendering/culling.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pikdraw::rendering {

namespace {

geometry::Rect2D union_rects(const geometry::Rect2D &a,
                             const geometry::Rect2D &b) noexcept {
  if (a.width <= 0.0 || a.height <= 0.0)
    return b;
  if (b.width <= 0.0 || b.height <= 0.0)
    return a;
  const double left = std::min(a.x, b.x);
  const double top = std::min(a.y, b.y);
  const double right = std::max(a.right(), b.right());
  const double bottom = std::max(a.bottom(), b.bottom());
  return {left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
}

bool same_render_structure(const document::Page &lhs,
                           const document::Page &rhs) {
  if (lhs.width_mm != rhs.width_mm || lhs.height_mm != rhs.height_mm ||
      lhs.layers.size() != rhs.layers.size())
    return false;
  std::unordered_map<document::ObjectId, size_t> lhs_layer_by_id;
  std::unordered_map<document::ObjectId, size_t> rhs_layer_by_id;
  for (size_t i = 0; i < lhs.layers.size(); ++i) {
    const auto &left = lhs.layers[i];
    const auto &right = rhs.layers[i];
    if (left.visible != right.visible)
      return false;
    for (const auto id : left.object_ids)
      lhs_layer_by_id.emplace(id, i);
    for (const auto id : right.object_ids)
      rhs_layer_by_id.emplace(id, i);

    std::unordered_set<document::ObjectId> left_ids(left.object_ids.begin(),
                                                     left.object_ids.end());
    std::unordered_set<document::ObjectId> right_ids(right.object_ids.begin(),
                                                      right.object_ids.end());
    std::vector<document::ObjectId> left_common;
    std::vector<document::ObjectId> right_common;
    for (const auto id : left.object_ids)
      if (right_ids.contains(id))
        left_common.push_back(id);
    for (const auto id : right.object_ids)
      if (left_ids.contains(id))
        right_common.push_back(id);
    if (left_common != right_common)
      return false;
  }
  for (const auto &[id, old_layer] : lhs_layer_by_id) {
    const auto new_layer = rhs_layer_by_id.find(id);
    if (new_layer != rhs_layer_by_id.end() && new_layer->second != old_layer)
      return false;
  }
  return true;
}

} // namespace

struct DamageTracker::Impl {
  std::unordered_map<document::ObjectId, geometry::Rect2D> cached_bounds;
  geometry::Rect2D accumulated_damage{0, 0, 0, 0};
  bool has_damage{false};
  bool full_damage{false};

  void reset() {
    cached_bounds.clear();
    accumulated_damage = {0, 0, 0, 0};
    has_damage = false;
    full_damage = false;
  }

  void add_damage(const geometry::Rect2D &rect) {
    if (!rect.is_finite() || rect.width <= 0.0 || rect.height <= 0.0)
      return;
    if (!has_damage) {
      accumulated_damage = rect;
      has_damage = true;
    } else {
      accumulated_damage = union_rects(accumulated_damage, rect);
    }
  }

  void mark_full_damage() {
    full_damage = true;
    has_damage = true;
    accumulated_damage = {-1e9, -1e9, 2e9, 2e9};
  }

  void update(const document::DocumentSnapshot &old_snapshot,
              const document::DocumentSnapshot &new_snapshot,
              double camera_zoom) {
    if (full_damage)
      return;

    if (!same_render_structure(old_snapshot.page(), new_snapshot.page())) {
      mark_full_damage();
      return;
    }

    const auto &old_objs = old_snapshot.objects();
    const auto &new_objs = new_snapshot.objects();
    std::unordered_map<document::ObjectId, geometry::Rect2D> new_bounds;
    new_bounds.reserve(new_snapshot.object_count());

    const auto object_bounds = [&](const document::ObjectVariant &object,
                                   geometry::Rect2D &bounds) {
      if (!SpatialCuller::compute_object_bounds(object, bounds, camera_zoom,
                                                0.0)) {
        mark_full_damage();
        return false;
      }
      return true;
    };
    const auto add_object_bounds = [&](const document::ObjectVariant &object) {
      geometry::Rect2D bounds;
      if (object_bounds(object, bounds))
        add_damage(bounds);
    };

    // Evaluate bounds for objects in new snapshot
    for (const auto &layer : new_snapshot.page().layers) {
      for (auto id : layer.object_ids) {
        const auto *new_val = new_objs.lookup_ref(id);
        if (!new_val)
          continue;
        geometry::Rect2D new_bounds_value;
        if (object_bounds(*new_val, new_bounds_value))
          new_bounds[id] = new_bounds_value;

        const auto *old_val = old_objs.lookup_ref(id);
        if (!old_val) {
          // Newly added object: damage is its new bounds
          add_object_bounds(*new_val);
        } else if (old_val != new_val) {
          // Persistent snapshots share the immutable entry for unchanged
          // objects. A different borrowed value address means some field in
          // the full object payload changed, including geometry/effects/media.
          add_object_bounds(*old_val);
          add_object_bounds(*new_val);
        }
      }
    }

    // Check deleted objects against the supplied old snapshot. This works on
    // the first update too, when no previous cache has been populated.
    for (const auto &layer : old_snapshot.page().layers) {
      for (const auto id : layer.object_ids) {
        const auto *old_val = old_objs.lookup_ref(id);
        if (old_val && !new_objs.lookup_ref(id)) {
          add_object_bounds(*old_val);
        }
      }
    }
    cached_bounds = std::move(new_bounds);
  }
};

DamageTracker::DamageTracker() : impl_(std::make_unique<Impl>()) {}
DamageTracker::~DamageTracker() = default;
DamageTracker::DamageTracker(DamageTracker &&) noexcept = default;
DamageTracker &DamageTracker::operator=(DamageTracker &&) noexcept = default;

void DamageTracker::reset() {
  impl_->reset();
}

void DamageTracker::mark_full_damage() {
  impl_->mark_full_damage();
}

bool DamageTracker::is_full_damage() const noexcept {
  return impl_->full_damage;
}

bool DamageTracker::has_damage() const noexcept {
  return impl_->has_damage;
}

const geometry::Rect2D &DamageTracker::damage_rect() const noexcept {
  return impl_->accumulated_damage;
}

void DamageTracker::add_damage_rect(const geometry::Rect2D &rect) {
  impl_->add_damage(rect);
}

void DamageTracker::update(const document::DocumentSnapshot &old_snapshot,
                           const document::DocumentSnapshot &new_snapshot,
                           double camera_zoom) {
  impl_->update(old_snapshot, new_snapshot, camera_zoom);
}

void DamageTracker::clear_damage() {
  impl_->has_damage = false;
  impl_->full_damage = false;
  impl_->accumulated_damage = {0, 0, 0, 0};
}

} // namespace pikdraw::rendering
