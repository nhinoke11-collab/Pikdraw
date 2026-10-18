#include "object_compositor.hpp"
#include "object_compositor_internal.hpp"
#include "blend_operator.hpp"
#include "drop_shadow.hpp"
#include "gaussian_blur.hpp"
#include "gaussian_blur_simd.hpp"
#include "../raster/path_raster.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace pikdraw::rendering::effects {
namespace {
bool compose_shadow_under_input(FilterPixel &input,
                                const FilterPixel &shadow) noexcept {
  if (!std::isfinite(input.r) || !std::isfinite(input.g) ||
      !std::isfinite(input.b) || !unit_interval(input.a) ||
      !std::isfinite(shadow.r) || !std::isfinite(shadow.g) ||
      !std::isfinite(shadow.b) || !unit_interval(shadow.a) ||
      input.r < 0.0f || input.g < 0.0f || input.b < 0.0f ||
      shadow.r < 0.0f || shadow.g < 0.0f || shadow.b < 0.0f ||
      input.r > input.a + 1e-5f || input.g > input.a + 1e-5f ||
      input.b > input.a + 1e-5f || shadow.r > shadow.a + 1e-5f ||
      shadow.g > shadow.a + 1e-5f || shadow.b > shadow.a + 1e-5f)
    return false;
  const float remaining = 1.0f - input.a;
  input = {input.r + shadow.r * remaining,
           input.g + shadow.g * remaining,
           input.b + shadow.b * remaining,
           input.a + shadow.a * remaining};
  input.a = std::clamp(input.a, 0.0f, 1.0f);
  input.r = std::clamp(input.r, 0.0f, input.a);
  input.g = std::clamp(input.g, 0.0f, input.a);
  input.b = std::clamp(input.b, 0.0f, input.a);
  return true;
}

bool render_effect_stack(const document::PathObject &path,
                         const document::Appearance &appearance,
                         const Camera2D &camera, PixelBufferTarget &target,
                         const color::IccPreview *preview,
                         size_t *out_peak_gaussian_blur_scratch_capacity_bytes,
                         size_t *out_peak_drop_shadow_scratch_capacity_bytes,
                         int32_t raster_dpi) {
  if (path.path.elements().empty())
    return true;
  const auto source_bounds = path.path.bounds();
  if (!finite_rect(source_bounds) || !std::isfinite(path.stroke_width_mm) ||
      path.stroke_width_mm < 0.0)
    return false;
  if ((source_bounds.width <= 0.0 || source_bounds.height <= 0.0) &&
      (!path.has_stroke || path.stroke_width_mm <= 0.0))
    return true;

  double transform_scale = 0.0;
  if (!max_linear_scale(path.transform, transform_scale))
    return false;
  const double density = (raster_dpi > 0 ? raster_dpi / 25.4 : camera.zoom()) * transform_scale;
  if (!std::isfinite(density) || density <= 0.0)
    return false;

  geometry::Rect2D output_bounds;
  uint64_t work_per_pixel = 0;
  if (!effect_output_bounds(path, appearance, density, output_bounds,
                            work_per_pixel))
    return false;
  if (!finite_nonempty_rect(output_bounds))
    return true;

  LocalSurface surface;
  if (!make_local_surface(output_bounds, density, surface))
    return false;
  const uint64_t pixel_count = static_cast<uint64_t>(surface.width) *
                               static_cast<uint64_t>(surface.height);
  if (work_per_pixel != 0 &&
      pixel_count > kMaxEffectJobWorkUnits / work_per_pixel)
    return false;

  const auto world_output_bounds = path.transform.map_rect(surface.bounds);
  if (!finite_nonempty_rect(world_output_bounds))
    return false;
  int32_t target_left = 0;
  int32_t target_top = 0;
  int32_t target_width = 0;
  int32_t target_height = 0;
  if (!screen_bounds(world_output_bounds, camera, target, target_left,
                     target_top, target_width, target_height))
    return false;
  if (target_width <= 0 || target_height <= 0)
    return true;

  std::vector<float> source_pixels(static_cast<size_t>(pixel_count) * 4, 0.0f);
  PixelBufferTarget source_target;
  source_target.width = surface.width;
  source_target.height = surface.height;
  source_target.stride_bytes =
      surface.width * 4 * static_cast<int32_t>(sizeof(float));
  source_target.premultiplied_rgba = source_pixels.data();
  if (!source_target.is_valid())
    return false;

  const auto local_center = surface.bounds.center();
  const Camera2D local_camera(local_center, surface.density, surface.width,
                              surface.height);
  auto source_path = path;
  source_path.transform = geometry::Transform2D::identity();
  source_path.fill = scale_alpha(path.fill, appearance.fill_opacity);
  source_path.stroke = scale_alpha(path.stroke, appearance.stroke_opacity);
  if (!raster::paint(source_path, local_camera, source_target, preview))
    return false;

  std::vector<FilterPixel> current;
  if (!encoded_to_linear(source_pixels, current) ||
      current.size() != static_cast<size_t>(pixel_count))
    return false;

  const auto record_gaussian_peak = [&](size_t additional_bytes) {
    if (!out_peak_gaussian_blur_scratch_capacity_bytes)
      return;
    size_t live_bytes = saturated_add(vector_capacity_bytes(source_pixels),
                                      vector_capacity_bytes(current));
    live_bytes = saturated_add(live_bytes, additional_bytes);
    *out_peak_gaussian_blur_scratch_capacity_bytes = std::max(
        *out_peak_gaussian_blur_scratch_capacity_bytes, live_bytes);
  };
  const auto record_drop_shadow_peak = [&](size_t alpha_bytes,
                                           size_t branch_bytes) {
    if (!out_peak_drop_shadow_scratch_capacity_bytes)
      return;
    size_t live_bytes = saturated_add(vector_capacity_bytes(source_pixels),
                                      vector_capacity_bytes(current));
    live_bytes = saturated_add(live_bytes, alpha_bytes);
    live_bytes = saturated_add(live_bytes, branch_bytes);
    *out_peak_drop_shadow_scratch_capacity_bytes = std::max(
        *out_peak_drop_shadow_scratch_capacity_bytes, live_bytes);
  };

  std::vector<FilterPixel> deferred_shadow;
  document::BlendMode deferred_shadow_blend = document::BlendMode::Normal;
  bool has_deferred_shadow = false;
  for (const auto &node : appearance.effects) {
    if (!node.enabled)
      continue;
    const bool applied = std::visit(
        [&](const auto &effect) {
          using T = std::decay_t<decltype(effect)>;
          const double sigma_x_px = effect.sigma_x_mm * surface.density;
          const double sigma_y_px = effect.sigma_y_mm * surface.density;
          if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            std::vector<FilterPixel> output;
            size_t internal_scratch_capacity_bytes = 0;
            if (gaussian_blur_linear_srgb_sse2(
                    current, static_cast<size_t>(surface.width),
                    static_cast<size_t>(surface.height), sigma_x_px,
                    sigma_y_px, output, {},
                    &internal_scratch_capacity_bytes) !=
                GaussianBlurStatus::Success)
              return false;
            record_gaussian_peak(internal_scratch_capacity_bytes);
            record_gaussian_peak(vector_capacity_bytes(output));
            current.swap(output);
            record_gaussian_peak(vector_capacity_bytes(output));
            return true;
          } else {
            std::vector<float> alpha(current.size());
            for (size_t index = 0; index < current.size(); ++index)
              alpha[index] = current[index].a;
            const auto tint = preview ? preview->convert(effect.color)
                                      : color::to_preview_rgba(effect.color);
            if (!unit_interval(tint.r) || !unit_interval(tint.g) ||
                !unit_interval(tint.b) || !unit_interval(tint.a))
              return false;
            DropShadowParameters parameters;
            parameters.offset_x_px = effect.dx_mm * surface.density;
            parameters.offset_y_px = effect.dy_mm * surface.density;
            parameters.sigma_x_px = sigma_x_px;
            parameters.sigma_y_px = sigma_y_px;
            parameters.color_r_linear_srgb = srgb_to_linear(tint.r);
            parameters.color_g_linear_srgb = srgb_to_linear(tint.g);
            parameters.color_b_linear_srgb = srgb_to_linear(tint.b);
            parameters.color_alpha = tint.a;
            parameters.opacity = static_cast<float>(effect.opacity);
            std::vector<FilterPixel> shadow;
            size_t branch_scratch_capacity_bytes = 0;
            const auto shadow_status = drop_shadow_linear_srgb(
                    alpha, static_cast<size_t>(surface.width),
                    static_cast<size_t>(surface.height), parameters,
                    shadow, {}, &branch_scratch_capacity_bytes);
            record_drop_shadow_peak(vector_capacity_bytes(alpha),
                                    branch_scratch_capacity_bytes);
            if (shadow_status != GaussianBlurStatus::Success ||
                shadow.size() != current.size())
              return false;
            if (effect.blend_mode != document::BlendMode::Normal) {
              const auto node_position = std::find_if(
                  appearance.effects.begin(), appearance.effects.end(),
                  [&node](const document::EffectNode &candidate) {
                    return candidate.id == node.id;
                  });
              if (node_position == appearance.effects.end() ||
                  std::any_of(std::next(node_position), appearance.effects.end(),
                      [](const document::EffectNode &candidate) {
                        return candidate.enabled;
                      }))
                return false;
              deferred_shadow = std::move(shadow);
              deferred_shadow_blend = effect.blend_mode;
              has_deferred_shadow = true;
              return true;
            }
            for (size_t index = 0; index < current.size(); ++index) {
              if (!compose_shadow_under_input(current[index], shadow[index]))
                return false;
            }
            return true;
          }
        },
        node.parameters);
    if (!applied)
      return false;
  }

  const auto inverse = path.transform.inverted();
  if (!inverse || !inverse->is_finite())
    return false;
  const auto sample_surface_at = [&](const std::vector<FilterPixel> &pixels,
                                     int32_t x, int32_t y,
                                     BlendPixel &pixel) -> bool {
    const auto world = camera.screen_to_world(
        {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f});
    const auto local = inverse->map_point(world);
    if (!local.is_finite())
      return false;
    const double sample_x =
        (local.x - surface.bounds.x) * surface.density - 0.5;
    const double sample_y =
        (surface.bounds.bottom() - local.y) * surface.density - 0.5;
    return linear_to_encoded(sample_linear_surface(
        pixels, surface.width, surface.height, sample_x, sample_y), pixel);
  };
  const auto sample_output = [&](int32_t x, int32_t y,
                                 BlendPixel &pixel) -> bool {
    if (!sample_surface_at(current, x, y, pixel)) return false;
    if (has_deferred_shadow) return true;
    const float opacity = static_cast<float>(appearance.object_opacity);
    pixel.r *= opacity;
    pixel.g *= opacity;
    pixel.b *= opacity;
    pixel.a *= opacity;
    return is_valid_premultiplied_srgb(pixel);
  };
  const auto resolve_output_pixel = [&](int32_t x, int32_t y,
                                        BlendPixel &output) -> bool {
    BlendPixel source_pixel;
    if (!sample_output(x, y, source_pixel)) return false;
    BlendPixel shadow_pixel{};
    if (has_deferred_shadow &&
        !sample_surface_at(deferred_shadow, x, y, shadow_pixel))
      return false;
    if (!has_deferred_shadow && source_pixel.a <= 0.0f) {
      output = {};
      return true;
    }
    if (has_deferred_shadow && source_pixel.a <= 0.0f &&
        shadow_pixel.a <= 0.0f) {
      output = {};
      return true;
    }
    const auto backdrop = read_target_pixel(target, x, y);
    if (!is_valid_premultiplied_srgb(backdrop)) return false;
    if (has_deferred_shadow) {
      const auto shadowed = blend_source_over(
          deferred_shadow_blend, backdrop, shadow_pixel);
      if (!shadowed) return false;
      const auto grouped = blend_source_over(
          appearance.blend_mode, *shadowed, source_pixel);
      if (!grouped) return false;
      const float opacity = static_cast<float>(appearance.object_opacity);
      const float remaining = 1.0f - opacity;
      output = {backdrop.r * remaining + grouped->r * opacity,
                backdrop.g * remaining + grouped->g * opacity,
                backdrop.b * remaining + grouped->b * opacity,
                backdrop.a * remaining + grouped->a * opacity};
      return is_valid_premultiplied_srgb(output);
    }
    if (appearance.blend_mode == document::BlendMode::Normal) {
      output = source_pixel;
      return true;
    }
    const auto composed = blend_source_over(
        appearance.blend_mode, backdrop, source_pixel);
    if (!composed) return false;
    output = *composed;
    return true;
  };

  const uint64_t target_pixel_count =
      static_cast<uint64_t>(target_width) *
      static_cast<uint64_t>(target_height);
  const bool cache_output =
      target_pixel_count <= kMaxCachedEffectOutputPixels;
  std::vector<BlendPixel> staged_output;
  if (cache_output)
    staged_output.resize(static_cast<size_t>(target_pixel_count));

  // Validate every destination sample before the first write. The filter
  // helpers and local source surface are private until this loop succeeds.
  // For bounded outputs, stage the exact source or composed pixel so the
  // write pass does not repeat transforms, sampling, or blend evaluation.
  for (int32_t y = target_top; y < target_top + target_height; ++y) {
    for (int32_t x = target_left; x < target_left + target_width; ++x) {
      BlendPixel output_pixel;
      if (!resolve_output_pixel(x, y, output_pixel)) return false;
      if (output_pixel.a <= 0.0f) continue;
      if (cache_output) {
        const size_t output_index =
            static_cast<size_t>(y - target_top) *
                static_cast<size_t>(target_width) +
            static_cast<size_t>(x - target_left);
        staged_output[output_index] = output_pixel;
      }
    }
  }

  for (int32_t y = target_top; y < target_top + target_height; ++y) {
    for (int32_t x = target_left; x < target_left + target_width; ++x) {
      if (cache_output) {
        const size_t output_index =
            static_cast<size_t>(y - target_top) *
                static_cast<size_t>(target_width) +
            static_cast<size_t>(x - target_left);
        const auto pixel = staged_output[output_index];
        if (pixel.a <= 0.0f)
          continue;
        if (!has_deferred_shadow &&
            appearance.blend_mode == document::BlendMode::Normal) {
          target.blend_premultiplied_pixel(x, y, pixel.r, pixel.g, pixel.b,
                                           pixel.a);
        } else {
          write_target_pixel(target, x, y, pixel);
        }
        continue;
      }

      BlendPixel output_pixel;
      if (!resolve_output_pixel(x, y, output_pixel) || output_pixel.a <= 0.0f)
        continue;
      if (!has_deferred_shadow &&
          appearance.blend_mode == document::BlendMode::Normal) {
        target.blend_premultiplied_pixel(
            x, y, output_pixel.r, output_pixel.g, output_pixel.b,
            output_pixel.a);
      } else {
        write_target_pixel(target, x, y, output_pixel);
      }
    }
  }
  return true;
}

} // namespace

bool render_path_with_appearance(const document::PathObject &path,
                                 const document::Appearance &appearance,
                                 const Camera2D &camera,
                                 PixelBufferTarget &target,
                                 const color::IccPreview *preview,
                                 size_t *out_peak_gaussian_blur_scratch_capacity_bytes,
                                 size_t *out_peak_drop_shadow_scratch_capacity_bytes,
                                 int32_t raster_dpi) {
  if (out_peak_gaussian_blur_scratch_capacity_bytes)
    *out_peak_gaussian_blur_scratch_capacity_bytes = 0;
  if (out_peak_drop_shadow_scratch_capacity_bytes)
    *out_peak_drop_shadow_scratch_capacity_bytes = 0;
  if (!target.is_valid() || !valid_appearance(appearance))
    return false;
  if (is_default_appearance(appearance))
    return raster::paint(path, camera, target, preview);
  if (appearance.object_opacity == 0.0)
    return true;
  if (has_enabled_effects(appearance)) {
    try {
      return render_effect_stack(
          path, appearance, camera, target, preview,
          out_peak_gaussian_blur_scratch_capacity_bytes,
          out_peak_drop_shadow_scratch_capacity_bytes, raster_dpi);
    } catch (const std::bad_alloc &) {
      return false;
    } catch (const std::length_error &) {
      return false;
    }
  }

  int32_t left = 0;
  int32_t top = 0;
  int32_t width = 0;
  int32_t height = 0;
  if (!screen_bounds(path, camera, target, left, top, width, height))
    return false;
  if (width <= 0 || height <= 0)
    return true;
  const auto pixel_count = static_cast<uint64_t>(width) *
                           static_cast<uint64_t>(height);
  if (pixel_count > kMaxSourceSurfacePixels ||
      pixel_count > std::numeric_limits<size_t>::max() / (4 * sizeof(float)))
    return false;

  try {
    std::vector<float> source_pixels(static_cast<size_t>(pixel_count) * 4, 0.0f);
    PixelBufferTarget source_target;
    source_target.width = width;
    source_target.height = height;
    source_target.stride_bytes = width * 4 * static_cast<int32_t>(sizeof(float));
    source_target.premultiplied_rgba = source_pixels.data();
    if (!source_target.is_valid())
      return false;

    const auto center = camera.screen_to_world(
        {static_cast<float>(left) + static_cast<float>(width) * 0.5f,
         static_cast<float>(top) + static_cast<float>(height) * 0.5f});
    const Camera2D source_camera(center, camera.zoom(), width, height);
    auto source = path;
    source.fill = scale_alpha(path.fill, appearance.fill_opacity);
    source.stroke = scale_alpha(path.stroke, appearance.stroke_opacity);
    if (!raster::paint(source, source_camera, source_target, preview))
      return false;

    const float object_opacity = static_cast<float>(appearance.object_opacity);
    const size_t source_stride =
        static_cast<size_t>(source_target.stride_bytes / sizeof(float));

    // Validate every source/backdrop pair before the first write for both
    // Normal and non-Normal compositing. Float targets are caller-owned and
    // may contain malformed samples even when their buffer shape is valid.
    for (int32_t y = 0; y < height; ++y) {
      const auto *row = source_pixels.data() +
                        static_cast<size_t>(y) * source_stride;
      for (int32_t x = 0; x < width; ++x) {
        const auto *pixel = row + static_cast<size_t>(x) * 4;
        const BlendPixel source_pixel{pixel[0] * object_opacity,
                                      pixel[1] * object_opacity,
                                      pixel[2] * object_opacity,
                                      pixel[3] * object_opacity};
        if (!is_valid_premultiplied_srgb(source_pixel))
          return false;
        if (source_pixel.a <= 0.0f)
          continue;
        const auto backdrop = read_target_pixel(target, left + x, top + y);
        if (!is_valid_premultiplied_srgb(backdrop))
          return false;
        if (appearance.blend_mode != document::BlendMode::Normal &&
            !blend_source_over(appearance.blend_mode, backdrop, source_pixel))
          return false;
      }
    }

    if (appearance.blend_mode != document::BlendMode::Normal) {
      for (int32_t y = 0; y < height; ++y) {
        const auto *row = source_pixels.data() +
                          static_cast<size_t>(y) * source_stride;
        for (int32_t x = 0; x < width; ++x) {
          const auto *pixel = row + static_cast<size_t>(x) * 4;
          const BlendPixel source_pixel{pixel[0] * object_opacity,
                                        pixel[1] * object_opacity,
                                        pixel[2] * object_opacity,
                                        pixel[3] * object_opacity};
          if (source_pixel.a <= 0.0f)
            continue;
          const auto composed = blend_source_over(
              appearance.blend_mode,
              read_target_pixel(target, left + x, top + y), source_pixel);
          if (!composed)
            return false;
          write_target_pixel(target, left + x, top + y, *composed);
        }
      }
      return true;
    }

    for (int32_t y = 0; y < height; ++y) {
      const auto *row = source_pixels.data() +
                        static_cast<size_t>(y) * source_stride;
      for (int32_t x = 0; x < width; ++x) {
        const auto *pixel = row + static_cast<size_t>(x) * 4;
        const float alpha = pixel[3] * object_opacity;
        if (alpha <= 0.0f)
          continue;
        target.blend_premultiplied_pixel(
            left + x, top + y, pixel[0] * object_opacity,
            pixel[1] * object_opacity, pixel[2] * object_opacity, alpha);
      }
    }
  } catch (const std::bad_alloc &) {
    return false;
  } catch (const std::length_error &) {
    return false;
  }
  return true;
}

} // namespace pikdraw::rendering::effects
