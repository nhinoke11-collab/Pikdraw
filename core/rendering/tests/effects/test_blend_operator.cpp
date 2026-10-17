#include "../../src/effects/blend_operator.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::rendering::effects;

namespace {

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void close_to(float actual, double expected, const char *message) {
  check(std::abs(static_cast<double>(actual) - expected) <= 1.0e-6, message);
}

void check_pixel(PremultipliedSrgbRgba actual,
                 const std::array<double, 4> &expected,
                 const char *message) {
  close_to(actual.r, expected[0], message);
  close_to(actual.g, expected[1], message);
  close_to(actual.b, expected[2], message);
  close_to(actual.a, expected[3], message);
  check(is_valid_premultiplied_srgb(actual),
        "blend output violated premultiplied RGBA invariants");
}

void opaque_reference_vectors() {
  const PremultipliedSrgbRgba backdrop{0.2f, 0.4f, 0.6f, 1.0f};
  const PremultipliedSrgbRgba source{0.8f, 0.5f, 0.25f, 1.0f};
  struct Vector {
    document::BlendMode mode;
    std::array<double, 3> expected;
  };
  constexpr std::array<Vector, 6> vectors{{
      {document::BlendMode::Normal, {0.8, 0.5, 0.25}},
      {document::BlendMode::Multiply, {0.16, 0.2, 0.15}},
      {document::BlendMode::Screen, {0.84, 0.7, 0.7}},
      {document::BlendMode::Overlay, {0.32, 0.4, 0.4}},
      {document::BlendMode::Darken, {0.2, 0.4, 0.25}},
      {document::BlendMode::Lighten, {0.8, 0.5, 0.6}},
  }};

  for (const auto &vector : vectors) {
    const auto result = blend_source_over(vector.mode, backdrop, source);
    check(result.has_value(), "opaque reference vector was rejected");
    check_pixel(*result,
                {vector.expected[0], vector.expected[1], vector.expected[2],
                 1.0},
                "opaque blend reference vector mismatch");
  }
}

void translucent_multiply_reference_vector() {
  const PremultipliedSrgbRgba backdrop{0.1f, 0.2f, 0.3f, 0.5f};
  const PremultipliedSrgbRgba source{0.4f, 0.25f, 0.125f, 0.5f};
  const auto result =
      blend_source_over(document::BlendMode::Multiply, backdrop, source);
  check(result.has_value(), "translucent Multiply vector was rejected");
  check_pixel(*result, {0.29, 0.275, 0.25, 0.75},
              "translucent Multiply reference vector mismatch");
}

void transparent_source_and_backdrop_vectors() {
  const PremultipliedSrgbRgba backdrop{0.2f, 0.4f, 0.6f, 1.0f};
  const PremultipliedSrgbRgba transparent{};
  constexpr std::array modes{
      document::BlendMode::Normal, document::BlendMode::Multiply,
      document::BlendMode::Screen, document::BlendMode::Overlay,
      document::BlendMode::Darken, document::BlendMode::Lighten};

  for (const auto mode : modes) {
    const auto unchanged = blend_source_over(mode, backdrop, transparent);
    check(unchanged.has_value(), "transparent source was rejected");
    check_pixel(*unchanged, {0.2, 0.4, 0.6, 1.0},
                "transparent source changed the backdrop");
  }

  const PremultipliedSrgbRgba translucent_source{0.2f, 0.1f, 0.05f, 0.5f};
  const auto source_over_clear = blend_source_over(
      document::BlendMode::Multiply, transparent, translucent_source);
  check(source_over_clear.has_value(), "transparent backdrop was rejected");
  check_pixel(*source_over_clear, {0.2, 0.1, 0.05, 0.5},
              "transparent backdrop did not preserve source");
}

void rejects_invalid_pixels_and_unknown_modes() {
  constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  constexpr float infinity = std::numeric_limits<float>::infinity();
  const PremultipliedSrgbRgba valid{0.1f, 0.2f, 0.3f, 0.5f};
  constexpr std::array invalid_pixels{
      PremultipliedSrgbRgba{-0.01f, 0.0f, 0.0f, 0.5f},
      PremultipliedSrgbRgba{0.6f, 0.0f, 0.0f, 0.5f},
      PremultipliedSrgbRgba{0.0f, 0.0f, 0.0f, -0.01f},
      PremultipliedSrgbRgba{0.0f, 0.0f, 0.0f, 1.01f},
      PremultipliedSrgbRgba{1.01f, 0.0f, 0.0f, 1.0f},
      PremultipliedSrgbRgba{nan, 0.0f, 0.0f, 0.5f},
      PremultipliedSrgbRgba{0.0f, infinity, 0.0f, 0.5f},
      PremultipliedSrgbRgba{0.0f, 0.0f, 0.0f, nan},
      PremultipliedSrgbRgba{0.0f, 0.0f, 0.0f, infinity},
  };

  for (const auto invalid : invalid_pixels) {
    check(!is_valid_premultiplied_srgb(invalid),
          "invalid sample passed premultiplied validation");
    check(!blend_source_over(document::BlendMode::Normal, valid, invalid),
          "invalid source sample was accepted");
    check(!blend_source_over(document::BlendMode::Normal, invalid, valid),
          "invalid backdrop sample was accepted");
  }

  const auto unknown = static_cast<document::BlendMode>(255);
  check(!blend_source_over(unknown, valid, valid),
        "unknown blend mode was accepted");
}

} // namespace

int main() {
  try {
    opaque_reference_vectors();
    translucent_multiply_reference_vector();
    transparent_source_and_backdrop_vectors();
    rejects_invalid_pixels_and_unknown_modes();
  } catch (const std::exception &error) {
    std::cerr << "Blend operator acceptance failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
