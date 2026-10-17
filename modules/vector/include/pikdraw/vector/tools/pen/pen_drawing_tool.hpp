#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "pikdraw/vector/interaction/tool_session.hpp"
#include "pikdraw/vector/precision/numeric/numeric_input.hpp"
#include "pikdraw/vector/precision/snap/direction_snap.hpp"
#include "pikdraw/vector/precision/snap/snap_index.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::vector::tools::pen {

struct PenAnchorInput {
    geometry::Point2D anchor{};
    document::PathNodeMode mode{document::PathNodeMode::Corner};
    std::optional<geometry::Point2D> incoming_handle;
    std::optional<geometry::Point2D> outgoing_handle;
};

enum class PenAngleReference : uint8_t {
    WorldX,
    WorldY,
    ConstructionFrameX,
    PreviousSegment
};

/// Active Pen numeric drivers. Text is parsed with the shared D06 unit parser.
/// Delta fields are measured along the active construction frame axes.
struct PenNumericConstraints {
    std::optional<std::string> length;
    std::optional<std::string> angle;
    PenAngleReference angle_reference{PenAngleReference::WorldX};
    std::optional<std::string> delta_x;
    std::optional<std::string> delta_y;
};

enum class PenDrawingResultCode {
    Applied,
    PreviewUpdated,
    Committed,
    Cancelled,
    Ignored,
    InvalidInput,
    BudgetExceeded,
    Stale,
    CommandFailed
};

struct PenDrawingResult {
    PenDrawingResultCode code{PenDrawingResultCode::Ignored};
    std::string message;
    std::optional<document::ObjectId> object_id;
    std::optional<precision::numeric::Diagnostic> numeric_diagnostic;

    [[nodiscard]] bool committed() const noexcept {
        return code == PenDrawingResultCode::Committed;
    }
};

/// Core authoring session for a mixed Line/Cubic editable path.
/// Mouse dispatch and rendering remain desktop consumers of this model.
class PenDrawingTool {
public:
    [[nodiscard]] PenDrawingResult begin(
        document::SnapshotPtr base, size_t target_layer = 0,
        interaction::ToolSessionLimits limits = {});
    [[nodiscard]] PenDrawingResult begin_resume(
        document::SnapshotPtr base, document::ObjectId object_id,
        document::SubpathId subpath_id, document::NodeId endpoint_id,
        interaction::ToolSessionLimits limits = {});

    [[nodiscard]] PenDrawingResult update_pointer(geometry::Point2D raw_world);
    [[nodiscard]] PenDrawingResult update_from_snap_query(
        geometry::Point2D raw_world,
        const precision::snap::SnapQueryResult& query,
        const precision::snap::SnapQueryStamp& expected_stamp,
        const precision::snap::SnapCameraTransform& camera,
        const std::optional<precision::snap::DirectionSnapResult>& direction = std::nullopt,
        bool hard_direction_lock = false);
    [[nodiscard]] PenDrawingResult update_numeric_candidate(
        std::string_view expression,
        precision::numeric::ParseOptions options = {},
        std::optional<precision::numeric::ConstructionFrame> frame = std::nullopt);
    [[nodiscard]] PenDrawingResult update_numeric_constraints(
        PenNumericConstraints constraints, geometry::Point2D pointer_world,
        precision::numeric::ParseOptions options = {},
        std::optional<precision::numeric::ConstructionFrame> frame = std::nullopt);
    [[nodiscard]] PenDrawingResult confirm_candidate();
    [[nodiscard]] PenDrawingResult confirm_anchor(PenAnchorInput anchor);

    [[nodiscard]] PenDrawingResult undo_local();
    [[nodiscard]] PenDrawingResult redo_local();
    [[nodiscard]] PenDrawingResult finish_open(document::Document& document);
    [[nodiscard]] PenDrawingResult close(document::Document& document);
    [[nodiscard]] PenDrawingResult escape(document::Document& document);
    [[nodiscard]] PenDrawingResult observe_snapshot(document::SnapshotPtr current);
    [[nodiscard]] PenDrawingResult write_recovery_checkpoint(
        const std::string& path, uint32_t base_document_crc32,
        const document::SaveOptions& options = {});
    [[nodiscard]] PenDrawingResult stage_recovery_checkpoint(
        const std::string& path, document::SnapshotPtr current,
        uint32_t expected_base_crc32,
        interaction::ToolSessionLimits limits = {});
    [[nodiscard]] PenDrawingResult accept_recovery_checkpoint(
        document::SnapshotPtr current, uint32_t expected_base_crc32);
    void discard_recovery_checkpoint() noexcept;

    [[nodiscard]] interaction::ToolSessionResult suspend_for_pan() noexcept;
    [[nodiscard]] interaction::ToolSessionResult resume_after_pan() noexcept;
    [[nodiscard]] interaction::ToolSessionResult on_focus_lost() noexcept;
    [[nodiscard]] interaction::ToolSessionResult on_capture_lost() noexcept;
    void cancel_all() noexcept;

    [[nodiscard]] bool active() const noexcept { return session_.active(); }
    [[nodiscard]] bool recovery_pending() const noexcept {
        return pending_recovery_.has_value();
    }
    [[nodiscard]] bool can_finish() const noexcept;
    [[nodiscard]] size_t anchor_count() const noexcept {
        return subpath_.nodes.size();
    }
    [[nodiscard]] const std::optional<PenAnchorInput>& candidate() const noexcept {
        return candidate_;
    }
    [[nodiscard]] const document::EditableSubpath& draft() const noexcept {
        return subpath_;
    }
    [[nodiscard]] interaction::ToolSessionToken token() const {
        return session_.token();
    }
    [[nodiscard]] bool resuming() const noexcept { return resume_target_.has_value(); }
    [[nodiscard]] geometry::Point2D source_point_from_world(
        geometry::Point2D world) const noexcept {
        return source_from_world_.map_point(world);
    }
    [[nodiscard]] geometry::Point2D world_point_from_source(
        geometry::Point2D source) const noexcept {
        return world_from_source_.map_point(source);
    }
    [[nodiscard]] geometry::Point2D source_vector_from_world(
        geometry::Point2D world_vector) const noexcept {
        return source_from_world_.map_vector(world_vector);
    }
    [[nodiscard]] std::optional<double> previous_segment_angle_world() const noexcept;

private:
    struct ResumeTarget {
        document::ObjectId object_id{document::kInvalidObjectId};
        document::SubpathId subpath_id{};
        document::NodeId endpoint_id{};
    };

    struct AnchorEdit {
        PenAnchorInput anchor;
        std::optional<document::EditableSegment> incoming_segment;
        document::PathNode node;
    };

    struct PendingRecovery {
        std::string document_id;
        uint64_t base_revision{0};
        uint32_t base_document_crc32{0};
        std::vector<uint8_t> payload;
        interaction::ToolSessionLimits limits{};
    };

    [[nodiscard]] PenDrawingResult finish_path(document::Document& document,
                                               bool closed);
    [[nodiscard]] PenDrawingResult evaluate_numeric_constraints();
    [[nodiscard]] bool within_payload_budget(size_t anchor_count) const noexcept;
    void clear_draft() noexcept;

    interaction::ToolSessionLimits limits_{};
    interaction::ToolSession session_;
    document::SnapshotPtr base_snapshot_;
    document::EditableSubpath subpath_;
    std::optional<PenAnchorInput> candidate_;
    std::optional<ResumeTarget> resume_target_;
    geometry::Transform2D source_from_world_{geometry::Transform2D::identity()};
    geometry::Transform2D world_from_source_{geometry::Transform2D::identity()};
    std::optional<PenNumericConstraints> numeric_constraints_;
    precision::numeric::ParseOptions numeric_options_{};
    std::optional<precision::numeric::ConstructionFrame> numeric_frame_;
    std::optional<geometry::Point2D> numeric_pointer_world_;
    bool numeric_conflicted_{false};
    std::optional<PendingRecovery> pending_recovery_;
    std::vector<AnchorEdit> undo_;
    std::vector<AnchorEdit> redo_;
    size_t target_layer_{0};
};

} // namespace pikdraw::vector::tools::pen
