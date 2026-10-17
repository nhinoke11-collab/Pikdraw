#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/tools/pen/pen_drawing_tool.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pikdraw::desktop::tools {

enum class PenShortcutProfile : uint8_t { PikDraw, Corel, Illustrator };

struct PenGestureModifiers {
    bool shift{false};
    bool control{false};
    bool alt{false};
};

struct PenGesturePreferences {
    PenShortcutProfile profile{PenShortcutProfile::PikDraw};
    double direction_step_degrees{15.0};
};

struct PenPointerPreview {
    geometry::Point2D anchor{};
    std::optional<geometry::Point2D> incoming_handle;
    std::optional<geometry::Point2D> outgoing_handle;
    document::PathNodeMode node_mode{document::PathNodeMode::Corner};
    bool direction_locked{false};
    bool dragging{false};
};

struct PenHandleNumericConstraints {
    std::optional<std::string> length;
    std::optional<std::string> angle;
    vector::tools::pen::PenAngleReference angle_reference{
        vector::tools::pen::PenAngleReference::WorldX};
};

/// Translates desktop pointer gestures into the vector module's Pen draft API.
/// It does not own document writes; commits still use PenDrawingTool commands.
class PenToolController {
public:
    explicit PenToolController(document::Document& document) noexcept
        : document_(document) {}

    PenToolController(const PenToolController&) = delete;
    PenToolController& operator=(const PenToolController&) = delete;

    [[nodiscard]] vector::tools::pen::PenDrawingResult activate();
    [[nodiscard]] vector::tools::pen::PenDrawingResult resume(
        document::ObjectId object_id, document::SubpathId subpath_id,
        document::NodeId endpoint_id);
    [[nodiscard]] vector::tools::pen::PenDrawingResult pointer_down(
        geometry::Point2D world);
    [[nodiscard]] vector::tools::pen::PenDrawingResult pointer_move(
        geometry::Point2D world, bool dragged,
        PenGestureModifiers modifiers = {});
    [[nodiscard]] vector::tools::pen::PenDrawingResult update_from_snap_query(
        geometry::Point2D raw_world,
        const vector::precision::snap::SnapQueryResult& query,
        const vector::precision::snap::SnapQueryStamp& expected_stamp,
        const vector::precision::snap::SnapCameraTransform& camera,
        const std::optional<vector::precision::snap::DirectionSnapResult>& direction =
            std::nullopt,
        bool hard_direction_lock = false);
    [[nodiscard]] vector::tools::pen::PenDrawingResult pointer_up();
    [[nodiscard]] vector::tools::pen::PenDrawingResult update_numeric_candidate(
        std::string_view expression,
        vector::precision::numeric::ParseOptions options = {});
    [[nodiscard]] vector::tools::pen::PenDrawingResult update_numeric_handle_constraints(
        PenHandleNumericConstraints constraints,
        vector::precision::numeric::ParseOptions options = {},
        std::optional<vector::precision::numeric::ConstructionFrame> frame = std::nullopt);
    [[nodiscard]] vector::tools::pen::PenDrawingResult write_recovery_checkpoint(
        const std::string& path, uint32_t base_document_crc32,
        const document::SaveOptions& options = {}) {
        using ResultCode = vector::tools::pen::PenDrawingResultCode;
        if (pointer_down_)
            return {ResultCode::InvalidInput,
                    "Finish or cancel the active Pen gesture before checkpointing",
                    {}, {}};
        const auto freshness = tool_.observe_snapshot(document_.current_snapshot());
        if (freshness.code == ResultCode::Stale) {
            clear_pointer();
            return freshness;
        }
        return tool_.write_recovery_checkpoint(path, base_document_crc32, options);
    }
    [[nodiscard]] vector::tools::pen::PenDrawingResult stage_recovery_checkpoint(
        const std::string& path, uint32_t expected_base_crc32,
        vector::interaction::ToolSessionLimits limits = {}) {
        using ResultCode = vector::tools::pen::PenDrawingResultCode;
        if (pointer_down_)
            return {ResultCode::InvalidInput,
                    "Finish or cancel the active Pen gesture before recovery", {}, {}};
        return tool_.stage_recovery_checkpoint(
            path, document_.current_snapshot(), expected_base_crc32, limits);
    }
    [[nodiscard]] vector::tools::pen::PenDrawingResult accept_recovery_checkpoint(
        uint32_t expected_base_crc32) {
        clear_pointer();
        return tool_.accept_recovery_checkpoint(document_.current_snapshot(),
                                                expected_base_crc32);
    }
    void discard_recovery_checkpoint() noexcept {
        clear_pointer();
        tool_.discard_recovery_checkpoint();
    }
    [[nodiscard]] bool set_gesture_preferences(
        PenGesturePreferences preferences) noexcept;
    [[nodiscard]] PenGesturePreferences gesture_preferences() const noexcept {
        return gesture_preferences_;
    }
    [[nodiscard]] vector::tools::pen::PenDrawingResult update_numeric_constraints(
        vector::tools::pen::PenNumericConstraints constraints,
        geometry::Point2D pointer_world,
        vector::precision::numeric::ParseOptions options = {},
        std::optional<vector::precision::numeric::ConstructionFrame> frame = std::nullopt);
    [[nodiscard]] vector::tools::pen::PenDrawingResult confirm_candidate();
    [[nodiscard]] vector::tools::pen::PenDrawingResult finish_open();
    [[nodiscard]] vector::tools::pen::PenDrawingResult close();
    [[nodiscard]] vector::tools::pen::PenDrawingResult undo_local();
    [[nodiscard]] vector::tools::pen::PenDrawingResult redo_local();
    [[nodiscard]] vector::interaction::ToolSessionResult suspend_for_pan() noexcept;
    [[nodiscard]] vector::interaction::ToolSessionResult resume_after_pan() noexcept;
    [[nodiscard]] vector::interaction::ToolSessionResult on_focus_lost() noexcept;
    [[nodiscard]] vector::interaction::ToolSessionResult on_capture_lost() noexcept;
    void cancel() noexcept;

    [[nodiscard]] bool active() const noexcept { return tool_.active(); }
    [[nodiscard]] bool resuming() const noexcept { return tool_.resuming(); }
    [[nodiscard]] bool recovery_pending() const noexcept {
        return tool_.recovery_pending();
    }
    [[nodiscard]] bool pointer_pending() const noexcept { return pointer_down_.has_value(); }
    [[nodiscard]] size_t anchor_count() const noexcept { return tool_.anchor_count(); }
    [[nodiscard]] geometry::Point2D source_point_from_world(
        geometry::Point2D point) const noexcept {
        return tool_.source_point_from_world(point);
    }
    [[nodiscard]] geometry::Point2D world_point_from_source(
        geometry::Point2D point) const noexcept {
        return tool_.world_point_from_source(point);
    }
    [[nodiscard]] const document::EditableSubpath& draft() const noexcept {
        return tool_.draft();
    }
    [[nodiscard]] const std::optional<vector::tools::pen::PenAnchorInput>& candidate()
        const noexcept { return tool_.candidate(); }
    [[nodiscard]] const std::optional<PenPointerPreview>& pointer_preview()
        const noexcept { return pointer_preview_; }

private:
    [[nodiscard]] std::optional<size_t> first_editable_layer(
        const document::SnapshotPtr& snapshot) const noexcept;
    [[nodiscard]] bool direction_lock_active(
        PenGestureModifiers modifiers) const noexcept;
    [[nodiscard]] geometry::Point2D handle_position(
        geometry::Point2D anchor, geometry::Point2D pointer,
        PenGestureModifiers modifiers) const noexcept;
    [[nodiscard]] vector::tools::pen::PenDrawingResult
    recompute_numeric_handle_preview();
    void clear_pointer() noexcept;

    document::Document& document_;
    vector::tools::pen::PenDrawingTool tool_;
    std::optional<geometry::Point2D> pointer_down_;
    std::optional<geometry::Point2D> pointer_current_;
    std::optional<PenPointerPreview> pointer_preview_;
    std::optional<PenHandleNumericConstraints> pointer_numeric_constraints_;
    vector::precision::numeric::ParseOptions pointer_numeric_options_{};
    std::optional<vector::precision::numeric::ConstructionFrame>
        pointer_numeric_frame_;
    bool pointer_numeric_conflicted_{false};
    PenGesturePreferences gesture_preferences_{};
    PenGestureModifiers pointer_modifiers_{};
    bool pointer_dragged_{false};
};

} // namespace pikdraw::desktop::tools
