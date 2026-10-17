#include "../../src/effects/drop_shadow.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace pikdraw::rendering::effects;

namespace {

using Pixel = LinearPremultipliedRgba;

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void close_to(float actual, double expected, const char *message,
              double tolerance = 2.0e-7) {
  check(std::abs(static_cast<double>(actual) - expected) <= tolerance,
        message);
}

DropShadowParameters opaque_black_shadow() {
  DropShadowParameters parameters;
  parameters.color_r_linear_srgb = 0.0f;
  parameters.color_g_linear_srgb = 0.0f;
  parameters.color_b_linear_srgb = 0.0f;
  parameters.color_alpha = 1.0f;
  parameters.opacity = 1.0f;
  return parameters;
}

void sigma_zero_translates_local_positive_y_to_raster_up() {
  // Given a single source-alpha impulse and no blur, When its shadow is moved
  // by (+1,+1) local pixels, Then it appears one column right and one raster
  // row up while remaining a separate branch.
  std::array<float, 25> input{};
  input[2 * 5 + 2] = 0.75f;
  auto parameters = opaque_black_shadow();
  parameters.offset_x_px = 1.0;
  parameters.offset_y_px = 1.0;
  std::vector<Pixel> output;
  std::size_t peak_scratch_capacity_bytes = 0;

  const auto status = drop_shadow_linear_srgb(
      input, 5, 5, parameters, output, {}, &peak_scratch_capacity_bytes);
  check(status == GaussianBlurStatus::Success && output.size() == input.size(),
        "analytic integer-offset shadow was rejected");
  check(peak_scratch_capacity_bytes > 0,
        "Drop Shadow did not report its temporary vector capacity");
  for (std::size_t index = 0; index < output.size(); ++index) {
    const float expected = index == 1 * 5 + 3 ? 0.75f : 0.0f;
    close_to(output[index].a, expected,
             "shadow offset did not follow local +Y-up coordinates");
  }
}

void blurred_shadow_uses_gaussian_dispatch_and_keeps_normalized_alpha() {
  // Given an interior impulse with a unit normalized Gaussian, When Drop
  // Shadow builds its alpha branch through the SIMD dispatch, Then the branch
  // remains normalized and reports the live Gaussian/filter vector capacity.
  std::array<float, 81> input{};
  input[4 * 9 + 4] = 1.0f;
  auto parameters = opaque_black_shadow();
  parameters.sigma_x_px = 1.0;
  parameters.sigma_y_px = 1.0;
  std::vector<Pixel> output;
  std::size_t peak_scratch_capacity_bytes = 0;

  const auto status = drop_shadow_linear_srgb(
      input, 9, 9, parameters, output, {}, &peak_scratch_capacity_bytes);
  check(status == GaussianBlurStatus::Success && output.size() == input.size(),
        "blurred Drop Shadow failed through Gaussian SIMD dispatch");
  check(peak_scratch_capacity_bytes > 0,
        "blurred Drop Shadow omitted its Gaussian scratch capacity");
  double alpha_sum = 0.0;
  for (const auto &pixel : output)
    alpha_sum += pixel.a;
  close_to(static_cast<float>(alpha_sum), 1.0,
           "normalized Gaussian shadow changed total interior alpha",
           5.0e-6);
}

void fractional_offset_uses_bilinear_reference_sampling() {
  // Given an impulse away from every edge, When the shadow is translated by
  // half a pixel right and one quarter pixel up, Then the four neighboring
  // samples equal the independent bilinear products of those fractions.
  std::array<float, 25> input{};
  input[2 * 5 + 2] = 1.0f;
  auto parameters = opaque_black_shadow();
  parameters.offset_x_px = 0.5;
  parameters.offset_y_px = 0.25;
  std::vector<Pixel> output;

  const auto status = drop_shadow_linear_srgb(input, 5, 5, parameters, output);
  check(status == GaussianBlurStatus::Success,
        "fractional-offset shadow was rejected");
  close_to(output[1 * 5 + 2].a, 0.125,
           "upper-left bilinear sample has the wrong weight");
  close_to(output[1 * 5 + 3].a, 0.125,
           "upper-right bilinear sample has the wrong weight");
  close_to(output[2 * 5 + 2].a, 0.375,
           "lower-left bilinear sample has the wrong weight");
  close_to(output[2 * 5 + 3].a, 0.375,
           "lower-right bilinear sample has the wrong weight");
  double sum = 0.0;
  for (const Pixel &pixel : output)
    sum += pixel.a;
  close_to(static_cast<float>(sum), 1.0,
           "interior bilinear translation did not conserve alpha");
}

void shadow_color_alpha_and_opacity_multiply_blurred_mask() {
  // Given mask alpha .8, source tint alpha .5 and shadow opacity .25, When
  // sigma-zero rendering tints one pixel, Then alpha is .1 and RGB is the
  // unassociated linear tint multiplied by that final alpha.
  const std::array<float, 1> input{0.8f};
  auto parameters = opaque_black_shadow();
  parameters.color_r_linear_srgb = 0.5f;
  parameters.color_g_linear_srgb = 0.25f;
  parameters.color_b_linear_srgb = 1.0f;
  parameters.color_alpha = 0.5f;
  parameters.opacity = 0.25f;
  std::vector<Pixel> output;

  const auto status = drop_shadow_linear_srgb(input, 1, 1, parameters, output);
  check(status == GaussianBlurStatus::Success && output.size() == 1,
        "tinted one-pixel shadow was rejected");
  close_to(output[0].a, 0.1, "shadow alpha factors were not multiplied");
  close_to(output[0].r, 0.05,
           "red shadow channel was not premultiplied in linear sRGB");
  close_to(output[0].g, 0.025,
           "green shadow channel was not premultiplied in linear sRGB");
  close_to(output[0].b, 0.1,
           "blue shadow channel was not premultiplied in linear sRGB");
}

void samples_beyond_surface_are_transparent_without_renormalization() {
  // Given an edge impulse and a half-pixel shift toward the exterior, When
  // the shifted branch is sampled, Then the missing neighbor contributes
  // transparent zero instead of renormalizing the remaining sample.
  const std::array<float, 2> input{1.0f, 0.0f};
  auto parameters = opaque_black_shadow();
  parameters.offset_x_px = -0.5;
  std::vector<Pixel> output;

  const auto status = drop_shadow_linear_srgb(input, 2, 1, parameters, output);
  check(status == GaussianBlurStatus::Success,
        "edge exterior shadow fixture was rejected");
  close_to(output[0].a, 0.5,
           "transparent exterior was not included in bilinear sampling");
  close_to(output[1].a, 0.0,
           "out-of-bounds shadow samples were not transparent");
}

struct CancelAfterCalls final {
  int calls{};
  int cancel_at{};
};

bool cancel_after_calls(void *context) noexcept {
  auto &state = *static_cast<CancelAfterCalls *>(context);
  ++state.calls;
  return state.calls >= state.cancel_at;
}

void cancellation_and_budget_failures_leave_existing_output_untouched() {
  // Given a valid old result, When a Gaussian shadow job is cancelled or its
  // surface exceeds admission limits, Then neither failure publishes a
  // partial replacement.
  const Pixel sentinel{0.125f, 0.125f, 0.125f, 0.25f};
  std::vector<Pixel> output{sentinel};
  std::array<float, 64 * 64> input{};
  auto parameters = opaque_black_shadow();
  parameters.sigma_x_px = 1.0;
  parameters.sigma_y_px = 1.0;
  CancelAfterCalls cancel_state{0, 9};
  const GaussianBlurCancellation cancellation{&cancel_state,
                                               cancel_after_calls};

  auto status = drop_shadow_linear_srgb(input, 64, 64, parameters, output,
                                        cancellation);
  check(status == GaussianBlurStatus::Cancelled && cancel_state.calls >= 9 &&
            output.size() == 1 && output[0] == sentinel,
        "cancelled shadow published partial output");

  status = drop_shadow_linear_srgb({}, DropShadowPolicy::max_dimension,
                                   DropShadowPolicy::max_dimension, parameters,
                                   output);
  check(status == GaussianBlurStatus::BudgetExceeded && output.size() == 1 &&
            output[0] == sentinel,
        "over-budget shadow allocated or changed previous output");
}

void invalid_alpha_or_parameters_do_not_publish() {
  // Given an existing valid result, When the source mask or authored
  // translation is invalid, Then the caller retains its prior result.
  constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  const Pixel sentinel{0.125f, 0.125f, 0.125f, 0.25f};
  std::vector<Pixel> output{sentinel};
  auto parameters = opaque_black_shadow();
  std::array<float, 1> input{1.01f};

  auto status = drop_shadow_linear_srgb(input, 1, 1, parameters, output);
  check(status == GaussianBlurStatus::OutOfRange && output.size() == 1 &&
            output[0] == sentinel,
        "out-of-range source alpha changed existing output");

  input[0] = 1.0f;
  parameters.offset_x_px = nan;
  status = drop_shadow_linear_srgb(input, 1, 1, parameters, output);
  check(status == GaussianBlurStatus::InvalidArgument && output.size() == 1 &&
            output[0] == sentinel,
        "non-finite shadow offset changed existing output");
}

} // namespace

int main() {
  try {
    sigma_zero_translates_local_positive_y_to_raster_up();
    blurred_shadow_uses_gaussian_dispatch_and_keeps_normalized_alpha();
    fractional_offset_uses_bilinear_reference_sampling();
    shadow_color_alpha_and_opacity_multiply_blurred_mask();
    samples_beyond_surface_are_transparent_without_renormalization();
    cancellation_and_budget_failures_leave_existing_output_untouched();
    invalid_alpha_or_parameters_do_not_publish();
  } catch (const std::exception &error) {
    std::cerr << "Drop-shadow acceptance failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
