#include "path_raster.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace pikdraw::rendering::raster {
namespace {
constexpr size_t kWorkBudget = 20000000;

bool max_linear_scale(const geometry::Transform2D &transform, double &scale) {
  const double trace = transform.a * transform.a +
                       transform.b * transform.b +
                       transform.c * transform.c +
                       transform.d * transform.d;
  const double determinant = transform.determinant();
  double discriminant = trace * trace - 4.0 * determinant * determinant;
  if (!std::isfinite(trace) || !std::isfinite(determinant) ||
      !std::isfinite(discriminant))
    return false;
  discriminant = std::max(0.0, discriminant);
  scale = std::sqrt((trace + std::sqrt(discriminant)) * 0.5);
  return std::isfinite(scale) && scale > 0.0;
}

struct Coverage {
  int left{0};
  int top{0};
  size_t width{0};
  std::vector<uint8_t> mask;
  std::vector<size_t> covered;

  Coverage(const std::vector<Contour> &contours, double screen_radius,
           const PixelBufferTarget &target) {
    if (contours.empty() || target.width <= 0 || target.height <= 0)
      return;
    double min_x = target.width, min_y = target.height, max_x = 0, max_y = 0;
    bool has_points = false;
    for (const auto &contour : contours) {
      for (const auto &p : contour.screen) {
        if (!has_points) {
          min_x = max_x = p.x;
          min_y = max_y = p.y;
          has_points = true;
        } else {
          min_x = std::min(min_x, p.x);
          min_y = std::min(min_y, p.y);
          max_x = std::max(max_x, p.x);
          max_y = std::max(max_y, p.y);
        }
      }
    }
    if (!has_points)
      return;

    const double pad = screen_radius + 2.0;
    left = std::max(0, static_cast<int>(std::floor(min_x - pad)));
    top = std::max(0, static_cast<int>(std::floor(min_y - pad)));
    const int right =
        std::min(target.width, static_cast<int>(std::ceil(max_x + pad)));
    const int bottom =
        std::min(target.height, static_cast<int>(std::ceil(max_y + pad)));

    if (right > left && bottom > top) {
      width = static_cast<size_t>(right - left);
      mask.resize(width * static_cast<size_t>(bottom - top), 0);
    }
  }

  void mark(int x, int y, uint8_t cov) {
    if (cov == 0 || x < left || y < top)
      return;
    const size_t col = static_cast<size_t>(x - left);
    const size_t row = static_cast<size_t>(y - top);
    if (col >= width)
      return;
    const size_t index = row * width + col;
    if (index >= mask.size())
      return;
    if (mask[index] == 0) {
      covered.push_back(index);
    }
    if (cov > mask[index]) {
      mask[index] = cov;
    }
  }

  void blend_into(PixelBufferTarget &target, const RasterColor &value) const {
    // Union coverage: joins/self-overlaps composite once per source object.
    for (auto index : covered) {
      const uint8_t cov = mask[index];
      if (cov == 0)
        continue;
      const int px = left + static_cast<int>(index % width);
      const int py = top + static_cast<int>(index / width);
      if (cov == 255) {
        blend_preview(target, px, py, value);
      } else {
        if (target.premultiplied_rgba) {
          const float alpha =
              value.preview.a * (static_cast<float>(cov) / 255.0f);
          target.blend_premultiplied_pixel(
              px, py, value.preview.r * alpha, value.preview.g * alpha,
              value.preview.b * alpha, alpha);
        } else {
          const uint8_t alpha = static_cast<uint8_t>(
              (static_cast<int>(value.rgba8.a) * cov + 127) / 255);
          target.blend_pixel(px, py, value.rgba8.r, value.rgba8.g, value.rgba8.b,
                             alpha);
        }
      }
    }
  }
};

struct StrokeSampler {
  const Camera2D &camera;
  const geometry::Transform2D &transform;
  const geometry::Transform2D &inverse;
  double radius;
  double screen_radius;
  size_t work{0};

  double dudx{0.0};
  double dudy{0.0};
  double dvdx{0.0};
  double dvdy{0.0};
  double u_origin{0.0};
  double v_origin{0.0};

  StrokeSampler(const Camera2D &cam, const geometry::Transform2D &xf,
                const geometry::Transform2D &inv, double r, double sr)
      : camera(cam), transform(xf), inverse(inv), radius(r),
        screen_radius(sr) {
    const double inv_zoom = 1.0 / camera.zoom();
    dudx = inverse.a * inv_zoom;
    dudy = -inverse.c * inv_zoom;
    dvdx = inverse.b * inv_zoom;
    dvdy = -inverse.d * inv_zoom;

    const double w0_x =
        camera.center().x + (0.5 - camera.width() * 0.5) * inv_zoom;
    const double w0_y =
        camera.center().y - (0.5 - camera.height() * 0.5) * inv_zoom;
    u_origin = inverse.a * w0_x + inverse.c * w0_y + inverse.tx;
    v_origin = inverse.b * w0_x + inverse.d * w0_y + inverse.ty;
  }

  bool segment(Point local_a, Point local_b, Point a, Point b,
               Coverage &coverage, const PixelBufferTarget &target) {
    const double pad = screen_radius + 2.0;
    const int x0 =
        std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - pad)));
    const int x1 = std::min(
        target.width, static_cast<int>(std::ceil(std::max(a.x, b.x) + pad)));
    const int y0 =
        std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - pad)));
    const int y1 = std::min(
        target.height, static_cast<int>(std::ceil(std::max(a.y, b.y) + pad)));

    if (x1 <= x0 || y1 <= y0)
      return true;

    work += static_cast<size_t>(x1 - x0) * static_cast<size_t>(y1 - y0);
    if (work > kWorkBudget)
      return false;

    // Normal scale factor from local mm to screen pixels along the segment normal
    const auto delta = local_b - local_a;
    const double length2 = delta.x * delta.x + delta.y * delta.y;
    const double inv_length2 = length2 > 1e-18 ? (1.0 / length2) : 0.0;
    const double len = std::sqrt(length2);

    double scale_normal = screen_radius / std::max(radius, 1e-9);
    if (len > 1e-9) {
      const double nx = -delta.y / len;
      const double ny = delta.x / len;
      const double dnx = transform.a * nx + transform.c * ny;
      const double dny = transform.b * nx + transform.d * ny;
      scale_normal = camera.zoom() * std::hypot(dnx, dny);
      if (scale_normal <= 1e-9)
        scale_normal = screen_radius / std::max(radius, 1e-9);
    }

    // Inner and outer distance thresholds in local coordinates
    const double inv_scale = 1.0 / scale_normal;
    const double r_inner = radius - 0.5 * inv_scale;
    const double r_inner2 =
        r_inner >= -1e-9 ? (std::max(0.0, r_inner) * std::max(0.0, r_inner))
                         : -1.0;
    const double r_outer = radius + 0.95 * inv_scale;
    const double r_outer2 = r_outer * r_outer;

    // 4x Rotated Grid MSAA Subpixel offsets (relative to pixel center (0,0)):
    // (-1/8, -3/8), (3/8, -1/8), (-3/8, 1/8), (1/8, 3/8)
    constexpr double sub_dx[4] = {-0.125, 0.375, -0.375, 0.125};
    constexpr double sub_dy[4] = {-0.375, -0.125, 0.125, 0.375};
    const double sub_du[4] = {
        sub_dx[0] * dudx + sub_dy[0] * dudy,
        sub_dx[1] * dudx + sub_dy[1] * dudy,
        sub_dx[2] * dudx + sub_dy[2] * dudy,
        sub_dx[3] * dudx + sub_dy[3] * dudy,
    };
    const double sub_dv[4] = {
        sub_dx[0] * dvdx + sub_dy[0] * dvdy,
        sub_dx[1] * dvdx + sub_dy[1] * dvdy,
        sub_dx[2] * dvdx + sub_dy[2] * dvdy,
        sub_dx[3] * dvdx + sub_dy[3] * dvdy,
    };

    for (int y = y0; y < y1; ++y) {
      const double u_row = u_origin + x0 * dudx + y * dudy;
      const double v_row = v_origin + x0 * dvdx + y * dvdy;
      double u = u_row;
      double v = v_row;
      for (int x = x0; x < x1; ++x, u += dudx, v += dvdx) {
        const double dot =
            (u - local_a.x) * delta.x + (v - local_a.y) * delta.y;
        const double t = std::clamp(dot * inv_length2, 0.0, 1.0);
        const double qx = local_a.x + delta.x * t;
        const double qy = local_a.y + delta.y * t;
        const double dx = u - qx;
        const double dy = v - qy;
        const double d2 = dx * dx + dy * dy;

        // Fast path: fully inside stroke (solid)
        if (d2 <= r_inner2) {
          coverage.mark(x, y, 255);
          continue;
        }

        // Fast path: fully outside stroke (no coverage)
        if (d2 >= r_outer2) {
          continue;
        }

        // Boundary region: 4x MSAA rotated subpixel sampling with smooth Hermite falloff
        double total_cov = 0.0;
        for (int k = 0; k < 4; ++k) {
          const double su = u + sub_du[k];
          const double sv = v + sub_dv[k];
          const double sdot =
              (su - local_a.x) * delta.x + (sv - local_a.y) * delta.y;
          const double st = std::clamp(sdot * inv_length2, 0.0, 1.0);
          const double sqx = local_a.x + delta.x * st;
          const double sqy = local_a.y + delta.y * st;
          const double sdx = su - sqx;
          const double sdy = sv - sqy;
          const double sd2 = sdx * sdx + sdy * sdy;
          const double sdist = std::sqrt(sd2);
          const double sedge = (sdist - radius) * scale_normal;
          const double st_cov = std::clamp(0.5 - sedge, 0.0, 1.0);
          total_cov += st_cov * st_cov * (3.0 - 2.0 * st_cov);
        }
        const double cov = total_cov * 0.25;
        if (cov > 0.0) {
          coverage.mark(x, y, static_cast<uint8_t>(std::lround(cov * 255.0)));
        }
      }
    }
    return true;
  }
};
} // namespace

bool stroke(const std::vector<Contour> &contours,
            const document::PathObject &object, const Camera2D &camera,
            color::PreviewRGBA value, PixelBufferTarget &target) {
  if (object.stroke_width_mm == 0)
    return true;
  const auto raster_color = make_raster_color(value, target);
  if ((target.premultiplied_rgba && value.a <= 0.0f) ||
      (!target.premultiplied_rgba && raster_color.rgba8.a == 0))
    return true;
  const auto inverse = object.transform.inverted();
  if (!inverse || !inverse->is_finite())
    return false;
  const auto &t = object.transform;
  const double radius = object.stroke_width_mm * 0.5;

  double transform_scale = 1.0;
  if (!max_linear_scale(t, transform_scale)) {
    transform_scale = std::max(std::hypot(t.a, t.b), std::hypot(t.c, t.d));
  }
  const double raw_screen_radius = radius * camera.zoom() * transform_scale;
  if (!std::isfinite(raw_screen_radius) || raw_screen_radius < 0.0)
    return false;

  // Hairline minimum threshold (CorelDRAW / Illustrator fidelity):
  // Regardless of camera zoom level (thu phóng) or object scale,
  // a visible stroke never collapses below a 1.0px hairline (0.5px screen radius).
  const double screen_radius = std::max(0.5, raw_screen_radius);
  const double effective_radius = (raw_screen_radius < 0.5)
                                      ? (0.5 / std::max(camera.zoom() * transform_scale, 1e-9))
                                      : radius;

  Coverage coverage{contours, screen_radius, target};
  if (coverage.mask.empty())
    return true;

  StrokeSampler sampler{camera, t, *inverse, effective_radius, screen_radius};
  for (const auto &contour : contours) {
    const size_t count = contour.local.size();
    if (count < 2)
      continue;
    const size_t segments = contour.closed ? count : count - 1;
    for (size_t i = 0; i < segments; ++i) {
      const size_t j = (i + 1) % count;
      if (!sampler.segment(contour.local[i], contour.local[j],
                           contour.screen[i], contour.screen[j], coverage,
                           target))
        return false;
    }
  }
  coverage.blend_into(target, raster_color);
  return true;
}
} // namespace pikdraw::rendering::raster
