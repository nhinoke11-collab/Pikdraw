#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace pikdraw::rendering {

/// Borrowed straight-alpha RGBA8 buffer. The caller owns stride * height bytes.
struct PixelBufferTarget {
  uint8_t *pixels{nullptr};
  int32_t width{0};
  int32_t height{0};
  int32_t stride_bytes{0};

  [[nodiscard]] bool is_valid() const noexcept {
    return pixels && width > 0 && height > 0 && stride_bytes > 0 &&
           static_cast<int64_t>(stride_bytes) >=
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
    auto *px = pixels +
               static_cast<size_t>(y) * static_cast<size_t>(stride_bytes) +
               static_cast<size_t>(x) * 4;
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = a;
  }

  void blend_pixel(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t a) noexcept {
    if (!is_valid() || x < 0 || x >= width || y < 0 || y >= height || a == 0)
      return;
    auto *px = pixels +
               static_cast<size_t>(y) * static_cast<size_t>(stride_bytes) +
               static_cast<size_t>(x) * 4;
    const uint32_t alpha = static_cast<uint32_t>(a) * 255 +
                           static_cast<uint32_t>(px[3]) * (255 - a);
    const uint8_t src[3]{r, g, b};
    for (size_t i = 0; i < 3; ++i) {
      const uint32_t value = static_cast<uint32_t>(src[i]) * a * 255 +
                             static_cast<uint32_t>(px[i]) * px[3] * (255 - a);
      px[i] = static_cast<uint8_t>((value + alpha / 2) / alpha);
    }
    px[3] = static_cast<uint8_t>((alpha + 127) / 255);
  }
};

} // namespace pikdraw::rendering
