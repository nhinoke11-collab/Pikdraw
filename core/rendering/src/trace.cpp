#include "pikdraw/rendering/trace.hpp"

namespace pikdraw::rendering {

void ViewportTracer::record_frame(const FrameMetric& metric) {
    if (frames_.size() >= MaxRecordedFrames) {
        frames_.erase(frames_.begin());
    }
    frames_.push_back(metric);
}

void ViewportTracer::export_json(std::ostream& os) const {
    os << "{\n  \"schema_version\": 1,\n  \"frames_recorded\": " << frames_.size() << ",\n  \"frames\": [\n";
    for (size_t i = 0; i < frames_.size(); ++i) {
        const auto& f = frames_[i];
        os << "    {\n"
           << "      \"frame_index\": " << f.frame_index << ",\n"
           << "      \"camera_x_mm\": " << f.camera_x_mm << ",\n"
           << "      \"camera_y_mm\": " << f.camera_y_mm << ",\n"
           << "      \"zoom\": " << f.zoom << ",\n"
           << "      \"total_nodes\": " << f.total_nodes << ",\n"
           << "      \"visible_nodes\": " << f.visible_nodes << ",\n"
           << "      \"culled_nodes\": " << f.culled_nodes << ",\n"
           << "      \"dirty_rect_area_mm2\": " << f.dirty_rect_area_mm2 << ",\n"
           << "      \"cpu_phases_us\": {\n"
           << "        \"input\": " << f.cpu_input_us << ",\n"
           << "        \"transform\": " << f.cpu_transform_us << ",\n"
           << "        \"cull\": " << f.cpu_cull_us << ",\n"
           << "        \"raster\": " << f.cpu_raster_us << ",\n"
           << "        \"paint_submission\": " << f.cpu_paint_submission_us << ",\n"
           << "        \"total\": " << f.cpu_total_us << "\n"
           << "      },\n"
           << "      \"actual_present_not_measured\": true\n"
           << "    }" << (i + 1 < frames_.size() ? "," : "") << "\n";
    }
    os << "  ]\n}\n";
}

} // namespace pikdraw::rendering
