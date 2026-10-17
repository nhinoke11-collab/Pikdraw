#include "pikdraw/rendering/gradient_sampler.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/document/model/gradient_paint.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::rendering;

namespace {

bool near_eq(double a, double b, double eps = 1e-6) {
  return std::abs(a - b) <= eps;
}

bool near_eq(float a, float b, float eps = 1e-4f) {
  return std::abs(a - b) <= eps;
}

} // namespace

void test_spread_modes() {
  // Test Pad
  assert(near_eq(GradientSampler::apply_spread(-2.0, document::GradientSpread::Pad), 0.0));
  assert(near_eq(GradientSampler::apply_spread(-0.25, document::GradientSpread::Pad), 0.0));
  assert(near_eq(GradientSampler::apply_spread(0.0, document::GradientSpread::Pad), 0.0));
  assert(near_eq(GradientSampler::apply_spread(0.25, document::GradientSpread::Pad), 0.25));
  assert(near_eq(GradientSampler::apply_spread(1.0, document::GradientSpread::Pad), 1.0));
  assert(near_eq(GradientSampler::apply_spread(1.25, document::GradientSpread::Pad), 1.0));
  assert(near_eq(GradientSampler::apply_spread(2.0, document::GradientSpread::Pad), 1.0));

  // Test Repeat
  assert(near_eq(GradientSampler::apply_spread(-2.0, document::GradientSpread::Repeat), 0.0));
  assert(near_eq(GradientSampler::apply_spread(-1.0, document::GradientSpread::Repeat), 0.0));
  assert(near_eq(GradientSampler::apply_spread(-0.25, document::GradientSpread::Repeat), 0.75));
  assert(near_eq(GradientSampler::apply_spread(0.0, document::GradientSpread::Repeat), 0.0));
  assert(near_eq(GradientSampler::apply_spread(0.25, document::GradientSpread::Repeat), 0.25));
  assert(near_eq(GradientSampler::apply_spread(1.0, document::GradientSpread::Repeat), 0.0));
  assert(near_eq(GradientSampler::apply_spread(1.25, document::GradientSpread::Repeat), 0.25));
  assert(near_eq(GradientSampler::apply_spread(2.0, document::GradientSpread::Repeat), 0.0));

  // Test Reflect: r = t - 2 * floor(t/2); if r <= 1 r else 2 - r
  assert(near_eq(GradientSampler::apply_spread(-2.0, document::GradientSpread::Reflect), 0.0));
  assert(near_eq(GradientSampler::apply_spread(-1.0, document::GradientSpread::Reflect), 1.0));
  assert(near_eq(GradientSampler::apply_spread(-0.25, document::GradientSpread::Reflect), 0.25));
  assert(near_eq(GradientSampler::apply_spread(0.0, document::GradientSpread::Reflect), 0.0));
  assert(near_eq(GradientSampler::apply_spread(0.25, document::GradientSpread::Reflect), 0.25));
  assert(near_eq(GradientSampler::apply_spread(1.0, document::GradientSpread::Reflect), 1.0));
  assert(near_eq(GradientSampler::apply_spread(1.25, document::GradientSpread::Reflect), 0.75));
  assert(near_eq(GradientSampler::apply_spread(2.0, document::GradientSpread::Reflect), 0.0));

  std::cout << "[PASS] test_spread_modes\n";
}

void test_ac_g03_straight_alpha_and_midpoint() {
  // AC-G03: Red (alpha 0) -> Blue (alpha 1), stop opacity 1.0
  // Straight RGB at t=0.5 should be (0.5, 0.0, 0.5), alpha=0.5
  document::GradientPaint paint;
  paint.id = 101;
  paint.kind = document::GradientKind::Linear;
  paint.spread = document::GradientSpread::Pad;
  paint.stops = {
      document::GradientStop{1, 0.0, color::RGBColor{1.0, 0.0, 0.0, 0.0}, 1.0, 0.5},
      document::GradientStop{2, 1.0, color::RGBColor{0.0, 0.0, 1.0, 1.0}, 1.0, 0.5}
  };

  Camera2D camera({50.0, 25.0}, 1.0, 100, 50);
  auto sampler = GradientSampler::prepare(paint, geometry::Transform2D::identity(), camera, nullptr, 1.0);
  assert(sampler.has_value());

  // Direct sample at t = 0.5
  const auto mid = sampler->sample_at_t(0.5);
  assert(near_eq(mid.r, 0.5f));
  assert(near_eq(mid.g, 0.0f));
  assert(near_eq(mid.b, 0.5f));
  assert(near_eq(mid.a, 0.5f));

  // Premultiplied after sample:
  const float pm_r = mid.r * mid.a;
  const float pm_b = mid.b * mid.a;
  assert(near_eq(pm_r, 0.25f));
  assert(near_eq(pm_b, 0.25f));

  // With fill_opacity 50%:
  auto sampler_half_fill = GradientSampler::prepare(paint, geometry::Transform2D::identity(), camera, nullptr, 0.5);
  assert(sampler_half_fill.has_value());
  const auto mid_half = sampler_half_fill->sample_at_t(0.5);
  assert(near_eq(mid_half.r, 0.5f));
  assert(near_eq(mid_half.g, 0.0f));
  assert(near_eq(mid_half.b, 0.5f));
  assert(near_eq(mid_half.a, 0.25f)); // 0.5 * 0.5

  std::cout << "[PASS] test_ac_g03_straight_alpha_and_midpoint\n";
}

void test_hard_edge_ties() {
  // 4 stops: Green at 0.0, Red at 0.5, Blue at 0.5, Yellow at 1.0
  document::GradientPaint paint;
  paint.id = 102;
  paint.kind = document::GradientKind::Linear;
  paint.spread = document::GradientSpread::Pad;
  paint.stops = {
      document::GradientStop{1, 0.0, color::RGBColor{0.0, 1.0, 0.0, 1.0}, 1.0, 0.5},
      document::GradientStop{2, 0.5, color::RGBColor{1.0, 0.0, 0.0, 1.0}, 1.0, 0.5},
      document::GradientStop{3, 0.5, color::RGBColor{0.0, 0.0, 1.0, 1.0}, 1.0, 0.5},
      document::GradientStop{4, 1.0, color::RGBColor{1.0, 1.0, 0.0, 1.0}, 1.0, 0.5}
  };

  Camera2D camera({50.0, 25.0}, 1.0, 100, 50);
  auto sampler = GradientSampler::prepare(paint, geometry::Transform2D::identity(), camera, nullptr, 1.0);
  assert(sampler.has_value());

  // Left of hard edge: t = 0.499 approaches Red (1.0, 0.0, 0.0)
  const auto left = sampler->sample_at_t(0.499);
  assert(left.r > 0.95f && left.b < 0.05f);

  // Exact tie at t = 0.5: returns last stop in cluster (Blue: 0.0, 0.0, 1.0)
  const auto tie = sampler->sample_at_t(0.5);
  assert(near_eq(tie.r, 0.0f));
  assert(near_eq(tie.g, 0.0f));
  assert(near_eq(tie.b, 1.0f));
  assert(near_eq(tie.a, 1.0f));

  // Right of hard edge: t = 0.501 approaches Blue (0.0, 0.0, 1.0)
  const auto right = sampler->sample_at_t(0.501);
  assert(right.b > 0.95f && right.r < 0.05f);

  std::cout << "[PASS] test_hard_edge_ties\n";
}

void test_radial_geometry() {
  document::GradientPaint paint;
  paint.id = 103;
  paint.kind = document::GradientKind::Radial;
  paint.spread = document::GradientSpread::Pad;
  // Center (0, 0) in gradient space, radius 50 mm in object space
  paint.gradient_to_object = geometry::Transform2D::scale(50.0, 50.0);
  paint.stops = {
      document::GradientStop{1, 0.0, color::RGBColor{1.0, 1.0, 1.0, 1.0}, 1.0, 0.5},
      document::GradientStop{2, 1.0, color::RGBColor{0.0, 0.0, 0.0, 1.0}, 1.0, 0.5}
  };

  Camera2D camera({-0.5, 0.5}, 1.0, 200, 200);
  auto sampler = GradientSampler::prepare(paint, geometry::Transform2D::identity(), camera, nullptr, 1.0);
  assert(sampler.has_value());

  // Direct sample at t=0.0 and t=1.0
  assert(near_eq(sampler->sample_at_t(0.0).r, 1.0f));
  assert(near_eq(sampler->sample_at_t(1.0).r, 0.0f));

  // Pixel center at camera center (screen x=100.5, y=100.5) -> world (0, 0) -> q=(0, 0) -> t=0.0 (White)
  const auto center_sample = sampler->sample_straight(100, 100);
  assert(near_eq(center_sample.r, 1.0f));
  assert(near_eq(center_sample.g, 1.0f));
  assert(near_eq(center_sample.b, 1.0f));

  // Pixel at screen x=150.5, y=100.5 -> dx = +50 mm -> q=(1, 0) -> t=1.0 (Black)
  const auto rim_sample = sampler->sample_straight(150, 100);
  assert(near_eq(rim_sample.r, 0.0f));
  assert(near_eq(rim_sample.g, 0.0f));
  assert(near_eq(rim_sample.b, 0.0f));

  std::cout << "[PASS] test_radial_geometry\n";
}

void test_singular_matrix_rejected() {
  document::GradientPaint paint;
  paint.id = 104;
  paint.kind = document::GradientKind::Linear;
  paint.stops = {
      document::GradientStop{1, 0.0, color::RGBColor{1, 0, 0, 1}, 1.0, 0.5},
      document::GradientStop{2, 1.0, color::RGBColor{0, 0, 1, 1}, 1.0, 0.5}
  };

  Camera2D camera({0.0, 0.0}, 1.0, 100, 100);
  // Singular object transform (scale 0 on x)
  const auto singular_obj = geometry::Transform2D::scale(0.0, 1.0);
  auto sampler = GradientSampler::prepare(paint, singular_obj, camera, nullptr, 1.0);
  assert(!sampler.has_value()); // Rejected, no solid fallback

  std::cout << "[PASS] test_singular_matrix_rejected\n";
}

int main() {
  try {
    test_spread_modes();
    test_ac_g03_straight_alpha_and_midpoint();
    test_hard_edge_ties();
    test_radial_geometry();
    test_singular_matrix_rejected();
    std::cout << "All test_gradient_pixels checks PASSED.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Exception in test_gradient_pixels: " << e.what() << "\n";
    return 1;
  }
}
