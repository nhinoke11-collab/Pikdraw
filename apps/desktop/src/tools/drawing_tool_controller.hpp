#pragma once

#include "drawing_preferences.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/vector/tools/line/path_drawing_tool.hpp"
#include "snap_index_worker.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::desktop::tools {

struct DrawingToolVisualState {
    bool active{false};
    bool snap_index_ready{false};
    bool snap_index_failed{false};
    vector::tools::line::PathDrawingKind kind{vector::tools::line::PathDrawingKind::Line};
    std::vector<geometry::Point2D> points;
    std::optional<geometry::Point2D> candidate;
    std::optional<vector::precision::snap::SnapOption> snap_kind;
    std::string numeric_entry;
    std::string message;
    std::string profile;
    vector::precision::snap::SnapConfigSnapshot snap_config;
};

class DrawingToolController {
public:
    explicit DrawingToolController(document::Document& document,
        std::filesystem::path preference_path = {});
    ~DrawingToolController() = default;
    DrawingToolController(const DrawingToolController&) = delete;
    DrawingToolController& operator=(const DrawingToolController&) = delete;

    [[nodiscard]] bool activate(vector::tools::line::PathDrawingKind kind);
    void cancel() noexcept;
    void pointer_move(geometry::Point2D point, const rendering::Camera2D& camera,
                      double dpi_scale, bool hard_45_lock, bool hard_ortho_lock);
    [[nodiscard]] vector::tools::line::PathDrawingResult confirm_candidate();
    [[nodiscard]] vector::tools::line::PathDrawingResult enter();
    [[nodiscard]] vector::tools::line::PathDrawingResult escape();
    [[nodiscard]] vector::tools::line::PathDrawingResult backspace();
    [[nodiscard]] bool append_numeric_character(char value);
    void erase_numeric_character();
    void clear_numeric_entry() noexcept;
    [[nodiscard]] bool toggle_snap_master();
    [[nodiscard]] bool toggle_snap_option(vector::precision::snap::SnapOption option);
    [[nodiscard]] bool toggle_direction(vector::precision::snap::DirectionMode mode);
    [[nodiscard]] bool cycle_polar_step();
    [[nodiscard]] bool cycle_profile();
    [[nodiscard]] DrawingToolVisualState visual_state() const;
    [[nodiscard]] bool active() const noexcept { return path_.active(); }
    [[nodiscard]] bool has_numeric_entry() const noexcept { return !numeric_entry_.empty(); }
    [[nodiscard]] bool snap_panel_open() const noexcept { return snap_panel_open_; }
    void toggle_snap_panel() noexcept { snap_panel_open_ = !snap_panel_open_; }
    [[nodiscard]] bool snap_panel_open_value() const noexcept { return snap_panel_open_; }
    [[nodiscard]] bool snap_pending() const noexcept { return active() && !index_ready_; }
    void suspend_for_pan() noexcept;
    void resume_after_pan() noexcept;
    void save_preferences() const;

private:
    [[nodiscard]] vector::precision::snap::SnapCameraTransform camera_transform(
        const rendering::Camera2D& camera, double dpi_scale);
    [[nodiscard]] vector::precision::snap::SnapQueryStamp make_stamp(
        const document::DocumentSnapshot& snapshot, const char* tool_context);
    void apply_configuration();
    void update_result_message(const vector::tools::line::PathDrawingResult& result);

    document::Document& document_;
    vector::tools::line::PathDrawingTool path_;
    vector::precision::snap::SnapConfiguration snap_config_;
    vector::precision::snap::SnapCapabilities capabilities_;
    SnapIndexWorker index_worker_;
    std::shared_ptr<const vector::precision::snap::SnapSourceIndex> index_;
    DrawingPreferences preferences_;
    std::filesystem::path preference_path_;
    uint64_t camera_generation_{1};
    uint64_t query_generation_{0};
    std::array<uint64_t, 6> camera_signature_{};
    bool have_camera_signature_{false};
    bool index_ready_{false};
    bool index_failed_{false};
    bool snap_panel_open_{false};
    std::string numeric_entry_;
    std::string message_;
    std::string requested_document_id_;
    uint64_t requested_epoch_{0};
    uint64_t requested_revision_{0};
    std::optional<vector::precision::snap::SnapOption> snap_kind_;
    std::optional<vector::precision::snap::SnapAcquisitionState> snap_acquisition_;
    std::optional<vector::precision::snap::DirectionLockState> direction_lock_;
};

} // namespace pikdraw::desktop::tools
