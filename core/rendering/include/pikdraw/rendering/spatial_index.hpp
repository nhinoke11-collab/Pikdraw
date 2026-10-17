#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/rendering/culling.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace pikdraw::rendering {

/// Spatial acceleration structure (BVH) for 2D bounding boxes.
/// Caches spatial partitioning keyed by SnapshotRevision and camera zoom.
/// When object count > 256, provides O(log N) frustum culling.
class SpatialIndex {
public:
  SpatialIndex();
  ~SpatialIndex();

  SpatialIndex(const SpatialIndex &) = delete;
  SpatialIndex &operator=(const SpatialIndex &) = delete;
  SpatialIndex(SpatialIndex &&) noexcept;
  SpatialIndex &operator=(SpatialIndex &&) noexcept;

  /// Clear any cached hierarchy.
  void clear();

  /// Returns the revision of the currently cached tree, or 0 if empty.
  [[nodiscard]] uint64_t cached_revision() const noexcept;

  /// Returns the number of indexed objects in the current cache.
  [[nodiscard]] size_t indexed_count() const noexcept;

  /// Builds or updates the spatial index for the given snapshot and zoom if needed.
  void update(const document::DocumentSnapshot &snapshot, double camera_zoom);

  /// Queries objects intersecting the visible world bounds.
  /// Always returns results sorted in original document back-to-front painter order.
  [[nodiscard]] std::vector<CulledObjectRef>
  query_visible_refs(const document::DocumentSnapshot &snapshot,
                     const geometry::Rect2D &visible_bounds_mm,
                     double camera_zoom,
                     double conservative_world_margin_mm = 0.0);

  /// Helper returning copies instead of borrowed references.
  [[nodiscard]] std::vector<CulledObject>
  query_visible(const document::DocumentSnapshot &snapshot,
                const geometry::Rect2D &visible_bounds_mm,
                double camera_zoom,
                double conservative_world_margin_mm = 0.0);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::rendering
