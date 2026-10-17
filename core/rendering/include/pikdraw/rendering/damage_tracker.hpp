#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace pikdraw::rendering {

/// Tracks damaged world bounds between document snapshots and revisions.
/// Conforms to Contract C3 §3 and C9 §3:
/// "Damage after an appearance edit is the union of old and new visual-output
/// bounds, clipped only to the render target."
class DamageTracker {
public:
  DamageTracker();
  ~DamageTracker();

  DamageTracker(const DamageTracker &) = delete;
  DamageTracker &operator=(const DamageTracker &) = delete;
  DamageTracker(DamageTracker &&) noexcept;
  DamageTracker &operator=(DamageTracker &&) noexcept;

  /// Resets all accumulated damage and clears snapshot cache.
  void reset();

  /// Flags the entire canvas as needing repaint.
  void mark_full_damage();

  /// Returns true if the entire viewport is invalidated.
  [[nodiscard]] bool is_full_damage() const noexcept;

  /// Returns true if there is any pending dirty region.
  [[nodiscard]] bool has_damage() const noexcept;

  /// Returns the current dirty bounding box in world space (mm).
  [[nodiscard]] const geometry::Rect2D &damage_rect() const noexcept;

  /// Accumulates an explicit damage rectangle in world space.
  void add_damage_rect(const geometry::Rect2D &rect);

  /// Computes and accumulates the union of old and new visual bounds for changed,
  /// added, or deleted objects between the old and new snapshot.
  void update(const document::DocumentSnapshot &old_snapshot,
              const document::DocumentSnapshot &new_snapshot,
              double camera_zoom);

  /// Clears pending damage while preserving cached object bounds.
  void clear_damage();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::rendering
