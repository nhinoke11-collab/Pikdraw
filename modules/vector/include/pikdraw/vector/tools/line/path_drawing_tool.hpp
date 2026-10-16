#pragma once

#include "pikdraw/document/document.hpp"
#include "pikdraw/vector/interaction/tool_session.hpp"
#include "pikdraw/vector/precision/numeric/numeric_input.hpp"
#include "pikdraw/vector/precision/snap/direction_snap.hpp"
#include "pikdraw/vector/precision/snap/snap_index.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pikdraw::vector::tools::line {

enum class PathDrawingKind : uint8_t { Line, Polyline };

enum class PathDrawingResultCode : uint8_t {
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

struct PathDrawingResult {
    PathDrawingResultCode code{PathDrawingResultCode::Ignored};
    std::string message;
    std::optional<document::ObjectId> object_id;
    std::optional<precision::numeric::Diagnostic> numeric_diagnostic;

    PathDrawingResult() = default;
    PathDrawingResult(PathDrawingResultCode result_code,
                      std::string result_message = {},
                      std::optional<document::ObjectId> result_object = std::nullopt,
                      std::optional<precision::numeric::Diagnostic> diagnostic =
                          std::nullopt)
        : code(result_code), message(std::move(result_message)),
          object_id(result_object), numeric_diagnostic(std::move(diagnostic)) {}

    [[nodiscard]] bool committed() const noexcept {
        return code == PathDrawingResultCode::Committed;
    }
};

class PathDrawingTool {
public:
    [[nodiscard]] PathDrawingResult begin(
        PathDrawingKind kind, document::SnapshotPtr base,
        size_t target_layer = 0,
        interaction::ToolSessionLimits limits = {});

    [[nodiscard]] PathDrawingResult update_pointer(geometry::Point2D raw_world);
    [[nodiscard]] PathDrawingResult update_from_snap_query(
        geometry::Point2D raw_world,
        const precision::snap::SnapQueryResult& query,
        const precision::snap::SnapQueryStamp& expected_stamp,
        const precision::snap::SnapCameraTransform& camera,
        const std::optional<precision::snap::DirectionSnapResult>& direction = std::nullopt,
        bool hard_direction_lock = false);
    [[nodiscard]] PathDrawingResult update_numeric_candidate(
        std::string_view expression,
        precision::numeric::ParseOptions options = {},
        std::optional<precision::numeric::ConstructionFrame> frame = std::nullopt);

    [[nodiscard]] PathDrawingResult confirm_candidate(document::Document& document);
    [[nodiscard]] PathDrawingResult enter(document::Document& document);
    [[nodiscard]] PathDrawingResult close(document::Document& document);
    [[nodiscard]] PathDrawingResult escape(document::Document& document);
    [[nodiscard]] PathDrawingResult backspace();
    [[nodiscard]] PathDrawingResult observe_snapshot(document::SnapshotPtr current);
    [[nodiscard]] interaction::ToolSessionResult suspend_for_pan() noexcept;
    [[nodiscard]] interaction::ToolSessionResult resume_after_pan() noexcept;
    [[nodiscard]] interaction::ToolSessionResult on_focus_lost() noexcept;
    [[nodiscard]] interaction::ToolSessionResult on_capture_lost() noexcept;
    void cancel_all() noexcept;

    [[nodiscard]] bool active() const noexcept { return session_.active(); }
    [[nodiscard]] bool can_finish() const noexcept;
    [[nodiscard]] PathDrawingKind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::vector<geometry::Point2D>& points() const noexcept {
        return points_;
    }
    [[nodiscard]] const std::optional<geometry::Point2D>& candidate() const noexcept {
        return candidate_;
    }
    [[nodiscard]] interaction::ToolSessionToken token() const {
        return session_.token();
    }

private:
    [[nodiscard]] PathDrawingResult set_candidate(geometry::Point2D point,
                                                  bool numeric);
    [[nodiscard]] PathDrawingResult finish_path(document::Document& document,
                                                bool closed,
                                                bool continue_tool);
    [[nodiscard]] PathDrawingResult restart(document::SnapshotPtr base);
    void clear_draft() noexcept;

    PathDrawingKind kind_{PathDrawingKind::Line};
    size_t target_layer_{0};
    interaction::ToolSessionLimits limits_{};
    interaction::ToolSession session_;
    document::SnapshotPtr base_snapshot_;
    std::vector<geometry::Point2D> points_;
    std::optional<geometry::Point2D> candidate_;
    bool candidate_is_numeric_{false};
};

} // namespace pikdraw::vector::tools::line
