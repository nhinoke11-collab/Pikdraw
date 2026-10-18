#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace pikdraw::rendering {

/// Borrowed straight-alpha RGBA8 buffer or an internal premultiplied-float
/// scratch surface. The caller owns the backing storage.
struct PixelBufferTarget {
  uint8_t *pixels{nullptr};
  int32_t width{0};
  int32_t height{0};
  int32_t stride_bytes{0};
  float *premultiplied_rgba{nullptr};
  uint32_t *bgra_pixels{nullptr};

  [[nodiscard]] uint8_t *byte_pixel(int32_t x, int32_t y) const noexcept {
    auto *bytes = bgra_pixels ? reinterpret_cast<uint8_t *>(bgra_pixels) : pixels;
    return bytes + static_cast<size_t>(y) * stride_bytes + static_cast<size_t>(x) * 4;
  }
  [[nodiscard]] size_t channel_index(size_t rgba_channel) const noexcept {
    return bgra_pixels && rgba_channel < 3 ? 2 - rgba_channel : rgba_channel;
  }

  [[nodiscard]] bool is_valid() const noexcept {
    if (width <= 0 || height <= 0 || stride_bytes <= 0)
      return false;
    if (static_cast<int>(pixels != nullptr) + static_cast<int>(bgra_pixels != nullptr) +
        static_cast<int>(premultiplied_rgba != nullptr) != 1)
      return false;
    if (premultiplied_rgba)
      return pixels == nullptr && stride_bytes % sizeof(float) == 0 &&
             static_cast<int64_t>(stride_bytes) >=
                 static_cast<int64_t>(width) * 4 *
                     static_cast<int64_t>(sizeof(float)) &&
             static_cast<uint64_t>(stride_bytes) *
                     static_cast<uint64_t>(height) <=
                 static_cast<uint64_t>(
                     std::numeric_limits<std::ptrdiff_t>::max());
    return (pixels || bgra_pixels) && (!bgra_pixels || stride_bytes % 4 == 0) && static_cast<int64_t>(stride_bytes) >=
                         static_cast<int64_t>(width) * 4 &&
           static_cast<uint64_t>(stride_bytes) *
                   static_cast<uint64_t>(height) <=
               static_cast<uint64_t>(
                   std::numeric_limits<std::ptrdiff_t>::max());
  }

  void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
    if (!is_valid())
      return;
    for (int32_t y = 0; y < height; ++y) {
      for (int32_t x = 0; x < width; ++x)
        set_pixel(x, y, r, g, b, a);
    }
  }

  void set_pixel(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b,
                 uint8_t a) noexcept {
    if (!is_valid() || x < 0 || x >= width || y < 0 || y >= height)
      return;
    if (premultiplied_rgba) {
      auto *px = premultiplied_rgba +
                 static_cast<size_t>(y) *
                     static_cast<size_t>(stride_bytes / sizeof(float)) +
                 static_cast<size_t>(x) * 4;
      const float alpha = static_cast<float>(a) / 255.0f;
      px[0] = static_cast<float>(r) / 255.0f * alpha;
      px[1] = static_cast<float>(g) / 255.0f * alpha;
      px[2] = static_cast<float>(b) / 255.0f * alpha;
      px[3] = alpha;
      return;
    }
    auto *px = byte_pixel(x, y);
    px[channel_index(0)] = r;
    px[1] = g;
    px[channel_index(2)] = b;
    px[3] = a;
  }

  void blend_pixel(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t a) noexcept {
    if (!is_valid() || x < 0 || x >= width || y < 0 || y >= height || a == 0)
      return;
    if (premultiplied_rgba) {
      const float alpha = static_cast<float>(a) / 255.0f;
      blend_premultiplied_pixel(
          x, y, static_cast<float>(r) / 255.0f * alpha,
          static_cast<float>(g) / 255.0f * alpha,
          static_cast<float>(b) / 255.0f * alpha, alpha);
      return;
    }
    auto *px = byte_pixel(x, y);
    const uint32_t alpha = static_cast<uint32_t>(a) * 255 +
                           static_cast<uint32_t>(px[3]) * (255 - a);
    const uint8_t src[3]{r, g, b};
    for (size_t i = 0; i < 3; ++i) {
      const auto channel = channel_index(i);
      const uint32_t value = static_cast<uint32_t>(src[i]) * a * 255 +
                             static_cast<uint32_t>(px[channel]) * px[3] * (255 - a);
      px[channel] = static_cast<uint8_t>((value + alpha / 2) / alpha);
    }
    px[3] = static_cast<uint8_t>((alpha + 127) / 255);
  }

  /// Source-over a premultiplied device-RGB sample. This keeps intermediates
  /// in float for isolated object surfaces and converts once at the final
  /// RGBA8 target boundary.
  void blend_premultiplied_pixel(int32_t x, int32_t y, float r, float g,
                                 float b, float a) noexcept {
    if (!is_valid() || x < 0 || x >= width || y < 0 || y >= height ||
        !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) ||
        !std::isfinite(a) || a <= 0.0f)
      return;
    const float alpha = std::clamp(a, 0.0f, 1.0f);
    const float source[3]{std::clamp(r, 0.0f, alpha),
                           std::clamp(g, 0.0f, alpha),
                           std::clamp(b, 0.0f, alpha)};
    if (premultiplied_rgba) {
      auto *px = premultiplied_rgba +
                 static_cast<size_t>(y) *
                     static_cast<size_t>(stride_bytes / sizeof(float)) +
                 static_cast<size_t>(x) * 4;
      const float remaining = 1.0f - alpha;
      px[0] = source[0] + px[0] * remaining;
      px[1] = source[1] + px[1] * remaining;
      px[2] = source[2] + px[2] * remaining;
      px[3] = alpha + px[3] * remaining;
      return;
    }
    auto *px = byte_pixel(x, y);
    const float destination_alpha = static_cast<float>(px[3]) / 255.0f;
    const float remaining = 1.0f - alpha;
    const float out_alpha = alpha + destination_alpha * remaining;
    const float destination[3]{static_cast<float>(px[channel_index(0)]) / 255.0f,
                               static_cast<float>(px[1]) / 255.0f,
                               static_cast<float>(px[channel_index(2)]) / 255.0f};
    for (size_t i = 0; i < 3; ++i) {
      const float premultiplied =
          source[i] + destination[i] * destination_alpha * remaining;
      const float straight = out_alpha > 0.0f ? premultiplied / out_alpha : 0.0f;
      px[channel_index(i)] = static_cast<uint8_t>(
          std::lround(std::clamp(straight, 0.0f, 1.0f) * 255.0f));
    }
    px[3] = static_cast<uint8_t>(
        std::lround(std::clamp(out_alpha, 0.0f, 1.0f) * 255.0f));
  }
};

} // namespace pikdraw::rendering
