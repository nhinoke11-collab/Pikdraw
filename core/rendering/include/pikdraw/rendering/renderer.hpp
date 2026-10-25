#pragma once

#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/rendering/backend.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <memory>
#include <vector>

namespace pikdraw::rendering {

/// Viewport Renderer Facade
class ViewportRenderer {
public:
    explicit ViewportRenderer(std::shared_ptr<IRenderBackend> backend = nullptr);

    [[nodiscard]] Camera2D& camera() noexcept { return camera_; }
    [[nodiscard]] const Camera2D& camera() const noexcept { return camera_; }

    [[nodiscard]] IRenderBackend& backend() noexcept { return *backend_; }
    void set_view_context(RenderViewContext context) { backend_->set_view_context(context); }
    void set_backend(std::shared_ptr<IRenderBackend> backend) {
        if (backend) backend_ = std::move(backend);
    }

    /// Renders the given immutable snapshot into the target buffer
    bool render_frame(
        const document::DocumentSnapshot& snapshot,
        PixelBufferTarget& target,
        RenderMetric* out_metric = nullptr);

    /// Recovers from context/device loss if possible
    bool handle_device_loss_recovery();

    [[nodiscard]] const RenderMetric& last_metric() const noexcept {
        return last_metric_;
    }

private:
    Camera2D camera_;
    std::shared_ptr<IRenderBackend> backend_;
    RenderMetric last_metric_;
};

} // namespace pikdraw::rendering
