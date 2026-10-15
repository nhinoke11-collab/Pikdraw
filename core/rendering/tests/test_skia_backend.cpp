#include "pikdraw/rendering/backend.hpp"
#include "pikdraw/rendering/fixtures.hpp"
#include "pikdraw/rendering/skia_backend.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
    using namespace pikdraw::rendering;

    constexpr size_t node_count = 100000;
    constexpr double spacing_mm = 63.25;
    constexpr int32_t width = 1280;
    constexpr int32_t height = 800;

    const auto scene = FixtureGenerator::generate_many_small_paths(node_count, spacing_mm);
    if (scene.nodes.size() != node_count ||
        scene.total_bounds_mm.width < 19980.0 ||
        scene.total_bounds_mm.height < 19900.0) {
        std::cerr << "fixture does not match the 100k-node, approximately 20 m workload\n";
        return 1;
    }

    Camera2D camera(Point2D{0.0, 0.0}, 1.0, width, height);
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
    PixelBufferTarget target{
        pixels.data(), pixels.size(), width, height, width * 4
    };
    FrameMetric metric;
    SkiaRasterBackend backend;
    if (!backend.render_frame(camera, scene, target, metric)) {
        std::cerr << "Skia CPU raster backend rejected the fixture\n";
        return 1;
    }

    uint32_t background_pixel = 0;
    std::memcpy(&background_pixel, pixels.data(), sizeof(background_pixel));
    size_t changed_pixels = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += sizeof(uint32_t)) {
        uint32_t pixel = 0;
        std::memcpy(&pixel, pixels.data() + offset, sizeof(pixel));
        changed_pixels += pixel != background_pixel;
    }

    if (metric.total_nodes != node_count || metric.visible_nodes == 0 ||
        metric.culled_nodes + metric.visible_nodes != node_count || changed_pixels == 0) {
        std::cerr << "rendered frame did not produce expected visible and culled pixel output\n";
        return 1;
    }

    std::cout << "Skia CPU raster integration passed: nodes=" << metric.total_nodes
              << " visible=" << metric.visible_nodes
              << " culled=" << metric.culled_nodes
              << " extent_mm=" << scene.total_bounds_mm.width << 'x'
              << scene.total_bounds_mm.height
              << " changed_pixels=" << changed_pixels << '\n';
    return 0;
}
