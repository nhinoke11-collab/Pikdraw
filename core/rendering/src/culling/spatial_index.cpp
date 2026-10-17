#include "pikdraw/rendering/spatial_index.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stack>
#include <unordered_set>
#include <vector>

namespace pikdraw::rendering {

namespace {

struct IndexedItem {
  document::ObjectId id{document::kInvalidObjectId};
  const document::ObjectVariant *object{nullptr};
  geometry::Rect2D bounds;
  uint32_t z_order{0};

  [[nodiscard]] double center_x() const noexcept {
    return bounds.x + bounds.width * 0.5;
  }
  [[nodiscard]] double center_y() const noexcept {
    return bounds.y + bounds.height * 0.5;
  }
};

struct BvhNode {
  geometry::Rect2D bbox;
  int32_t left_child{-1};
  int32_t right_child{-1};
  uint32_t first_item{0};
  uint32_t item_count{0};

  [[nodiscard]] bool is_leaf() const noexcept {
    return left_child == -1 && right_child == -1;
  }
};

geometry::Rect2D union_bounds(const geometry::Rect2D &a,
                              const geometry::Rect2D &b) noexcept {
  const double left = std::min(a.x, b.x);
  const double top = std::min(a.y, b.y);
  const double right = std::max(a.right(), b.right());
  const double bottom = std::max(a.bottom(), b.bottom());
  return {left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
}

} // namespace

struct SpatialIndex::Impl {
  uint64_t cached_revision{0};
  double cached_zoom{0.0};
  double cached_margin{0.0};
  std::vector<IndexedItem> items;
  std::vector<BvhNode> nodes;
  int32_t root_index{-1};

  void clear() {
    cached_revision = 0;
    cached_zoom = 0.0;
    cached_margin = 0.0;
    items.clear();
    nodes.clear();
    root_index = -1;
  }

  void build(const document::DocumentSnapshot &snapshot, double camera_zoom,
             double margin) {
    clear();
    cached_revision = snapshot.revision();
    cached_zoom = camera_zoom;
    cached_margin = margin;

    // Collect visible objects in layer/z-order
    std::unordered_set<document::ObjectId> visited;
    const auto &objects = snapshot.objects();
    uint32_t current_z = 0;

    for (const auto &layer : snapshot.page().layers) {
      if (!layer.visible)
        continue;
      for (auto id : layer.object_ids) {
        if (!visited.insert(id).second)
          continue;
        const auto *obj = objects.lookup_ref(id);
        if (!obj)
          continue;

        bool visible_obj = false;
        std::visit([&](const auto &o) { visible_obj = o.visible; }, *obj);
        if (!visible_obj)
          continue;

        geometry::Rect2D world_bounds;
        const bool valid_bounds = SpatialCuller::compute_object_bounds(
            *obj, world_bounds, camera_zoom, margin);

        IndexedItem item;
        item.id = id;
        item.object = obj;
        item.bounds = valid_bounds ? world_bounds
                                   : geometry::Rect2D{-1e9, -1e9, 2e9, 2e9};
        item.z_order = current_z++;
        items.push_back(item);
      }
    }

    if (items.empty())
      return;

    nodes.reserve(items.size() * 2);
    root_index = build_recursive(0, static_cast<uint32_t>(items.size()));
  }

  int32_t build_recursive(uint32_t start, uint32_t count) {
    if (count == 0)
      return -1;

    const int32_t node_index = static_cast<int32_t>(nodes.size());
    nodes.emplace_back();

    // Compute bounding box
    geometry::Rect2D bbox = items[start].bounds;
    for (uint32_t i = 1; i < count; ++i) {
      bbox = union_bounds(bbox, items[start + i].bounds);
    }
    nodes[node_index].bbox = bbox;

    // Leaf condition: small number of items or cannot split
    constexpr uint32_t kLeafThreshold = 4;
    if (count <= kLeafThreshold) {
      nodes[node_index].first_item = start;
      nodes[node_index].item_count = count;
      return node_index;
    }

    // Split on longest axis of bounding box
    const bool split_x = bbox.width >= bbox.height;
    const auto mid = start + count / 2;

    std::nth_element(
        items.begin() + start, items.begin() + mid, items.begin() + start + count,
        [split_x](const IndexedItem &a, const IndexedItem &b) {
          return split_x ? a.center_x() < b.center_x()
                         : a.center_y() < b.center_y();
        });

    const int32_t left = build_recursive(start, count / 2);
    const int32_t right = build_recursive(mid, count - count / 2);

    nodes[node_index].left_child = left;
    nodes[node_index].right_child = right;
    nodes[node_index].first_item = 0;
    nodes[node_index].item_count = 0;

    return node_index;
  }

  void query_bvh(const geometry::Rect2D &visible_bounds,
                 std::vector<const IndexedItem *> &out_items) const {
    if (root_index < 0 || nodes.empty())
      return;

    std::vector<int32_t> stack;
    stack.reserve(64);
    stack.push_back(root_index);

    while (!stack.empty()) {
      const int32_t curr = stack.back();
      stack.pop_back();

      const auto &node = nodes[curr];
      if (!node.bbox.intersects(visible_bounds))
        continue;

      if (node.is_leaf()) {
        for (uint32_t i = 0; i < node.item_count; ++i) {
          const auto &item = items[node.first_item + i];
          if (item.bounds.intersects(visible_bounds)) {
            out_items.push_back(&item);
          }
        }
      } else {
        if (node.left_child >= 0)
          stack.push_back(node.left_child);
        if (node.right_child >= 0)
          stack.push_back(node.right_child);
      }
    }
  }
};

SpatialIndex::SpatialIndex() : impl_(std::make_unique<Impl>()) {}
SpatialIndex::~SpatialIndex() = default;
SpatialIndex::SpatialIndex(SpatialIndex &&) noexcept = default;
SpatialIndex &SpatialIndex::operator=(SpatialIndex &&) noexcept = default;

void SpatialIndex::clear() {
  impl_->clear();
}

uint64_t SpatialIndex::cached_revision() const noexcept {
  return impl_->cached_revision;
}

size_t SpatialIndex::indexed_count() const noexcept {
  return impl_->items.size();
}

void SpatialIndex::update(const document::DocumentSnapshot &snapshot,
                          double camera_zoom) {
  if (impl_->cached_revision == snapshot.revision() &&
      std::abs(impl_->cached_zoom - camera_zoom) < 1e-6) {
    return;
  }
  impl_->build(snapshot, camera_zoom, 0.0);
}

std::vector<CulledObjectRef> SpatialIndex::query_visible_refs(
    const document::DocumentSnapshot &snapshot,
    const geometry::Rect2D &visible_bounds_mm,
    double camera_zoom,
    double conservative_world_margin_mm) {
  // If count is <= 256, use linear culling directly for zero-overhead
  if (snapshot.object_count() <= 256) {
    return SpatialCuller::query_visible_refs(
        snapshot, visible_bounds_mm, camera_zoom, conservative_world_margin_mm);
  }

  // When count > 256, use BVH index
  if (impl_->cached_revision != snapshot.revision() ||
      std::abs(impl_->cached_zoom - camera_zoom) > 1e-6 ||
      std::abs(impl_->cached_margin - conservative_world_margin_mm) > 1e-6) {
    impl_->build(snapshot, camera_zoom, conservative_world_margin_mm);
  }

  std::vector<const IndexedItem *> matched;
  matched.reserve(128);
  impl_->query_bvh(visible_bounds_mm, matched);

  // Sort matched items to restore strict back-to-front painter z-order
  std::sort(matched.begin(), matched.end(),
            [](const IndexedItem *a, const IndexedItem *b) {
              return a->z_order < b->z_order;
            });

  std::vector<CulledObjectRef> result;
  result.reserve(matched.size());
  for (const auto *item : matched) {
    result.push_back({item->id, item->object, item->bounds});
  }
  return result;
}

std::vector<CulledObject> SpatialIndex::query_visible(
    const document::DocumentSnapshot &snapshot,
    const geometry::Rect2D &visible_bounds_mm,
    double camera_zoom,
    double conservative_world_margin_mm) {
  const auto refs = query_visible_refs(snapshot, visible_bounds_mm, camera_zoom,
                                       conservative_world_margin_mm);
  std::vector<CulledObject> result;
  result.reserve(refs.size());
  for (const auto &ref : refs) {
    result.push_back({ref.id, *ref.object, ref.bounds});
  }
  return result;
}

} // namespace pikdraw::rendering
