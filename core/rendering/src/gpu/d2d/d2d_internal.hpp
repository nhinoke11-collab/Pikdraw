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
      double camera_center_x,
      double camera_center_y);

private:
  struct CacheKey {
    document::ObjectId id{document::kInvalidObjectId};
    uint64_t revision{0};
    double center_x{0.0};
    double center_y{0.0};

    bool operator==(const CacheKey &other) const noexcept {
      return id == other.id && revision == other.revision &&
             std::abs(center_x - other.center_x) < 1e-4 &&
             std::abs(center_y - other.center_y) < 1e-4;
    }
  };

  struct KeyHash {
    size_t operator()(const CacheKey &k) const noexcept {
      size_t h1 = std::hash<uint64_t>{}(k.id);
      size_t h2 = std::hash<uint64_t>{}(k.revision);
      return h1 ^ (h2 << 1);
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
      double camera_zoom,
      ComPtr<ID2D1Image> &out_image,
      std::vector<RenderDiagnostic> &diagnostics);

  ComPtr<ID2D1Brush> create_brush(
      ID2D1DeviceContext *context,
      const color::ColorValue &color,
      float opacity);

  ComPtr<ID2D1Brush> create_gradient_brush(
      ID2D1DeviceContext *context,
      const document::GradientPaint &gradient,
      const Camera2D &camera,
      float opacity);
};

} // namespace pikdraw::rendering::d2d
