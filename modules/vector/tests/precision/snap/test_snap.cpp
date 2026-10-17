#include "../../test_support.hpp"
#include "pikdraw/vector/precision/snap/direction_snap.hpp"
#include "pikdraw/geometry/queries/curve_intersections.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::vector::precision::snap;

document::PathObject make_line_object(document::ObjectId object_id,
                                      uint64_t id_base,
                                      const std::vector<geometry::Point2D>& points,
                                      bool closed = false) {
    document::EditablePath path;
    document::EditableSubpath subpath;
    subpath.id = document::SubpathId{id_base};
    subpath.closed = closed;
    for (size_t i = 0; i < points.size(); ++i) {
        document::PathNode node;
        node.id = document::NodeId{id_base + 1 + i};
        node.anchor = points[i];
        subpath.nodes.push_back(node);
    }
    const size_t segment_count = closed ? points.size() : points.size() - 1;
    for (size_t i = 0; i < segment_count; ++i) {
        document::EditableSegment segment;
        segment.id = document::SegmentId{id_base + 1 + points.size() + i};
        segment.kind = document::PathSegmentKind::Line;
        subpath.segments.push_back(segment);
    }
    path.subpaths.push_back(std::move(subpath));
    document::PathObject object;
    object.id = object_id;
    object.set_authoring_source(std::move(path));
    return object;
}

document::PathObject make_arc_circle_object(
    document::ObjectId object_id, uint64_t id_base,
    geometry::Point2D center, double radius) {
    document::EditablePath path;
    document::EditableSubpath subpath;
    subpath.id = document::SubpathId{id_base};
    subpath.closed = true;

    document::PathNode n0;
    n0.id = document::NodeId{id_base + 1};
    n0.anchor = {center.x + radius, center.y};
    subpath.nodes.push_back(n0);

    document::PathNode n1;
    n1.id = document::NodeId{id_base + 2};
    n1.anchor = {center.x - radius, center.y};
    subpath.nodes.push_back(n1);

    document::EditableSegment seg0;
    seg0.id = document::SegmentId{id_base + 3};
    seg0.kind = document::PathSegmentKind::EllipticArc;
    seg0.arc = geometry::EllipticArc{center, {radius, 0.0}, {0.0, radius}, 0.0, 3.141592653589793};
    subpath.segments.push_back(seg0);

    document::EditableSegment seg1;
    seg1.id = document::SegmentId{id_base + 4};
    seg1.kind = document::PathSegmentKind::EllipticArc;
    seg1.arc = geometry::EllipticArc{center, {radius, 0.0}, {0.0, radius}, 3.141592653589793, 3.141592653589793};
    subpath.segments.push_back(seg1);

    path.subpaths.push_back(std::move(subpath));
    document::PathObject object;
    object.id = object_id;
    object.set_authoring_source(std::move(path));
    return object;
}

document::PathObject make_cubic_object(
    document::ObjectId object_id, uint64_t id_base,
    geometry::Point2D p0, geometry::Point2D h0, geometry::Point2D h1, geometry::Point2D p1) {
    document::EditablePath path;
    document::EditableSubpath subpath;
    subpath.id = document::SubpathId{id_base};
    subpath.closed = false;

    document::PathNode n0;
    n0.id = document::NodeId{id_base + 1};
    n0.anchor = p0;
    n0.mode = document::PathNodeMode::Cusp;
    n0.has_outgoing_handle = true;
    n0.outgoing_handle = h0;
    subpath.nodes.push_back(n0);

    document::PathNode n1;
    n1.id = document::NodeId{id_base + 2};
    n1.anchor = p1;
    n1.mode = document::PathNodeMode::Cusp;
    n1.has_incoming_handle = true;
    n1.incoming_handle = h1;
    subpath.nodes.push_back(n1);

    document::EditableSegment seg;
    seg.id = document::SegmentId{id_base + 3};
    seg.kind = document::PathSegmentKind::CubicBezier;
    subpath.segments.push_back(seg);

    path.subpaths.push_back(std::move(subpath));
    document::PathObject object;
    object.id = object_id;
    object.set_authoring_source(std::move(path));
    return object;
}

document::DocumentSnapshot make_snapshot(std::vector<document::PathObject> paths,
                                         uint64_t epoch, uint64_t revision,
                                         std::string id) {
    auto page = document::Page::make_default();
    page.width_mm = 500000.0;
    page.height_mm = 1000.0;
    document::PersistentObjectMap objects;
    for (auto& path : paths) {
        page.layers[0].object_ids.push_back(path.id);
        objects = objects.set(path.id, std::move(path));
    }
    return {epoch, revision, std::move(page), std::move(objects), std::move(id)};
}

SnapCameraTransform camera(double pixels_per_mm, double pixels_per_dip, uint64_t generation = 1) {
    return {geometry::Transform2D::scale(pixels_per_mm, pixels_per_mm),
            pixels_per_dip, pixels_per_dip, generation};
}

SnapQueryStamp stamp_for(const document::DocumentSnapshot& snapshot,
                         const SnapCameraTransform& view,
                         const SnapConfigSnapshot& config,
                         const SnapCapabilities& capabilities,
                         uint64_t query_generation = 1,
                         std::string tool = "pen") {
    return {snapshot.document_id(), snapshot.epoch(), snapshot.revision(), view.generation,
            config.generation, capabilities.generation, std::move(tool), query_generation};
}

SnapQueryContext active_layer_zero() {
    SnapQueryContext context;
    context.active_layer_index = 0;
    return context;
}

void config_masks_and_presets() {
    SnapConfiguration configuration;
    const auto defaults = configuration.snapshot();
    CHECK(defaults.master_enabled);
    CHECK(defaults.options[static_cast<size_t>(SnapOption::Endpoint)]);
    CHECK(defaults.options[static_cast<size_t>(SnapOption::SourceNode)]);
    CHECK(defaults.options[static_cast<size_t>(SnapOption::LineMidpoint)]);
    CHECK(!defaults.options[static_cast<size_t>(SnapOption::Grid)]);

    CHECK(configuration.set_direction_mode(DirectionMode::Polar));
    CHECK(configuration.set_polar_preset(45.0));
    SnapToolOverride pen_override;
    pen_override.options[static_cast<size_t>(SnapOption::Endpoint)] = false;
    pen_override.priorities[static_cast<size_t>(SnapOption::Grid)] = -5;
    CHECK(configuration.set_tool_override("pen", pen_override));
    const auto before_off = configuration.snapshot();
    CHECK(configuration.toggle_master());
    auto disabled = configuration.snapshot();
    CHECK(!disabled.master_enabled);
    CHECK(disabled.options == before_off.options);
    CHECK(disabled.groups == before_off.groups);
    CHECK(disabled.tool_overrides == before_off.tool_overrides);
    CHECK(disabled.direction.mode == DirectionMode::Polar);
    CHECK(!effective_option_enabled(disabled, "pen", SnapOption::Endpoint));

    CHECK(configuration.toggle_master());
    CHECK(configuration.set_group_enabled(SnapGroup::ObjectPoints, false));
    const auto children_before = configuration.snapshot().options;
    CHECK(configuration.set_all_group_options(SnapGroup::ObjectPoints, true));
    auto grouped = configuration.snapshot();
    CHECK(!grouped.groups[static_cast<size_t>(SnapGroup::ObjectPoints)]);
    CHECK(grouped.options != children_before);
    CHECK(grouped.options[static_cast<size_t>(SnapOption::Endpoint)]);
    CHECK(!effective_option_enabled(grouped, "pen", SnapOption::Endpoint));
    CHECK(effective_group_enabled(grouped, "other-tool", SnapGroup::ObjectPoints) == false);

    SnapToolOverride group_reenable;
    group_reenable.groups[static_cast<size_t>(SnapGroup::ObjectPoints)] = true;
    CHECK(configuration.set_tool_override("other-tool", group_reenable));
    CHECK(effective_group_enabled(configuration.snapshot(), "other-tool",
                                  SnapGroup::ObjectPoints));
    CHECK(configuration.set_polar_preset(15.0));
    CHECK(configuration.snapshot().direction.polar.step_degrees == 15.0);
    CHECK(!configuration.set_polar_preset(20.0));
    std::string error;
    CHECK(configuration.set_custom_polar_angles({-45.0, 15.0, 205.0}, &error));
    CHECK(error.empty());
    const auto custom = configuration.snapshot().direction.polar.custom_angles_degrees;
    CHECK(custom.size() == 3);
    CHECK(custom[0] == 15.0 && custom[1] == 205.0 && custom[2] == 315.0);
    CHECK(!configuration.set_custom_polar_angles({0.0, 360.0}, &error));
    CHECK(!configuration.set_custom_polar_angles(
        {std::numeric_limits<double>::quiet_NaN()}, &error));
    CHECK(!configuration.set_hysteresis(12.0, 8.0));
    CHECK(!configuration.set_hysteresis(0.5, 1.0));
    CHECK(configuration.set_hysteresis(8.0, 12.0));

    CHECK(configuration.set_direction_mode(DirectionMode::Ortho));
    CHECK(configuration.snapshot().direction.mode == DirectionMode::Ortho);
    CHECK(configuration.toggle_direction_mode(DirectionMode::Ortho));
    CHECK(configuration.snapshot().direction.mode == DirectionMode::Free);
}

void source_candidates_dip_ties_and_hysteresis() {
    auto source = make_snapshot({
        make_line_object(100, 1000, {{0.0, 0.0}, {10.0, 0.0}, {20.0, 0.0}})},
        8, 12, "snap-source");
    const auto built = SnapSourceIndex::build(source);
    CHECK(built.status == SnapIndexBuildStatus::Success);
    CHECK(built.entry_count == 5);
    SnapConfiguration configuration;
    const auto config = configuration.snapshot();
    SnapCapabilities capabilities;
    const auto view = camera(4.0, 2.0, 9); // 2 DIP/mm after DPI conversion.
    auto stamp = stamp_for(source, view, config, capabilities);
    const auto& index = *built.index;

    auto result = index.query(stamp, {10.0, 0.0}, view, config, capabilities,
                              active_layer_zero());
    CHECK(result.status == SnapQueryStatus::Success);
    CHECK(result.chosen.has_value());
    CHECK(result.chosen->key.kind == SnapOption::SourceNode);
    CHECK(result.chosen->key.source_node_or_segment_id == 1002);
    CHECK(result.chosen->source_revision == 12);
    CHECK(result.chosen->distance_dip == 0.0);

    stamp.query_generation = 2;
    result = index.query(stamp, {5.0, 0.0}, view, config, capabilities,
                         active_layer_zero());
    CHECK(result.chosen.has_value());
    CHECK(result.chosen->key.kind == SnapOption::LineMidpoint);
    CHECK(result.chosen->key.source_node_or_segment_id == 1004);
    CHECK(result.chosen->parameter_t == 0.5);

    stamp.query_generation = 3;
    result = index.query(stamp, {0.0, 4.0}, view, config, capabilities,
                         active_layer_zero());
    CHECK(result.chosen.has_value());
    CHECK(result.chosen->key.kind == SnapOption::Endpoint);
    CHECK(result.chosen->distance_dip == 8.0);

    auto separated = make_snapshot({
        make_line_object(100, 10000, {{0.0, 0.0}, {100.0, 0.0}}),
        make_line_object(200, 20000, {{5.0, 0.0}, {105.0, 0.0}})},
        3, 7, "hysteresis");
    const auto separated_index = SnapSourceIndex::build(separated);
    CHECK(separated_index.status == SnapIndexBuildStatus::Success);
    stamp = stamp_for(separated, view, config, capabilities, 10);
    result = separated_index.index->query(stamp, {0.5, 0.0}, view, config, capabilities,
                                          active_layer_zero());
    CHECK(result.chosen.has_value());
    CHECK(result.chosen->key.source_object_id == 100);
    const auto acquired = result.next_acquisition;
    CHECK(acquired.has_value());
    stamp.query_generation = 11;
    auto without_lock = separated_index.index->query(
        stamp, {4.5, 0.0}, view, config, capabilities, active_layer_zero());
    CHECK(without_lock.chosen->key.source_object_id == 200);
    auto hysteresis_context = active_layer_zero();
    hysteresis_context.previous_acquisition = acquired;
    auto retained = separated_index.index->query(
        stamp, {4.5, 0.0}, view, config, capabilities, hysteresis_context);
    CHECK(retained.chosen.has_value());
    CHECK(retained.chosen->key.source_object_id == 100);
    CHECK(retained.chosen->distance_dip == 9.0);

    stamp.query_generation = 12;
    auto tied = separated_index.index->query(
        stamp, {2.5, 0.0}, view, config, capabilities, active_layer_zero());
    CHECK(tied.chosen.has_value());
    CHECK(tied.chosen->key.source_object_id == 100);
}

void grid_stale_and_unsupported_filtering() {
    auto empty = make_snapshot({}, 4, 2, "grid-doc");
    const auto empty_index = SnapSourceIndex::build(empty);
    CHECK(empty_index.status == SnapIndexBuildStatus::Success);
    SnapConfiguration grid_configuration;
    CHECK(grid_configuration.set_option_enabled(SnapOption::Grid, true));
    CHECK(grid_configuration.set_grid({0.0, 0.0, 10.0, 64}));
    const auto grid_config = grid_configuration.snapshot();
    SnapCapabilities capabilities;
    const auto grid_view = camera(2.0, 2.0, 2);
    auto grid_stamp = stamp_for(empty, grid_view, grid_config, capabilities);
    const auto missing_layer = empty_index.index->query(grid_stamp, {4.0, 4.0}, grid_view,
                                                        grid_config, capabilities);
    CHECK(missing_layer.status == SnapQueryStatus::MissingActiveLayer);
    CHECK(missing_layer.stats.kd_nodes_visited == 0);
    const auto grid = empty_index.index->query(grid_stamp, {4.0, 4.0}, grid_view,
                                               grid_config, capabilities, active_layer_zero());
    CHECK(grid.status == SnapQueryStatus::Success);
    CHECK(grid.chosen.has_value());
    CHECK(grid.chosen->key.kind == SnapOption::Grid);
    CHECK(grid.chosen->key.grid_cell.has_value());
    CHECK(grid.chosen->key.grid_cell->x == 0 && grid.chosen->key.grid_cell->y == 0);

    auto source = make_snapshot({
        make_line_object(300, 30000, {{0.0, 0.0}, {10.0, 0.0}})}, 8, 12, "stale-doc");
    const auto index = SnapSourceIndex::build(source);
    CHECK(index.status == SnapIndexBuildStatus::Success);
    SnapConfiguration configuration;
    auto current = configuration.snapshot();
    const auto view = camera(2.0, 2.0, 5);
    auto stamp = stamp_for(source, view, current, capabilities);
    auto valid = index.index->query(stamp, {0.0, 0.0}, view, current, capabilities,
                                    active_layer_zero());
    CHECK(valid.chosen.has_value());
    CHECK(snap_result_is_current(valid.stamp, stamp));

    const auto reject_before_traversal = [&](const SnapQueryStamp& obsolete,
                                              SnapQueryStatus expected) {
        auto context = active_layer_zero();
        context.current_stamp = stamp;
        const auto rejected = index.index->query(obsolete, {0.0, 0.0}, view, current,
                                                  capabilities, context);
        CHECK(rejected.status == expected);
        CHECK(rejected.stats.kd_nodes_visited == 0);
    };
    auto stale_document_id = stamp;
    stale_document_id.document_id = "stale-doc-reopened";
    reject_before_traversal(stale_document_id, SnapQueryStatus::StaleSource);
    auto stale_epoch = stamp;
    ++stale_epoch.document_epoch;
    reject_before_traversal(stale_epoch, SnapQueryStatus::StaleSource);
    auto stale_revision = stamp;
    ++stale_revision.document_revision;
    reject_before_traversal(stale_revision, SnapQueryStatus::StaleSource);
    auto stale_camera = stamp;
    ++stale_camera.camera_generation;
    reject_before_traversal(stale_camera, SnapQueryStatus::StaleCamera);
    auto stale_tool = stamp;
    stale_tool.tool_context = "node";
    reject_before_traversal(stale_tool, SnapQueryStatus::StaleToolContext);
    auto stale_query = stamp;
    ++stale_query.query_generation;
    reject_before_traversal(stale_query, SnapQueryStatus::StaleQuery);

    CHECK(configuration.set_option_priority(SnapOption::Endpoint, -1));
    const auto newer_config = configuration.snapshot();
    const auto stale_config = index.index->query(stamp, {0.0, 0.0}, view, newer_config,
                                                  capabilities, active_layer_zero());
    CHECK(stale_config.status == SnapQueryStatus::StaleConfig);
    CHECK(stale_config.stats.kd_nodes_visited == 0);

    auto newer_capabilities = capabilities;
    ++newer_capabilities.generation;
    const auto stale_capability = index.index->query(stamp, {0.0, 0.0}, view, current,
        newer_capabilities, active_layer_zero());
    CHECK(stale_capability.status == SnapQueryStatus::StaleCapabilities);
    CHECK(stale_capability.stats.kd_nodes_visited == 0);
    auto newer_query = stamp;
    ++newer_query.query_generation;
    CHECK(!snap_result_is_current(stamp, newer_query));

    SnapConfiguration unsupported_configuration;
    CHECK(unsupported_configuration.set_option_enabled(SnapOption::Endpoint, false));
    CHECK(unsupported_configuration.set_option_enabled(SnapOption::SourceNode, false));
    CHECK(unsupported_configuration.set_option_enabled(SnapOption::LineMidpoint, false));
    CHECK(unsupported_configuration.set_option_enabled(SnapOption::Tangent, true));
    const auto unsupported_config = unsupported_configuration.snapshot();
    const auto unsupported_stamp = stamp_for(source, view, unsupported_config, capabilities);
    const auto unsupported = index.index->query(unsupported_stamp, {0.0, 0.0}, view,
        unsupported_config, capabilities, active_layer_zero());
    CHECK(unsupported.status == SnapQueryStatus::Success);
    CHECK(unsupported.candidates.empty());
    CHECK(unsupported.stats.kd_nodes_visited == 0);
    for (const size_t count : unsupported.stats.kind_tree_queries) CHECK(count == 0);
}

void large_source_index_benchmark() {
    constexpr size_t node_count = 100001;
    constexpr uint64_t id_base = 10000000000ULL;
    document::EditablePath path;
    document::EditableSubpath subpath;
    subpath.id = document::SubpathId{id_base};
    subpath.closed = true;
    subpath.nodes.reserve(node_count);
    subpath.segments.reserve(node_count);
    for (size_t i = 0; i < node_count; ++i) {
        document::PathNode node;
        node.id = document::NodeId{id_base + 1 + i};
        node.anchor = {static_cast<double>(i) * 3.0, 0.0};
        subpath.nodes.push_back(node);
        document::EditableSegment segment;
        segment.id = document::SegmentId{id_base + 1 + node_count + i};
        segment.kind = document::PathSegmentKind::Line;
        subpath.segments.push_back(segment);
    }
    path.subpaths.push_back(std::move(subpath));
    document::PathObject object;
    object.id = 9000000000ULL;
    object.set_authoring_source(std::move(path));
    auto source = make_snapshot({std::move(object)}, 1, 1, "large-source");

    const auto build_begin = std::chrono::steady_clock::now();
    const auto built = SnapSourceIndex::build(source);
    const auto build_end = std::chrono::steady_clock::now();
    CHECK(built.status == SnapIndexBuildStatus::Success);
    CHECK(built.entry_count == node_count * 2);

    SnapConfiguration configuration;
    CHECK(configuration.set_option_enabled(SnapOption::LineMidpoint, false));
    const auto config = configuration.snapshot();
    SnapCapabilities capabilities;
    const auto view = camera(2.0, 2.0, 3);
    auto stamp = stamp_for(source, view, config, capabilities, 1, "node");
    auto context = active_layer_zero();
    const auto query_begin = std::chrono::steady_clock::now();
    size_t total_visited = 0;
    for (uint64_t i = 0; i < 1000; ++i) {
        stamp.query_generation = i + 1;
        const double x = 150000.0 + static_cast<double>(i % 10) * 0.01;
        const auto result = built.index->query(stamp, {x, 0.0}, view, config,
                                               capabilities, context);
        CHECK(result.status == SnapQueryStatus::Success);
        CHECK(result.chosen.has_value());
        CHECK(result.chosen->key.kind == SnapOption::SourceNode);
        CHECK(result.stats.source_entries_examined < 500);
        CHECK(result.stats.kind_tree_queries[static_cast<size_t>(SnapOption::LineMidpoint)] == 0);
        total_visited += result.stats.kd_nodes_visited;
    }
    const auto query_end = std::chrono::steady_clock::now();
    const auto build_ms = std::chrono::duration<double, std::milli>(build_end - build_begin).count();
    const auto query_ms = std::chrono::duration<double, std::milli>(query_end - query_begin).count();
    std::cout << "BENCH source_nodes=" << node_count
              << " indexed_candidates=" << built.entry_count
              << " build_ms=" << build_ms
              << " steady_queries=1000 steady_total_ms=" << query_ms
              << " mean_nodes_visited=" << static_cast<double>(total_visited) / 1000.0
              << " (single run; no percentile claim)\n";
}

void advanced_osnap_features() {
    // 1. AnalyticCenter & Quadrant
    const auto circle_obj = make_arc_circle_object(10, 1000, {100.0, 100.0}, 50.0);
    auto snapshot_circle = make_snapshot({circle_obj}, 1, 1, "doc-circle");
    const auto built_circle = SnapSourceIndex::build(snapshot_circle);
    CHECK(built_circle.status == SnapIndexBuildStatus::Success);

    SnapConfiguration configuration;
    CHECK(configuration.set_option_enabled(SnapOption::AnalyticCenter, true));
    CHECK(configuration.set_option_enabled(SnapOption::Quadrant, true));
    CHECK(configuration.set_option_enabled(SnapOption::Endpoint, false));
    CHECK(configuration.set_option_enabled(SnapOption::SourceNode, false));
    auto config = configuration.snapshot();
    SnapCapabilities capabilities;
    const auto view = camera(2.0, 2.0, 1);
    auto stamp = stamp_for(snapshot_circle, view, config, capabilities, 1, "pen");

    // Center query: hover near center (100, 100)
    auto res_center = built_circle.index->query(stamp, {100.0, 100.5}, view, config, capabilities, active_layer_zero());
    CHECK(res_center.status == SnapQueryStatus::Success);
    CHECK(res_center.chosen.has_value());
    CHECK(res_center.chosen->key.kind == SnapOption::AnalyticCenter);
    CHECK(res_center.chosen->world_point.x == 100.0);
    CHECK(res_center.chosen->world_point.y == 100.0);

    // Quadrant query: hover near (100, 150)
    stamp.query_generation = 2;
    auto res_quad = built_circle.index->query(stamp, {100.0, 149.0}, view, config, capabilities, active_layer_zero());
    CHECK(res_quad.status == SnapQueryStatus::Success);
    CHECK(res_quad.chosen.has_value());
    CHECK(res_quad.chosen->key.kind == SnapOption::Quadrant);
    CHECK(std::abs(res_quad.chosen->world_point.x - 100.0) < 1e-3);
    CHECK(std::abs(res_quad.chosen->world_point.y - 150.0) < 1e-3);

    // 2. BezierTMidpoint & HalfCurveLength
    const auto cubic_obj = make_cubic_object(20, 2000, {0.0, 0.0}, {5.0, 0.0}, {10.0, 0.0}, {100.0, 60.0});
    auto snapshot_cubic = make_snapshot({cubic_obj}, 1, 1, "doc-cubic");
    const auto built_cubic = SnapSourceIndex::build(snapshot_cubic);
    CHECK(built_cubic.status == SnapIndexBuildStatus::Success);

    // TMid query
    CHECK(configuration.set_option_enabled(SnapOption::BezierTMidpoint, true));
    CHECK(configuration.set_option_enabled(SnapOption::HalfCurveLength, false));
    config = configuration.snapshot();
    stamp = stamp_for(snapshot_cubic, view, config, capabilities, 10, "pen");
    // t=0.5 point of the cubic
    geometry::CubicBezier cb{geometry::Point2D{0.0, 0.0}, geometry::Point2D{5.0, 0.0}, geometry::Point2D{10.0, 0.0}, geometry::Point2D{100.0, 60.0}};
    geometry::Point2D tmid_pt = cb.point_at(0.5);
    auto res_tmid = built_cubic.index->query(stamp, {tmid_pt.x, tmid_pt.y + 0.5}, view, config, capabilities, active_layer_zero());
    CHECK(res_tmid.chosen.has_value());
    CHECK(res_tmid.chosen->key.kind == SnapOption::BezierTMidpoint);
    CHECK(std::abs(res_tmid.chosen->world_point.x - tmid_pt.x) < 1e-3);
    CHECK(std::abs(res_tmid.chosen->world_point.y - tmid_pt.y) < 1e-3);

    // Half length query
    CHECK(configuration.set_option_enabled(SnapOption::BezierTMidpoint, false));
    CHECK(configuration.set_option_enabled(SnapOption::HalfCurveLength, true));
    config = configuration.snapshot();
    stamp = stamp_for(snapshot_cubic, view, config, capabilities, 11, "pen");
    auto half_eval = geometry::find_half_curve_length(cb);
    auto res_half = built_cubic.index->query(stamp, {half_eval.point.x, half_eval.point.y + 0.5}, view, config, capabilities, active_layer_zero());
    CHECK(res_half.chosen.has_value());
    CHECK(res_half.chosen->key.kind == SnapOption::HalfCurveLength);
    CHECK(std::abs(res_half.chosen->world_point.x - half_eval.point.x) < 1e-3);
    CHECK(std::abs(res_half.chosen->world_point.y - half_eval.point.y) < 1e-3);

    // 3. Nearest
    CHECK(configuration.set_option_enabled(SnapOption::HalfCurveLength, false));
    CHECK(configuration.set_option_enabled(SnapOption::Nearest, true));
    config = configuration.snapshot();
    stamp = stamp_for(snapshot_cubic, view, config, capabilities, 12, "pen");
    geometry::Point2D near_target = cb.point_at(0.75);
    auto res_nearest = built_cubic.index->query(stamp, {near_target.x, near_target.y + 1.0}, view, config, capabilities, active_layer_zero());
    CHECK(res_nearest.chosen.has_value());
    CHECK(res_nearest.chosen->key.kind == SnapOption::Nearest);
    CHECK(std::abs(res_nearest.chosen->parameter_t - 0.75) < 0.1);

    // 4. Intersection
    auto l1 = make_line_object(30, 3000, {{0.0, 0.0}, {100.0, 100.0}});
    auto l2 = make_line_object(40, 4000, {{0.0, 30.0}, {100.0, 30.0}});
    auto snapshot_intersect = make_snapshot({l1, l2}, 1, 1, "doc-intersect");
    const auto built_intersect = SnapSourceIndex::build(snapshot_intersect);
    CHECK(configuration.set_option_enabled(SnapOption::Nearest, false));
    CHECK(configuration.set_option_enabled(SnapOption::Intersection, true));
    config = configuration.snapshot();
    stamp = stamp_for(snapshot_intersect, view, config, capabilities, 20, "pen");
    auto res_int = built_intersect.index->query(stamp, {30.0, 30.5}, view, config, capabilities, active_layer_zero());
    CHECK(res_int.chosen.has_value());
    CHECK(res_int.chosen->key.kind == SnapOption::Intersection);
    CHECK(std::abs(res_int.chosen->world_point.x - 30.0) < 1e-3);
    CHECK(std::abs(res_int.chosen->world_point.y - 30.0) < 1e-3);
    CHECK(res_int.chosen->key.source_object_id == 30);
    CHECK(res_int.chosen->key.secondary_object_id == 40);

    // 5. Perpendicular & Tangent from anchor
    CHECK(configuration.set_option_enabled(SnapOption::Intersection, false));
    CHECK(configuration.set_option_enabled(SnapOption::Quadrant, false));
    CHECK(configuration.set_option_enabled(SnapOption::AnalyticCenter, false));
    CHECK(configuration.set_option_enabled(SnapOption::Perpendicular, true));
    config = configuration.snapshot();
    stamp = stamp_for(snapshot_circle, view, config, capabilities, 30, "pen");
    auto perp_ctx = active_layer_zero();
    perp_ctx.external_anchor = geometry::Point2D{100.0, 200.0};
    auto res_perp = built_circle.index->query(stamp, {100.0, 150.5}, view, config, capabilities, perp_ctx);
    CHECK(res_perp.chosen.has_value());
    CHECK(res_perp.chosen->key.kind == SnapOption::Perpendicular);
    CHECK(std::abs(res_perp.chosen->world_point.x - 100.0) < 1e-3);
    CHECK(std::abs(res_perp.chosen->world_point.y - 150.0) < 1e-3);

    CHECK(configuration.set_option_enabled(SnapOption::Perpendicular, false));
    CHECK(configuration.set_option_enabled(SnapOption::Tangent, true));
    config = configuration.snapshot();
    stamp = stamp_for(snapshot_circle, view, config, capabilities, 31, "pen");
    auto res_tan = built_circle.index->query(stamp, {56.7, 125.0}, view, config, capabilities, perp_ctx);
    CHECK(res_tan.chosen.has_value());
    CHECK(res_tan.chosen->key.kind == SnapOption::Tangent);
    CHECK(std::abs(res_tan.chosen->world_point.y - 125.0) < 1.0);
}

int main() {
    return run([] {
        config_masks_and_presets();
        source_candidates_dip_ties_and_hysteresis();
        grid_stale_and_unsupported_filtering();
        large_source_index_benchmark();
        advanced_osnap_features();
    });
}
