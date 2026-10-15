#pragma once

#include "pikdraw/rendering/backend.hpp"

namespace pikdraw::rendering {

class SkiaRasterBackend final : public IRendererBackend {
public:
    SkiaRasterBackend();
    ~SkiaRasterBackend() override;

    bool render_frame(
        const Camera2D& camera,
        const ExperimentalSceneSnapshot& scene,
        const PixelBufferTarget& target,
        FrameMetric& metric
    ) override;

    [[nodiscard]] DeviceLossClassification classify_loss(int error_code) const noexcept override;
    bool recover_context() override;

private:
    bool context_valid_{true};
};

} // namespace pikdraw::rendering
