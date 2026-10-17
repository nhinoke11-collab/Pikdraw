#pragma once

#include "pikdraw/rendering/backend.hpp"
#include <memory>

namespace pikdraw::rendering {

/// Skia GPU (Ganesh / Graphite) Render Backend.
/// Industrial 2D rendering engine with D3D12/Vulkan GPU acceleration,
/// custom SkSL ICC color correction shaders, and sub-pixel anti-aliasing.
/// Conforms to Contract C3 (camera-relative coordinate projection) and Contract C9
/// (appearance graph effects and compositing order).
/// Falls back gracefully to D2DRenderBackend or SoftwareRasterizer if GPU device is unavailable.
class SkiaRenderBackend : public IRenderBackend {
public:
  SkiaRenderBackend();
  ~SkiaRenderBackend() override;

  SkiaRenderBackend(const SkiaRenderBackend &) = delete;
  SkiaRenderBackend &operator=(const SkiaRenderBackend &) = delete;
  SkiaRenderBackend(SkiaRenderBackend &&) noexcept;
  SkiaRenderBackend &operator=(SkiaRenderBackend &&) noexcept;

  [[nodiscard]] DeviceLossStatus device_status() const noexcept override;
  bool recover_device() override;
  bool set_color_preview(std::shared_ptr<const color::IccPreview> preview) override;
  bool set_canvas_background(color::DeviceRGBA background) override;

  bool render(const document::DocumentSnapshot &snapshot,
              const Camera2D &camera, PixelBufferTarget &target,
              RenderMetric *out_metric = nullptr) override;

  bool render_gpu(const document::DocumentSnapshot &snapshot,
                  const Camera2D &camera, const GpuSurfaceTarget &target,
                  RenderMetric *out_metric = nullptr) override;

  /// Returns true if native Skia GPU pipeline is initialized and active.
  [[nodiscard]] bool is_skia_available() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::rendering
