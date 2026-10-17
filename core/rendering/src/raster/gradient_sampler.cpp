#include "pikdraw/rendering/gradient_sampler.hpp"
#include "path_raster.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::rendering {

std::optional<GradientSampler> GradientSampler::prepare(
    const document::GradientPaint &paint,
    const geometry::Transform2D &object_transform,
    const Camera2D &camera,
    const color::IccPreview *preview,
    double fill_opacity) {
  if (const auto error = paint.validation_error()) {
    return std::nullopt;
  }
  if (!std::isfinite(fill_opacity) || fill_opacity < 0.0) {
    return std::nullopt;
  }
  const double clamped_fill_opacity = std::clamp(fill_opacity, 0.0, 1.0);

  // Validate object transform conditioning
  if (!object_transform.is_finite()) {
    return std::nullopt;
  }
  const double max_obj_coeff = std::max({
      std::abs(object_transform.a), std::abs(object_transform.b),
      std::abs(object_transform.c), std::abs(object_transform.d)
  });
  if (max_obj_coeff <= 0.0) {
    return std::nullopt;
  }
  const double rel_obj_det = (object_transform.a / max_obj_coeff) * (object_transform.d / max_obj_coeff) -
                             (object_transform.b / max_obj_coeff) * (object_transform.c / max_obj_coeff);
  if (std::abs(rel_obj_det) <= 1e-12) {
    return std::nullopt;
  }
  const auto inv_obj = object_transform.inverted();
  if (!inv_obj || !inv_obj->is_finite()) {
    return std::nullopt;
  }

  // Validate gradient_to_object transform conditioning
  const auto inv_grad = paint.gradient_to_object.inverted();
  if (!inv_grad || !inv_grad->is_finite()) {
    return std::nullopt;
  }

  // Build screen-to-world transform from camera
  if (!std::isfinite(camera.zoom()) || camera.zoom() <= 0.0) {
    return std::nullopt;
  }
  const double inv_zoom = 1.0 / camera.zoom();
  const double half_w = static_cast<double>(camera.width()) * 0.5;
  const double half_h = static_cast<double>(camera.height()) * 0.5;
  const geometry::Transform2D screen_to_world(
      inv_zoom, 0.0, 0.0, -inv_zoom,
      camera.center().x - half_w * inv_zoom,
      camera.center().y + half_h * inv_zoom
  );
  if (!screen_to_world.is_finite()) {
    return std::nullopt;
  }

  // Compose screen_to_q = inv_grad * inv_obj * screen_to_world
  const auto screen_to_q = (*inv_grad) * (*inv_obj) * screen_to_world;
  if (!screen_to_q.is_finite()) {
    return std::nullopt;
  }

  // Convert stops to straight encoded sRGB with effective alpha
  std::vector<PreparedGradientStop> prepared_stops;
  prepared_stops.reserve(paint.stops.size());
  for (const auto &stop : paint.stops) {
    const auto preview_color = preview ? preview->convert(stop.color) : color::to_preview_rgba(stop.color);
    const float eff_a = std::clamp(preview_color.a * static_cast<float>(stop.opacity), 0.0f, 1.0f);
    const float eff_r = std::clamp(preview_color.r, 0.0f, 1.0f);
    const float eff_g = std::clamp(preview_color.g, 0.0f, 1.0f);
    const float eff_b = std::clamp(preview_color.b, 0.0f, 1.0f);
    prepared_stops.push_back(PreparedGradientStop{
        stop.offset,
        stop.midpoint_to_next,
        eff_r,
        eff_g,
        eff_b,
        eff_a
    });
  }

  return GradientSampler(paint.kind, paint.spread, clamped_fill_opacity, screen_to_q, std::move(prepared_stops));
}

geometry::Point2D GradientSampler::screen_to_q(double screen_x, double screen_y) const noexcept {
  return screen_to_q_.map_point(geometry::Point2D{screen_x, screen_y});
}

double GradientSampler::q_to_t(geometry::Point2D q) const noexcept {
  if (kind_ == document::GradientKind::Linear) {
    return q.x;
  }
  return std::hypot(q.x, q.y);
}

double GradientSampler::apply_spread(double t, document::GradientSpread spread) noexcept {
  if (!std::isfinite(t)) {
    return 0.0;
  }
  switch (spread) {
  case document::GradientSpread::Pad:
    return std::clamp(t, 0.0, 1.0);
  case document::GradientSpread::Repeat: {
    const double wrapped = t - std::floor(t);
    return std::clamp(wrapped, 0.0, 1.0);
  }
  case document::GradientSpread::Reflect: {
    const double r = t - 2.0 * std::floor(t * 0.5);
    const double val = (r <= 1.0) ? r : (2.0 - r);
    return std::clamp(val, 0.0, 1.0);
  }
  }
  return 0.0;
}

color::PreviewRGBA GradientSampler::sample_at_t(double t) const noexcept {
  if (stops_.empty()) {
    return color::PreviewRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  }
  const float fill_op = static_cast<float>(fill_opacity_);
  if (t <= stops_.front().offset) {
    const auto &st = stops_.front();
    return color::PreviewRGBA{st.r, st.g, st.b, st.a * fill_op};
  }
  if (t >= stops_.back().offset) {
    const auto &st = stops_.back();
    return color::PreviewRGBA{st.r, st.g, st.b, st.a * fill_op};
  }

  // Check for exact tie cluster match:
  // "Stops cùng offset là hard edge, giữ thứ tự vector ổn định. Với t đúng điểm trùng lấy stop cuối tại điểm ấy"
  int exact_last_idx = -1;
  for (size_t i = 0; i < stops_.size(); ++i) {
    if (std::abs(stops_[i].offset - t) <= 1e-12) {
      exact_last_idx = static_cast<int>(i);
    }
  }
  if (exact_last_idx >= 0) {
    const auto &st = stops_[static_cast<size_t>(exact_last_idx)];
    return color::PreviewRGBA{st.r, st.g, st.b, st.a * fill_op};
  }

  // Find enclosing interval [left_idx, right_idx]
  // left_idx: last stop strictly before t
  // right_idx: first stop strictly after t
  int left_idx = -1;
  int right_idx = -1;
  for (size_t i = 0; i < stops_.size(); ++i) {
    if (stops_[i].offset <= t - 1e-12) {
      left_idx = static_cast<int>(i);
    }
  }
  for (size_t i = 0; i < stops_.size(); ++i) {
    if (stops_[i].offset >= t + 1e-12) {
      right_idx = static_cast<int>(i);
      break;
    }
  }

  if (left_idx < 0) {
    const auto &st = stops_.front();
    return color::PreviewRGBA{st.r, st.g, st.b, st.a * fill_op};
  }
  if (right_idx < 0) {
    const auto &st = stops_.back();
    return color::PreviewRGBA{st.r, st.g, st.b, st.a * fill_op};
  }

  const auto &left = stops_[static_cast<size_t>(left_idx)];
  const auto &right = stops_[static_cast<size_t>(right_idx)];
  const double delta = right.offset - left.offset;
  if (delta <= 1e-12) {
    return color::PreviewRGBA{right.r, right.g, right.b, right.a * fill_op};
  }

  // Piecewise-linear midpoint remap:
  // u = (t - a) / (b - a)
  // v = 0.5 * u / m when u <= m, else 0.5 + 0.5 * (u - m) / (1 - m)
  const double u = std::clamp((t - left.offset) / delta, 0.0, 1.0);
  const double m = std::clamp(left.midpoint, 0.01, 0.99);
  double v = 0.0;
  if (u <= m) {
    v = 0.5 * (u / m);
  } else {
    v = 0.5 + 0.5 * ((u - m) / (1.0 - m));
  }
  v = std::clamp(v, 0.0, 1.0);

  const float r = static_cast<float>((1.0 - v) * left.r + v * right.r);
  const float g = static_cast<float>((1.0 - v) * left.g + v * right.g);
  const float b = static_cast<float>((1.0 - v) * left.b + v * right.b);
  const float a = static_cast<float>((1.0 - v) * left.a + v * right.a);
  return color::PreviewRGBA{r, g, b, a * fill_op};
}

color::PreviewRGBA GradientSampler::sample_straight(int col, int row) const noexcept {
  const auto q = screen_to_q(static_cast<double>(col) + 0.5, static_cast<double>(row) + 0.5);
  const double t_raw = q_to_t(q);
  const double t_spread = apply_spread(t_raw, spread_);
  return sample_at_t(t_spread);
}

raster::RasterColor GradientSampler::sample_raster_color(
    int col, int row, const PixelBufferTarget &target) const noexcept {
  const auto preview = sample_straight(col, row);
  return raster::make_raster_color(preview, target);
}

} // namespace pikdraw::rendering
