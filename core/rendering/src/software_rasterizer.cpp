#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "effects/object_compositor.hpp"
#include "raster/path_raster.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <unordered_set>

namespace pikdraw::rendering {
namespace {
constexpr uint64_t kMaxTargetPixels = 33554432ULL;
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

bool point_in_contours(const std::vector<raster::Contour> &contours,
                       geometry::WindingRule rule,
                       const geometry::Point2D &point) {
  int winding = 0;
  for (const auto &contour : contours) {
    const auto &points = contour.screen;
    if (points.size() < 2)
      continue;
    for (size_t i = 0; i < points.size(); ++i) {
      const auto &a = points[i];
      const auto &b = points[(i + 1) % points.size()];
      if (a.y <= point.y && b.y > point.y) {
        const double cross = (b.x - a.x) * (point.y - a.y) -
                             (point.x - a.x) * (b.y - a.y);
        if (cross > 0.0)
          ++winding;
      } else if (a.y > point.y && b.y <= point.y) {
        const double cross = (b.x - a.x) * (point.y - a.y) -
                             (point.x - a.x) * (b.y - a.y);
        if (cross < 0.0)
          --winding;
      }
    }
  }
  return rule == geometry::WindingRule::EvenOdd ? (std::abs(winding) % 2) != 0
                                                 : winding != 0;
}

bool render_image(const document::ImageObject &image,
                  const Camera2D &camera, PixelBufferTarget &target) {
  if (image.width_px <= 0 || image.height_px <= 0 ||
      static_cast<uint64_t>(image.width_px) *
              static_cast<uint64_t>(image.height_px) !=
          image.pixels_rgba.size() ||
      image.bounds_rect.width <= 0.0 || image.bounds_rect.height <= 0.0)
    return false;
  const auto inverse = image.transform.inverted();
  if (!inverse)
    return false;

  const auto world_bounds = image.transform.map_rect(image.bounds_rect);
  const std::array<geometry::Point2D, 4> corners{{
      {world_bounds.x, world_bounds.y},
      {world_bounds.right(), world_bounds.y},
      {world_bounds.right(), world_bounds.bottom()},
      {world_bounds.x, world_bounds.bottom()},
  }};
  double min_screen_x = std::numeric_limits<double>::infinity();
  double min_screen_y = std::numeric_limits<double>::infinity();
  double max_screen_x = -std::numeric_limits<double>::infinity();
  double max_screen_y = -std::numeric_limits<double>::infinity();
  for (const auto &corner : corners) {
    const auto screen = camera.world_to_screen(corner);
    min_screen_x = std::min(min_screen_x, static_cast<double>(screen.x));
    min_screen_y = std::min(min_screen_y, static_cast<double>(screen.y));
    max_screen_x = std::max(max_screen_x, static_cast<double>(screen.x));
    max_screen_y = std::max(max_screen_y, static_cast<double>(screen.y));
  }
  if (!std::isfinite(min_screen_x) || !std::isfinite(min_screen_y) ||
      !std::isfinite(max_screen_x) || !std::isfinite(max_screen_y))
    return false;

  const int first_x = static_cast<int>(std::clamp(
      std::floor(min_screen_x), 0.0, static_cast<double>(target.width)));
  const int end_x = static_cast<int>(std::clamp(
      std::ceil(max_screen_x), 0.0, static_cast<double>(target.width)));
  const int first_y = static_cast<int>(std::clamp(
      std::floor(min_screen_y), 0.0, static_cast<double>(target.height)));
  const int end_y = static_cast<int>(std::clamp(
      std::ceil(max_screen_y), 0.0, static_cast<double>(target.height)));

  std::vector<raster::Contour> clip_contours;
  if (image.clip_path) {
    document::PathObject clip;
    clip.id = image.id;
    clip.path = *image.clip_path;
    clip.transform = image.transform;
    if (!raster::flatten(clip, camera, clip_contours))
      return false;
  }

  for (int y = first_y; y < end_y; ++y) {
    for (int x = first_x; x < end_x; ++x) {
      const geometry::Point2D screen_point{x + 0.5, y + 0.5};
      if (image.clip_path &&
          !point_in_contours(clip_contours, image.clip_path->winding_rule(),
                             screen_point))
        continue;
      const auto local = inverse->map_point(camera.screen_to_world(
          {static_cast<float>(screen_point.x),
           static_cast<float>(screen_point.y)}));
      if (local.x < image.bounds_rect.x || local.y < image.bounds_rect.y ||
          local.x >= image.bounds_rect.right() ||
          local.y >= image.bounds_rect.bottom())
        continue;
      const double u = (local.x - image.bounds_rect.x) /
                       image.bounds_rect.width;
      const double v = (image.bounds_rect.bottom() - local.y) /
                       image.bounds_rect.height;
      const int source_x = std::clamp(
          static_cast<int>(u * image.width_px), 0, image.width_px - 1);
      const int source_y = std::clamp(
          static_cast<int>(v * image.height_px), 0, image.height_px - 1);
      const uint32_t packed = image.pixels_rgba[
          static_cast<size_t>(source_y) * image.width_px + source_x];
      const auto channel = [packed](unsigned shift) {
        return static_cast<uint8_t>((packed >> shift) & 0xffu);
      };
      target.blend_pixel(x, y, channel(24), channel(16), channel(8),
                         channel(0));
    }
  }
  return true;
}

bool render_object(const document::DocumentSnapshot &snapshot,
                   const document::ObjectVariant &variant,
                   const geometry::Transform2D &parent_transform,
                   const Camera2D &camera, PixelBufferTarget &target,
                   RenderMetric &metric, const color::IccPreview *preview,
                   size_t &rendered_objects, size_t depth = 0) {
  if (depth > 256)
    return false;
  return std::visit(
      [&](const auto &object) -> bool {
        using T = std::decay_t<decltype(object)>;
        if (!object.visible)
          return true;
        if constexpr (std::is_same_v<T, document::PathObject>) {
          auto transformed = object;
          transformed.transform = parent_transform * object.transform;
          size_t gaussian_scratch_capacity_bytes = 0;
          size_t shadow_scratch_capacity_bytes = 0;
          const bool rendered = effects::render_path_with_appearance(
              transformed, transformed.appearance, camera, target, preview,
              &gaussian_scratch_capacity_bytes,
              &shadow_scratch_capacity_bytes,
              (snapshot.page().color_mode_explicit || snapshot.page().raster_dpi_explicit) ? snapshot.raster_dpi() : 0);
          metric.peak_gaussian_blur_scratch_capacity_bytes = std::max(
              metric.peak_gaussian_blur_scratch_capacity_bytes,
              gaussian_scratch_capacity_bytes);
          metric.peak_drop_shadow_scratch_capacity_bytes = std::max(
              metric.peak_drop_shadow_scratch_capacity_bytes,
              shadow_scratch_capacity_bytes);
          if (rendered)
            ++rendered_objects;
          return rendered;
        } else if constexpr (std::is_same_v<T, document::TextObject>) {
      // Enabled only when a genuine platform outline backend is compiled in.
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
          try {
            const auto layout = object.layout();
            for (const auto &line : layout.lines()) {
              if (line.run.is_fallback) {
                diagnostic(metric, object.id, false,
                           line.run.warning_message +
                               " Resolved face: " + line.run.font.family_name);
              }
            }
            document::PathObject outlines;
            outlines.path = typography::text_to_path_outlines(layout, object.underline);
            outlines.transform = (parent_transform * object.transform) *
                                 geometry::Transform2D::translation(
                                     object.position.x, object.position.y) *
                                 geometry::Transform2D::scale(1, -1);
            outlines.fill = object.fill;
            outlines.has_stroke = false;
            size_t gaussian_scratch_capacity_bytes = 0;
            size_t shadow_scratch_capacity_bytes = 0;
            const bool rendered = effects::render_path_with_appearance(
                outlines, object.appearance, camera, target, preview,
                &gaussian_scratch_capacity_bytes,
                &shadow_scratch_capacity_bytes,
                (snapshot.page().color_mode_explicit || snapshot.page().raster_dpi_explicit) ? snapshot.raster_dpi() : 0);
            metric.peak_gaussian_blur_scratch_capacity_bytes = std::max(
                metric.peak_gaussian_blur_scratch_capacity_bytes,
                gaussian_scratch_capacity_bytes);
            metric.peak_drop_shadow_scratch_capacity_bytes = std::max(
                metric.peak_drop_shadow_scratch_capacity_bytes,
                shadow_scratch_capacity_bytes);
            if (rendered)
              ++rendered_objects;
            return rendered;
          } catch (const std::exception &) {
            return false;
          }
#else
          if (object.text.empty()) {
            ++rendered_objects;
            return true;
          }
          return false;
#endif
        } else if constexpr (std::is_same_v<T, document::ImageObject>) {
          auto transformed = object;
          transformed.transform = parent_transform * object.transform;
          if (!render_image(transformed, camera, target))
            return false;
          ++rendered_objects;
          return true;
        } else {
          const auto child_transform = parent_transform * object.transform;
          for (const auto child_id : object.children_ids) {
            const auto *child = snapshot.objects().lookup_ref(child_id);
            if (!child || !render_object(snapshot, *child, child_transform,
                                         camera, target, metric, preview,
                                         rendered_objects, depth + 1))
              return false;
          }
          ++rendered_objects;
          return true;
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
  // Match the compositor's rounded local filter support. Keep one extra world
  // pixel for target pixel-center sampling after the affine bounds mapping.
  const double culling_margin_world_mm = 1.0 / camera.zoom();
  const auto visible = SpatialCuller::query_visible_refs(
      snapshot, camera.visible_world_bounds(), camera.zoom(),
      culling_margin_world_mm);
  bool success = true;
  size_t rendered_count = 0;
  for (const auto &item : visible) {
    if (render_object(snapshot, *item.object,
                      geometry::Transform2D::identity(), camera, target,
                      metric, preview_.get(), rendered_count))
      metric.rendered_objects = rendered_count;
    else {
      diagnostic(metric, item.id, true,
                 "Object cannot be rendered: invalid/unsupported data or CPU "
                 "work budget exceeded");
      success = false;
    }
  }
  metric.culled_objects = snapshot.object_count() > rendered_count
                              ? snapshot.object_count() - rendered_count
                              : 0;
  metric.render_duration =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start);
  if (out_metric)
    *out_metric = metric;
  return success;
}

} // namespace pikdraw::rendering
