#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/vector/tools/line/path_drawing_tool.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

using namespace pikdraw;
using namespace pikdraw::vector;
using namespace pikdraw::vector::tools::line;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

document::PathObject path_at(const document::DocumentSnapshot& snapshot,
                             document::ObjectId id) {
    const auto object = snapshot.get_object(id);
    check(object.has_value(), "committed path exists in the document snapshot");
    check(std::holds_alternative<document::PathObject>(*object),
          "drawing tools create path objects");
    return std::get<document::PathObject>(*object);
}

document::ObjectId confirm(PathDrawingTool& tool, document::Document& doc,
                           geometry::Point2D point) {
    check(tool.update_pointer(point).code == PathDrawingResultCode::PreviewUpdated,
          "pointer produces a transient endpoint candidate");
    const auto result = tool.confirm_candidate(doc);
    check(result.code == PathDrawingResultCode::Applied || result.committed(),
          "pointer endpoint is accepted");
    return result.object_id.value_or(document::kInvalidObjectId);
}

void test_line_is_one_object_per_segment_and_undoable() {
    document::Document doc("d08-line");
    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Line, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Line tool starts against a document revision");
    check(confirm(tool, doc, {0, 0}) == document::kInvalidObjectId &&
              doc.current_snapshot()->object_count() == 0,
          "the first Line point is transient and creates no empty object");
    const auto committed = tool.update_pointer({100, 0});
    check(committed.code == PathDrawingResultCode::PreviewUpdated,
          "Line endpoint preview is separate from the confirmed start");
    const auto line = tool.confirm_candidate(doc);
    check(line.committed() && doc.current_snapshot()->object_count() == 1 &&
              tool.active() && tool.points().empty(),
          "a Line commits one object and remains ready for an independent segment");

    const auto source = path_at(*doc.current_snapshot(), *line.object_id);
    check(source.authoring_source && !source.has_fill && source.has_stroke,
          "Line keeps editable source geometry and stroke-only styling");
    const auto& subpath = source.authoring_source->subpaths.front();
    check(!subpath.closed && subpath.nodes.size() == 2 &&
              subpath.segments.size() == 1 &&
              subpath.nodes[0].anchor == geometry::Point2D{0, 0} &&
              subpath.nodes[1].anchor == geometry::Point2D{100, 0},
          "Line has exactly two source nodes and one open segment");

    confirm(tool, doc, {5, 5});
    const auto second = tool.update_pointer({5, 25});
    check(second.code == PathDrawingResultCode::PreviewUpdated &&
              tool.confirm_candidate(doc).committed() &&
              doc.current_snapshot()->object_count() == 2,
          "the next Line is a separate editable object");
    check(doc.undo() && doc.current_snapshot()->object_count() == 1,
          "one document undo removes the latest Line as one command");
    check(doc.redo() && doc.current_snapshot()->object_count() == 2,
          "document redo restores that same Line object");
}

void test_zero_length_line_is_rejected_without_document_mutation() {
    document::Document doc("d08-zero-line");
    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Line, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Line tool begins");
    confirm(tool, doc, {0, 0});
    (void)tool.update_pointer({0, 0});
    const auto rejected = tool.confirm_candidate(doc);
    check(rejected.code == PathDrawingResultCode::InvalidInput &&
              doc.current_snapshot()->object_count() == 0 && tool.points().size() == 1,
          "zero-length Line is rejected while preserving its valid first anchor");
    check(tool.update_pointer({10, 0}).code == PathDrawingResultCode::PreviewUpdated &&
              tool.confirm_candidate(doc).committed() &&
              doc.current_snapshot()->object_count() == 1,
          "a corrected endpoint can finish the same Line draft");
}

void test_polyline_escape_commits_confirmed_open_geometry_and_reopens() {
    document::Document doc("d08-polyline-escape");
    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Polyline, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Polyline tool begins");
    confirm(tool, doc, {0, 0});
    confirm(tool, doc, {100, 0});
    confirm(tool, doc, {100, 50});
    check(doc.current_snapshot()->object_count() == 0 && tool.can_finish(),
          "confirmed Polyline draft remains outside the document until finish");
    const auto finished = tool.escape(doc);
    check(finished.committed() && !tool.active() &&
              doc.current_snapshot()->object_count() == 1,
          "Escape commits confirmed Polyline segments as one open path");

    const auto source = path_at(*doc.current_snapshot(), *finished.object_id);
    const auto& subpath = source.authoring_source->subpaths.front();
    check(!subpath.closed && subpath.nodes.size() == 3 &&
              subpath.segments.size() == 2 &&
              subpath.nodes[2].anchor == geometry::Point2D{100, 50},
          "open Polyline retains its three authored vertices");

    const auto bytes = document::NativeFormat::serialize(*doc.current_snapshot());
    std::string error;
    const auto decoded = document::NativeFormat::deserialize(
        bytes.data(), bytes.size(), &error);
    check(decoded != nullptr, "native storage reopens the committed Polyline");
    document::Document reopened(*decoded);
    const auto reopened_path = path_at(*reopened.current_snapshot(), *finished.object_id);
    const auto& reopened_source = reopened_path.authoring_source->subpaths.front();
    check(reopened_source.nodes.size() == 3 && reopened_source.segments.size() == 2 &&
              reopened_source.nodes[1].anchor == geometry::Point2D{100, 0} &&
              reopened_source.nodes[2].anchor == geometry::Point2D{100, 50},
          "native reopen preserves path topology and exact source coordinates");
    check(doc.undo() && doc.current_snapshot()->object_count() == 0,
          "one undo removes the whole Polyline");
}

void test_close_removes_duplicate_endpoint_and_closes_source_topology() {
    document::Document doc("d08-close");
    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Polyline, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Polyline tool begins");
    confirm(tool, doc, {0, 0});
    confirm(tool, doc, {100, 0});
    confirm(tool, doc, {100, 100});
    confirm(tool, doc, {0, 0});
    const auto closed = tool.close(doc);
    check(closed.committed() && tool.active(),
          "Close commits and starts a fresh Polyline draft");
    const auto object = path_at(*doc.current_snapshot(), *closed.object_id);
    const auto& subpath = object.authoring_source->subpaths.front();
    check(subpath.closed && subpath.nodes.size() == 3 &&
              subpath.segments.size() == 3 && object.has_fill,
          "closed path has three vertices, three segments, and fill semantics");
    for (size_t index = 1; index < subpath.nodes.size(); ++index)
        check(subpath.nodes[index - 1].anchor != subpath.nodes[index].anchor,
              "closed path contains no duplicate consecutive nodes");
    check(subpath.nodes.back().anchor != subpath.nodes.front().anchor,
          "closing edge is represented by topology rather than a duplicate endpoint");
}

void test_numeric_points_backspace_and_enter_continuation() {
    document::Document doc("d08-numeric");
    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Polyline, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Polyline tool begins");
    check(tool.update_numeric_candidate("#0;0").code ==
              PathDrawingResultCode::PreviewUpdated && tool.enter(doc).code ==
              PathDrawingResultCode::Applied,
          "absolute numeric input places the first point once");
    check(tool.update_numeric_candidate("@100<0").code ==
              PathDrawingResultCode::PreviewUpdated && tool.enter(doc).code ==
              PathDrawingResultCode::Applied,
          "relative length-angle input places the next point");
    check(tool.update_numeric_candidate("@50<90").code ==
              PathDrawingResultCode::PreviewUpdated && tool.enter(doc).code ==
              PathDrawingResultCode::Applied,
          "a second relative input extends the same Polyline draft");
    check(tool.backspace().code == PathDrawingResultCode::Applied &&
              tool.points().size() == 2 &&
              tool.backspace().code == PathDrawingResultCode::Applied &&
              tool.points().size() == 1,
          "Backspace removes confirmed points without reaching document history");
    check(tool.update_numeric_candidate("@100<0").code ==
              PathDrawingResultCode::PreviewUpdated && tool.enter(doc).code ==
              PathDrawingResultCode::Applied,
          "a Polyline can add a valid point again after undo reaches one anchor");
    const auto first_commit = tool.enter(doc);
    check(first_commit.committed() && tool.active() &&
              doc.current_snapshot()->object_count() == 1,
          "Enter finishes the open path and starts a continuous fresh draft");
    auto first_path = path_at(*doc.current_snapshot(), *first_commit.object_id);
    check(first_path.authoring_source->subpaths.front().nodes.size() == 2 &&
              first_path.authoring_source->subpaths.front().nodes[1].anchor ==
                  geometry::Point2D{100, 0},
          "numeric path keeps exact millimeter coordinates after draft undo");

    confirm(tool, doc, {0, 200});
    confirm(tool, doc, {100, 200});
    const auto second_commit = tool.escape(doc);
    check(second_commit.committed() && !tool.active() &&
              doc.current_snapshot()->object_count() == 2,
          "Escape commits the next open Polyline and exits the tool");
}

precision::snap::SnapCandidate grid_candidate(
    geometry::Point2D point, uint64_t cell_x, uint64_t cell_y,
    int priority, const precision::snap::SnapQueryStamp& stamp) {
    precision::snap::SnapCandidate candidate;
    candidate.key.kind = precision::snap::SnapOption::Grid;
    candidate.key.grid_cell = precision::snap::GridCellKey{
        static_cast<int64_t>(cell_x), static_cast<int64_t>(cell_y)};
    candidate.world_point = point;
    candidate.source_revision = stamp.document_revision;
    candidate.distance_dip = 1.0;
    candidate.per_tool_priority = priority;
    candidate.stamp = stamp;
    return candidate;
}

void test_hard_direction_filters_candidates_before_ranking_and_rejects_stale() {
    document::Document doc("d08-snap-feasibility");
    PathDrawingTool first_point_tool;
    check(first_point_tool.begin(PathDrawingKind::Line, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Line tool begins before its first snapped point");
    const auto first_snapshot = doc.current_snapshot();
    precision::snap::SnapQueryStamp first_stamp{
        doc.id(), first_snapshot->epoch(), first_snapshot->revision(), 7, 5, 3,
        "line", 1};
    precision::snap::SnapQueryResult first_query;
    first_query.status = precision::snap::SnapQueryStatus::Success;
    first_query.stamp = first_stamp;
    const auto first_snap = grid_candidate({10, 20}, 2, 4, 0, first_stamp);
    first_query.candidates = {first_snap};
    first_query.chosen = first_snap;
    precision::snap::DirectionSnapResult unused_direction;
    unused_direction.status = precision::snap::SnapQueryStatus::Success;
    unused_direction.stamp = first_stamp;
    unused_direction.mode = precision::snap::DirectionMode::Ortho;
    unused_direction.constrained = false;
    precision::snap::SnapCameraTransform first_camera;
    first_camera.generation = 7;
    check(first_point_tool.update_from_snap_query({10, 20}, first_query,
              first_stamp, first_camera, unused_direction, true).code ==
              PathDrawingResultCode::PreviewUpdated &&
              first_point_tool.candidate() ==
                  std::optional<geometry::Point2D>{{10, 20}},
          "direction locks do not interfere with snapping the first anchor");
    first_point_tool.cancel_all();

    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Polyline, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Polyline tool begins");
    confirm(tool, doc, {0, 0});

    const auto snapshot = doc.current_snapshot();
    precision::snap::SnapQueryStamp stamp{
        doc.id(), snapshot->epoch(), snapshot->revision(), 7, 5, 3, "polyline", 1};
    precision::snap::SnapQueryResult query;
    query.status = precision::snap::SnapQueryStatus::Success;
    query.stamp = stamp;
    const auto off_ray = grid_candidate({50, 25}, 1, 2, 0, stamp);
    const auto on_ray = grid_candidate({50, 0}, 1, 0, 10, stamp);
    query.candidates = {on_ray, off_ray};
    query.chosen = off_ray;

    precision::snap::DirectionSnapResult direction;
    direction.status = precision::snap::SnapQueryStatus::Success;
    direction.stamp = stamp;
    direction.mode = precision::snap::DirectionMode::Ortho;
    direction.constrained = true;
    direction.world_angle_degrees = 0.0;
    direction.world_point = {50, 0};
    precision::snap::SnapCameraTransform camera;
    camera.generation = 7;
    const auto selected = tool.update_from_snap_query(
        {50, 25}, query, stamp, camera, direction, true);
    check(selected.code == PathDrawingResultCode::PreviewUpdated &&
              tool.candidate() == std::optional<geometry::Point2D>{{50, 0}},
          "an infeasible high-priority snap is filtered before selecting a feasible candidate");

    const auto current_candidate = tool.candidate();
    auto newer_camera = camera;
    ++newer_camera.generation;
    check(tool.update_from_snap_query({60, 25}, query, stamp, newer_camera,
              direction, true).code == PathDrawingResultCode::Stale &&
              tool.candidate() == current_candidate,
          "snap results from an older camera generation cannot replace the preview");

    precision::snap::SnapQueryResult no_candidates;
    no_candidates.status = precision::snap::SnapQueryStatus::Success;
    no_candidates.stamp = stamp;
    direction.world_point = {30, 0};
    check(tool.update_from_snap_query({30, 25}, no_candidates, stamp, camera,
              direction, true).code == PathDrawingResultCode::PreviewUpdated &&
              tool.candidate() == std::optional<geometry::Point2D>{{30, 0}},
          "an empty snap set may use a direction fallback that lies on the locked ray");

    direction.world_point = {30, 15};
    check(tool.update_from_snap_query({30, 25}, no_candidates, stamp, camera,
              direction, true).code == PathDrawingResultCode::InvalidInput &&
              tool.candidate() == std::optional<geometry::Point2D>{{30, 0}},
          "an off-ray direction fallback is rejected without changing the preview");

    auto stale_stamp = stamp;
    ++stale_stamp.document_revision;
    query.stamp = stale_stamp;
    check(tool.update_from_snap_query({60, 25}, query, stale_stamp, camera).code ==
              PathDrawingResultCode::Stale,
          "snap proposals from another document revision are rejected");
}

void test_escape_before_first_segment_creates_no_object() {
    document::Document doc("d08-escape-empty");
    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Polyline, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Polyline tool begins");
    confirm(tool, doc, {2, 3});
    check(tool.escape(doc).code == PathDrawingResultCode::Cancelled &&
              !tool.active() && doc.current_snapshot()->object_count() == 0 &&
              doc.revision() == 0,
          "Escape before a valid segment discards the draft without a document command");
}

void test_polyline_rejects_zero_segments_and_stale_drafts() {
    document::Document doc("d08-polyline-invalid");
    PathDrawingTool tool;
    check(tool.begin(PathDrawingKind::Polyline, doc.current_snapshot()).code ==
              PathDrawingResultCode::Applied,
          "Polyline tool begins");
    confirm(tool, doc, {0, 0});
    check(tool.update_pointer({0, 0}).code == PathDrawingResultCode::PreviewUpdated &&
              tool.confirm_candidate(doc).code == PathDrawingResultCode::InvalidInput &&
              tool.points().size() == 1 && doc.revision() == 0,
          "a zero-length Polyline segment is rejected without publishing junk");
    confirm(tool, doc, {25, 0});

    document::TextObject external;
    external.id = document::generate_object_id();
    external.text = "external edit";
    check(doc.execute_command(
              std::make_unique<document::AddObjectCommand>(external)).success,
          "an external document command advances the source revision");
    const auto stale = tool.observe_snapshot(doc.current_snapshot());
    check(stale.code == PathDrawingResultCode::Stale && !tool.active() &&
              tool.points().empty() && doc.current_snapshot()->object_count() == 1,
          "a stale Polyline draft is discarded and cannot commit over a newer revision");
}

} // namespace

int main() {
    try {
        test_line_is_one_object_per_segment_and_undoable();
        test_zero_length_line_is_rejected_without_document_mutation();
        test_polyline_escape_commits_confirmed_open_geometry_and_reopens();
        test_close_removes_duplicate_endpoint_and_closes_source_topology();
        test_numeric_points_backspace_and_enter_continuation();
        test_hard_direction_filters_candidates_before_ranking_and_rejects_stale();
        test_escape_before_first_segment_creates_no_object();
        test_polyline_rejects_zero_segments_and_stale_drafts();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "PASS: D08 Line/Polyline semantic scenarios\n";
    return 0;
}
