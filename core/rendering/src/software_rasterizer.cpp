#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <vector>

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

    // 1. Clear viewport with outer void background (#12141a)
    target.clear(18, 20, 26, 255);

    // 2. Render 40-meter Canvas Workspace (vùng canvas trắng tối đa 40 mét)
    render_canvas_workspace(snapshot.page(), camera, target);

    // 3. Render Page Background (khổ giấy in kèm đổ bóng và viền rõ nét)
    render_page_background(snapshot.page(), camera, target);

    // 4. Spatial Culling against visible camera world bounds
    auto visible_bounds = camera.visible_world_bounds();
    auto culled_objects = SpatialCuller::query_visible(snapshot, visible_bounds);

    // 5. Render visible objects in compositing order
    for (const auto& item : culled_objects) {
        std::visit([&](const auto& obj) {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                render_path_object(obj, camera, target);
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                render_text_object(obj, camera, target);
            } else if constexpr (std::is_same_v<T, document::ImageObject>) {
                render_image_object(obj, camera, target);
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

void SoftwareRasterizer::render_canvas_workspace(
    const document::Page& page,
    const Camera2D& camera,
    PixelBufferTarget& target) {
    constexpr double kMaxCanvasMm = geometry::kMaxCanvasDimensionMm; // 40,000 mm (40m)
    const double half_canvas = kMaxCanvasMm * 0.5; // 20,000 mm
    const double cx = page.width_mm * 0.5;
    const double cy = page.height_mm * 0.5;

    // 40m canvas bounds in world coordinates centered around page
    const double min_x = cx - half_canvas;
    const double max_x = cx + half_canvas;
    const double min_y = cy - half_canvas;
    const double max_y = cy + half_canvas;

    auto p0 = camera.world_to_screen({min_x, max_y});
    auto p1 = camera.world_to_screen({max_x, min_y});

    int32_t x = static_cast<int32_t>(std::floor(p0.x));
    int32_t y = static_cast<int32_t>(std::floor(p0.y));
    int32_t w = static_cast<int32_t>(std::ceil(p1.x - p0.x));
    int32_t h = static_cast<int32_t>(std::ceil(p1.y - p0.y));

    // 1. Fill 40m canvas workspace with pure white (#FFFFFF)
    draw_rect_filled_screen(x, y, w, h, {255, 255, 255, 255}, target);

    // 2. Draw 40m canvas boundary limit outline (slate-500 #64748b)
    color::DeviceRGBA limit_border{100, 116, 139, 255};
    draw_line_screen(x, y, x + w, y, limit_border, target);
    draw_line_screen(x + w, y, x + w, y + h, limit_border, target);
    draw_line_screen(x + w, y + h, x, y + h, limit_border, target);
    draw_line_screen(x, y + h, x, y, limit_border, target);
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

    // 1. Page Drop Shadow (CorelDRAW & Illustrator reference: soft shadow on right & bottom)
    int32_t shadow_offset = std::clamp(static_cast<int32_t>(std::lround(4.0 * camera.zoom())), 3, 16);
    draw_rect_filled_screen(x + shadow_offset, y + shadow_offset, w, h, {203, 213, 225, 255}, target);

    // 2. Page interior (white paper #FFFFFF)
    draw_rect_filled_screen(x, y, w, h, {255, 255, 255, 255}, target);

    // 3. Page border (subtle crisp outline #94a3b8)
    color::DeviceRGBA border_color{148, 163, 184, 255};
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

    // 1. Fill closed polygons with scanline rasterization if has_fill
    if (obj.has_fill) {
        geometry::Point2D current_pt{0, 0};
        geometry::Point2D subpath_start{0, 0};
        std::vector<ScreenPoint> current_poly;

        auto finish_subpath = [&]() {
            if (current_poly.size() >= 3) {
                draw_polygon_filled_screen(current_poly, fill_rgba, target);
            }
            current_poly.clear();
        };

        for (const auto& el : transformed_path.elements()) {
            if (el.verb == geometry::PathVerb::MoveTo) {
                finish_subpath();
                current_pt = el.pt;
                subpath_start = el.pt;
                current_poly.push_back(camera.world_to_screen(el.pt));
            } else if (el.verb == geometry::PathVerb::LineTo) {
                current_poly.push_back(camera.world_to_screen(el.pt));
                current_pt = el.pt;
            } else if (el.verb == geometry::PathVerb::QuadTo) {
                geometry::QuadraticBezier quad{current_pt, el.ctrl1, el.pt};
                for (int i = 1; i <= 4; ++i) {
                    double t = i / 4.0;
                    auto next_pt = quad.point_at(t);
                    current_poly.push_back(camera.world_to_screen(next_pt));
                    current_pt = next_pt;
                }
            } else if (el.verb == geometry::PathVerb::CubicTo) {
                geometry::CubicBezier cubic{current_pt, el.ctrl1, el.ctrl2, el.pt};
                for (int i = 1; i <= 8; ++i) {
                    double t = i / 8.0;
                    auto next_pt = cubic.point_at(t);
                    current_poly.push_back(camera.world_to_screen(next_pt));
                    current_pt = next_pt;
                }
            } else if (el.verb == geometry::PathVerb::Close) {
                current_poly.push_back(camera.world_to_screen(subpath_start));
                finish_subpath();
                current_pt = subpath_start;
            }
        }
        finish_subpath();
    }

    // 2. Stroke outline if has_stroke
    if (obj.has_stroke) {
        geometry::Point2D current_pt{0, 0};
        geometry::Point2D subpath_start{0, 0};
        for (const auto& el : transformed_path.elements()) {
            if (el.verb == geometry::PathVerb::MoveTo) {
                current_pt = el.pt;
                subpath_start = el.pt;
            } else if (el.verb == geometry::PathVerb::LineTo) {
                auto s0 = camera.world_to_screen(current_pt);
                auto s1 = camera.world_to_screen(el.pt);
                draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                 static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                 stroke_rgba, target);
                current_pt = el.pt;
            } else if (el.verb == geometry::PathVerb::QuadTo) {
                geometry::QuadraticBezier quad{current_pt, el.ctrl1, el.pt};
                for (int i = 1; i <= 4; ++i) {
                    double t = i / 4.0;
                    auto next_pt = quad.point_at(t);
                    auto s0 = camera.world_to_screen(current_pt);
                    auto s1 = camera.world_to_screen(next_pt);
                    draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                     static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                     stroke_rgba, target);
                    current_pt = next_pt;
                }
            } else if (el.verb == geometry::PathVerb::CubicTo) {
                geometry::CubicBezier cubic{current_pt, el.ctrl1, el.ctrl2, el.pt};
                for (int i = 1; i <= 8; ++i) {
                    double t = i / 8.0;
                    auto next_pt = cubic.point_at(t);
                    auto s0 = camera.world_to_screen(current_pt);
                    auto s1 = camera.world_to_screen(next_pt);
                    draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                     static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                     stroke_rgba, target);
                    current_pt = next_pt;
                }
            } else if (el.verb == geometry::PathVerb::Close) {
                auto s0 = camera.world_to_screen(current_pt);
                auto s1 = camera.world_to_screen(subpath_start);
                draw_line_screen(static_cast<int32_t>(s0.x), static_cast<int32_t>(s0.y),
                                 static_cast<int32_t>(s1.x), static_cast<int32_t>(s1.y),
                                 stroke_rgba, target);
                current_pt = subpath_start;
            }
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

void SoftwareRasterizer::render_image_object(
    const document::ImageObject& obj,
    const Camera2D& camera,
    PixelBufferTarget& target) {
    if (!obj.visible || obj.width_px <= 0 || obj.height_px <= 0 || obj.pixels_rgba.empty()) {
        return;
    }

    auto b = obj.bounds();
    auto s0 = camera.world_to_screen({b.x, b.y + b.height});
    auto s1 = camera.world_to_screen({b.x + b.width, b.y});

    double min_x = std::min(s0.x, s1.x);
    double max_x = std::max(s0.x, s1.x);
    double min_y = std::min(s0.y, s1.y);
    double max_y = std::max(s0.y, s1.y);

    int32_t x0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(min_x)));
    int32_t y0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(min_y)));
    int32_t x1 = std::min<int32_t>(target.width - 1, static_cast<int32_t>(std::ceil(max_x)));
    int32_t y1 = std::min<int32_t>(target.height - 1, static_cast<int32_t>(std::ceil(max_y)));

    double screen_w = max_x - min_x;
    double screen_h = max_y - min_y;
    if (screen_w <= 0.0 || screen_h <= 0.0) return;

    for (int32_t y = y0; y <= y1; ++y) {
        double v = (y - min_y) / screen_h;
        int32_t src_y = std::clamp<int32_t>(static_cast<int32_t>(v * obj.height_px), 0, obj.height_px - 1);

        for (int32_t x = x0; x <= x1; ++x) {
            double u = (x - min_x) / screen_w;
            int32_t src_x = std::clamp<int32_t>(static_cast<int32_t>(u * obj.width_px), 0, obj.width_px - 1);

            size_t pixel_idx = static_cast<size_t>(src_y) * obj.width_px + src_x;
            if (pixel_idx < obj.pixels_rgba.size()) {
                uint32_t rgba = obj.pixels_rgba[pixel_idx];
                uint8_t r = static_cast<uint8_t>((rgba >> 24) & 0xFF);
                uint8_t g = static_cast<uint8_t>((rgba >> 16) & 0xFF);
                uint8_t b_col = static_cast<uint8_t>((rgba >> 8) & 0xFF);
                uint8_t a = static_cast<uint8_t>(rgba & 0xFF);
                target.set_pixel(x, y, r, g, b_col, a);
            }
        }
    }

    // Border around bitmap asset
    color::DeviceRGBA bdr_col{14, 165, 233, 220};
    draw_line_screen(x0, y0, x1, y0, bdr_col, target);
    draw_line_screen(x1, y0, x1, y1, bdr_col, target);
    draw_line_screen(x1, y1, x0, y1, bdr_col, target);
    draw_line_screen(x0, y1, x0, y0, bdr_col, target);
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

void SoftwareRasterizer::draw_polygon_filled_screen(
    const std::vector<ScreenPoint>& pts,
    color::DeviceRGBA color,
    PixelBufferTarget& target) {
    if (pts.size() < 3) return;

    double min_y = pts[0].y;
    double max_y = pts[0].y;
    for (const auto& p : pts) {
        if (p.y < min_y) min_y = p.y;
        if (p.y > max_y) max_y = p.y;
    }

    int32_t y_start = std::max<int32_t>(0, static_cast<int32_t>(std::floor(min_y)));
    int32_t y_end = std::min<int32_t>(target.height - 1, static_cast<int32_t>(std::ceil(max_y)));
    const size_t n = pts.size();

    std::vector<double> x_inters;
    x_inters.reserve(8);

    for (int32_t y = y_start; y <= y_end; ++y) {
        x_inters.clear();
        double scan_y = y + 0.5;

        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            double y0 = pts[i].y;
            double y1 = pts[j].y;
            if ((y0 <= scan_y && y1 > scan_y) || (y1 <= scan_y && y0 > scan_y)) {
                double t = (scan_y - y0) / (y1 - y0);
                double x_int = pts[i].x + t * (pts[j].x - pts[i].x);
                x_inters.push_back(x_int);
            }
        }

        if (x_inters.size() >= 2) {
            std::sort(x_inters.begin(), x_inters.end());
            for (size_t k = 0; k + 1 < x_inters.size(); k += 2) {
                int32_t x0 = std::max<int32_t>(0, static_cast<int32_t>(std::ceil(x_inters[k])));
                int32_t x1 = std::min<int32_t>(target.width - 1, static_cast<int32_t>(std::floor(x_inters[k + 1])));
                for (int32_t x = x0; x <= x1; ++x) {
                    target.set_pixel(x, y, color.r, color.g, color.b, color.a);
                }
            }
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
