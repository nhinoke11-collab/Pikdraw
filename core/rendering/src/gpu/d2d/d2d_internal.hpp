#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/rendering/backend.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pikdraw::rendering::d2d {

using Microsoft::WRL::ComPtr;

inline D2D1_POINT_2F camera_relative_point(double world_x, double world_y,
                                           double camera_center_x,
                                           double camera_center_y) noexcept {
  return D2D1::Point2F(static_cast<float>(world_x - camera_center_x),
                       static_cast<float>(world_y - camera_center_y));
}

inline D2D1_COLOR_F to_d2d_color(float r, float g, float b, float a = 1.0f) noexcept {
  return D2D1::ColorF(r, g, b, a);
}

struct ImageCacheKey {
  const uint32_t* pixels{};
  int32_t width_px{};
  int32_t height_px{};
  bool operator==(const ImageCacheKey&) const noexcept = default;
};

struct ImageCacheKeyHash {
  size_t operator()(const ImageCacheKey& key) const noexcept {
    size_t value = std::hash<const uint32_t*>{}(key.pixels);
    value ^= std::hash<int32_t>{}(key.width_px) + 0x9e3779b9u +
             (value << 6) + (value >> 2);
    value ^= std::hash<int32_t>{}(key.height_px) + 0x9e3779b9u +
             (value << 6) + (value >> 2);
    return value;
  }
};

// Per-device LRU, keyed by immutable pixel storage rather than document
// revision: moving an image, Undo or editing a path does not re-upload pixels.
class D2DImageCache {
public:
  explicit D2DImageCache(size_t max_bytes = 256u * 1024u * 1024u,
                         size_t max_images = 1024)
      : max_bytes_(max_bytes), max_images_(max_images) {}
  void clear() { images_.clear(); cached_bytes_ = 0; }
  void begin_frame() noexcept { memory_pressure_encountered_ = false; }
  [[nodiscard]] bool memory_pressure_encountered() const noexcept {
    return memory_pressure_encountered_;
  }
  [[nodiscard]] size_t cached_image_count() const noexcept { return images_.size(); }
  [[nodiscard]] size_t cached_bytes() const noexcept { return cached_bytes_; }
  [[nodiscard]] uint64_t upload_count() const noexcept { return upload_count_; }

  ComPtr<ID2D1Bitmap1> get(ID2D1DeviceContext* context,
                          const document::ImageObject& image, uint64_t /*revision*/) {
    if (!context || image.width_px <= 0 || image.height_px <= 0) return nullptr;
    const auto pixels = image.rgba_pixels();
    const size_t expected_pixels = static_cast<size_t>(image.width_px) *
                                   static_cast<size_t>(image.height_px);
    if (pixels.empty() || pixels.size() != expected_pixels ||
        pixels.size() > std::numeric_limits<size_t>::max() / sizeof(uint32_t))
      return nullptr;
    const ImageCacheKey key{pixels.data(), image.width_px, image.height_px};
    if (auto it = images_.find(key); it != images_.end()) {
      // Inline legacy buffers are mutable. Compare against the retained copy
      // so same-address reuse or in-place edits never return stale colors.
      if ((image.shared_pixels_rgba && image.shared_pixels_rgba == it->second.owner) ||
          std::equal(pixels.begin(), pixels.end(), it->second.owner->begin())) {
        it->second.last_use = ++clock_;
        return it->second.bitmap;
      }
      cached_bytes_ -= it->second.bytes;
      images_.erase(it);
    }

    const size_t bytes = pixels.size() * sizeof(uint32_t);
    if (bytes > std::numeric_limits<size_t>::max() / 3) return nullptr;
    const size_t retained_bytes = bytes * (image.shared_pixels_rgba ? 1u : 2u);
    const bool cacheable = retained_bytes <= max_bytes_ && max_images_ != 0;
    if (cacheable) {
      while (!images_.empty() &&
             (images_.size() >= max_images_ || cached_bytes_ > max_bytes_ - retained_bytes))
        evict_oldest();
    }
    // Account for both the temporary premultiplied upload buffer and the
    // persistent Direct2D bitmap when deciding whether this allocation fits.
    while (!has_system_memory_headroom(bytes * 3) && !images_.empty()) evict_oldest();
    if (!has_system_memory_headroom(bytes * 3)) {
      memory_pressure_encountered_ = true;
      return nullptr;
    }

    std::vector<uint32_t> bgra;
    try {
      bgra.resize(pixels.size());
    } catch (...) {
      memory_pressure_encountered_ = true;
      return nullptr;
    }
    for (int32_t y = 0; y < image.height_px; ++y) {
      for (int32_t x = 0; x < image.width_px; ++x) {
        const auto rgba = pixels[
            static_cast<size_t>(image.height_px - 1 - y) * image.width_px + x];
        const uint32_t a = rgba & 255;
        const uint32_t r = (((rgba >> 24) & 255) * a + 127) / 255;
        const uint32_t g = (((rgba >> 16) & 255) * a + 127) / 255;
        const uint32_t b = (((rgba >> 8) & 255) * a + 127) / 255;
        bgra[static_cast<size_t>(y) * image.width_px + x] =
            (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    props.dpiX = props.dpiY = 96;
    ComPtr<ID2D1Bitmap1> bitmap;
    if (FAILED(context->CreateBitmap(D2D1::SizeU(image.width_px, image.height_px),
        bgra.data(), image.width_px * 4, &props, bitmap.ReleaseAndGetAddressOf()))) {
      memory_pressure_encountered_ = true;
      return nullptr;
    }
    ++upload_count_;
    if (cacheable) {
      auto owner = image.shared_pixels_rgba;
      if (!owner) owner = std::make_shared<const std::vector<uint32_t>>(
          pixels.begin(), pixels.end());
      images_.emplace(key, Entry{bitmap, std::move(owner), retained_bytes, ++clock_});
      cached_bytes_ += retained_bytes;
    }
    return bitmap;
  }
private:
  bool memory_pressure_encountered_{false};
  struct Entry {
    ComPtr<ID2D1Bitmap1> bitmap;
    // Keeps the key's immutable storage alive across snapshot destruction.
    std::shared_ptr<const std::vector<uint32_t>> owner;
    size_t bytes{};
    uint64_t last_use{};
  };
  void evict_oldest() {
    const auto oldest = std::min_element(images_.begin(), images_.end(),
        [](const auto& lhs, const auto& rhs) {
          return lhs.second.last_use < rhs.second.last_use;
        });
    cached_bytes_ -= oldest->second.bytes;
    images_.erase(oldest);
  }
  static bool has_system_memory_headroom(size_t additional_bytes) noexcept {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status) || status.ullTotalPhys == 0 ||
        status.ullAvailPhys > status.ullTotalPhys) return false;
    constexpr uint64_t limit_percent = 95;
    const uint64_t threshold = (status.ullTotalPhys / 100) * limit_percent +
        ((status.ullTotalPhys % 100) * limit_percent) / 100;
    const uint64_t used = status.ullTotalPhys - status.ullAvailPhys;
    return used < threshold && static_cast<uint64_t>(additional_bytes) < threshold - used;
  }

  std::unordered_map<ImageCacheKey, Entry, ImageCacheKeyHash> images_;
  size_t max_bytes_{};
  size_t max_images_{};
  size_t cached_bytes_{};
  uint64_t clock_{};
  uint64_t upload_count_{};
};

class D2DDeviceManager {
public:
  D2DDeviceManager();
  ~D2DDeviceManager();

  bool initialize();
  void shutdown();
  bool recover();

  [[nodiscard]] bool is_valid() const noexcept;
  [[nodiscard]] bool is_hardware() const noexcept { return is_hardware_; }
  [[nodiscard]] DeviceLossStatus status() const noexcept;

  [[nodiscard]] ID2D1Factory1 *factory() const noexcept { return d2d_factory_.Get(); }
  [[nodiscard]] ID2D1Device *d2d_device() const noexcept { return d2d_device_.Get(); }
  [[nodiscard]] ID2D1DeviceContext *context() const noexcept { return d2d_context_.Get(); }
  [[nodiscard]] ID3D11Device *d3d_device() const noexcept { return d3d_device_.Get(); }
  [[nodiscard]] ID3D11DeviceContext *d3d_context() const noexcept { return d3d_context_.Get(); }
  [[nodiscard]] IDWriteFactory *dwrite_factory() const noexcept { return dwrite_factory_.Get(); }

private:
  bool create_d3d_device(D3D_DRIVER_TYPE driver_type);
  bool create_d2d_device();

  ComPtr<ID3D11Device> d3d_device_;
  ComPtr<ID3D11DeviceContext> d3d_context_;
  ComPtr<IDXGIDevice> dxgi_device_;
  ComPtr<ID2D1Factory1> d2d_factory_;
  ComPtr<ID2D1Device> d2d_device_;
  ComPtr<ID2D1DeviceContext> d2d_context_;
  ComPtr<IDWriteFactory> dwrite_factory_;
  bool is_hardware_{false};
  DeviceLossStatus status_{DeviceLossStatus::Healthy};
};

class D2DGeometryCache {
public:
  D2DGeometryCache();
  ~D2DGeometryCache();

  void clear();
  void evict_stale(uint64_t current_revision);

  ComPtr<ID2D1PathGeometry> get_or_create_path(
      ID2D1Factory1 *factory,
      document::ObjectId id,
      uint64_t revision,
      const geometry::Path2D &path,
      uint8_t kind = 0);

private:
  struct CacheKey {
    document::ObjectId id{document::kInvalidObjectId};
    uint64_t revision{0};
    uint8_t kind{0};
    bool even_odd{false};

    bool operator==(const CacheKey &other) const noexcept {
      return id == other.id && revision == other.revision &&
             kind == other.kind && even_odd == other.even_odd;
    }
  };

  struct KeyHash {
    size_t operator()(const CacheKey &k) const noexcept {
      size_t h1 = std::hash<uint64_t>{}(k.id);
      size_t h2 = std::hash<uint64_t>{}(k.revision);
      size_t result = h1 ^ (h2 << 1);
      result ^= std::hash<uint8_t>{}(k.kind) + (result << 6) + (result >> 2);
      return result ^ (std::hash<bool>{}(k.even_odd) + (result << 6) + (result >> 2));
    }
  };

  std::unordered_map<CacheKey, ComPtr<ID2D1PathGeometry>, KeyHash> cache_;
  uint64_t last_revision_{0};
};

class D2DEffectsPipeline {
public:
  D2DEffectsPipeline();
  ~D2DEffectsPipeline();

  bool apply_effects(
      ID2D1DeviceContext *context,
      ID2D1Image *source_image,
      const document::Appearance &appearance,
      document::ObjectId object_id,
      double camera_zoom,
      ComPtr<ID2D1Image> &out_image,
      std::vector<RenderDiagnostic> &diagnostics,
      const color::IccPreview *color_preview = nullptr);

  bool apply_document_effects(ID2D1DeviceContext* context, ID2D1Image* source,
      const document::Appearance& appearance, document::ObjectId id,
      double viewport_density, int32_t raster_dpi, ComPtr<ID2D1Image>& output,
      std::vector<RenderDiagnostic>& diagnostics, const color::IccPreview* preview);

  ComPtr<ID2D1Brush> create_brush(
      ID2D1DeviceContext *context,
      const color::ColorValue &color,
      float opacity,
      const color::IccPreview *color_preview = nullptr);

  ComPtr<ID2D1Brush> create_gradient_brush(
      ID2D1DeviceContext *context,
      const document::GradientPaint &gradient,
      float opacity,
      const color::IccPreview *color_preview = nullptr);
};

} // namespace pikdraw::rendering::d2d
