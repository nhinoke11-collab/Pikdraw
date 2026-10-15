#pragma once

#include "pikdraw/rendering/types.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/scene.hpp"
#include "pikdraw/rendering/trace.hpp"
#include <cstddef>

namespace pikdraw::rendering {

enum class DeviceLossClassification {
    NotMeasured,
    RecoverableContextLoss,
    FatalNativeDriverCrash
};

struct PixelBufferTarget {
    uint8_t* data{nullptr};
    size_t capacity_bytes{0};
    int32_t width{0};
    int32_t height{0};
    int32_t stride_bytes{0};

    [[nodiscard]] bool is_valid() const noexcept {
        if (!data || width <= 0 || height <= 0 || stride_bytes <= 0) {
            return false;
        }
        const size_t stride = static_cast<size_t>(stride_bytes);
        if (static_cast<size_t>(width) > stride / 4) return false;
        return static_cast<size_t>(height) <= capacity_bytes / stride;
    }
};

class IRendererBackend {
public:
    virtual ~IRendererBackend() = default;
    virtual bool render_frame(
        const Camera2D& camera,
        const ExperimentalSceneSnapshot& scene,
        const PixelBufferTarget& target,
        FrameMetric& metric
    ) = 0;
    [[nodiscard]] virtual DeviceLossClassification classify_loss(int error_code) const noexcept = 0;
    virtual bool recover_context() = 0;
};

} // namespace pikdraw::rendering
