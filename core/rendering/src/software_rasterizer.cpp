#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include <chrono>
#include <cmath>

namespace pikdraw::rendering {

bool SoftwareRasterizer::render(
    const document::DocumentSnapshot& snapshot,
    const Camera2D& camera,
    PixelBufferTarget& target,
    RenderMetric* out_metric) {
    auto start_time = std::chrono::steady_clock::now();

    if (loss_status_ != DeviceLossStatus::Healthy) {
        return false;
    }

    if (!target.is_valid()) {
        return false;
    }

    // 1. Clear viewport with Cyber Dark canvas background (#12141a)
    target.clear(18, 20, 26, 255);

    // 2. Render Page Background (white billboard canvas with border)
    render_page_background(snapshot.page(), camera, target);

    // 3. Spatial Culling against visible camera world bounds
    auto visible_bounds = camera.visible_world_bounds();
    auto culled_objects = SpatialCuller::query_visible(snapshot, visible_bounds);

    // 4. Render visible objects in compositing order
    for (const auto& item : culled_objects) {
        std::visit([&](const auto& obj) {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                render_path_object(obj, camera, target);
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                render_text_object(obj, camera, target);
            }
        }, item.object);
    }

    auto end_time = std::chrono::steady_clock::now();
    if (out_metric) {
        out_metric->snapshot_revision = snapshot.revision();
        out_metric->total_objects = snapshot.object_count();
        out_metric->rendered_objects = culled_objects.size();
        out_metric->culled_objects = snapshot.object_count() - culled_objects.size();
        out_metric->render_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
    }

    return true;
}

void SoftwareRasterizer::render_page_background(
    const document::Page& page,
    const Camera2D& camera,
    PixelBufferTarget& target) {
    auto p0 = camera.world_to_screen({0.0, page.height_mm});
    auto p1 = camera.world_to_screen({page.width_mm, 0.0});

    int32_t x = static_cast<int32_t>(std::floor(p0.x));
    int32_t y = static_cast<int32_t>(std::floor(p0.y));
    int32_t w = static_cast<int32_t>(std::ceil(p1.x - p0.x));
    int32_t h = static_cast<int32_t>(std::ceil(p1.y - p0.y));

    // Page interior (white paper #FFFFFF)
    draw_rect_filled_screen(x, y, w, h, {255, 255, 255, 255}, target);

    // Page border (subtle gray outline #444955)
    color::DeviceRGBA border_color{68, 73, 85, 255};
    draw_line_screen(x, y, x + w, y, border_color, target);
    draw_line_screen(x + w, y, x + w, y + h, border_color, target);
    draw_line_screen(x + w, y + h, x, y + h, border_color, target);
    draw_line_screen(x, y + h, x, y, border_color, target);
}

void SoftwareRasterizer::render_path_object(
    const document::PathObject& obj,
    const Camera2D& camera,
    PixelBufferTarget& target) {
    if (!obj.visible) return;

    auto transformed_path = obj.path.transformed(obj.transform);
    auto stroke_rgba = color::to_preview_rgba(obj.stroke).to_device();
    auto fill_rgba = color::to_preview_rgba(obj.fill).to_device();

    // Flatten to polyline segments
    geometry::Point2D current_pt{0, 0};
    geometry::Point2D subpath_start{0, 0};

    for (const auto& el : transformed_path.elements()) {
        if (el.verb == geometry::PathVerb::MoveTo) {
            current_pt = el.pt;
            subpath_start = el.pt;
        } else if (el.verb == geometry::PathVerb::LineTo) {
            auto s0 = camera.world_to_screen(current_pt);
            auto s1 = camera.world_to_screen(el.pt);
            if (obj.has_stroke) {
                draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                 static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                 stroke_rgba, target);
            }
            current_pt = el.pt;
        } else if (el.verb == geometry::PathVerb::QuadTo) {
            // Sample quadratic curve at 4 intervals
            geometry::QuadraticBezier quad{current_pt, el.ctrl1, el.pt};
            for (int i = 1; i <= 4; ++i) {
                double t = i / 4.0;
                auto next_pt = quad.point_at(t);
                auto s0 = camera.world_to_screen(current_pt);
                auto s1 = camera.world_to_screen(next_pt);
                if (obj.has_stroke) {
                    draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                     static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                     stroke_rgba, target);
                }
                current_pt = next_pt;
            }
        } else if (el.verb == geometry::PathVerb::CubicTo) {
            // Sample cubic curve at 8 intervals
            geometry::CubicBezier cubic{current_pt, el.ctrl1, el.ctrl2, el.pt};
            for (int i = 1; i <= 8; ++i) {
                double t = i / 8.0;
                auto next_pt = cubic.point_at(t);
                auto s0 = camera.world_to_screen(current_pt);
                auto s1 = camera.world_to_screen(next_pt);
                if (obj.has_stroke) {
                    draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                     static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                     stroke_rgba, target);
                }
                current_pt = next_pt;
            }
        } else if (el.verb == geometry::PathVerb::Close) {
            auto s0 = camera.world_to_screen(current_pt);
            auto s1 = camera.world_to_screen(subpath_start);
            if (obj.has_stroke) {
                draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                 static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                 stroke_rgba, target);
            }
            current_pt = subpath_start;
        }
    }
}

void SoftwareRasterizer::render_text_object(
    const document::TextObject& obj,
    const Camera2D& camera,
    PixelBufferTarget& target) {
    if (!obj.visible || obj.text.empty()) return;

    auto b = obj.bounds();
    auto s0 = camera.world_to_screen({b.x, b.y + b.height});
    auto s1 = camera.world_to_screen({b.x + b.width, b.y});

    int32_t x = static_cast<int32_t>(std::floor(s0.x));
    int32_t y = static_cast<int32_t>(std::floor(s0.y));
    int32_t w = static_cast<int32_t>(std::ceil(s1.x - s0.x));
    int32_t h = static_cast<int32_t>(std::ceil(s1.y - s0.y));

    auto fill_rgba = color::to_preview_rgba(obj.fill).to_device();
    draw_rect_filled_screen(x, y, w, h, fill_rgba, target);
}

void SoftwareRasterizer::draw_rect_filled_screen(
    int32_t x, int32_t y, int32_t w, int32_t h,
    color::DeviceRGBA color,
    PixelBufferTarget& target) {
    int32_t x0 = std::clamp(x, 0, target.width);
    int32_t y0 = std::clamp(y, 0, target.height);
    int32_t x1 = std::clamp(x + w, 0, target.width);
    int32_t y1 = std::clamp(y + h, 0, target.height);

    for (int32_t row = y0; row < y1; ++row) {
        for (int32_t col = x0; col < x1; ++col) {
            target.set_pixel(col, row, color.r, color.g, color.b, color.a);
        }
    }
}

// Bresenham's line algorithm
void SoftwareRasterizer::draw_line_screen(
    int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    color::DeviceRGBA color,
    PixelBufferTarget& target) {
    int32_t dx = std::abs(x1 - x0);
    int32_t dy = -std::abs(y1 - y0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    while (true) {
        target.set_pixel(x0, y0, color.r, color.g, color.b, color.a);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

} // namespace pikdraw::rendering
