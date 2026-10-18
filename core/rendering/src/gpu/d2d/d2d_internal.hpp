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

// Per-device image uploads, bounded to 64 images / 256 MiB.
class D2DImageCache {
public:
  void clear() { images_.clear(); bytes_ = 0; }
  ComPtr<ID2D1Bitmap1> get(ID2D1DeviceContext* context,
                          const document::ImageObject& image, uint64_t revision) {
    if (revision_ != revision) { clear(); revision_ = revision; }
    if (const auto it = images_.find(image.id); it != images_.end()) return it->second;
    constexpr size_t budget = 256ULL * 1024 * 1024;
    const size_t bytes = image.pixels_rgba.size() * sizeof(uint32_t);
    if (bytes > budget) return nullptr;
    if (images_.size() >= 64 || bytes_ > budget - bytes) clear();
    std::vector<uint32_t> bgra(image.pixels_rgba.size());
    for (int32_t y = 0; y < image.height_px; ++y) {
      for (int32_t x = 0; x < image.width_px; ++x) {
        const auto rgba = image.pixels_rgba[
            static_cast<size_t>(image.height_px - 1 - y) * image.width_px + x];
        const uint32_t a = rgba & 255;
        const uint32_t r = ((rgba >> 24) & 255) * a / 255;
        const uint32_t g = ((rgba >> 16) & 255) * a / 255;
        const uint32_t b = ((rgba >> 8) & 255) * a / 255;
        bgra[static_cast<size_t>(y) * image.width_px + x] =
            (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    props.dpiX = props.dpiY = 96;
    ComPtr<ID2D1Bitmap1> bitmap;
    if (FAILED(context->CreateBitmap(D2D1::SizeU(image.width_px, image.height_px),
        bgra.data(), image.width_px * 4, &props, bitmap.ReleaseAndGetAddressOf())))
      return nullptr;
    images_.emplace(image.id, bitmap);
    bytes_ += bytes;
    return bitmap;
  }
private:
  std::unordered_map<document::ObjectId, ComPtr<ID2D1Bitmap1>> images_;
  uint64_t revision_{};
  size_t bytes_{};
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
