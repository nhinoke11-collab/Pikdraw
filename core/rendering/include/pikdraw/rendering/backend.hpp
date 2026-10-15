#pragma once

#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <chrono>

namespace pikdraw::rendering {

enum class DeviceLossStatus {
    Healthy,
    RecoverableLoss,
    FatalDriverCrash
};

struct RenderMetric {
    uint64_t snapshot_revision{0};
    size_t total_objects{0};
    size_t culled_objects{0};
    size_t rendered_objects{0};
    std::chrono::microseconds render_duration{0};
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    [[nodiscard]] virtual DeviceLossStatus device_status() const noexcept = 0;
    virtual bool recover_device() = 0;

    virtual bool render(
        const document::DocumentSnapshot& snapshot,
        const Camera2D& camera,
        PixelBufferTarget& target,
        RenderMetric* out_metric = nullptr) = 0;
};

} // namespace pikdraw::rendering
