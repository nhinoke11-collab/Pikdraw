#pragma once

#include "pikdraw/rendering/types.hpp"
#include <vector>
#include <string>

namespace pikdraw::rendering {

enum class SegmentType {
    MoveTo,
    LineTo,
    QuadTo,
    CubicTo,
    Close
};

struct PathSegment {
    SegmentType type{SegmentType::LineTo};
    Point2D pt{0.0, 0.0};
    Point2D ctrl1{0.0, 0.0};
    Point2D ctrl2{0.0, 0.0};
};

struct PathGeometry {
    std::vector<PathSegment> segments;
    ColorRGBA8 fill_color{100, 150, 220, 255};
    ColorRGBA8 stroke_color{20, 40, 80, 255};
    double stroke_width_mm{0.2};
    bool has_fill{true};
    bool has_stroke{true};
};

enum class TextShapingStatus {
    UnsupportedWithoutTypeface,
    ShapedWithLoadedTypeface
};

struct TextNode {
    std::string text_utf8;
    Point2D position{0.0, 0.0};
    double font_size_mm{5.0};
    ColorRGBA8 color{30, 30, 30, 255};
    TextShapingStatus shaping_status{TextShapingStatus::UnsupportedWithoutTypeface};
};

enum class NodeType {
    Path,
    Text
};

struct SceneNode {
    uint64_t id{0};
    NodeType type{NodeType::Path};
    Transform2D local_transform{Transform2D::identity()};
    Rect2D world_bounds_mm{0.0, 0.0, 0.0, 0.0};
    PathGeometry path_data;
    TextNode text_data;
};

class ExperimentalSceneSnapshot {
public:
    uint64_t document_id{1};
    uint64_t epoch{1};
    uint64_t revision{1};
    uint64_t generation{1};
    Rect2D total_bounds_mm{0.0, 0.0, 0.0, 0.0};
    std::vector<SceneNode> nodes;

    [[nodiscard]] bool is_stale(uint64_t request_epoch, uint64_t request_revision) const noexcept {
        return (epoch != request_epoch) || (revision != request_revision);
    }

    [[nodiscard]] size_t total_points_count() const noexcept {
        size_t count = 0;
        for (const auto& node : nodes) {
            if (node.type == NodeType::Path) {
                count += node.path_data.segments.size();
            } else if (node.type == NodeType::Text) {
                count += 4;
            }
        }
        return count;
    }

    void compute_total_bounds() noexcept {
        if (nodes.empty()) {
            total_bounds_mm = Rect2D{0.0, 0.0, 0.0, 0.0};
            return;
        }
        double min_x = nodes.front().world_bounds_mm.x;
        double min_y = nodes.front().world_bounds_mm.y;
        double max_x = min_x + nodes.front().world_bounds_mm.width;
        double max_y = min_y + nodes.front().world_bounds_mm.height;
        for (size_t i = 1; i < nodes.size(); ++i) {
            const auto& b = nodes[i].world_bounds_mm;
            min_x = std::min(min_x, b.x);
            min_y = std::min(min_y, b.y);
            max_x = std::max(max_x, b.x + b.width);
            max_y = std::max(max_y, b.y + b.height);
        }
        total_bounds_mm = Rect2D{ min_x, min_y, max_x - min_x, max_y - min_y };
    }
};

} // namespace pikdraw::rendering
