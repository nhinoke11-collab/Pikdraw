#pragma once

#include <string>
#include <vector>
#include <ostream>
#include <cstdint>

namespace pikdraw::rendering {

struct FrameMetric {
    uint64_t frame_index{0};
    double camera_x_mm{0.0};
    double camera_y_mm{0.0};
    double zoom{1.0};
    size_t total_nodes{0};
    size_t visible_nodes{0};
    size_t culled_nodes{0};
    double dirty_rect_area_mm2{0.0};
    double cpu_input_us{0.0};
    double cpu_transform_us{0.0};
    double cpu_cull_us{0.0};
    double cpu_raster_us{0.0};
    double cpu_paint_submission_us{0.0};
    double cpu_total_us{0.0};
    bool actual_present_not_measured{true};
};

class ViewportTracer {
public:
    static constexpr size_t MaxRecordedFrames = 1000;

    ViewportTracer() = default;
    void record_frame(const FrameMetric& metric);
    void export_json(std::ostream& os) const;
    [[nodiscard]] const std::vector<FrameMetric>& frames() const noexcept { return frames_; }
    void clear() noexcept { frames_.clear(); }

private:
    std::vector<FrameMetric> frames_;
};

} // namespace pikdraw::rendering
