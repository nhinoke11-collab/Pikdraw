#include "pikdraw/rendering/fixtures.hpp"
#include <cmath>

namespace pikdraw::rendering {

ExperimentalSceneSnapshot FixtureGenerator::generate_many_small_paths(size_t object_count) {
    ExperimentalSceneSnapshot scene;
    scene.document_id = 1001;
    scene.epoch = 1;
    scene.revision = 1;
    scene.generation = 1;
    scene.nodes.reserve(object_count + 1);

    const size_t grid_dim = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(object_count))));
    const double spacing = 4.0;
    const double size = 2.5;

    for (size_t i = 0; i < object_count; ++i) {
        const size_t row = i / grid_dim;
        const size_t col = i % grid_dim;
        const double bx = col * spacing;
        const double by = row * spacing;

        SceneNode node;
        node.id = static_cast<uint64_t>(i + 1);
        node.type = NodeType::Path;
        node.world_bounds_mm = Rect2D{ bx, by, size, size };
        node.path_data.has_fill = true;
        node.path_data.has_stroke = true;
        node.path_data.stroke_width_mm = 0.1;
        node.path_data.fill_color = ColorRGBA8{
            static_cast<uint8_t>((col * 7) % 255),
            static_cast<uint8_t>((row * 13) % 255),
            static_cast<uint8_t>((i * 17) % 255),
            255
        };
        node.path_data.stroke_color = ColorRGBA8{ 10, 20, 40, 255 };

        node.path_data.segments.push_back(PathSegment{ SegmentType::MoveTo, Point2D{ bx, by }, {}, {} });
        node.path_data.segments.push_back(PathSegment{ SegmentType::LineTo, Point2D{ bx + size, by }, {}, {} });
        node.path_data.segments.push_back(PathSegment{ SegmentType::LineTo, Point2D{ bx + size, by + size }, {}, {} });
        node.path_data.segments.push_back(PathSegment{ SegmentType::LineTo, Point2D{ bx, by + size }, {}, {} });
        node.path_data.segments.push_back(PathSegment{ SegmentType::Close, Point2D{ bx, by }, {}, {} });

        scene.nodes.push_back(std::move(node));
    }

    SceneNode text_node;
    text_node.id = static_cast<uint64_t>(object_count + 1);
    text_node.type = NodeType::Text;
    text_node.text_data.text_utf8 = "PikDraw: Do hoa vector chuyen nghiep (Kiem thu 100k nodes)";
    text_node.text_data.position = Point2D{ 10.0, -10.0 };
    text_node.text_data.font_size_mm = 6.0;
    text_node.text_data.shaping_status = TextShapingStatus::UnsupportedWithoutTypeface;
    text_node.world_bounds_mm = Rect2D{ 10.0, -15.0, 200.0, 10.0 };
    scene.nodes.push_back(std::move(text_node));

    scene.compute_total_bounds();
    return scene;
}

ExperimentalSceneSnapshot FixtureGenerator::generate_few_large_paths(size_t path_count, size_t points_per_path) {
    ExperimentalSceneSnapshot scene;
    scene.document_id = 1002;
    scene.epoch = 1;
    scene.revision = 1;
    scene.generation = 1;
    scene.nodes.reserve(path_count);

    for (size_t p = 0; p < path_count; ++p) {
        SceneNode node;
        node.id = static_cast<uint64_t>(p + 1);
        node.type = NodeType::Path;
        node.path_data.has_fill = false;
        node.path_data.has_stroke = true;
        node.path_data.stroke_width_mm = 0.25;
        node.path_data.stroke_color = ColorRGBA8{
            static_cast<uint8_t>((p * 50 + 20) % 255),
            static_cast<uint8_t>((p * 80 + 50) % 255),
            static_cast<uint8_t>((p * 110 + 100) % 255),
            255
        };
        node.path_data.segments.reserve(points_per_path);

        const double base_y = p * 150.0;
        double min_x = 0.0, max_x = 0.0, min_y = base_y, max_y = base_y;

        for (size_t i = 0; i < points_per_path; ++i) {
            const double t = static_cast<double>(i) * 0.05;
            const double x = i * 0.1;
            const double y = base_y + std::sin(t) * 30.0 + std::cos(t * 0.3) * 15.0;
            if (i == 0) {
                node.path_data.segments.push_back(PathSegment{ SegmentType::MoveTo, Point2D{ x, y }, {}, {} });
                min_x = max_x = x;
                min_y = max_y = y;
            } else {
                node.path_data.segments.push_back(PathSegment{ SegmentType::LineTo, Point2D{ x, y }, {}, {} });
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
        node.world_bounds_mm = Rect2D{ min_x, min_y, max_x - min_x, max_y - min_y };
        scene.nodes.push_back(std::move(node));
    }

    scene.compute_total_bounds();
    return scene;
}

} // namespace pikdraw::rendering
