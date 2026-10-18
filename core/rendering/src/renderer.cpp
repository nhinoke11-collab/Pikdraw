#include "pikdraw/rendering/renderer.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include <cstdlib>
#include <exception>
#include <string_view>

namespace pikdraw::rendering {

namespace {

std::shared_ptr<IRenderBackend> create_default_backend() {
  const char *env = std::getenv("PIKDRAW_RENDER_BACKEND");
  if (env && std::string_view(env) == "cpu") {
    return std::make_shared<SoftwareRasterizer>();
  }
#if defined(_WIN32)
  auto d2d = std::make_shared<D2DRenderBackend>();
  if (d2d->device_status() == DeviceLossStatus::Healthy) {
    return d2d;
  }
#endif
  return std::make_shared<SoftwareRasterizer>();
}

} // namespace

ViewportRenderer::ViewportRenderer(std::shared_ptr<IRenderBackend> backend)
    : backend_(backend ? std::move(backend) : create_default_backend()) {}

bool ViewportRenderer::render_frame(const document::DocumentSnapshot &snapshot,
                                    PixelBufferTarget &target,
                                    RenderMetric *out_metric) {
  last_metric_ = {};
  if (out_metric)
    *out_metric = last_metric_;
  const auto fail = [&](const char *message) {
    last_metric_.diagnostics.push_back(
        {document::kInvalidObjectId, true, message});
    if (out_metric)
      *out_metric = last_metric_;
    return false;
  };
  if (!target.is_valid())
    return fail("Invalid RGBA target");

  camera_.set_viewport_size(target.width, target.height);

  bool ok = false;
  try {
    ok = backend_->render(snapshot, camera_, target, &last_metric_);
  } catch (const std::exception &) {
    return fail("Backend exception: frame incomplete");
  }
  if (!ok && backend_->device_status() == DeviceLossStatus::RecoverableLoss) {
    // Attempt recovery once per frame
    if (handle_device_loss_recovery()) {
      try {
        ok = backend_->render(snapshot, camera_, target, &last_metric_);
      } catch (const std::exception &) {
        ok = false;
      }
    }
  }

  if (out_metric) {
    *out_metric = last_metric_;
  }
  return ok;
}

bool ViewportRenderer::handle_device_loss_recovery() {
  if (!backend_)
    return false;
  try {
    return backend_->recover_device();
  } catch (const std::exception &) {
    return false;
  }
}

} // namespace pikdraw::rendering
