#include "../../src/effects/gaussian_blur.hpp"
#include "../../src/effects/gaussian_blur_simd.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace pikdraw::rendering::effects;

namespace {

using Pixel = LinearPremultipliedRgba;

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::vector<Pixel> make_pixels(std::size_t count, std::uint32_t seed) {
  std::mt19937 generator(seed);
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  std::vector<Pixel> pixels(count);
  for (auto &pixel : pixels) {
    pixel.a = unit(generator);
    pixel.r = unit(generator) * pixel.a;
    pixel.g = unit(generator) * pixel.a;
    pixel.b = unit(generator) * pixel.a;
  }
  return pixels;
}

void compare_against_scalar_reference() {
  constexpr std::size_t width = 73;
  constexpr std::size_t height = 41;
  const auto input = make_pixels(width * height, 0x4a17u);
  const bool avx_available = gaussian_blur_avx_runtime_supported();
  constexpr std::array<std::array<double, 2>, 8> sigmas{{
      {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {0.5, 0.75},
      {1.0, 0.5}, {1.5, 1.5}, {5.75, 3.125}, {16.0, 16.0}}};
  for (const auto &sigma : sigmas) {
    std::vector<Pixel> reference;
    std::vector<Pixel> accelerated;
    const auto reference_status = gaussian_blur_linear_srgb(
        input, width, height, sigma[0], sigma[1], reference);
    const auto accelerated_status = gaussian_blur_linear_srgb_sse2(
        input, width, height, sigma[0], sigma[1], accelerated);
    check(reference_status == GaussianBlurStatus::Success &&
              accelerated_status == reference_status &&
              accelerated.size() == reference.size(),
          "SIMD blur disagreed with the reference status or extent");
    double max_error = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index) {
      const std::array<double, 4> left{reference[index].r, reference[index].g,
                                       reference[index].b, reference[index].a};
      const std::array<double, 4> right{accelerated[index].r,
                                        accelerated[index].g,
                                        accelerated[index].b,
                                        accelerated[index].a};
      for (std::size_t channel = 0; channel < left.size(); ++channel)
        max_error = std::max(max_error, std::abs(left[channel] - right[channel]));
      check(accelerated[index].r >= 0.0f &&
                accelerated[index].r <= accelerated[index].a &&
                accelerated[index].g >= 0.0f &&
                accelerated[index].g <= accelerated[index].a &&
                accelerated[index].b >= 0.0f &&
                accelerated[index].b <= accelerated[index].a &&
                accelerated[index].a >= 0.0f &&
                accelerated[index].a <= 1.0f,
            "SIMD blur violated premultiplied channel bounds");
    }
    check(max_error <= 4.0e-6,
          "SIMD blur exceeded the documented numerical tolerance");
  }
  // The common accelerated entry point ran every nonzero sigma above. This
  // report makes it clear whether this host exercised AVX or the SSE2 fallback.
  std::cout << "Gaussian AVX runtime dispatch: "
            << (avx_available ? "enabled" : "SSE2 fallback") << '\n';
}

void compare_small_shapes_to_scalar() {
  constexpr std::array<std::array<std::size_t, 2>, 7> shapes{{
      {1, 1}, {2, 3}, {9, 3}, {3, 9}, {17, 12}, {65, 3}, {3, 65}}};
  constexpr std::array<std::array<double, 2>, 4> sigmas{{
      {0.5, 0.75}, {1.5, 1.0}, {5.75, 3.125}, {16.0, 16.0}}};
  for (std::size_t shape_index = 0; shape_index < shapes.size();
       ++shape_index) {
    const std::size_t width = shapes[shape_index][0];
    const std::size_t height = shapes[shape_index][1];
    const auto input = make_pixels(
        width * height, static_cast<std::uint32_t>(0x6a00u + shape_index));
    for (const auto &sigma : sigmas) {
      std::vector<Pixel> reference;
      std::vector<Pixel> accelerated;
      const auto reference_status = gaussian_blur_linear_srgb(
          input, width, height, sigma[0], sigma[1], reference);
      const auto accelerated_status = gaussian_blur_linear_srgb_sse2(
          input, width, height, sigma[0], sigma[1], accelerated);
      check(reference_status == GaussianBlurStatus::Success &&
                accelerated_status == reference_status &&
                accelerated.size() == reference.size(),
            "SIMD small-surface status or extent disagreed with the reference");
      double max_error = 0.0;
      for (std::size_t index = 0; index < reference.size(); ++index) {
        const std::array<double, 4> left{reference[index].r, reference[index].g,
                                         reference[index].b, reference[index].a};
        const std::array<double, 4> right{accelerated[index].r,
                                          accelerated[index].g,
                                          accelerated[index].b,
                                          accelerated[index].a};
        for (std::size_t channel = 0; channel < left.size(); ++channel)
          max_error = std::max(max_error,
                               std::abs(left[channel] - right[channel]));
      }
      check(max_error <= 4.0e-6,
            "SIMD small-surface blur exceeded the scalar tolerance");
    }
  }
}

void edge_energy_and_identity_are_preserved() {
  std::array<Pixel, 25> impulse{};
  impulse[0] = Pixel{1.0f, 0.5f, 0.25f, 1.0f};
  std::vector<Pixel> blurred;
  auto status = gaussian_blur_linear_srgb_sse2(
      impulse, 5, 5, 1.0, 1.0, blurred);
  check(status == GaussianBlurStatus::Success,
        "SIMD edge impulse blur failed");
  double energy = 0.0;
  for (const auto &pixel : blurred)
    energy += pixel.a;
  check(std::abs(energy - 0.489260707644227) <= 2.5e-6,
        "SIMD edge blur renormalized transparent samples");

  const auto input = make_pixels(24, 0x129u);
  std::vector<Pixel> identity;
  check(gaussian_blur_linear_srgb_sse2(input, 6, 4, 0.0, 0.0, identity) ==
                GaussianBlurStatus::Success &&
            identity == input,
        "zero-sigma SIMD path did not preserve the exact input");
}

void reports_peak_internal_vector_capacity() {
  constexpr std::size_t width = 17;
  constexpr std::size_t height = 12;
  const auto input = make_pixels(width * height, 0x6b19u);
  std::vector<Pixel> output;
  std::size_t peak_capacity_bytes = 0;
  auto status = gaussian_blur_linear_srgb_sse2(
      input, width, height, 1.5, 1.25, output, {}, &peak_capacity_bytes);
  check(status == GaussianBlurStatus::Success,
        "Gaussian scratch telemetry case did not render");
  check(peak_capacity_bytes >= 2 * input.size() * sizeof(Pixel),
        "Gaussian telemetry omitted its two live convolution surfaces");
  check(peak_capacity_bytes <= GaussianBlurPolicy::max_scratch_bytes,
        "Gaussian telemetry exceeded the admitted scratch budget");

  const auto previous_peak = peak_capacity_bytes;
  status = gaussian_blur_linear_srgb_sse2(
      input, width, height, 0.0, 0.0, output, {}, &peak_capacity_bytes);
  check(status == GaussianBlurStatus::Success &&
            peak_capacity_bytes >= input.size() * sizeof(Pixel) &&
            peak_capacity_bytes < previous_peak,
        "zero-sigma telemetry did not reset and report one staging surface");
}

bool cancel_callback(void *context) noexcept {
  return *static_cast<bool *>(context);
}

void rejection_and_cancellation_are_atomic() {
  const Pixel sentinel{0.125f, 0.0625f, 0.03125f, 0.25f};
  std::vector<Pixel> output{sentinel};
  constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  const std::array invalid{Pixel{nan, 0.0f, 0.0f, 0.5f}};
  check(gaussian_blur_linear_srgb_sse2(invalid, 1, 1, 1.0, 1.0, output) ==
                GaussianBlurStatus::OutOfRange &&
            output.size() == 1 && output.front() == sentinel,
        "SIMD invalid input changed the caller output");

  bool cancel = true;
  const GaussianBlurCancellation cancellation{&cancel, cancel_callback};
  const auto valid = make_pixels(64 * 64, 0x53u);
  check(gaussian_blur_linear_srgb_sse2(valid, 64, 64, 1.0, 1.0, output,
                                       cancellation) ==
                GaussianBlurStatus::Cancelled &&
            output.size() == 1 && output.front() == sentinel,
        "SIMD cancellation published a partial surface");
}

} // namespace

int main() {
  try {
    compare_against_scalar_reference();
    compare_small_shapes_to_scalar();
    edge_energy_and_identity_are_preserved();
    reports_peak_internal_vector_capacity();
    rejection_and_cancellation_are_atomic();
  } catch (const std::exception &error) {
    std::cerr << "Gaussian SIMD acceptance failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
