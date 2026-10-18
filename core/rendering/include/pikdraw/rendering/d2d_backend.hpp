#pragma once

#include "pikdraw/rendering/backend.hpp"
#include <memory>

namespace pikdraw::rendering {

/// Direct2D 1.1 / Direct3D 11 GPU Render Backend with automatic WARP fallback.
/// Hardware accelerated 2D graphics using Windows native Direct2D/Direct3D.
/// Conforms to Contract C3 (camera-relative coordinate projection) and Contract C9
/// (appearance graph effects and compositing order).
/// Falls back gracefully to SoftwareRasterizer if hardware is unavailable or resets.
class D2DRenderBackend : public IRenderBackend {
public:
  D2DRenderBackend();
  ~D2DRenderBackend() override;

  D2DRenderBackend(const D2DRenderBackend &) = delete;
  D2DRenderBackend &operator=(const D2DRenderBackend &) = delete;
  D2DRenderBackend(D2DRenderBackend &&) noexcept;
  D2DRenderBackend &operator=(D2DRenderBackend &&) noexcept;

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

  /// Returns true if rendering is currently utilizing a hardware GPU device.
  [[nodiscard]] bool is_hardware_accelerated() const noexcept override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::rendering
