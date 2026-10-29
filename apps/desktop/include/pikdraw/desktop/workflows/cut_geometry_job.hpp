#pragma once

#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/model/editable_path.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/point.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace pikdraw::desktop {

inline constexpr double kCutGeometryDefaultToleranceMm = 0.05;

enum class CutGeometryOperation : std::uint8_t {
    Knife, VectorEraser, Scissors, SmartTrim, TrimObjects, SmartFill, ShapeBuilder
};
enum class CutGeometryStatus : std::uint8_t {
    Success, NoChange, Unsupported, Ambiguous, Invalid, Cancelled, Stale,
    BudgetExceeded, ToleranceNotMet
};

struct CutGeometryRequest {
    CutGeometryOperation operation{CutGeometryOperation::Knife};
    std::shared_ptr<const document::DocumentSnapshot> snapshot;
    std::vector<document::ObjectId> selected_ids;
    document::ObjectId target_id{document::kInvalidObjectId};
    std::vector<document::ObjectId> source_ids;
    std::vector<geometry::Point2D> points_world;
    double hit_tolerance_mm{0.0};
    double gap_mm{0.0};
    double separate_offset_mm{0.0};
    double diameter_mm{2.0};
    double max_approximation_error_mm{kCutGeometryDefaultToleranceMm};
    bool round_nib{true};
    bool keep_sources{true};
    bool erase_faces{false};
    bool menu_action{false};
    bool preview_only{false};
};

struct CutGeometryReplacement {
    document::ObjectId target_id{document::kInvalidObjectId};
    std::vector<document::PathObject> pieces;
};

struct CutGeometryAddition {
    document::PathObject object;
    std::size_t layer{0};
};

struct CutGeometryPreviewContour {
    std::vector<geometry::Point2D> points_world;
    bool closed{false};
    bool removed{false};
};

struct ScissorsGeometryEdit {
    document::ObjectId object_id{document::kInvalidObjectId};
    document::SubpathId subpath_id{};
    document::SegmentId segment_id{};
    double parameter{0.5};
    document::NodeId endpoint_node{};
};

struct SmartTrimGeometryEdit {
    document::ObjectId object_id{document::kInvalidObjectId};
    document::SubpathId subpath_id{};
    document::SegmentId start_segment{};
    double start_parameter{0.0};
    document::SegmentId end_segment{};
    double end_parameter{1.0};
    bool wraps_closed_subpath{false};
};

struct CutGeometryProposal {
    CutGeometryOperation operation{CutGeometryOperation::Knife};
    CutGeometryStatus status{CutGeometryStatus::Invalid};
    std::shared_ptr<const document::DocumentSnapshot> base_snapshot;
    std::vector<CutGeometryReplacement> replacements;
    std::vector<CutGeometryAddition> additions;
    std::vector<CutGeometryPreviewContour> preview_contours;
    std::vector<document::ObjectId> remove_ids;
    std::vector<document::ObjectId> preserve_selection_ids;
    std::vector<document::ObjectId> source_ids;
    document::ObjectId style_source_id{document::kInvalidObjectId};
    std::optional<ScissorsGeometryEdit> scissors_edit;
    std::optional<SmartTrimGeometryEdit> smart_trim_edit;
    std::string message;
    std::size_t candidates_processed{0};
    std::size_t candidates_total{0};
    double approximation_error_bound_mm{0.0};
    double max_approximation_error_mm{kCutGeometryDefaultToleranceMm};
    bool keep_sources{true};
    bool erase_faces{false};
};

struct CutGeometryJobState {
    std::uint64_t generation{0};
    std::string tab_id;
    std::shared_ptr<const document::DocumentSnapshot> base_snapshot;
    std::vector<document::ObjectId> selected_ids;
    CutGeometryOperation operation{CutGeometryOperation::Knife};
    document::ObjectId target_id{document::kInvalidObjectId};
    std::vector<document::ObjectId> source_ids;
    std::vector<geometry::Point2D> points_world;
    bool mode_flag{false};
    bool menu_action{false};
    bool preview_only{false};
    int initiating_tool{0};
    std::atomic<std::size_t> completed{0};
    std::atomic<std::size_t> total{0};
    std::mutex result_mutex;
    std::optional<CutGeometryProposal> proposal;
    std::mutex notification_mutex;
    bool notifications_enabled{true};

    template <typename Dispatch>
    void dispatch_notification_if_enabled(Dispatch&& dispatch) {
        std::lock_guard lock(notification_mutex);
        if (notifications_enabled) dispatch();
    }

    void disable_notifications() {
        std::lock_guard lock(notification_mutex);
        notifications_enabled = false;
    }
};

using CutGeometryProgress = std::function<void(std::size_t completed, std::size_t total)>;

[[nodiscard]] CutGeometryProposal compute_cut_geometry(
    const CutGeometryRequest& request,
    std::stop_token stop,
    const CutGeometryProgress& progress = {});

[[nodiscard]] CutGeometryProposal compute_trim_objects_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress = {});
[[nodiscard]] CutGeometryProposal compute_smart_fill_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress = {});
[[nodiscard]] CutGeometryProposal compute_shape_builder_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress = {});

} // namespace pikdraw::desktop
