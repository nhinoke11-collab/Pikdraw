#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::rendering {

enum class DeviceLossStatus { Healthy, RecoverableLoss, FatalDriverCrash };

struct RenderViewContext {
  document::ObjectId owner{0};
  uint64_t generation{0};
  bool reveal_contents{true};
  bool edit_frame{false};
  bool operator==(const RenderViewContext&) const = default;
};

struct RenderDiagnostic {
  document::ObjectId object_id{document::kInvalidObjectId};
  bool error{false};
  std::string message;
};

struct RenderMetric {
  // True only when this frame was rendered by a GPU backend. A CPU fallback
  // and metrics from SoftwareRasterizer leave this false.
  bool used_gpu{false};
  uint64_t snapshot_revision{0};
  size_t total_objects{0};
  size_t culled_objects{0};
  size_t rendered_objects{0};
  std::chrono::microseconds render_duration{0};
  // Peak live vector capacity observed while a Gaussian Blur node executes.
  // Includes isolated source/filter surfaces and Gaussian temporary buffers;
  // excludes allocator metadata, the caller target, geometry, and Drop Shadow
  // internals. This is a capacity measure, not process resident memory.
  size_t peak_gaussian_blur_scratch_capacity_bytes{0};
  // Peak live vector capacity observed while a Drop Shadow node executes.
  // Includes object-local source/current surfaces, alpha conversion, shadow
  // result, and Drop Shadow/Gaussian temporary vectors; excludes allocator
  // metadata, the caller target/backdrop, and process memory.
  size_t peak_drop_shadow_scratch_capacity_bytes{0};
  // Caller must surface errors/warnings. Software renderer caps this to 65
  // entries.
  std::vector<RenderDiagnostic> diagnostics;
};

enum class GpuTargetType {
  PixelBuffer,
  GpuTexture,
  DxgiSwapChain
};

struct GpuSurfaceTarget {
  GpuTargetType type{GpuTargetType::PixelBuffer};
  // Borrowed IDXGISurface* for GpuTexture, or IDXGISwapChain1* for
  // DxgiSwapChain when consumed by D2DRenderBackend. PixelBuffer is not a GPU
  // target. Resource and renderer must share the same D3D device.
  void *native_resource{nullptr};
  int32_t width{0};
  int32_t height{0};
  float device_pixel_ratio{1.0f};
  // Optional destination viewport in DIPs inside a larger GPU surface. A zero
  // viewport_width/height selects the complete surface's D2D size.
  float viewport_x{0.0f};
  float viewport_y{0.0f};
  float viewport_width{0.0f};
  float viewport_height{0.0f};

  [[nodiscard]] bool is_valid() const noexcept {
    return native_resource != nullptr && width > 0 && height > 0;
  }
};

class IRenderBackend {
public:
  using RasterImageResolver = std::function<std::shared_ptr<const document::ImageObject>(
      const document::DocumentSnapshot&, const document::ImageObject&)>;

  virtual ~IRenderBackend() = default;

  [[nodiscard]] virtual DeviceLossStatus device_status() const noexcept = 0;
  virtual bool recover_device() = 0;
  [[nodiscard]] virtual bool is_hardware_accelerated() const noexcept { return false; }
  virtual bool set_color_preview(std::shared_ptr<const color::IccPreview>) { return false; }
  virtual bool set_canvas_background(color::DeviceRGBA) { return false; }
  // UI should not enable effect families or blend modes that the active
  // backend cannot render. Parameter budgets still surface as render
  // diagnostics; existing documents remain readable when a backend lacks a
  // particular effect family.
  [[nodiscard]] virtual bool supports_effect(
      const document::EffectParameters &parameters) const noexcept {
    (void)parameters;
    return false;
  }
  void set_raster_image_resolver(RasterImageResolver resolver) {
    raster_image_resolver_ = std::move(resolver);
  }
  virtual void set_view_context(RenderViewContext context) { view_context_ = context; }

  virtual bool render(const document::DocumentSnapshot &snapshot,
                      const Camera2D &camera, PixelBufferTarget &target,
                      RenderMetric *out_metric = nullptr) = 0;

  virtual bool render_gpu(const document::DocumentSnapshot &snapshot,
                          const Camera2D &camera, const GpuSurfaceTarget &target,
                          RenderMetric *out_metric = nullptr) {
    (void)snapshot;
    (void)camera;
    (void)target;
    (void)out_metric;
    return false;
  }
protected:
  [[nodiscard]] std::shared_ptr<const document::ImageObject> resolve_raster_image(
      const document::DocumentSnapshot& snapshot,
      const document::ImageObject& image) const {
    return raster_image_resolver_ ? raster_image_resolver_(snapshot, image) : nullptr;
  }
  RasterImageResolver raster_image_resolver_;
  RenderViewContext view_context_;
};

} // namespace pikdraw::rendering
