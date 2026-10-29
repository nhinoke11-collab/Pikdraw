#include "powerclip_view.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_on_path.hpp"
#include "effects/object_compositor.hpp"
#include "raster/path_raster.hpp"
#include "pdf_source_preview.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <new>
#include <span>
#include <string>
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
                  const Camera2D &camera, PixelBufferTarget &target,
                  const geometry::Transform2D &parent_transform = geometry::Transform2D::identity()) {
  const auto pixels = image.rgba_pixels();
  if (image.width_px <= 0 || image.height_px <= 0 ||
      static_cast<uint64_t>(image.width_px) *
              static_cast<uint64_t>(image.height_px) !=
          pixels.size() ||
      image.bounds_rect.width <= 0.0 || image.bounds_rect.height <= 0.0)
    return false;
  const auto effective_transform = parent_transform * image.transform;
  const auto inverse = effective_transform.inverted();
  if (!inverse)
    return false;

  const auto world_bounds = effective_transform.map_rect(image.bounds_rect);
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
    clip.transform = effective_transform;
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

struct ClipScratch {
  size_t live{0};
  static constexpr size_t budget=64u*1024u*1024u;
};
struct ClipScratchLease {
  ClipScratch* scratch; size_t bytes;
  ~ClipScratchLease(){if(scratch)scratch->live-=bytes;}
};
bool render_object(const document::DocumentSnapshot &snapshot,
                   const document::ObjectVariant &variant,
                   const geometry::Transform2D &parent_transform,
                   const Camera2D &camera, PixelBufferTarget &target,
                   RenderMetric &metric, const color::IccPreview *preview,
                   size_t &rendered_objects, size_t depth = 0,
                   document::ObjectId excluded = 0, ClipScratch* scratch = nullptr,
                   const IRenderBackend::RasterImageResolver* raster_resolver = nullptr,
                   bool exclude_derived_annotations = false) {
  if (depth > 256)
    return false;
  return std::visit(
      [&](const auto &object) -> bool {
        using T = std::decay_t<decltype(object)>;
        if (!object.visible || (excluded && object.id == excluded))
          return true;
        if constexpr (std::is_same_v<T, document::PathObject>) {
          if (exclude_derived_annotations &&
              (object.contour_spec.starts_with("annotation:v1") ||
               object.contour_spec.starts_with("detail_view:v1")))
            return true;
          auto transformed = object;
          transformed.transform = parent_transform * object.transform;
          size_t gaussian_scratch_capacity_bytes = 0;
          size_t shadow_scratch_capacity_bytes = 0;
          std::string effect_failure;
          const bool rendered = effects::render_path_with_appearance(
              transformed, transformed.appearance, camera, target, preview,
              &gaussian_scratch_capacity_bytes,
              &shadow_scratch_capacity_bytes,
              (snapshot.page().color_mode_explicit || snapshot.page().raster_dpi_explicit) ? snapshot.raster_dpi() : 0,
              &effect_failure);
          metric.peak_gaussian_blur_scratch_capacity_bytes = std::max(
              metric.peak_gaussian_blur_scratch_capacity_bytes,
              gaussian_scratch_capacity_bytes);
          metric.peak_drop_shadow_scratch_capacity_bytes = std::max(
              metric.peak_drop_shadow_scratch_capacity_bytes,
              shadow_scratch_capacity_bytes);
          if (rendered)
            ++rendered_objects;
          else if (!effect_failure.empty())
            diagnostic(metric, object.id, true, std::move(effect_failure));
          if (!rendered) return false;

          if (object.contour_spec.starts_with("detail_view:v1")) {
            const auto detail = document::deserialize_detail_view_spec(object.contour_spec);
            if (!detail) {
              diagnostic(metric, object.id, true, "Invalid detail view source metadata");
              return false;
            }
            if (detail->source_page_id != snapshot.active_page_id()) {
              diagnostic(metric, object.id, true, "Detail view source page is not active");
              return false;
            }
            const auto layout = document::evaluate_detail_view_layout(*detail);
            const auto inverse_object = transformed.transform.inverted();
            const auto inverse_source = layout.source_to_frame_transform.inverted();
            if (!inverse_object || !inverse_source) {
              diagnostic(metric, object.id, true, "Detail view transform is not invertible");
              return false;
            }
            const geometry::Point2D source_center{
                detail->source_region.center().x, detail->source_region.center().y};
            const auto origin = transformed.transform.map_point(source_center);
            const auto x_axis = transformed.transform.map_point(
                {source_center.x + 1.0, source_center.y});
            const auto y_axis = transformed.transform.map_point(
                {source_center.x, source_center.y + 1.0});
            const double object_scale = std::sqrt(
                std::hypot(x_axis.x - origin.x, x_axis.y - origin.y) *
                std::hypot(y_axis.x - origin.x, y_axis.y - origin.y));
            const double source_zoom = camera.zoom() * detail->scale_ratio * object_scale;
            const double width_px = std::ceil(detail->source_region.width * source_zoom);
            const double height_px = std::ceil(detail->source_region.height * source_zoom);
            if (!std::isfinite(source_zoom) || source_zoom < Camera2D::kMinZoom ||
                source_zoom > Camera2D::kMaxZoom || width_px < 1.0 || height_px < 1.0 ||
                width_px > 16384.0 || height_px > 16384.0) {
              diagnostic(metric, object.id, true, "Detail view scratch dimensions exceed renderer limits");
              return false;
            }
            const int32_t sw = static_cast<int32_t>(width_px);
            const int32_t sh = static_cast<int32_t>(height_px);
            const uint64_t bytes64 = static_cast<uint64_t>(sw) * sh * 4u;
            if (!scratch || bytes64 > ClipScratch::budget - scratch->live ||
                bytes64 > std::numeric_limits<size_t>::max()) {
              diagnostic(metric, object.id, true, "Detail view source scratch budget exceeded");
              return false;
            }
            const auto bytes = static_cast<size_t>(bytes64);
            scratch->live += bytes;
            ClipScratchLease source_lease{scratch, bytes};
            std::vector<uint8_t> source_pixels;
            try { source_pixels.resize(bytes); }
            catch (const std::bad_alloc&) {
              diagnostic(metric, object.id, true, "Detail view source scratch allocation failed");
              return false;
            }
            PixelBufferTarget source_target{source_pixels.data(), sw, sh, sw * 4};
            source_target.clear(0, 0, 0, 0);
            const Camera2D source_camera(source_center, source_zoom, sw, sh);
            const auto source_refs = SpatialCuller::query_visible_refs(
                snapshot, detail->source_region, source_zoom, 1.0 / source_zoom);
            for (const auto& source_ref : source_refs) {
              if (!source_ref.object || source_ref.id == object.id) continue;
              if (!render_object(snapshot, *source_ref.object,
                    geometry::Transform2D::identity(), source_camera, source_target,
                    metric, preview, rendered_objects, depth + 1, object.id,
                    scratch, raster_resolver, true)) {
                diagnostic(metric, object.id, true, "Detail view source object failed to render");
                return false;
              }
            }

            const auto frame_bounds = transformed.transform.map_rect(detail->frame_rect);
            const auto a = camera.world_to_screen({frame_bounds.x, frame_bounds.y});
            const auto b = camera.world_to_screen({frame_bounds.right(), frame_bounds.bottom()});
            const int32_t x0 = static_cast<int32_t>(std::clamp(
                std::floor(static_cast<double>(std::min(a.x, b.x))), 0.0,
                static_cast<double>(target.width)));
            const int32_t y0 = static_cast<int32_t>(std::clamp(
                std::floor(static_cast<double>(std::min(a.y, b.y))), 0.0,
                static_cast<double>(target.height)));
            const int32_t x1 = static_cast<int32_t>(std::clamp(
                std::ceil(static_cast<double>(std::max(a.x, b.x))), 0.0,
                static_cast<double>(target.width)));
            const int32_t y1 = static_cast<int32_t>(std::clamp(
                std::ceil(static_cast<double>(std::max(a.y, b.y))), 0.0,
                static_cast<double>(target.height)));
            const double radius = std::min(detail->frame_rect.width,
                                           detail->frame_rect.height) * 0.5;
            const auto frame_center = detail->frame_rect.center();
            for (int32_t y = y0; y < y1; ++y) {
              for (int32_t x = x0; x < x1; ++x) {
                const auto local = inverse_object->map_point(
                    camera.screen_to_world({x + 0.5f, y + 0.5f}));
                bool inside = false;
                if (detail->shape == document::DetailViewShape::Circle) {
                  const double dx = local.x - frame_center.x;
                  const double dy = local.y - frame_center.y;
                  inside = dx * dx + dy * dy <= radius * radius;
                } else {
                  inside = detail->frame_rect.contains(local);
                }
                if (!inside) continue;
                const auto source_point = inverse_source->map_point(local);
                const auto sample = source_camera.world_to_screen(source_point);
                const int32_t sx = static_cast<int32_t>(std::floor(sample.x));
                const int32_t sy = static_cast<int32_t>(std::floor(sample.y));
                if (sx < 0 || sx >= sw || sy < 0 || sy >= sh) continue;
                const auto* pixel = source_target.byte_pixel(sx, sy);
                if (pixel[3]) target.blend_pixel(x, y,
                    pixel[source_target.channel_index(0)], pixel[1],
                    pixel[source_target.channel_index(2)], pixel[3]);
              }
            }
            document::PathObject frame_outline;
            frame_outline.id = object.id;
            frame_outline.path = layout.frame_path;
            frame_outline.transform = transformed.transform;
            frame_outline.stroke = object.stroke;
            frame_outline.stroke_width_mm = object.stroke_width_mm;
            frame_outline.has_stroke = true;
            frame_outline.has_fill = false;
            if (!raster::paint(frame_outline, camera, target)) {
              diagnostic(metric, object.id, true, "Detail view frame outline could not be drawn");
              return false;
            }
          }

          const auto render_display_text = [&](std::string text_value,
                                               std::string font_family,
                                               double size_mm,
                                               geometry::Point2D origin,
                                               color::ColorValue fill) {
            if (text_value.empty()) return true;
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
            document::TextObject display_text;
            display_text.id = object.id;
            display_text.name = object.name + " display text";
            display_text.text = std::move(text_value);
            display_text.font = typography::FontIdentity::make_default();
            display_text.font.family_name = std::move(font_family);
            display_text.font.size_pt = size_mm * (72.0 / 25.4);
            display_text.fill = std::move(fill);
            display_text.position = origin;
            display_text.transform = object.transform;
            document::ObjectVariant text_variant{std::move(display_text)};
            size_t text_object_count = 0;
            return render_object(snapshot, text_variant, parent_transform, camera,
                                 target, metric, preview, text_object_count,
                                 depth + 1, excluded, scratch, raster_resolver,
                                 exclude_derived_annotations);
#else
            diagnostic(metric, object.id, true,
                       "Annotation text rendering requires the native font outline backend");
            return false;
#endif
          };

          if (object.contour_spec.starts_with("annotation:v1")) {
            const auto annotation = document::deserialize_annotation_spec(
                object.contour_spec);
            if (!annotation) {
              diagnostic(metric, object.id, true,
                         "Invalid annotation metadata; annotation text omitted");
              return false;
            }
            const auto layout = document::evaluate_annotation_layout(*annotation);
            if (!layout.arrow_path.empty()) {
              document::PathObject arrow;
              arrow.id = object.id;
              arrow.path = layout.arrow_path;
              arrow.transform = parent_transform * object.transform;
              arrow.stroke = object.stroke;
              arrow.fill = object.stroke;
              arrow.stroke_width_mm = object.stroke_width_mm;
              const bool filled_arrow =
                  annotation->style.arrow_type == geometry::ArrowType::ClosedFilled ||
                  annotation->style.arrow_type == geometry::ArrowType::Dot;
              arrow.has_stroke = !filled_arrow;
              arrow.has_fill = filled_arrow;
              if (!raster::paint(arrow, camera, target)) {
                diagnostic(metric, object.id, true,
                           "Annotation arrow could not be rasterized");
                return false;
              }
            }
            if (!render_display_text(layout.formatted_text,
                                     annotation->style.font_family,
                                     annotation->style.text_size_mm,
                                     layout.text_origin,
                                     object.stroke))
              return false;
          } else if (object.contour_spec.starts_with("detail_view:v1")) {
            const auto detail_view = document::deserialize_detail_view_spec(
                object.contour_spec);
            if (!detail_view) {
              diagnostic(metric, object.id, true,
                         "Invalid detail view metadata; label omitted");
              return false;
            }
            const auto layout = document::evaluate_detail_view_layout(*detail_view);
            if (!render_display_text(layout.formatted_label, "Segoe UI",
                                     detail_view->text_size_mm,
                                     layout.label_origin, object.stroke))
              return false;
          }
          return true;
        } else if constexpr (std::is_same_v<T, document::TextObject>) {
      // Enabled only when a genuine platform outline backend is compiled in.
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
          try {
            document::PathObject outlines;
            if (object.is_text_on_path()) {
              auto path_opt = snapshot.objects().get(object.path_binding_id);
              if (path_opt && std::holds_alternative<document::PathObject>(*path_opt)) {
                const auto& host_path = std::get<document::PathObject>(*path_opt);
                typography::TextOnPathOptions top_opts;
                top_opts.start_offset_mm = object.path_offset_mm;
                top_opts.baseline_offset_mm = object.path_baseline_offset_mm;
                top_opts.reversed = object.path_reversed;
                top_opts.alignment = object.alignment;
                auto top_res = typography::layout_text_on_path(host_path.path, object.text, object.font, top_opts);
                outlines.path = std::move(top_res.combined_outlines);
                outlines.transform = (parent_transform * host_path.transform) * geometry::Transform2D::scale(1, -1);
              }
            }
            if (outlines.path.empty()) {
              const auto layout = object.layout();
              for (const auto &line : layout.lines()) {
                if (line.run.is_fallback) {
                  diagnostic(metric, object.id, false,
                             line.run.warning_message +
                                 " Resolved face: " + line.run.font.family_name);
                }
              }
              outlines.path = typography::text_to_path_outlines(layout, object.underline, object.strikethrough);
              outlines.transform = (parent_transform * object.transform) *
                                   geometry::Transform2D::translation(
                                       object.position.x, object.position.y) *
                                   geometry::Transform2D::scale(1, -1);
            }
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
          const auto resolved = raster_resolver && *raster_resolver
              ? (*raster_resolver)(snapshot, object) : nullptr;
          const auto& renderable = resolved ? *resolved : object;
          if (!render_image(renderable, camera, target, parent_transform))
            return false;
          ++rendered_objects;
          return true;
        } else {
          const auto child_transform = parent_transform * object.transform;
          if (object.is_clipping_container()) {
            const auto *mask_child = snapshot.objects().lookup_ref(object.clip->mask_object_id);
            const auto mask_geometry = mask_child ? document::frame_geometry(*mask_child) : std::nullopt;
            if (mask_geometry) {
              const auto &mask_obj = *mask_geometry;
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
                if (!rendered_fill) return false;
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
              if (!has_contours) return false;

              const auto world_bounds=mask_effective_transform.map_rect(mask_obj.path.bounds());
              const auto tl=camera.world_to_screen({world_bounds.x,world_bounds.bottom()});
              const auto br=camera.world_to_screen({world_bounds.right(),world_bounds.y});
              const int32_t x0=int32_t(std::clamp(std::floor(tl.x)-2.,0.,double(target.width)));
              const int32_t y0=int32_t(std::clamp(std::floor(tl.y)-2.,0.,double(target.height)));
              const int32_t x1=int32_t(std::clamp(std::ceil(br.x)+2.,0.,double(target.width)));
              const int32_t y1=int32_t(std::clamp(std::ceil(br.y)+2.,0.,double(target.height)));
              const auto width=x1-x0,height=y1-y0;
              if(width>0&&height>0) {
                const size_t bytes=size_t(width)*size_t(height)*4;
                if(!scratch||bytes>ClipScratch::budget-scratch->live) {
                  diagnostic(metric,object.id,true,"PowerClip scratch budget exceeded; frame incomplete");return false;
                }
                scratch->live+=bytes;ClipScratchLease lease{scratch,bytes};
                std::vector<uint32_t> intermediate(size_t(width)*size_t(height),0);
                PixelBufferTarget intermediate_target;
                intermediate_target.bgra_pixels=intermediate.data();intermediate_target.width=width;
                intermediate_target.height=height;intermediate_target.stride_bytes=width*4;
                Camera2D local_camera{camera.screen_to_world({float(x0+width*.5),float(y0+height*.5)}),camera.zoom(),width,height};
                for(auto child_id:object.children_ids) {
                  if(child_id==object.clip->mask_object_id)continue;
                  const auto* child=snapshot.objects().lookup_ref(child_id);
                  if(!child||!render_object(snapshot,*child,child_transform,local_camera,intermediate_target,
                      metric,preview,rendered_objects,depth+1,excluded,scratch,
                      raster_resolver,exclude_derived_annotations))return false;
                }
                for(int32_t y=0;y<height;++y)for(int32_t x=0;x<width;++x) {
                  const uint32_t px=intermediate[size_t(y)*width+x];const auto alpha=uint8_t(px>>24);
                  if(alpha && has_contours && point_in_contours(clip_contours,mask_obj.path.winding_rule(),{x+x0+.5,y+y0+.5}))
                    target.blend_pixel(x+x0,y+y0,uint8_t(px>>16),uint8_t(px>>8),uint8_t(px),alpha);
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
                if (!rendered_stroke) return false;
                metric.peak_gaussian_blur_scratch_capacity_bytes = std::max(
                    metric.peak_gaussian_blur_scratch_capacity_bytes, g_bytes);
                metric.peak_drop_shadow_scratch_capacity_bytes = std::max(
                    metric.peak_drop_shadow_scratch_capacity_bytes, s_bytes);
              }

              ++rendered_objects;
              return true;
            }
          }

          if (object.is_clipping_container()) return false;
          for (const auto child_id : object.children_ids) {
            const auto *child = snapshot.objects().lookup_ref(child_id);
            if (!child || !render_object(snapshot, *child, child_transform,
                                         camera, target, metric, preview,
                                         rendered_objects, depth + 1, excluded, scratch,
                                         raster_resolver, exclude_derived_annotations))
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
  if (render_page_background_ && !render_page(snapshot.page(), camera, target))
    return fail("Invalid/unsupported page projection");
  if (auto source_preview = detail::make_pdf_source_preview(snapshot.active_page())) {
    if (!render_image(*source_preview, camera, target))
      return fail("Locked PDF source preview could not be rendered");
  }
  // Match the compositor's rounded local filter support. Keep one extra world
  // pixel for target pixel-center sampling after the affine bounds mapping.
  const double culling_margin_world_mm = 1.0 / camera.zoom();
  const auto visible = spatial_index_.query_visible_refs(
      snapshot, camera.visible_world_bounds(), camera.zoom(),
      culling_margin_world_mm);
  bool success = true;
  size_t rendered_count = 0;
  ClipScratch clip_scratch;
  for (const auto &item : visible) {
    if (render_object(snapshot, *item.object,
                      geometry::Transform2D::identity(), camera, target,
                      metric, preview_.get(), rendered_count, 0, view_context_.owner,
                      &clip_scratch, &raster_image_resolver_))
      metric.rendered_objects = rendered_count;
    else {
      const bool has_specific_diagnostic = std::any_of(
          metric.diagnostics.begin(), metric.diagnostics.end(),
          [id = item.id](const RenderDiagnostic &entry) {
            return entry.error && entry.object_id == id;
          });
      if (!has_specific_diagnostic)
        diagnostic(metric, item.id, true,
                   "Object cannot be rendered: invalid/unsupported data or CPU "
                   "work budget exceeded");
      success = false;
    }
  }
  if (view_context_.owner) {
    const auto* entry = snapshot.hierarchy().find(view_context_.owner);
    const auto* object = snapshot.objects().lookup_ref(view_context_.owner);
    const auto* group = object ? std::get_if<document::GroupObject>(object) : nullptr;
    if (!entry || !group) return fail("Stale edit view owner");
    for (int32_t y = 0; y < target.height; ++y)
      for (int32_t x = 0; x < target.width; ++x) target.blend_pixel(x, y, 240, 240, 240, 170);
    if (!view_context_.reveal_contents || view_context_.edit_frame) {
      success = render_object(snapshot, *object, entry->parent_world, camera, target,
          metric, preview_.get(), rendered_count, 0, 0, &clip_scratch,
          &raster_image_resolver_) && success;
    } else {
      for (auto id : group->children_ids) {
        if (group->clip && id == group->clip->mask_object_id) continue;
        const auto* child = snapshot.objects().lookup_ref(id);
        if (!child || !render_object(snapshot, *child, entry->world, camera, target,
              metric, preview_.get(), rendered_count, 0, 0, &clip_scratch,
              &raster_image_resolver_)) success = false;
      }
    }
    if (auto outline = detail::clip_frame_outline(snapshot, view_context_.owner, camera))
      success = render_object(snapshot, document::ObjectVariant{*outline}, {}, camera, target,
          metric, nullptr, rendered_count, 0, 0, &clip_scratch,
          &raster_image_resolver_) && success;
  }
  metric.rendered_objects = rendered_count;
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
