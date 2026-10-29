#include "pikdraw/rendering/spatial_index.hpp"

#include "pikdraw/geometry/queries/aabb_index.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pikdraw::rendering {
namespace {
struct IndexedItem {
  document::ObjectId id{document::kInvalidObjectId};
  const document::ObjectVariant *object{nullptr};
  geometry::Rect2D bounds{};
};

geometry::Rect2D invalid_bounds() {
  return {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0};
}
} // namespace

struct SpatialIndex::Impl {
  uint64_t cached_epoch{0};
  uint64_t cached_revision{0};
  document::PageId cached_page_id{document::kInvalidPageId};
  double cached_zoom{0.0};
  double cached_margin{0.0};
  std::string cached_document_id;
  const document::DocumentSnapshot *cached_snapshot{nullptr};
  bool cache_initialized{false};
  bool index_ready{false};
  std::vector<IndexedItem> items;
  geometry::AabbIndex index;

  void clear() {
    cached_epoch = 0;
    cached_revision = 0;
    cached_page_id = document::kInvalidPageId;
    cached_zoom = 0.0;
    cached_margin = 0.0;
    cached_document_id.clear();
    cached_snapshot = nullptr;
    cache_initialized = false;
    index_ready = false;
    items.clear();
    index.clear();
  }

  [[nodiscard]] bool matches(const document::DocumentSnapshot &snapshot,
                             double camera_zoom, double margin) const {
    (void)camera_zoom;
    (void)margin;
    return cache_initialized && cached_snapshot == &snapshot &&
           cached_document_id == snapshot.document_id() &&
           cached_epoch == snapshot.epoch() &&
           cached_revision == snapshot.revision() &&
           cached_page_id == snapshot.active_page_id();
  }

  void build(const document::DocumentSnapshot &snapshot, double camera_zoom,
             double margin) {
    clear();
    cache_initialized = true;
    cached_snapshot = &snapshot;
    cached_document_id = snapshot.document_id();
    cached_epoch = snapshot.epoch();
    cached_revision = snapshot.revision();
    cached_page_id = snapshot.active_page_id();
    cached_zoom = camera_zoom;
    cached_margin = margin;

    std::vector<geometry::AabbIndexEntry> entries;
    const auto reserve_count = std::min(snapshot.object_count(),
                                        geometry::AabbIndex::kMaximumEntries);
    items.reserve(reserve_count);
    entries.reserve(reserve_count);

    std::unordered_set<document::ObjectId> visited;
    visited.reserve(reserve_count);
    uint32_t z_order = 0;
    const auto &objects = snapshot.objects();
    for (size_t layer_idx = 0; layer_idx < snapshot.page().layers.size(); ++layer_idx) {
      if (!snapshot.page().effective_layer_visible(layer_idx))
        continue;
      const auto &layer = snapshot.page().layers[layer_idx];
      for (const auto id : layer.object_ids) {
        if (!visited.insert(id).second)
          continue;
        const auto *object = objects.lookup_ref(id);
        if (!object)
          continue;

        bool object_visible = false;
        std::visit([&](const auto &value) { object_visible = value.visible; }, *object);
        if (!object_visible)
          continue;
        if (items.size() >= geometry::AabbIndex::kMaximumEntries) {
          items.clear();
          index.clear();
          index_ready = false;
          return;
        }

        geometry::Rect2D bounds{};
        const bool bounds_valid = SpatialCuller::compute_object_bounds(
            *object, bounds, camera_zoom, margin);
        if (!bounds_valid)
          bounds = {-1e9, -1e9, 2e9, 2e9};

        const size_t item_index = items.size();
        items.push_back({id, object, bounds});

        const bool indexable_object =
            (std::holds_alternative<document::PathObject>(*object) ||
             std::holds_alternative<document::ImageObject>(*object) ||
             std::holds_alternative<document::TextObject>(*object)) && bounds_valid;
        const auto query_bounds = indexable_object ? bounds : invalid_bounds();
        // Keep the stable object ID in items; the BVH only needs a compact
        // lookup token, so its ID field stores the already-resolved vector index.
        entries.push_back({static_cast<uint64_t>(item_index), query_bounds, z_order});
        if (z_order == std::numeric_limits<uint32_t>::max()) {
          items.clear();
          index.clear();
          index_ready = false;
          return;
        }
        ++z_order;
      }
    }
    index_ready = index.rebuild(std::move(entries));
  }
};

SpatialIndex::SpatialIndex() : impl_(std::make_unique<Impl>()) {}
SpatialIndex::~SpatialIndex() = default;
SpatialIndex::SpatialIndex(SpatialIndex &&) noexcept = default;
SpatialIndex &SpatialIndex::operator=(SpatialIndex &&) noexcept = default;

void SpatialIndex::clear() { impl_->clear(); }

uint64_t SpatialIndex::cached_revision() const noexcept {
  return impl_->cached_revision;
}

size_t SpatialIndex::indexed_count() const noexcept { return impl_->items.size(); }

void SpatialIndex::update(const document::DocumentSnapshot &snapshot,
                          double camera_zoom) {
  if (!impl_->matches(snapshot, camera_zoom, 0.0))
    impl_->build(snapshot, camera_zoom, 0.0);
}

std::vector<CulledObjectRef> SpatialIndex::query_visible_refs(
    const document::DocumentSnapshot &snapshot,
    const geometry::Rect2D &visible_bounds_mm, double camera_zoom,
    double conservative_world_margin_mm) {
  if (snapshot.object_count() <= 256 || !visible_bounds_mm.is_finite() ||
      visible_bounds_mm.width < 0.0 || visible_bounds_mm.height < 0.0) {
    return SpatialCuller::query_visible_refs(
        snapshot, visible_bounds_mm, camera_zoom,
        conservative_world_margin_mm);
  }

  if (!impl_->matches(snapshot, camera_zoom, conservative_world_margin_mm))
    impl_->build(snapshot, camera_zoom, conservative_world_margin_mm);
  if (!impl_->index_ready)
    return SpatialCuller::query_visible_refs(
        snapshot, visible_bounds_mm, camera_zoom,
        conservative_world_margin_mm);

  auto query_rect = visible_bounds_mm;
  if (conservative_world_margin_mm > 0.0) {
    query_rect.x -= conservative_world_margin_mm;
    query_rect.y -= conservative_world_margin_mm;
    query_rect.width += 2.0 * conservative_world_margin_mm;
    query_rect.height += 2.0 * conservative_world_margin_mm;
  }
  const auto matches = impl_->index.query(query_rect);
  std::vector<CulledObjectRef> result;
  result.reserve(matches.size());
  for (const auto &entry : matches) {
    if (entry.id >= impl_->items.size())
      continue;
    const auto &indexed = impl_->items[static_cast<size_t>(entry.id)];
    result.push_back({indexed.id, indexed.object, indexed.bounds});
  }
  return result;
}

std::vector<CulledObject> SpatialIndex::query_visible(
    const document::DocumentSnapshot &snapshot,
    const geometry::Rect2D &visible_bounds_mm, double camera_zoom,
    double conservative_world_margin_mm) {
  const auto refs = query_visible_refs(snapshot, visible_bounds_mm,
                                       camera_zoom,
                                       conservative_world_margin_mm);
  std::vector<CulledObject> result;
  result.reserve(refs.size());
  for (const auto &ref : refs)
    result.push_back({ref.id, *ref.object, ref.bounds});
  return result;
}

} // namespace pikdraw::rendering
