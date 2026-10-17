#include "../../src/effects/gaussian_blur.hpp"

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

void zero_sigma_is_exact_identity() {
  const std::array input{Pixel{0.01f, 0.2f, 0.3f, 0.75f},
                         Pixel{0.25f, 0.125f, 0.0625f, 0.5f}};
  std::vector<Pixel> output{Pixel{0.0f, 0.0f, 0.0f, 0.0f}};
  const auto status = gaussian_blur_linear_srgb(input, 2, 1, 0.0, 0.0,
                                                 output);
  check(status == GaussianBlurStatus::Success,
        "zero-sigma identity was rejected");
  check(output.size() == input.size() && output[0] == input[0] &&
            output[1] == input[1],
        "zero-sigma identity did not preserve exact input values");
}

void sigma_one_matches_frozen_independent_fixture() {
  // Values are frozen from docs/quality/reference-fixtures/effects-basic.json,
  // whose Gaussian oracle is declared independent from the implementation.
  constexpr std::array<double, 9> fixture_kernel{
      0.0001338306, 0.0044318616, 0.0539911274, 0.2419714457,
      0.3989434694, 0.2419714457, 0.0539911274, 0.0044318616,
      0.0001338306};
  std::array<Pixel, 9> input{};
  input[4] = Pixel{1.0f, 1.0f, 1.0f, 1.0f};
  std::vector<Pixel> output;
  const auto status = gaussian_blur_linear_srgb(input, input.size(), 1, 1.0,
                                                 0.0, output);
  check(status == GaussianBlurStatus::Success,
        "one-axis fixture blur was rejected");
  check(output.size() == fixture_kernel.size(),
        "one-axis fixture returned wrong extent");
  for (std::size_t x = 0; x < fixture_kernel.size(); ++x) {
    close_to(output[x].a, fixture_kernel[x],
             "sigma-one impulse differs from the frozen kernel fixture");
    close_to(output[x].r, fixture_kernel[x],
             "premultiplied red differs from the frozen kernel fixture");
  }
}

void zero_axis_is_identity_and_anisotropic_weights_match_fixture() {
  constexpr std::size_t side = 9;
  std::array<Pixel, side * side> impulse{};
  impulse[4 * side + 4] = Pixel{1.0f, 1.0f, 1.0f, 1.0f};

  std::vector<Pixel> horizontal;
  auto status = gaussian_blur_linear_srgb(impulse, side, side, 1.0, 0.0,
                                           horizontal);
  check(status == GaussianBlurStatus::Success,
        "horizontal-only blur was rejected");
  for (std::size_t y = 0; y < side; ++y) {
    if (y != 4)
      for (std::size_t x = 0; x < side; ++x)
        check(horizontal[y * side + x].a == 0.0f,
              "zero vertical sigma mixed adjacent rows");
  }

  std::vector<Pixel> vertical;
  status = gaussian_blur_linear_srgb(impulse, side, side, 0.0, 1.0, vertical);
  check(status == GaussianBlurStatus::Success,
        "vertical-only blur was rejected");
  for (std::size_t x = 0; x < side; ++x) {
    if (x != 4)
      for (std::size_t y = 0; y < side; ++y)
        check(vertical[y * side + x].a == 0.0f,
              "zero horizontal sigma mixed adjacent columns");
  }

  std::vector<Pixel> anisotropic;
  status = gaussian_blur_linear_srgb(impulse, side, side, 1.0, 0.5,
                                      anisotropic);
  check(status == GaussianBlurStatus::Success,
        "anisotropic Gaussian was rejected");
  // Independent normalized sigma=.5 Y fixture is [0.00026386508,
  // 0.10645077197, 0.78657072589, ...]; multiply by the frozen sigma=1 X
  // fixture values at the corresponding offsets.
  close_to(anisotropic[5 * side + 5].a, 0.025758047190331,
           "anisotropic diagonal weight mismatch");
  close_to(anisotropic[4 * side + 6].a, 0.042467840270494,
           "anisotropic horizontal weight mismatch");
}

void transparent_edges_lose_energy_without_renormalization() {
  std::array<Pixel, 25> input{};
  input[0] = Pixel{1.0f, 1.0f, 1.0f, 1.0f};
  std::vector<Pixel> output;
  const auto status = gaussian_blur_linear_srgb(input, 5, 5, 1.0, 1.0,
                                                 output);
  check(status == GaussianBlurStatus::Success,
        "edge-energy fixture blur was rejected");
  double alpha_energy = 0.0;
  for (const auto &pixel : output)
    alpha_energy += pixel.a;
  close_to(static_cast<float>(alpha_energy), 0.489260707644227,
           "transparent edge was renormalized or energy was misplaced",
           2.0e-6);
  for (const auto &pixel : output)
    check(pixel.r <= pixel.a && pixel.g <= pixel.a && pixel.b <= pixel.a,
          "blur output violated premultiplied channel bounds");
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

void rejects_invalid_values_and_keeps_previous_output() {
  constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  constexpr float infinity = std::numeric_limits<float>::infinity();
  const Pixel sentinel{0.125f, 0.125f, 0.125f, 0.25f};
  std::vector<Pixel> output{sentinel};

  const std::array hidden_rgb{Pixel{1.0f, 0.0f, 0.0f, 0.0f}};
  auto status = gaussian_blur_linear_srgb(hidden_rgb, 1, 1, 1.0, 1.0, output);
  check(status == GaussianBlurStatus::OutOfRange && output.size() == 1 &&
            output[0] == sentinel,
        "alpha-zero hidden RGB was accepted or published");

  const std::array non_finite{Pixel{nan, 0.0f, 0.0f, 0.5f}};
  status = gaussian_blur_linear_srgb(non_finite, 1, 1, 1.0, 1.0, output);
  check(status == GaussianBlurStatus::OutOfRange && output[0] == sentinel,
        "non-finite input was accepted or changed the previous output");

  const std::array valid{Pixel{0.1f, 0.1f, 0.1f, 0.5f}};
  status = gaussian_blur_linear_srgb(valid, 1, 1, infinity, 1.0, output);
  check(status == GaussianBlurStatus::InvalidArgument &&
            output.size() == 1 && output[0] == sentinel,
        "non-finite sigma was accepted or changed the previous output");
}

void cancellation_and_budget_refusal_are_atomic() {
  const Pixel sentinel{0.125f, 0.125f, 0.125f, 0.25f};
  std::vector<Pixel> output{sentinel};
  std::array<Pixel, 64 * 64> input{};
  CancelAfterCalls cancel_state{0, 9};
  const GaussianBlurCancellation cancellation{&cancel_state,
                                               cancel_after_calls};
  auto status = gaussian_blur_linear_srgb(input, 64, 64, 1.0, 1.0, output,
                                           cancellation);
  check(status == GaussianBlurStatus::Cancelled && cancel_state.calls >= 9 &&
            output.size() == 1 && output[0] == sentinel,
        "cancelled pass published partial pixels");

  status = gaussian_blur_linear_srgb({}, 8192, 8192, 1.0, 1.0, output);
  check(status == GaussianBlurStatus::BudgetExceeded && output.size() == 1 &&
            output[0] == sentinel,
        "over-budget surface was allocated or changed the output");
}

} // namespace

int main() {
  try {
    zero_sigma_is_exact_identity();
    sigma_one_matches_frozen_independent_fixture();
    zero_axis_is_identity_and_anisotropic_weights_match_fixture();
    transparent_edges_lose_energy_without_renormalization();
    rejects_invalid_values_and_keeps_previous_output();
    cancellation_and_budget_refusal_are_atomic();
  } catch (const std::exception &error) {
    std::cerr << "Gaussian blur acceptance failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
