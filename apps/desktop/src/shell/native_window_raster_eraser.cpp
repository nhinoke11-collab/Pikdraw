#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/document/commands/raster_edit_commands.hpp"
#include "pikdraw/raster/edit_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace pikdraw::desktop {
namespace {

std::optional<document::ObjectId> selected_bitmap_id(const NativeWindow& window) {
    const auto& selected = window.root().vector_controller().selection().selected_ids();
    if (selected.size() != 1) return std::nullopt;
    return selected.front();
}

document::RasterEditNodeId next_raster_node_id(
    const document::RasterEditGraph& graph) noexcept {
    uint64_t maximum = 0;
    for (const auto& node : graph.nodes)
        maximum = std::max(maximum, node.id.value);
    if (maximum == std::numeric_limits<uint64_t>::max()) return {};
    return document::RasterEditNodeId{maximum + 1};
}

}  // namespace

bool NativeWindow::start_raster_eraser_stroke(
    const geometry::Point2D& point_world) {
    if (!point_world.is_finite() || !can_edit_selected_bitmap()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh có nguồn được giữ lại, hiển thị và mở khóa trước khi tẩy"
            : "Select one visible, unlocked bitmap with a preserved source before erasing";
        return false;
    }
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    const auto* value = selected && snapshot
        ? snapshot->objects().lookup_ref(*selected) : nullptr;
    const auto* image = value ? std::get_if<document::ImageObject>(value) : nullptr;
    const auto* owner = selected && snapshot ? snapshot->hierarchy().find(*selected) : nullptr;
    if (!image || !owner) return false;
    const auto [width, height] = raster::raster_edited_pixel_dimensions(*image);
    if (width == 0 || height == 0) return false;
    const auto local_bounds = raster::raster_edited_bounds(*image, width, height);
    // SceneHierarchy::Entry::world already includes this image's transform.
    const auto world_transform = owner->world;
    const auto world_inverse = world_transform.inverted();
    if (!local_bounds.is_finite() || local_bounds.width <= 0.0 ||
        local_bounds.height <= 0.0 || !world_inverse) return false;
    const auto local = world_inverse->map_point(point_world);
    const double normalized_x = (local.x - local_bounds.x) / local_bounds.width;
    const double normalized_y = (local.y - local_bounds.y) / local_bounds.height;
    if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y) ||
        normalized_x < 0.0 || normalized_x > 1.0 ||
        normalized_y < 0.0 || normalized_y > 1.0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bắt đầu nét bên trong vùng ảnh đã chọn"
            : "Begin the eraser stroke inside the selected image bounds";
        return false;
    }
    raster_eraser_stroke_.clear();
    raster_eraser_stroke_.push_back(point_world);
    raster_eraser_stroke_overflow_ = false;
    return true;
}

bool NativeWindow::commit_raster_eraser_stroke(
    const std::vector<geometry::Point2D>& points_world) {
    if (points_world.empty() ||
        points_world.size() > document::RasterEraserParams::kMaxPoints ||
        !std::isfinite(raster_eraser_diameter_px_) ||
        raster_eraser_diameter_px_ <= 0.0 || raster_eraser_diameter_px_ > 4096.0 ||
        !can_edit_selected_bitmap()) return false;
    const auto selected = selected_bitmap_id(*this);
    const auto snapshot = root().document().current_snapshot();
    const auto* value = selected && snapshot
        ? snapshot->objects().lookup_ref(*selected) : nullptr;
    const auto* image = value ? std::get_if<document::ImageObject>(value) : nullptr;
    const auto* owner = selected && snapshot ? snapshot->hierarchy().find(*selected) : nullptr;
    if (!image || !owner || image->edit_graph.nodes.size() >=
            document::RasterEditGraph::kMaxNodes) return false;
    const auto [width, height] = raster::raster_edited_pixel_dimensions(*image);
    if (width == 0 || height == 0) return false;
    const auto local_bounds = raster::raster_edited_bounds(*image, width, height);
    const auto world_inverse = owner->world.inverted();
    if (!local_bounds.is_finite() || local_bounds.width <= 0.0 ||
        local_bounds.height <= 0.0 || !world_inverse) return false;

    document::RasterEraserParams parameters;
    parameters.diameter_px = raster_eraser_diameter_px_;
    parameters.points.reserve(points_world.size() * 2);
    const auto map_normalized = [&](const geometry::Point2D& point_world)
        -> std::optional<document::RasterEraserPoint> {
        if (!point_world.is_finite()) return std::nullopt;
        const auto local = world_inverse->map_point(point_world);
        const double x = (local.x - local_bounds.x) / local_bounds.width;
        const double y = (local.y - local_bounds.y) / local_bounds.height;
        if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
        return document::RasterEraserPoint{x, y};
    };
    const auto append = [&](document::RasterEraserPoint point) {
        point.x = std::clamp(point.x, 0.0, 1.0);
        point.y = std::clamp(point.y, 0.0, 1.0);
        if (parameters.points.empty() ||
            std::hypot(parameters.points.back().x - point.x,
                       parameters.points.back().y - point.y) > 1e-12)
            parameters.points.push_back(point);
    };
    const auto clip_segment = [&](document::RasterEraserPoint first,
                                  document::RasterEraserPoint last) {
        const double dx = last.x - first.x;
        const double dy = last.y - first.y;
        double t0 = 0.0;
        double t1 = 1.0;
        const std::array<double, 4> p{-dx, dx, -dy, dy};
        const std::array<double, 4> q{first.x, 1.0 - first.x,
                                      first.y, 1.0 - first.y};
        for (size_t edge = 0; edge < p.size(); ++edge) {
            if (std::abs(p[edge]) <= 1e-15) {
                if (q[edge] < 0.0) return false;
                continue;
            }
            const double ratio = q[edge] / p[edge];
            if (p[edge] < 0.0) t0 = std::max(t0, ratio);
            else t1 = std::min(t1, ratio);
            if (t0 > t1) return false;
        }
        append({first.x + t0 * dx, first.y + t0 * dy});
        append({first.x + t1 * dx, first.y + t1 * dy});
        return true;
    };
    if (points_world.size() == 1) {
        const auto point = map_normalized(points_world.front());
        if (point && point->x >= 0.0 && point->x <= 1.0 &&
            point->y >= 0.0 && point->y <= 1.0)
            append(*point);
    }
    for (size_t index = 1; index < points_world.size(); ++index) {
        const auto first = map_normalized(points_world[index - 1]);
        const auto last = map_normalized(points_world[index]);
        if (!first || !last) return false;
        if (!clip_segment(*first, *last)) break;
        if (parameters.points.size() > document::RasterEraserParams::kMaxPoints)
            return false;
        if (last->x < 0.0 || last->x > 1.0 || last->y < 0.0 || last->y > 1.0)
            break;
    }
    if (parameters.points.empty()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nét tẩy nằm ngoài ảnh; tài liệu giữ nguyên"
            : "Eraser stroke did not intersect the image; document unchanged";
        return false;
    }
    const auto id = next_raster_node_id(image->edit_graph);
    if (!id.valid()) return false;
    const auto result = root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            *selected, document::RasterEditNode{id, true, std::move(parameters)}));
    if (!result.success) {
        status_msg_ = (language_ == UILanguage::Vietnamese
            ? "Không thể thêm nét tẩy ảnh: " : "Could not add raster erase stroke: ") +
            result.error_message;
        return false;
    }
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đã tẩy alpha không phá hủy · ảnh gốc được giữ · Ctrl+Z để hoàn tác"
        : "Applied non-destructive alpha erase · source pixels remain unchanged · Ctrl+Z to undo";
    return true;
}

}  // namespace pikdraw::desktop
