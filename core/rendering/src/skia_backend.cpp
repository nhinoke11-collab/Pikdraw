#include "pikdraw/rendering/skia_backend.hpp"
#include "pikdraw/rendering/culling.hpp"
#include <chrono>

#include <include/core/SkSurface.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkPath.h>
#include <include/core/SkPaint.h>
#include <include/core/SkColor.h>
#include <include/core/SkImageInfo.h>

namespace pikdraw::rendering {

using Clock = std::chrono::steady_clock;

SkiaRasterBackend::SkiaRasterBackend() = default;
SkiaRasterBackend::~SkiaRasterBackend() = default;

bool SkiaRasterBackend::render_frame(
    const Camera2D& camera,
    const ExperimentalSceneSnapshot& scene,
    const PixelBufferTarget& target,
    FrameMetric& metric
) {
    if (!target.is_valid() || !context_valid_) {
        return false;
    }
    const auto t_start = Clock::now();

    metric.camera_x_mm = camera.center().x;
    metric.camera_y_mm = camera.center().y;
    metric.zoom = camera.zoom();
    metric.total_nodes = scene.nodes.size();

    const auto t_cull_start = Clock::now();
    const auto culling_result = SpatialCuller::cull(scene, camera);
    const auto t_cull_end = Clock::now();
    metric.cpu_cull_us = std::chrono::duration<double, std::micro>(t_cull_end - t_cull_start).count();
    metric.visible_nodes = culling_result.visible_node_indices.size();
    metric.culled_nodes = culling_result.culled_nodes;

    const auto t_raster_start = Clock::now();
    const SkImageInfo image_info = SkImageInfo::MakeN32Premul(target.width, target.height);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(image_info, target.data, target.stride_bytes);
    if (!surface) {
        return false;
    }

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SkColorSetRGB(245, 245, 247));

    for (size_t idx : culling_result.visible_node_indices) {
        const auto& node = scene.nodes[idx];
        // Unsupported content must not silently disappear from a successful frame.
        if (node.type != NodeType::Path || !node.local_transform.is_finite()) return false;
        if (node.path_data.has_stroke &&
            (node.local_transform.a != 1 || node.local_transform.b != 0 ||
             node.local_transform.c != 0 || node.local_transform.d != 1)) return false;
        const auto project = [&](const Point2D& point, Point2F& result) {
            return camera.world_to_screen(node.local_transform.apply(point), result);
        };
        if (node.type == NodeType::Path) {
            SkPath path;
            for (const auto& seg : node.path_data.segments) {
                Point2F rel;
                if (seg.type == SegmentType::Close) { path.close(); continue; }
                if (!project(seg.pt, rel)) return false;
                const float sx = rel.x;
                const float sy = rel.y;
                switch (seg.type) {
                    case SegmentType::MoveTo: path.moveTo(sx, sy); break;
                    case SegmentType::LineTo: path.lineTo(sx, sy); break;
                    case SegmentType::QuadTo: {
                        Point2F c1;
                        if (!project(seg.ctrl1, c1)) return false;
                        path.quadTo(c1.x, c1.y, sx, sy);
                        break;
                    }
                    case SegmentType::CubicTo: {
                        Point2F c1, c2;
                        if (!project(seg.ctrl1, c1) || !project(seg.ctrl2, c2)) return false;
                        path.cubicTo(
                            c1.x, c1.y,
                            c2.x, c2.y,
                            sx, sy
                        );
                        break;
                    }
                    case SegmentType::Close: path.close(); break;
                }
            }
            if (node.path_data.has_fill) {
                SkPaint fill_paint;
                fill_paint.setStyle(SkPaint::kFill_Style);
                fill_paint.setAntiAlias(true);
                fill_paint.setColor(SkColorSetARGB(
                    node.path_data.fill_color.a,
                    node.path_data.fill_color.r,
                    node.path_data.fill_color.g,
                    node.path_data.fill_color.b
                ));
                canvas->drawPath(path, fill_paint);
            }
            if (node.path_data.has_stroke && node.path_data.stroke_width_mm > 0.0) {
                SkPaint stroke_paint;
                stroke_paint.setStyle(SkPaint::kStroke_Style);
                stroke_paint.setAntiAlias(true);
                stroke_paint.setStrokeWidth(static_cast<float>(node.path_data.stroke_width_mm * camera.zoom()));
                stroke_paint.setColor(SkColorSetARGB(
                    node.path_data.stroke_color.a,
                    node.path_data.stroke_color.r,
                    node.path_data.stroke_color.g,
                    node.path_data.stroke_color.b
                ));
                canvas->drawPath(path, stroke_paint);
            }
        }
    }

    const auto t_raster_end = Clock::now();
    metric.cpu_raster_us = std::chrono::duration<double, std::micro>(t_raster_end - t_raster_start).count();
    metric.cpu_total_us = std::chrono::duration<double, std::micro>(t_raster_end - t_start).count();
    return true;
}

DeviceLossClassification SkiaRasterBackend::classify_loss(int error_code) const noexcept {
    (void)error_code;
    return DeviceLossClassification::NotMeasured; // No GPU device/context is implemented.
}

bool SkiaRasterBackend::recover_context() {
    return false; // Recovery requires an actual backend implementation and fault evidence.
}

} // namespace pikdraw::rendering
