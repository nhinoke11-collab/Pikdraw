#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include <vector>

namespace pikdraw::rendering {

struct CulledObject {
    document::ObjectId id;
    document::ObjectVariant object;
    geometry::Rect2D bounds;
};

/// Spatial culling service filtering objects against camera visible frustum
class SpatialCuller {
public:
    /// Queries the document snapshot and returns all visible objects intersecting the view bounds
    [[nodiscard]] static std::vector<CulledObject> query_visible(
        const document::DocumentSnapshot& snapshot,
        const geometry::Rect2D& visible_bounds_mm) {
        std::vector<CulledObject> result;
        const auto& all_objects = snapshot.objects().all();

        for (const auto& [id, obj_var] : all_objects) {
            geometry::Rect2D bounds;
            bool visible = true;

            std::visit([&](const auto& obj) {
                using T = std::decay_t<decltype(obj)>;
                visible = obj.visible;
                if constexpr (std::is_same_v<T, document::PathObject>) {
                    bounds = obj.bounds();
                } else if constexpr (std::is_same_v<T, document::TextObject>) {
                    bounds = obj.bounds();
                } else {
                    bounds = geometry::Rect2D{0, 0, 0, 0};
                }
            }, obj_var);

            if (!visible) continue;

            if (visible_bounds_mm.intersects(bounds)) {
                result.push_back(CulledObject{id, obj_var, bounds});
            }
        }

        return result;
    }
};

} // namespace pikdraw::rendering
