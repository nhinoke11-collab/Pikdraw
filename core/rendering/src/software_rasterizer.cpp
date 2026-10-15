#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "raster/path_raster.hpp"
#include <chrono>
#include <exception>

namespace pikdraw::rendering {
namespace {
constexpr uint64_t kMaxTargetPixels = 8388608;
void diagnostic(RenderMetric &metric, document::ObjectId id, bool error,
                std::string message) {
  constexpr size_t kMaxDiagnostics = 64;
  if (metric.diagnostics.size() < kMaxDiagnostics) {
    message.resize(std::min(message.size(), size_t{512}));
    metric.diagnostics.push_back({id, error, std::move(message)});
  } else if (metric.diagnostics.size() == kMaxDiagnostics) {
    metric.diagnostics.push_back({document::kInvalidObjectId, error,
                                  "Further render diagnostics omitted"});
  } else if (error) {
    metric.diagnostics.back().error = true;
  }
}
bool render_page(const document::Page &page, const Camera2D &camera,
                 PixelBufferTarget &target) {
  if (!std::isfinite(page.width_mm) || !std::isfinite(page.height_mm) ||
      page.width_mm <= 0 || page.height_mm <= 0)
    return false;
  document::PathObject paper;
  paper.path = geometry::Path2D::make_rect(0, 0, page.width_mm, page.height_mm);
  paper.fill = color::RGBColor{1, 1, 1, 1};
  paper.has_stroke = false;
  return raster::paint(paper, camera, target);
}
bool render_object(const document::ObjectVariant &variant,
                   const Camera2D &camera, PixelBufferTarget &target,
                   [[maybe_unused]] RenderMetric &metric, const color::IccPreview *preview) {
  return std::visit(
      [&](const auto &object) -> bool {
        using T = std::decay_t<decltype(object)>;
        if constexpr (std::is_same_v<T, document::PathObject>) {
          return raster::paint(object, camera, target, preview);
        } else if constexpr (std::is_same_v<T, document::TextObject>) {
      // Enabled only when a genuine platform outline backend is compiled in.
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
          try {
            const auto layout =
                typography::TextLayout::layout_text(object.text, object.font);
            for (const auto &line : layout.lines()) {
              if (line.run.is_fallback) {
                diagnostic(metric, object.id, false,
                           line.run.warning_message +
                               " Resolved face: " + line.run.font.family_name);
              }
            }
            document::PathObject outlines;
            outlines.path = typography::text_to_path_outlines(layout);
            outlines.transform = object.transform *
                                 geometry::Transform2D::translation(
                                     object.position.x, object.position.y) *
                                 geometry::Transform2D::scale(1, -1);
            outlines.fill = object.fill;
            outlines.has_stroke = false;
            return raster::paint(outlines, camera, target, preview);
          } catch (const std::exception &) {
            return false;
          }
#else
          return object.text.empty();
#endif
        } else {
          return false; // Group transforms require hierarchical traversal;
                        // never ignore them.
        }
      },
      variant);
}
} // namespace

bool SoftwareRasterizer::render(const document::DocumentSnapshot &snapshot,
                                const Camera2D &camera,
                                PixelBufferTarget &target,
                                RenderMetric *out_metric) {
  const auto start = std::chrono::steady_clock::now();
  RenderMetric metric;
  metric.snapshot_revision = snapshot.revision();
  metric.total_objects = snapshot.object_count();
  if (out_metric)
    *out_metric = metric;
  const auto fail = [&](const char *message) {
    diagnostic(metric, document::kInvalidObjectId, true, message);
    if (out_metric)
      *out_metric = metric;
    return false;
  };
  if (loss_status_ != DeviceLossStatus::Healthy || !target.is_valid() ||
      target.width > 16384 || target.height > 16384 ||
      static_cast<uint64_t>(target.width) *
              static_cast<uint64_t>(target.height) >
          kMaxTargetPixels)
    return fail("Invalid target, pixel budget exceeded or backend not healthy");
  target.clear(background_.r, background_.g, background_.b, background_.a);
  if (!render_page(snapshot.page(), camera, target))
    return fail("Invalid/unsupported page projection");
  const auto visible = SpatialCuller::query_visible_refs(
      snapshot, camera.visible_world_bounds());
  bool success = true;
  for (const auto &item : visible) {
    if (render_object(*item.object, camera, target, metric, preview_.get()))
      ++metric.rendered_objects;
    else {
      diagnostic(metric, item.id, true,
                 "Object cannot be rendered: invalid/unsupported data or CPU "
                 "work budget exceeded");
      success = false;
    }
  }
  metric.culled_objects = snapshot.object_count() - visible.size();
  metric.render_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start);
  if (out_metric)
    *out_metric = metric;
  return success;
}
} // namespace pikdraw::rendering
