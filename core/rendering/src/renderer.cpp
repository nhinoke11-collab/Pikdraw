#include "pikdraw/rendering/renderer.hpp"

namespace pikdraw::rendering {

ViewportRenderer::ViewportRenderer(std::shared_ptr<IRenderBackend> backend)
    : backend_(backend ? std::move(backend) : std::make_shared<SoftwareRasterizer>()) {}

bool ViewportRenderer::render_frame(
    const document::DocumentSnapshot& snapshot,
    PixelBufferTarget& target,
    RenderMetric* out_metric) {
    if (!target.is_valid()) return false;

    camera_.set_viewport_size(target.width, target.height);

    bool ok = backend_->render(snapshot, camera_, target, &last_metric_);
    if (!ok && backend_->device_status() == DeviceLossStatus::RecoverableLoss) {
        // Attempt recovery once per frame
        if (handle_device_loss_recovery()) {
            ok = backend_->render(snapshot, camera_, target, &last_metric_);
        }
    }

    if (out_metric) {
        *out_metric = last_metric_;
    }
    return ok;
}

bool ViewportRenderer::handle_device_loss_recovery() {
    if (!backend_) return false;
    return backend_->recover_device();
}

} // namespace pikdraw::rendering
