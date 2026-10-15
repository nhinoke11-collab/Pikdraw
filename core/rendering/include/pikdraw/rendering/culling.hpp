#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <cmath>
#include <unordered_set>
#include <vector>

namespace pikdraw::rendering {

struct CulledObject {
  document::ObjectId id;
  document::ObjectVariant object;
  geometry::Rect2D bounds;
};

/// Borrowed entries remain valid while the input immutable snapshot is alive.
struct CulledObjectRef {
  document::ObjectId id;
  const document::ObjectVariant *object;
  geometry::Rect2D bounds;
};

/// Linear baseline query. Layer and object vectors define back-to-front order.
class SpatialCuller {
  template <class Map>
  static const document::ObjectVariant *lookup(const Map &map,
                                               document::ObjectId id) {
    if constexpr (requires { map.lookup_ref(id); }) {
      return map.lookup_ref(id);
    } else {
      // Source compatibility with pre-audit persistent map.
      const auto &values = map.all();
      const auto it = values.find(id);
      return it == values.end() ? nullptr : &it->second;
    }
  }

public:
  [[nodiscard]] static std::vector<CulledObjectRef>
  query_visible_refs(const document::DocumentSnapshot &snapshot,
                     const geometry::Rect2D &visible_bounds_mm) {
    std::vector<CulledObjectRef> result;
    std::unordered_set<document::ObjectId> visited;
    const auto &objects = snapshot.objects();
    for (const auto &layer : snapshot.page().layers) {
      if (!layer.visible)
        continue;
      for (auto id : layer.object_ids) {
        const auto *object = lookup(objects, id);
        if (!object || !visited.insert(id).second)
          continue;
        geometry::Rect2D bounds;
        bool include = false;
        std::visit(
            [&](const auto &obj) {
              using T = std::decay_t<decltype(obj)>;
              if (!obj.visible)
                return;
              if constexpr (std::is_same_v<T, document::PathObject>) {
                bounds = obj.transform.map_rect(obj.path.bounds());
                const auto &t = obj.transform;
                const double expansion =
                    obj.has_stroke ? obj.stroke_width_mm * 0.5 *
                                         std::hypot(std::hypot(t.a, t.b),
                                                    std::hypot(t.c, t.d))
                                   : 0.0;
                bounds.x -= expansion;
                bounds.y -= expansion;
                bounds.width += 2 * expansion;
                bounds.height += 2 * expansion;
                // Invalid data must reach render validation rather than vanish.
                include =
                    !obj.transform.is_finite() || !std::isfinite(expansion) ||
                    obj.stroke_width_mm < 0 || !std::isfinite(bounds.x) ||
                    !std::isfinite(bounds.y) || !std::isfinite(bounds.width) ||
                    !std::isfinite(bounds.height) ||
                    visible_bounds_mm.intersects(bounds);
              } else {
                // Text bounds are only estimates; groups need hierarchical
                // handling.
                include = true;
              }
            },
            *object);
        if (include)
          result.push_back({id, object, bounds});
      }
    }
    return result;
  }
  // Compatibility copy API; frame rendering uses borrowed query_visible_refs.
  [[nodiscard]] static std::vector<CulledObject>
  query_visible(const document::DocumentSnapshot &snapshot,
                const geometry::Rect2D &bounds) {
    std::vector<CulledObject> result;
    for (const auto &item : query_visible_refs(snapshot, bounds)) {
      result.push_back({item.id, *item.object, item.bounds});
    }
    return result;
  }
};

} // namespace pikdraw::rendering
