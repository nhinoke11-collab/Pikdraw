#include "pikdraw/vector/precision/snap/snap_index.hpp"
#include "snap_math.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/geometry/curves/curve.hpp"
#include "pikdraw/geometry/queries/curve_intersections.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace pikdraw::vector::precision::snap {
namespace {

using detail::distance_dip;
using detail::finite_point;
using detail::SnapWorldBounds;

constexpr size_t index_of(SnapOption option) noexcept {
    return static_cast<size_t>(option);
}

constexpr size_t kNoNode = std::numeric_limits<size_t>::max();
constexpr double kInt64MinAsDouble = -9223372036854775808.0;
constexpr double kInt64UpperExclusiveAsDouble = 9223372036854775808.0;

struct LayerInfo {
    size_t index{0};
    bool visible{true};
    bool locked{false};
};

struct IndexedPoint {
    SnapOption kind{SnapOption::Endpoint};
    document::ObjectId object_id{document::kInvalidObjectId};
    uint64_t element_id{0};
    double parameter_t{0.0};
    geometry::Point2D world_point;
    size_t layer_index{0};
    bool object_visible{true};
    bool object_locked{false};
    bool layer_visible{true};
    bool layer_locked{false};
};

bool intersects(const SnapWorldBounds& first, const SnapWorldBounds& second) noexcept {
    return first.min_x <= second.max_x && first.max_x >= second.min_x &&
           first.min_y <= second.max_y && first.max_y >= second.min_y;
}

bool same_acquisition_context(const SnapQueryStamp& first,
                              const SnapQueryStamp& second) noexcept {
    return first.document_id == second.document_id &&
           first.document_epoch == second.document_epoch &&
           first.document_revision == second.document_revision &&
           first.camera_generation == second.camera_generation &&
           first.config_generation == second.config_generation &&
           first.capability_generation == second.capability_generation &&
           first.tool_context == second.tool_context &&
           first.query_generation < second.query_generation;
}

bool candidate_less(const SnapCandidate& first, const SnapCandidate& second) noexcept {
    if (first.per_tool_priority != second.per_tool_priority)
        return first.per_tool_priority < second.per_tool_priority;
    if (first.configured_kind_priority != second.configured_kind_priority)
        return first.configured_kind_priority < second.configured_kind_priority;
    if (first.distance_dip != second.distance_dip)
        return first.distance_dip < second.distance_dip;
    const auto stable_key = [](const SnapCandidate& candidate) {
        const auto object = candidate.key.source_object_id.value_or(0);
        const auto element = candidate.key.source_node_or_segment_id.value_or(0);
        const auto sec_object = candidate.key.secondary_object_id.value_or(0);
        const auto sec_element = candidate.key.secondary_element_id.value_or(0);
        const auto cell_x = candidate.key.grid_cell ? candidate.key.grid_cell->x : 0;
        const auto cell_y = candidate.key.grid_cell ? candidate.key.grid_cell->y : 0;
        return std::tuple{object, element, sec_object, sec_element,
                          static_cast<uint8_t>(candidate.key.kind),
                          candidate.key.grid_cell.has_value(), cell_x, cell_y,
                          candidate.parameter_t, candidate.secondary_parameter_t};
    };
    return stable_key(first) < stable_key(second);
}

bool within_i64(double value) noexcept {
    return std::isfinite(value) && value >= kInt64MinAsDouble &&
           value < kInt64UpperExclusiveAsDouble;
}

std::optional<int64_t> grid_index(double coordinate, double origin, double spacing,
                                  bool round_up) noexcept {
    const double quotient = (coordinate - origin) / spacing;
    if (!std::isfinite(quotient)) return std::nullopt;
    const double integral = round_up ? std::ceil(quotient) : std::floor(quotient);
    if (!within_i64(integral)) return std::nullopt;
    return static_cast<int64_t>(integral);
}

bool previous_matches(const SnapQueryContext& context, const SnapQueryStamp& stamp) noexcept {
    return context.previous_acquisition &&
           same_acquisition_context(context.previous_acquisition->stamp, stamp);
}

} // namespace

struct SnapSourceIndex::Impl {
    struct Node {
        IndexedPoint point;
        size_t left{kNoNode};
        size_t right{kNoNode};
        SnapWorldBounds bounds;
    };
    struct Tree {
        std::vector<Node> nodes;
        size_t root{kNoNode};
        size_t entry_count{0};
    };
    struct IndexedCurve {
        document::ObjectId object_id{document::kInvalidObjectId};
        uint64_t segment_id{0};
        geometry::CurveSegment world_curve;
        SnapWorldBounds bounds;
        size_t layer_index{0};
        bool object_visible{true};
        bool object_locked{false};
        bool layer_visible{true};
        bool layer_locked{false};
    };

    std::string document_id;
    uint64_t epoch{0};
    uint64_t revision{0};
    size_t layers_count{0};
    size_t entry_count{0};
    std::array<Tree, kSnapOptionCount> trees;
    std::vector<IndexedCurve> curves;
};

namespace {

SnapWorldBounds point_bounds(geometry::Point2D point) noexcept {
    return {point.x, point.y, point.x, point.y};
}

void include_bounds(SnapWorldBounds& target, const SnapWorldBounds& other) noexcept {
    target.min_x = std::min(target.min_x, other.min_x);
    target.min_y = std::min(target.min_y, other.min_y);
    target.max_x = std::max(target.max_x, other.max_x);
    target.max_y = std::max(target.max_y, other.max_y);
}

size_t build_tree(std::vector<IndexedPoint>& points, std::vector<SnapSourceIndex::Impl::Node>& nodes,
                 size_t first, size_t last, size_t depth,
                 const std::function<bool()>& should_cancel, size_t& work_count,
                 bool& cancelled) {
    if (first >= last) return kNoNode;
    if (((++work_count & 255u) == 0u) && should_cancel && should_cancel()) {
        cancelled = true;
        return kNoNode;
    }
    const size_t middle = first + (last - first) / 2;
    const size_t axis = depth % 2;
    const auto compare = [axis](const IndexedPoint& left, const IndexedPoint& right) {
        const double left_axis = axis == 0 ? left.world_point.x : left.world_point.y;
        const double right_axis = axis == 0 ? right.world_point.x : right.world_point.y;
        if (left_axis != right_axis) return left_axis < right_axis;
        const double left_other = axis == 0 ? left.world_point.y : left.world_point.x;
        const double right_other = axis == 0 ? right.world_point.y : right.world_point.x;
        if (left_other != right_other) return left_other < right_other;
        return std::tie(left.object_id, left.element_id, left.parameter_t) <
               std::tie(right.object_id, right.element_id, right.parameter_t);
    };
    std::nth_element(points.begin() + static_cast<std::ptrdiff_t>(first),
                     points.begin() + static_cast<std::ptrdiff_t>(middle),
                     points.begin() + static_cast<std::ptrdiff_t>(last), compare);
    const size_t index = nodes.size();
    nodes.push_back({points[middle], kNoNode, kNoNode, point_bounds(points[middle].world_point)});
    const size_t left = build_tree(points, nodes, first, middle, depth + 1,
                                   should_cancel, work_count, cancelled);
    if (cancelled) return kNoNode;
    const size_t right = build_tree(points, nodes, middle + 1, last, depth + 1,
                                    should_cancel, work_count, cancelled);
    if (cancelled) return kNoNode;
    nodes[index].left = left;
    nodes[index].right = right;
    if (left != kNoNode) include_bounds(nodes[index].bounds, nodes[left].bounds);
    if (right != kNoNode) include_bounds(nodes[index].bounds, nodes[right].bounds);
    return index;
}

void append_source_point(std::array<std::vector<IndexedPoint>, kSnapOptionCount>& points,
                         const document::PathObject& object, const LayerInfo& layer,
                         SnapOption kind, uint64_t element_id, double parameter_t,
                         geometry::Point2D local_point, size_t max_entries,
                         size_t& entry_count, bool& limit_exceeded, bool& invalid) {
    if (element_id == 0 || !finite_point(local_point)) {
        invalid = true;
        return;
    }
    const auto world = object.transform.map_point(local_point);
    if (!finite_point(world)) {
        invalid = true;
        return;
    }
    if (entry_count >= max_entries) {
        limit_exceeded = true;
        return;
    }
    points[index_of(kind)].push_back({kind, object.id, element_id, parameter_t, world,
                                      layer.index, object.visible, object.locked,
                                      layer.visible, layer.locked});
    ++entry_count;
}

void append_source_point_world(std::array<std::vector<IndexedPoint>, kSnapOptionCount>& points,
                               document::ObjectId object_id, const LayerInfo& layer,
                               SnapOption kind, uint64_t element_id, double parameter_t,
                               geometry::Point2D world_point, bool object_visible, bool object_locked,
                               size_t max_entries, size_t& entry_count,
                               bool& limit_exceeded, bool& invalid) {
    if (element_id == 0 || !finite_point(world_point)) {
        invalid = true;
        return;
    }
    if (entry_count >= max_entries) {
        limit_exceeded = true;
        return;
    }
    points[index_of(kind)].push_back({kind, object_id, element_id, parameter_t, world_point,
                                      layer.index, object_visible, object_locked,
                                      layer.visible, layer.locked});
    ++entry_count;
}

geometry::CurveSegment map_curve_to_world(
    const geometry::CurveSegment& local, const geometry::Transform2D& xform) {
    return std::visit([&xform](const auto& seg) -> geometry::CurveSegment {
        using T = std::decay_t<decltype(seg)>;
        if constexpr (std::is_same_v<T, geometry::LineSegment>) {
            return geometry::LineSegment{xform.map_point(seg.p0), xform.map_point(seg.p1)};
        } else if constexpr (std::is_same_v<T, geometry::QuadraticBezier>) {
            return geometry::QuadraticBezier{xform.map_point(seg.p0), xform.map_point(seg.p1), xform.map_point(seg.p2)};
        } else if constexpr (std::is_same_v<T, geometry::CubicBezier>) {
            return geometry::CubicBezier{xform.map_point(seg.p0), xform.map_point(seg.p1), xform.map_point(seg.p2), xform.map_point(seg.p3)};
        } else {
            return seg.transformed(xform);
        }
    }, local);
}

} // namespace

SnapCapabilities::SnapCapabilities() noexcept {
    enabled[index_of(SnapOption::Endpoint)] = true;
    enabled[index_of(SnapOption::SourceNode)] = true;
    enabled[index_of(SnapOption::LineMidpoint)] = true;
    enabled[index_of(SnapOption::AnalyticCenter)] = true;
    enabled[index_of(SnapOption::Quadrant)] = true;
    enabled[index_of(SnapOption::Nearest)] = true;
    enabled[index_of(SnapOption::BezierTMidpoint)] = true;
    enabled[index_of(SnapOption::HalfCurveLength)] = true;
    enabled[index_of(SnapOption::Intersection)] = true;
    enabled[index_of(SnapOption::Perpendicular)] = true;
    enabled[index_of(SnapOption::Tangent)] = true;
    enabled[index_of(SnapOption::ReferenceTracking)] = true;
    enabled[index_of(SnapOption::Grid)] = true;
}

bool SnapCapabilities::supports(SnapOption option) const noexcept {
    return is_supported_snap_candidate(option) && enabled[index_of(option)];
}

SnapIndexBuildResult SnapSourceIndex::build(const document::DocumentSnapshot& source,
                                            SnapIndexBuildLimits limits) {
    if (source.document_id().empty() || source.epoch() == 0 || limits.max_entries == 0)
        return {SnapIndexBuildStatus::InvalidSource, {}, 0,
                "A source index requires a document identity and a positive entry limit"};
    if (limits.should_cancel && limits.should_cancel())
        return {SnapIndexBuildStatus::Cancelled, {}, 0, "Source snap index build was cancelled"};
    size_t cancellation_work = 0;
    const auto cancellation_requested = [&]() {
        return limits.should_cancel && ((++cancellation_work & 255u) == 0u) &&
            limits.should_cancel();
    };
    std::unordered_map<document::ObjectId, LayerInfo> layers;
    size_t layer_index = 0;
    for (const auto& layer : source.page().layers) {
        for (const auto id : layer.object_ids)
            layers.emplace(id, LayerInfo{layer_index, layer.visible, layer.locked});
        ++layer_index;
    }

    std::array<std::vector<IndexedPoint>, kSnapOptionCount> points;
    std::vector<SnapSourceIndex::Impl::IndexedCurve> indexed_curves;
    size_t entry_count = 0;
    bool limit_exceeded = false;
    bool invalid = false;
    bool cancelled = false;
    for (const auto& [id, value] : source.objects().all()) {
        if (cancellation_requested()) { cancelled = true; break; }
        const auto* source_object = std::get_if<document::PathObject>(&value);
        if (!source_object || !source_object->authoring_source) continue;
        const auto* hierarchy=source.hierarchy().find(id);
        if(!hierarchy) { invalid=true; break; }
        auto world_object=*source_object; world_object.transform=hierarchy->world;
        world_object.visible=hierarchy->visible; world_object.locked=hierarchy->locked;
        const auto* object=&world_object;
        layers.insert_or_assign(id,LayerInfo{hierarchy->layer,hierarchy->visible,hierarchy->locked});
        const auto layer = layers.find(id);
        if (layer == layers.end() || id == document::kInvalidObjectId ||
            !object->transform.is_finite()) {
            invalid = true;
            break;
        }
        const auto& source_path = *object->authoring_source;
        for (const auto& subpath : source_path.subpaths) {
            if (cancellation_requested()) { cancelled = true; break; }
            const size_t expected_segments = subpath.closed ? subpath.nodes.size()
                : (subpath.nodes.empty() ? 0 : subpath.nodes.size() - 1);
            if (subpath.nodes.empty() || (subpath.closed && subpath.nodes.size() < 2) ||
                subpath.segments.size() != expected_segments) {
                invalid = true;
                break;
            }
            for (size_t i = 0; i < subpath.nodes.size(); ++i) {
                if (cancellation_requested()) { cancelled = true; break; }
                const bool open_endpoint = !subpath.closed &&
                    (i == 0 || i + 1 == subpath.nodes.size());
                const auto kind = open_endpoint ? SnapOption::Endpoint : SnapOption::SourceNode;
                append_source_point(points, *object, layer->second, kind,
                    subpath.nodes[i].id.value(), 0.0, subpath.nodes[i].anchor,
                    limits.max_entries, entry_count, limit_exceeded, invalid);
                if (limit_exceeded || invalid) break;
            }
            if (limit_exceeded || invalid || cancelled) break;
            for (size_t i = 0; i < subpath.segments.size(); ++i) {
                if (cancellation_requested()) { cancelled = true; break; }
                const auto& segment = subpath.segments[i];
                const auto& start = subpath.nodes[i].anchor;
                const auto& end = subpath.nodes[(i + 1) % subpath.nodes.size()].anchor;

                geometry::CurveSegment local_curve = geometry::LineSegment{start, end};
                if (segment.kind == document::PathSegmentKind::Line) {
                    local_curve = geometry::LineSegment{start, end};
                    const geometry::Point2D midpoint{start.x + (end.x - start.x) * 0.5,
                                                     start.y + (end.y - start.y) * 0.5};
                    append_source_point(points, *object, layer->second, SnapOption::LineMidpoint,
                        segment.id.value(), 0.5, midpoint, limits.max_entries,
                        entry_count, limit_exceeded, invalid);
                } else if (segment.kind == document::PathSegmentKind::QuadraticBezier) {
                    local_curve = geometry::QuadraticBezier{start, segment.quadratic_control, end};
                    const auto quad = std::get<geometry::QuadraticBezier>(local_curve);
                    append_source_point(points, *object, layer->second, SnapOption::BezierTMidpoint,
                        segment.id.value(), 0.5, quad.point_at(0.5), limits.max_entries,
                        entry_count, limit_exceeded, invalid);
                } else if (segment.kind == document::PathSegmentKind::CubicBezier) {
                    const auto [start_in, start_out] = document::editable_path_handles_for(subpath, i);
                    const auto [end_in, end_out] = document::editable_path_handles_for(subpath, (i + 1) % subpath.nodes.size());
                    (void)start_in; (void)end_out;
                    local_curve = geometry::CubicBezier{start, start_out, end_in, end};
                    const auto cubic = std::get<geometry::CubicBezier>(local_curve);
                    append_source_point(points, *object, layer->second, SnapOption::BezierTMidpoint,
                        segment.id.value(), 0.5, cubic.point_at(0.5), limits.max_entries,
                        entry_count, limit_exceeded, invalid);
                } else if (segment.kind == document::PathSegmentKind::EllipticArc) {
                    local_curve = segment.arc;
                }

                if (limit_exceeded || invalid) break;

                const auto world_curve = map_curve_to_world(local_curve, object->transform);
                const auto cb = geometry::curve_bounds(world_curve);
                indexed_curves.push_back({
                    object->id, segment.id.value(), world_curve,
                    SnapWorldBounds{cb.left(), cb.top(), cb.right(), cb.bottom()},
                    layer->second.index, object->visible, object->locked,
                    layer->second.visible, layer->second.locked
                });

                if (segment.kind != document::PathSegmentKind::Line) {
                    const auto half_res = geometry::find_half_curve_length(world_curve);
                    if (half_res.status == geometry::CurveQueryStatus::BoundedApproximation ||
                        half_res.status == geometry::CurveQueryStatus::ExactForPrimitive) {
                        append_source_point_world(points, object->id, layer->second,
                            SnapOption::HalfCurveLength, segment.id.value(),
                            half_res.parameter, half_res.point, object->visible, object->locked,
                            limits.max_entries, entry_count, limit_exceeded, invalid);
                    }
                }

                if (segment.kind == document::PathSegmentKind::EllipticArc) {
                    const auto* world_arc = std::get_if<geometry::EllipticArc>(&world_curve);
                    if (world_arc) {
                        append_source_point_world(points, object->id, layer->second,
                            SnapOption::AnalyticCenter, segment.id.value(), 0.0,
                            world_arc->center, object->visible, object->locked,
                            limits.max_entries, entry_count, limit_exceeded, invalid);

                        const auto q_res = geometry::find_quadrant_points(*world_arc);
                        if (q_res.status == geometry::CurveQueryStatus::ExactForPrimitive) {
                            for (const auto& qp : q_res.points) {
                                append_source_point_world(points, object->id, layer->second,
                                    SnapOption::Quadrant, segment.id.value(), qp.parameter,
                                    qp.point, object->visible, object->locked,
                                    limits.max_entries, entry_count, limit_exceeded, invalid);
                                if (limit_exceeded || invalid) break;
                            }
                        }
                    }
                }
                if (limit_exceeded || invalid) break;
            }
            if (limit_exceeded || invalid || cancelled) break;
        }
        if (limit_exceeded || invalid || cancelled) break;
    }
    if (!cancelled && limits.should_cancel && limits.should_cancel()) cancelled = true;
    if (cancelled)
        return {SnapIndexBuildStatus::Cancelled, {}, entry_count,
                "Source snap index build was cancelled"};
    if (limit_exceeded)
        return {SnapIndexBuildStatus::EntryLimitExceeded, {}, entry_count,
                "Source snap index reached its configured candidate limit"};
    if (invalid)
        return {SnapIndexBuildStatus::InvalidSource, {}, entry_count,
                "Source geometry or object identity was invalid"};

    auto impl = std::make_shared<Impl>();
    impl->document_id = source.document_id();
    impl->epoch = source.epoch();
    impl->revision = source.revision();
    impl->layers_count = source.page().layers.size();
    impl->entry_count = entry_count;
    impl->curves = std::move(indexed_curves);
    for (size_t kind = 0; kind < kSnapOptionCount; ++kind) {
        if (limits.should_cancel && limits.should_cancel())
            return {SnapIndexBuildStatus::Cancelled, {}, entry_count,
                    "Source snap index build was cancelled"};
        auto& source_points = points[kind];
        if (source_points.empty()) continue;
        auto& tree = impl->trees[kind];
        tree.nodes.reserve(source_points.size());
        tree.entry_count = source_points.size();
        size_t tree_work = 0;
        bool tree_cancelled = false;
        tree.root = build_tree(source_points, tree.nodes, 0, source_points.size(), 0,
            limits.should_cancel, tree_work, tree_cancelled);
        if (tree_cancelled || (limits.should_cancel && limits.should_cancel()))
            return {SnapIndexBuildStatus::Cancelled, {}, entry_count,
                    "Source snap index build was cancelled"};
    }
    auto index = std::shared_ptr<const SnapSourceIndex>(new SnapSourceIndex(std::move(impl)));
    return {SnapIndexBuildStatus::Success, std::move(index), entry_count, {}};
}

uint64_t SnapSourceIndex::document_epoch() const noexcept { return impl_->epoch; }
uint64_t SnapSourceIndex::document_revision() const noexcept { return impl_->revision; }
const std::string& SnapSourceIndex::document_id() const noexcept { return impl_->document_id; }
size_t SnapSourceIndex::entry_count() const noexcept { return impl_->entry_count; }

SnapQueryResult SnapSourceIndex::query(
    const SnapQueryStamp& stamp, geometry::Point2D pointer_world,
    const SnapCameraTransform& camera, const SnapConfigSnapshot& config,
    const SnapCapabilities& capabilities, const SnapQueryContext& context) const {
    SnapQueryResult result;
    result.stamp = stamp;
    result.stats.index_entries = impl_->entry_count;
    if (context.current_stamp && stamp != *context.current_stamp) {
        const auto& current = *context.current_stamp;
        if (stamp.document_id != current.document_id ||
            stamp.document_epoch != current.document_epoch ||
            stamp.document_revision != current.document_revision) {
            result.status = SnapQueryStatus::StaleSource;
        } else if (stamp.camera_generation != current.camera_generation) {
            result.status = SnapQueryStatus::StaleCamera;
        } else if (stamp.config_generation != current.config_generation) {
            result.status = SnapQueryStatus::StaleConfig;
        } else if (stamp.capability_generation != current.capability_generation) {
            result.status = SnapQueryStatus::StaleCapabilities;
        } else if (stamp.tool_context != current.tool_context) {
            result.status = SnapQueryStatus::StaleToolContext;
        } else {
            result.status = SnapQueryStatus::StaleQuery;
        }
        return result;
    }
    if (stamp.document_id != impl_->document_id || stamp.document_epoch != impl_->epoch ||
        stamp.document_revision != impl_->revision) {
        result.status = SnapQueryStatus::StaleSource;
        return result;
    }
    if (stamp.camera_generation != camera.generation || !camera.valid()) {
        result.status = SnapQueryStatus::StaleCamera;
        return result;
    }
    if (stamp.config_generation != config.generation ||
        !std::isfinite(config.acquire_radius_dip) || !std::isfinite(config.release_radius_dip) ||
        config.acquire_radius_dip < 1.0 || config.acquire_radius_dip > 64.0 ||
        config.release_radius_dip < 1.0 || config.release_radius_dip > 64.0 ||
        config.release_radius_dip < config.acquire_radius_dip) {
        result.status = SnapQueryStatus::StaleConfig;
        return result;
    }
    if (stamp.capability_generation != capabilities.generation) {
        result.status = SnapQueryStatus::StaleCapabilities;
        return result;
    }
    if (!finite_point(pointer_world) || stamp.tool_context.empty() ||
        context.max_entries_examined == 0 || context.max_results == 0) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    if (!config.master_enabled) return result;

    std::array<bool, kSnapOptionCount> active{};
    bool any_source_kind = false;
    bool grid_active = false;
    for (size_t i = 0; i < kSnapOptionCount; ++i) {
        const auto option = static_cast<SnapOption>(i);
        active[i] = is_supported_snap_candidate(option) &&
                    effective_option_enabled(config, stamp.tool_context, option) &&
                    capabilities.supports(option);
        if (is_source_geometry_candidate(option) && active[i]) any_source_kind = true;
        if (option == SnapOption::Grid && active[i]) grid_active = true;
    }
    if (!any_source_kind && !grid_active) return result;
    if (grid_active && (!std::isfinite(config.grid.origin_x_mm) ||
        !std::isfinite(config.grid.origin_y_mm) || !std::isfinite(config.grid.spacing_mm) ||
        config.grid.spacing_mm <= 0.0 || config.grid.max_cells_per_query == 0 ||
        config.grid.max_cells_per_query > 65536)) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }

    const bool target_policy_enabled =
        effective_group_enabled(config, stamp.tool_context, SnapGroup::TargetPolicy);
    const bool current_layer_only = target_policy_enabled &&
        effective_option_enabled(config, stamp.tool_context, SnapOption::CurrentLayerOnly);
    const bool locked_as_reference = target_policy_enabled &&
        effective_option_enabled(config, stamp.tool_context, SnapOption::LockedAsReference);
    const bool self_path_enabled = target_policy_enabled &&
        effective_option_enabled(config, stamp.tool_context, SnapOption::SelfPath);
    const bool hidden_layers_enabled = target_policy_enabled &&
        effective_option_enabled(config, stamp.tool_context, SnapOption::HiddenLayers);
    if (any_source_kind && current_layer_only && (!context.active_layer_index ||
        *context.active_layer_index >= impl_->layers_count)) {
        result.status = SnapQueryStatus::MissingActiveLayer;
        return result;
    }

    const bool previous_valid = previous_matches(context, stamp);
    const double largest_radius = config.release_radius_dip;
    const auto world_bounds = detail::world_bounds_for_dip_square(
        camera, pointer_world, largest_radius);
    if (!world_bounds) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    const auto pointer_dip = camera.world_to_dip(pointer_world);
    if (!finite_point(pointer_dip)) {
        result.status = SnapQueryStatus::InvalidInput;
        return result;
    }
    bool budget_exceeded = false;
    const auto eligible = [&](const IndexedPoint& point) {
        if ((!point.object_visible || !point.layer_visible) && !hidden_layers_enabled) return false;
        if ((point.object_locked || point.layer_locked) && !locked_as_reference) return false;
        if (current_layer_only && point.layer_index != *context.active_layer_index) return false;
        if (!self_path_enabled && context.current_source_object_id == point.object_id) return false;
        return true;
    };
    const auto eligible_curve = [&](const SnapSourceIndex::Impl::IndexedCurve& curve) {
        if ((!curve.object_visible || !curve.layer_visible) && !hidden_layers_enabled) return false;
        if ((curve.object_locked || curve.layer_locked) && !locked_as_reference) return false;
        if (current_layer_only && curve.layer_index != *context.active_layer_index) return false;
        if (!self_path_enabled && context.current_source_object_id == curve.object_id) return false;
        return true;
    };
    const auto add_candidate = [&](SnapCandidateKey key, double parameter_t,
                                   double secondary_parameter_t,
                                   geometry::Point2D world_point, SnapOption kind,
                                   double distance, uint64_t source_revision) {
        const bool retained = previous_valid && context.previous_acquisition->key == key;
        const double threshold = retained ? config.release_radius_dip : config.acquire_radius_dip;
        if (distance > threshold) return;
        result.candidates.push_back({std::move(key), parameter_t, secondary_parameter_t, world_point, source_revision,
            distance, effective_tool_priority(config, stamp.tool_context, kind),
            effective_kind_priority(config, kind), stamp});
        if (result.candidates.size() > context.max_results) budget_exceeded = true;
    };

    for (size_t kind_index = 0; kind_index < kSnapOptionCount && !budget_exceeded; ++kind_index) {
        if (!active[kind_index] || !is_source_geometry_candidate(static_cast<SnapOption>(kind_index)))
            continue;
        const auto kind = static_cast<SnapOption>(kind_index);
        const auto& tree = impl_->trees[kind_index];
        if (tree.root == kNoNode) continue;
        ++result.stats.kind_tree_queries[kind_index];
        std::vector<size_t> stack{tree.root};
        while (!stack.empty() && !budget_exceeded) {
            const size_t node_index = stack.back();
            stack.pop_back();
            const auto& node = tree.nodes[node_index];
            ++result.stats.kd_nodes_visited;
            if (!intersects(node.bounds, *world_bounds)) continue;
            ++result.stats.source_entries_examined;
            if (result.stats.source_entries_examined > context.max_entries_examined) {
                budget_exceeded = true;
                break;
            }
            if (eligible(node.point)) {
                ++result.stats.distance_evaluations;
                const auto candidate_dip = camera.world_to_dip(node.point.world_point);
                const double distance = distance_dip(pointer_dip, candidate_dip);
                if (std::isfinite(distance)) {
                    SnapCandidateKey key{kind, node.point.object_id, node.point.element_id, {}, {}, {}};
                    add_candidate(std::move(key), node.point.parameter_t, 0.0, node.point.world_point,
                                  kind, distance, impl_->revision);
                }
            }
            if (node.left != kNoNode && intersects(tree.nodes[node.left].bounds, *world_bounds))
                stack.push_back(node.left);
            if (node.right != kNoNode && intersects(tree.nodes[node.right].bounds, *world_bounds))
                stack.push_back(node.right);
        }
    }

    if (active[index_of(SnapOption::Nearest)] && !budget_exceeded) {
        for (const auto& curve : impl_->curves) {
            if (!eligible_curve(curve)) continue;
            if (!intersects(curve.bounds, *world_bounds)) continue;
            const auto n_res = geometry::nearest_point_on_curve(curve.world_curve, pointer_world);
            if (n_res.status == geometry::GeometryOperationStatus::Success) {
                const auto n_dip = camera.world_to_dip(n_res.point);
                const double distance = distance_dip(pointer_dip, n_dip);
                if (std::isfinite(distance)) {
                    SnapCandidateKey key{SnapOption::Nearest, curve.object_id, curve.segment_id, {}, {}, {}};
                    add_candidate(std::move(key), n_res.parameter, 0.0, n_res.point,
                                  SnapOption::Nearest, distance, impl_->revision);
                    if (budget_exceeded) break;
                }
            }
        }
    }

    if (active[index_of(SnapOption::Intersection)] && !budget_exceeded) {
        std::vector<const SnapSourceIndex::Impl::IndexedCurve*> aperture_curves;
        for (const auto& curve : impl_->curves) {
            if (!eligible_curve(curve)) continue;
            if (intersects(curve.bounds, *world_bounds)) {
                aperture_curves.push_back(&curve);
            }
        }
        for (size_t i = 0; i < aperture_curves.size() && !budget_exceeded; ++i) {
            for (size_t j = i + 1; j < aperture_curves.size() && !budget_exceeded; ++j) {
                const auto* c1 = aperture_curves[i];
                const auto* c2 = aperture_curves[j];
                if (!intersects(c1->bounds, c2->bounds)) continue;

                const auto int_res = geometry::find_curve_intersections(c1->world_curve, c2->world_curve);
                for (const auto& ip : int_res.points) {
                    const auto ip_dip = camera.world_to_dip(ip.point);
                    const double distance = distance_dip(pointer_dip, ip_dip);
                    if (std::isfinite(distance)) {
                        SnapCandidateKey key{SnapOption::Intersection,
                                             c1->object_id, c1->segment_id,
                                             c2->object_id, c2->segment_id, {}};
                        add_candidate(std::move(key), ip.first_parameter, ip.second_parameter,
                                      ip.point, SnapOption::Intersection, distance, impl_->revision);
                        if (budget_exceeded) break;
                    }
                }
            }
        }
    }

    if (context.external_anchor && !budget_exceeded) {
        const bool do_perp = active[index_of(SnapOption::Perpendicular)];
        const bool do_tan = active[index_of(SnapOption::Tangent)];
        if (do_perp || do_tan) {
            for (const auto& curve : impl_->curves) {
                if (!eligible_curve(curve)) continue;
                if (!intersects(curve.bounds, *world_bounds)) continue;
                if (do_perp) {
                    const auto perp_res = geometry::find_perpendicular_points(curve.world_curve, *context.external_anchor);
                    for (const auto& pp : perp_res.points) {
                        const auto pp_dip = camera.world_to_dip(pp.point);
                        const double distance = distance_dip(pointer_dip, pp_dip);
                        if (std::isfinite(distance)) {
                            SnapCandidateKey key{SnapOption::Perpendicular, curve.object_id, curve.segment_id, {}, {}, {}};
                            add_candidate(std::move(key), pp.parameter, 0.0, pp.point,
                                          SnapOption::Perpendicular, distance, impl_->revision);
                            if (budget_exceeded) break;
                        }
                    }
                }
                if (do_tan && !budget_exceeded) {
                    const auto tan_res = geometry::find_tangent_points(curve.world_curve, *context.external_anchor);
                    for (const auto& tp : tan_res.points) {
                        const auto tp_dip = camera.world_to_dip(tp.point);
                        const double distance = distance_dip(pointer_dip, tp_dip);
                        if (std::isfinite(distance)) {
                            SnapCandidateKey key{SnapOption::Tangent, curve.object_id, curve.segment_id, {}, {}, {}};
                            add_candidate(std::move(key), tp.parameter, 0.0, tp.point,
                                          SnapOption::Tangent, distance, impl_->revision);
                            if (budget_exceeded) break;
                        }
                    }
                }
            }
        }
    }

    if (grid_active && !budget_exceeded) {
        const auto& grid = config.grid;
        const auto min_x = grid_index(world_bounds->min_x, grid.origin_x_mm, grid.spacing_mm, false);
        const auto min_y = grid_index(world_bounds->min_y, grid.origin_y_mm, grid.spacing_mm, false);
        const auto max_x = grid_index(world_bounds->max_x, grid.origin_x_mm, grid.spacing_mm, true);
        const auto max_y = grid_index(world_bounds->max_y, grid.origin_y_mm, grid.spacing_mm, true);
        if (!min_x || !min_y || !max_x || !max_y) {
            result.grid_density_limited = true;
        } else {
            const long double count_x = static_cast<long double>(*max_x) -
                                        static_cast<long double>(*min_x) + 1.0L;
            const long double count_y = static_cast<long double>(*max_y) -
                                        static_cast<long double>(*min_y) + 1.0L;
            if (count_x <= 0.0L || count_y <= 0.0L ||
                count_x * count_y > static_cast<long double>(grid.max_cells_per_query)) {
                result.grid_density_limited = true;
            } else {
                for (int64_t cell_x = *min_x; !budget_exceeded; ++cell_x) {
                    for (int64_t cell_y = *min_y; !budget_exceeded; ++cell_y) {
                        ++result.stats.grid_cells_examined;
                        const geometry::Point2D world{
                            grid.origin_x_mm + static_cast<double>(cell_x) * grid.spacing_mm,
                            grid.origin_y_mm + static_cast<double>(cell_y) * grid.spacing_mm};
                        if (finite_point(world)) {
                            ++result.stats.distance_evaluations;
                            const double distance = distance_dip(pointer_dip, camera.world_to_dip(world));
                            if (std::isfinite(distance)) {
                                SnapCandidateKey key{SnapOption::Grid, {}, {}, {}, {}, GridCellKey{cell_x, cell_y}};
                                add_candidate(std::move(key), 0.0, 0.0, world, SnapOption::Grid,
                                              distance, impl_->revision);
                            }
                        }
                        if (cell_y == *max_y) break;
                    }
                    if (cell_x == *max_x) break;
                }
            }
        }
    }

    if (budget_exceeded) {
        result.status = SnapQueryStatus::WorkBudgetExceeded;
        result.candidates.clear();
        return result;
    }
    std::sort(result.candidates.begin(), result.candidates.end(), candidate_less);
    if (previous_valid) {
        const auto retained = std::find_if(result.candidates.begin(), result.candidates.end(),
            [&](const SnapCandidate& candidate) {
                return candidate.key == context.previous_acquisition->key;
            });
        if (retained != result.candidates.end()) result.chosen = *retained;
    }
    if (!result.chosen && !result.candidates.empty()) result.chosen = result.candidates.front();
    if (result.chosen) result.next_acquisition = SnapAcquisitionState{result.chosen->key, stamp};
    return result;
}

bool snap_result_is_current(const SnapQueryStamp& result_stamp,
                            const SnapQueryStamp& current_stamp) noexcept {
    return result_stamp == current_stamp;
}

} // namespace pikdraw::vector::precision::snap
