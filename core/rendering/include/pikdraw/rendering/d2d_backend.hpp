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
  [[nodiscard]] bool supports_effect(
      const document::EffectParameters &parameters) const noexcept override;
  bool set_color_preview(std::shared_ptr<const color::IccPreview> preview) override;
  bool set_canvas_background(color::DeviceRGBA background) override;

  bool render(const document::DocumentSnapshot &snapshot,
              const Camera2D &camera, PixelBufferTarget &target,
              RenderMetric *out_metric = nullptr) override;

  bool render_gpu(const document::DocumentSnapshot &snapshot,
                  const Camera2D &camera, const GpuSurfaceTarget &target,
                  RenderMetric *out_metric = nullptr) override;

  /// Creates a swap chain on this backend's D3D device for a viewport HWND.
  /// The target is borrowed and remains valid until resize, recovery, or the
  /// backend is destroyed. Calls must stay on the backend's render thread.
  bool create_swap_chain_target(void *hwnd, int32_t width, int32_t height,
                                float device_pixel_ratio = 1.0f);
  [[nodiscard]] GpuSurfaceTarget swap_chain_target() const noexcept;
  void release_swap_chain_target() noexcept;
  bool resize_swap_chain_target(int32_t width, int32_t height);
  bool present_swap_chain_target(uint32_t sync_interval = 1);
  /// Acquires/releases the swap-chain back buffer for GDI overlay composition.
  /// D2D rendering and Present are invalid while the DC is held.
  [[nodiscard]] void *acquire_swap_chain_target_dc();
  bool release_swap_chain_target_dc();
  /// Explicit synchronous readback for pixel-oracle/screenshot use only.
  /// Call after render_gpu and before present to capture the drawn back buffer.
  bool capture_swap_chain_target(PixelBufferTarget &target);

  /// Returns true if rendering is currently utilizing a hardware GPU device.
  [[nodiscard]] bool is_hardware_accelerated() const noexcept override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::rendering
