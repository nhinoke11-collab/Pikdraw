#include "pikdraw/rendering/skia_backend.hpp"
#include "skia_internal.hpp"
#include <chrono>
#include <cstdlib>

namespace pikdraw::rendering {

struct SkiaRenderBackend::Impl {
  skia::SkiaContextD3D12 skia_context;
  skia::SkiaEffectsCompositor effects_compositor;
  D2DRenderBackend d2d_backend;
  SoftwareRasterizer software_rasterizer;
  color::DeviceRGBA background{255, 255, 255, 255};
  std::shared_ptr<const color::IccPreview> color_preview;

  Impl() {
    skia_context.initialize();
  }

  bool is_skia_active() const noexcept {
#if defined(PIKDRAW_HAS_SKIA)
    const char *env = std::getenv("PIKDRAW_RENDER_BACKEND");
    if (env && std::string_view(env) == "cpu") {
      return false;
    }
    return skia_context.is_valid();
#else
    return false;
#endif
  }

  bool render(const document::DocumentSnapshot &snapshot,
              const Camera2D &camera, PixelBufferTarget &target,
              RenderMetric *out_metric) {
    // When pixel buffer target has floating-point scratch or CPU fallback requested,
    // evaluate via the Golden Reference SoftwareRasterizer or D2D.
    if (target.premultiplied_rgba != nullptr) {
      return software_rasterizer.render(snapshot, camera, target, out_metric);
    }

    // Try hardware D2D backend or software rasterizer fallback
    if (d2d_backend.render(snapshot, camera, target, out_metric)) {
      return true;
    }

    return software_rasterizer.render(snapshot, camera, target, out_metric);
  }

  bool render_gpu(const document::DocumentSnapshot &snapshot,
                  const Camera2D &camera, const GpuSurfaceTarget &target,
                  RenderMetric *out_metric) {
    if (!target.is_valid())
      return false;

    // Delegate to D2D GPU pipeline for D3D11 / DXGI surfaces
    return d2d_backend.render_gpu(snapshot, camera, target, out_metric);
  }
};

SkiaRenderBackend::SkiaRenderBackend() : impl_(std::make_unique<Impl>()) {}
SkiaRenderBackend::~SkiaRenderBackend() = default;
SkiaRenderBackend::SkiaRenderBackend(SkiaRenderBackend &&) noexcept = default;
SkiaRenderBackend &SkiaRenderBackend::operator=(SkiaRenderBackend &&) noexcept = default;

DeviceLossStatus SkiaRenderBackend::device_status() const noexcept {
  return impl_->skia_context.status();
}

bool SkiaRenderBackend::recover_device() {
  return impl_->skia_context.recover() && impl_->d2d_backend.recover_device();
}

bool SkiaRenderBackend::set_color_preview(
    std::shared_ptr<const color::IccPreview> preview) {
  impl_->color_preview = preview;
  return impl_->d2d_backend.set_color_preview(preview) ||
         impl_->software_rasterizer.set_color_preview(preview);
}

bool SkiaRenderBackend::set_canvas_background(color::DeviceRGBA background) {
  impl_->background = background;
  return impl_->d2d_backend.set_canvas_background(background) ||
         impl_->software_rasterizer.set_canvas_background(background);
}

bool SkiaRenderBackend::is_skia_available() const noexcept {
  return impl_->is_skia_active();
}

bool SkiaRenderBackend::render(const document::DocumentSnapshot &snapshot,
                              const Camera2D &camera, PixelBufferTarget &target,
                              RenderMetric *out_metric) {
  return impl_->render(snapshot, camera, target, out_metric);
}

bool SkiaRenderBackend::render_gpu(const document::DocumentSnapshot &snapshot,
                                  const Camera2D &camera,
                                  const GpuSurfaceTarget &target,
                                  RenderMetric *out_metric) {
  return impl_->render_gpu(snapshot, camera, target, out_metric);
}

} // namespace pikdraw::rendering
