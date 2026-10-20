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
#include <span>
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

std::array<uint8_t, 4> sample_image_linear(
    std::span<const uint32_t> pixels, int32_t width, int32_t height,
    double u, double v) {
  const double source_x = std::clamp(u * width - 0.5, 0.0,
                                     static_cast<double>(width - 1));
  const double source_y = std::clamp(v * height - 0.5, 0.0,
                                     static_cast<double>(height - 1));
  const int32_t x0 = static_cast<int32_t>(std::floor(source_x));
  const int32_t y0 = static_cast<int32_t>(std::floor(source_y));
  const int32_t x1 = std::min(x0 + 1, width - 1);
  const int32_t y1 = std::min(y0 + 1, height - 1);
  const double tx = source_x - x0;
  const double ty = source_y - y0;
  const std::array<double, 4> weights{
      (1.0 - tx) * (1.0 - ty), tx * (1.0 - ty),
      (1.0 - tx) * ty, tx * ty};
  const std::array<uint32_t, 4> samples{
      pixels[static_cast<size_t>(y0) * width + x0],
      pixels[static_cast<size_t>(y0) * width + x1],
      pixels[static_cast<size_t>(y1) * width + x0],
      pixels[static_cast<size_t>(y1) * width + x1]};
  constexpr std::array<unsigned, 3> shifts{24, 16, 8};
  double alpha = 0.0;
  std::array<double, 3> premultiplied{};
  for (size_t sample = 0; sample < samples.size(); ++sample) {
    const double sample_alpha = static_cast<double>(samples[sample] & 0xffu) / 255.0;
    const double weight = weights[sample];
    alpha += weight * sample_alpha;
    for (size_t channel = 0; channel < premultiplied.size(); ++channel) {
      const double value = static_cast<double>(
          (samples[sample] >> shifts[channel]) & 0xffu) / 255.0;
      premultiplied[channel] += weight * value * sample_alpha;
    }
  }
  const auto to_byte = [](double value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0), 0l, 255l));
  };
  std::array<uint8_t, 4> result{};
  result[3] = to_byte(alpha);
  for (size_t channel = 0; channel < premultiplied.size(); ++channel)
    result[channel] = alpha > 0.0 ? to_byte(premultiplied[channel] / alpha) : 0;
  return result;
}

bool render_image(const document::ImageObject &image,
                  const Camera2D &camera, PixelBufferTarget &target) {
  const auto pixels = image.rgba_pixels();
  if (image.width_px <= 0 || image.height_px <= 0 ||
      static_cast<uint64_t>(image.width_px) *
              static_cast<uint64_t>(image.height_px) !=
          pixels.size() ||
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
      const auto sample = sample_image_linear(
          pixels, image.width_px, image.height_px, u, v);
      target.blend_pixel(x, y, sample[0], sample[1], sample[2], sample[3]);
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
          if (object.is_clipping_container()) {
            const auto *mask_child = snapshot.objects().lookup_ref(object.clip->mask_object_id);
            if (mask_child && std::holds_alternative<document::PathObject>(*mask_child)) {
              const auto &mask_obj = std::get<document::PathObject>(*mask_child);
              const auto mask_effective_transform = child_transform * mask_obj.transform;

              if (object.clip->preserve_frame_appearance && mask_obj.has_fill) {
                auto transformed_mask = mask_obj;
                transformed_mask.transform = mask_effective_transform;
                transformed_mask.has_stroke = false;
                size_t g_bytes = 0, s_bytes = 0;
                [[maybe_unused]] const bool rendered_fill = effects::render_path_with_appearance(
                    transformed_mask, transformed_mask.appearance, camera, target, preview,
                    &g_bytes, &s_bytes,
                    (snapshot.page().color_mode_explicit || snapshot.page().raster_dpi_explicit)
                        ? snapshot.raster_dpi()
                        : 0);
                metric.peak_gaussian_blur_scratch_capacity_bytes = std::max(
                    metric.peak_gaussian_blur_scratch_capacity_bytes, g_bytes);
                metric.peak_drop_shadow_scratch_capacity_bytes = std::max(
                    metric.peak_drop_shadow_scratch_capacity_bytes, s_bytes);
              }

              document::PathObject clip_path_obj;
              clip_path_obj.id = mask_obj.id;
              clip_path_obj.path = mask_obj.path;
              clip_path_obj.transform = mask_effective_transform;
              std::vector<raster::Contour> clip_contours;
              const bool has_contours = raster::flatten(clip_path_obj, camera, clip_contours);

              std::vector<uint32_t> intermediate(static_cast<size_t>(target.width) * target.height, 0);
              PixelBufferTarget intermediate_target;
              intermediate_target.bgra_pixels = intermediate.data();
              intermediate_target.width = target.width;
              intermediate_target.height = target.height;
              intermediate_target.stride_bytes = target.width * static_cast<int32_t>(sizeof(uint32_t));

              for (const auto child_id : object.children_ids) {
                if (child_id == object.clip->mask_object_id) continue;
                const auto *child = snapshot.objects().lookup_ref(child_id);
                if (!child || !render_object(snapshot, *child, child_transform,
                                             camera, intermediate_target, metric, preview,
                                             rendered_objects, depth + 1))
                  return false;
              }

              for (int32_t y = 0; y < target.height; ++y) {
                for (int32_t x = 0; x < target.width; ++x) {
                  const uint32_t px = intermediate[static_cast<size_t>(y) * target.width + x];
                  const uint8_t a = static_cast<uint8_t>((px >> 24) & 0xFF);
                  if (a > 0) {
                    const geometry::Point2D screen_point{x + 0.5, y + 0.5};
                    if (!has_contours || point_in_contours(clip_contours, mask_obj.path.winding_rule(), screen_point)) {
                      const uint8_t r = static_cast<uint8_t>((px >> 16) & 0xFF);
                      const uint8_t g = static_cast<uint8_t>((px >> 8) & 0xFF);
                      const uint8_t b = static_cast<uint8_t>(px & 0xFF);
                      target.blend_pixel(x, y, r, g, b, a);
                    }
                  }
                }
              }

              if (object.clip->preserve_frame_appearance && mask_obj.has_stroke && mask_obj.stroke_width_mm > 0.0) {
                auto transformed_mask = mask_obj;
                transformed_mask.transform = mask_effective_transform;
                transformed_mask.has_fill = false;
                size_t g_bytes = 0, s_bytes = 0;
                [[maybe_unused]] const bool rendered_stroke = effects::render_path_with_appearance(
                    transformed_mask, transformed_mask.appearance, camera, target, preview,
                    &g_bytes, &s_bytes,
                    (snapshot.page().color_mode_explicit || snapshot.page().raster_dpi_explicit)
                        ? snapshot.raster_dpi()
                        : 0);
                metric.peak_gaussian_blur_scratch_capacity_bytes = std::max(
                    metric.peak_gaussian_blur_scratch_capacity_bytes, g_bytes);
                metric.peak_drop_shadow_scratch_capacity_bytes = std::max(
                    metric.peak_drop_shadow_scratch_capacity_bytes, s_bytes);
              }

              ++rendered_objects;
              return true;
            }
          }

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
