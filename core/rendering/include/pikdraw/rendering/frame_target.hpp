#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cstring>

namespace pikdraw::rendering {

/// Memory buffer holding destination pixels for software rendering and framebuffers
struct PixelBufferTarget {
    uint8_t* pixels{nullptr};
    int32_t width{0};
    int32_t height{0};
    int32_t stride_bytes{0};

    [[nodiscard]] bool is_valid() const noexcept {
        return pixels != nullptr && width > 0 && height > 0 && stride_bytes >= (width * 4);
    }

    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
        if (!is_valid()) return;
        uint32_t val = (static_cast<uint32_t>(a) << 24) |
                       (static_cast<uint32_t>(b) << 16) |
                       (static_cast<uint32_t>(g) << 8)  |
                       static_cast<uint32_t>(r);
        for (int32_t y = 0; y < height; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(pixels + y * stride_bytes);
            std::fill_n(row, width, val);
        }
    }

    void set_pixel(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        uint8_t* px = pixels + y * stride_bytes + x * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = a;
    }
};

} // namespace pikdraw::rendering
