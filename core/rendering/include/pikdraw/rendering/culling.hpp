#pragma once

#include "pikdraw/rendering/scene.hpp"
#include "pikdraw/rendering/camera.hpp"
#include <vector>

namespace pikdraw::rendering {

struct CullingResult {
    std::vector<size_t> visible_node_indices;
    size_t total_nodes{0};
    size_t culled_nodes{0};
};

class SpatialCuller {
public:
    [[nodiscard]] static CullingResult cull(
        const ExperimentalSceneSnapshot& scene,
        const Camera2D& camera
    ) noexcept {
        CullingResult result;
        result.total_nodes = scene.nodes.size();
        const Rect2D visible_rect = camera.visible_world_bounds();
        result.visible_node_indices.reserve(scene.nodes.size());

        for (size_t i = 0; i < scene.nodes.size(); ++i) {
            const auto& node = scene.nodes[i];
            const double halo = std::max(0.0, node.path_data.stroke_width_mm * 0.5);
            const Rect2D expanded_bounds{
                node.world_bounds_mm.x - halo,
                node.world_bounds_mm.y - halo,
                node.world_bounds_mm.width + 2.0 * halo,
                node.world_bounds_mm.height + 2.0 * halo
            };
            if (expanded_bounds.intersects(visible_rect)) {
                result.visible_node_indices.push_back(i);
            } else {
                result.culled_nodes++;
            }
        }
        return result;
    }
};

} // namespace pikdraw::rendering
