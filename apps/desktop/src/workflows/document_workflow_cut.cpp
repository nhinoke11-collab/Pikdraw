#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/document/commands/path_cut_commands.hpp"
#include "pikdraw/document/commands/node_commands.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/geometry/flattening/flattening.hpp"
#include "pikdraw/geometry/operations/path_cut.hpp"
#include "pikdraw/geometry/queries/curve_intersections.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace pikdraw::desktop {
namespace {

constexpr std::size_t kMaxCutTraversalNodes = 100000;
constexpr std::size_t kMaxCutCandidates = 4096;

void collect_topmost_paths(const document::DocumentSnapshot& snapshot,
                           document::ObjectId id,
                           const geometry::Rect2D& gesture_bounds,
                           std::vector<document::ObjectId>& out) {
    if (out.size() > kMaxCutCandidates) return;
    const auto* entry = snapshot.hierarchy().find(id);
    if (!entry || entry->mask || !snapshot.hierarchy().editable(id)) return;
    const auto object = snapshot.objects().get(id);
    if (!object) return;
    if (const auto* group = std::get_if<document::GroupObject>(&*object)) {
        // Editing clipped contents requires entering the PowerClip scope first.
        if (group->clip) return;
        for (auto child = group->children_ids.rbegin(); child != group->children_ids.rend(); ++child) {
            collect_topmost_paths(snapshot, *child, gesture_bounds, out);
            if (out.size() > kMaxCutCandidates) return;
        }
        return;
    }
    if (!entry->bounds || !entry->bounds->intersects(gesture_bounds)) return;
    out.push_back(id);
}

std::vector<document::ObjectId> topmost_path_candidates(
    const document::DocumentSnapshot& snapshot,
    const geometry::Rect2D& gesture_bounds) {
    std::vector<document::ObjectId> result;
    const auto& layers = snapshot.page().layers;
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        if (!layer->visible || layer->locked) continue;
        for (auto id = layer->object_ids.rbegin(); id != layer->object_ids.rend(); ++id) {
            collect_topmost_paths(snapshot, *id, gesture_bounds, result);
            if (result.size() > kMaxCutCandidates) break;
        }
        if (result.size() > kMaxCutCandidates) break;
    }
    return result;
}

document::PathObject make_piece(const document::PathObject& source,
                                document::ObjectId id,
                                const std::string& name,
                                const geometry::Path2D& world_path,
                                const document::SceneHierarchy::Entry& owner,
                                const geometry::Transform2D& world_offset =
                                    geometry::Transform2D::identity()) {
    auto piece = source;
    piece.id = id;
    piece.name = name;
    const auto parent_inverse = owner.parent_world.inverted();
    if (!parent_inverse) {
        piece.path.clear();
        return piece;
    }
    piece.path = world_path.transformed(world_offset).transformed(*parent_inverse);
    piece.transform = geometry::Transform2D::identity();
    // The Boolean kernel returns lines at its declared error bound. Rebuild an
    // editable authoring graph from that committed geometry, and drop stale
    // parametric-rectangle metadata from the source object.
    piece.contour_spec.clear();
    const auto editable = document::editable_path_from_path2d(piece.path, nullptr);
    if (!editable) {
        piece.path.clear();
        return piece;
    }
    try {
        piece.set_authoring_source(*editable);
    } catch (const std::invalid_argument&) {
        piece.path.clear();
    }
    return piece;
}

std::vector<geometry::Path2D> output_components(geometry::BooleanResult&& result) {
    if (!result.components.empty()) return std::move(result.components);
    if (!result.path.empty()) return {std::move(result.path)};
    return {};
}

geometry::CurveSegment segment_curve_world(const document::EditableSubpath& subpath,
                                            size_t index,
                                            const geometry::Transform2D& world) {
    const size_t next = (index + 1) % subpath.nodes.size();
    const auto& start = subpath.nodes[index];
    const auto& end = subpath.nodes[next];
    const auto& segment = subpath.segments[index];
    const auto p0 = world.map_point(start.anchor);
    const auto p1 = world.map_point(end.anchor);
    switch (segment.kind) {
    case document::PathSegmentKind::Line:
        return geometry::LineSegment{p0, p1};
    case document::PathSegmentKind::QuadraticBezier:
        return geometry::QuadraticBezier{p0, world.map_point(segment.quadratic_control), p1};
    case document::PathSegmentKind::CubicBezier: {
        const auto handles_start = document::editable_path_handles_for(subpath, index);
        const auto handles_end = document::editable_path_handles_for(subpath, next);
        return geometry::CubicBezier{p0, world.map_point(handles_start.second),
                                     world.map_point(handles_end.first), p1};
    }
    case document::PathSegmentKind::EllipticArc:
        return segment.arc.transformed(world);
    }
    return geometry::LineSegment{p0, p1};
}

struct ScissorsHit {
    document::ObjectId object_id{document::kInvalidObjectId};
    document::SubpathId subpath_id{};
    document::SegmentId segment_id{};
    document::NodeId endpoint_node{};
    size_t segment_index{0};
    double parameter{0.5};
    double distance{std::numeric_limits<double>::infinity()};
    geometry::Point2D hit_point{};
};

std::optional<ScissorsHit> nearest_scissors_hit(
    const document::DocumentSnapshot& snapshot, document::ObjectId id,
    const geometry::Point2D& point_world, double tolerance_mm,
    bool& ambiguous, bool& unsupported) {
    const auto object = snapshot.objects().get(id);
    const auto* path = object ? std::get_if<document::PathObject>(&*object) : nullptr;
    const auto* owner = snapshot.hierarchy().find(id);
    if (!path || !owner || owner->mask || !snapshot.hierarchy().editable(id)) return std::nullopt;
    if (!path->authoring_source) {
        unsupported = true;
        return std::nullopt;
    }

    constexpr double kEndpointParameterTolerance = 1e-5;
    constexpr double kDistanceTieMm = 1e-5;
    std::optional<ScissorsHit> best;
    for (const auto& subpath : path->authoring_source->subpaths) {
        const size_t count = subpath.segments.size();
        if (count == 0 || subpath.nodes.size() < 2) continue;
        for (size_t index = 0; index < count; ++index) {
            geometry::NearestPointResult nearest;
            try {
                nearest = geometry::nearest_point_on_curve(
                    segment_curve_world(subpath, index, owner->world), point_world,
                    std::max(1e-4, tolerance_mm * 0.01), 4096);
            } catch (const std::exception&) {
                unsupported = true;
                continue;
            }
            if (nearest.status != geometry::GeometryOperationStatus::Success ||
                nearest.distance > tolerance_mm) continue;

            ScissorsHit candidate;
            candidate.object_id = id;
            candidate.subpath_id = subpath.id;
            candidate.segment_id = subpath.segments[index].id;
            candidate.segment_index = index;
            candidate.parameter = nearest.parameter;
            candidate.distance = nearest.distance;
            candidate.hit_point = nearest.point;
            const size_t next = (index + 1) % subpath.nodes.size();
            if (nearest.parameter <= kEndpointParameterTolerance) {
                candidate.endpoint_node = subpath.nodes[index].id;
            } else if (nearest.parameter >= 1.0 - kEndpointParameterTolerance) {
                candidate.endpoint_node = subpath.nodes[next].id;
            }

            if (!best || candidate.distance + kDistanceTieMm < best->distance) {
                best = candidate;
                ambiguous = false;
                continue;
            }
            if (std::abs(candidate.distance - best->distance) <= kDistanceTieMm) {
                const bool same_node = candidate.endpoint_node.valid() &&
                    candidate.endpoint_node == best->endpoint_node &&
                    candidate.subpath_id == best->subpath_id;
                if (!same_node && candidate.segment_id != best->segment_id) ambiguous = true;
            }
        }
    }
    return best;
}

void collect_all_editable_paths(const document::DocumentSnapshot& snapshot,
                                document::ObjectId id,
                                std::vector<document::ObjectId>& out,
                                std::unordered_set<document::ObjectId>& visited) {
    if (out.size() > kMaxCutCandidates || visited.size() > kMaxCutTraversalNodes ||
        !visited.insert(id).second) return;
    const auto* entry = snapshot.hierarchy().find(id);
    if (!entry || entry->mask || !snapshot.hierarchy().editable(id)) return;
    const auto object = snapshot.objects().get(id);
    if (!object) return;
    if (const auto* group = std::get_if<document::GroupObject>(&*object)) {
        if (group->clip) return;
        for (const auto child : group->children_ids) {
            collect_all_editable_paths(snapshot, child, out, visited);
            if (out.size() > kMaxCutCandidates ||
                visited.size() > kMaxCutTraversalNodes) return;
        }
    } else if (const auto* path = std::get_if<document::PathObject>(&*object)) {
        if (path->has_fill || path->has_stroke) out.push_back(id);
    }
}

std::vector<document::ObjectId> all_editable_path_ids(
    const document::DocumentSnapshot& snapshot) {
    std::vector<document::ObjectId> result;
    std::unordered_set<document::ObjectId> visited;
    for (const auto& layer : snapshot.page().layers) {
        if (!layer.visible || layer.locked) continue;
        for (const auto id : layer.object_ids) {
            collect_all_editable_paths(snapshot, id, result, visited);
            if (result.size() > kMaxCutCandidates ||
                visited.size() > kMaxCutTraversalNodes) return result;
        }
    }
    return result;
}

bool selected_paths_support_cut(const document::DocumentSnapshot& snapshot,
                                const std::vector<document::ObjectId>& selected,
                                bool require_fill) {
    std::unordered_set<document::ObjectId> seen;
    for (const auto id : selected) {
        if (id == document::kInvalidObjectId || !seen.insert(id).second)
            return false;
        const auto object = snapshot.objects().get(id);
        const auto* path = object ? std::get_if<document::PathObject>(&*object) : nullptr;
        const auto* owner = snapshot.hierarchy().find(id);
        if (!path || !owner || owner->mask || !snapshot.hierarchy().editable(id))
            return false;
        if (require_fill ? !path->has_fill : (!path->has_fill && !path->has_stroke))
            return false;
    }
    return true;
}

std::vector<geometry::Path2D> boolean_components(geometry::BooleanResult&& result) {
    if (!result.success) return {};
    if (!result.components.empty()) return std::move(result.components);
    if (!result.path.empty()) return {std::move(result.path)};
    return {};
}

size_t largest_component_index(const std::vector<geometry::Path2D>& paths) {
    size_t selected = 0;
    double largest = -1.0;
    for (size_t index = 0; index < paths.size(); ++index) {
        const auto bounds = paths[index].bounds();
        const double area = bounds.width * bounds.height;
        if (std::isfinite(area) && area > largest) {
            largest = area;
            selected = index;
        }
    }
    return selected;
}

CutGeometryProposal compute_scissors_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress) {
    CutGeometryProposal proposal;
    proposal.operation = CutGeometryOperation::Scissors;
    proposal.base_snapshot = request.snapshot;
    proposal.max_approximation_error_mm = request.max_approximation_error_mm;
    const auto fail = [&proposal](CutGeometryStatus status, const char* message) {
        proposal.status = status;
        proposal.message = message;
        proposal.scissors_edit.reset();
        return std::move(proposal);
    };
    if (!request.snapshot || request.snapshot->is_content_locked())
        return fail(CutGeometryStatus::Invalid, "Document snapshot is unavailable or locked");
    if (request.points_world.size() != 1 || !request.points_world.front().is_finite() ||
        !std::isfinite(request.hit_tolerance_mm) || request.hit_tolerance_mm <= 0.0)
        return fail(CutGeometryStatus::Invalid, "Scissors point or hit tolerance is invalid");
    if (request.selected_ids.size() > 4096)
        return fail(CutGeometryStatus::BudgetExceeded, "Selected target count exceeds the edit budget");
    if (stop.stop_requested())
        return fail(CutGeometryStatus::Cancelled, "Scissors operation was cancelled");

    const auto point = request.points_world.front();
    std::vector<document::ObjectId> candidates = request.selected_ids;
    if (candidates.empty()) {
        candidates = topmost_path_candidates(*request.snapshot,
            {point.x - request.hit_tolerance_mm, point.y - request.hit_tolerance_mm,
             request.hit_tolerance_mm * 2.0, request.hit_tolerance_mm * 2.0});
    }
    if (candidates.size() > 4096)
        return fail(CutGeometryStatus::BudgetExceeded, "Scissors candidate count exceeds the edit budget");
    proposal.candidates_total = candidates.size();
    if (progress) progress(0, proposal.candidates_total);
    constexpr std::size_t kMaximumSegments = 100000;
    std::size_t segment_count = 0;
    std::optional<ScissorsHit> best;
    bool ambiguous = false;
    bool unsupported = false;
    for (const auto id : candidates) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Scissors operation was cancelled");
        const auto value = request.snapshot->objects().get(id);
        const auto* path = value ? std::get_if<document::PathObject>(&*value) : nullptr;
        if (!path || !path->authoring_source) {
            unsupported = unsupported || path != nullptr;
            continue;
        }
        for (const auto& subpath : path->authoring_source->subpaths) {
            if (subpath.segments.size() > kMaximumSegments - segment_count)
                return fail(CutGeometryStatus::BudgetExceeded,
                    "Scissors source segment count exceeds the geometry budget");
            segment_count += subpath.segments.size();
        }
        bool candidate_ambiguous = false;
        auto candidate = nearest_scissors_hit(*request.snapshot, id, point,
            request.hit_tolerance_mm, candidate_ambiguous, unsupported);
        if (!candidate) continue;
        if (candidate_ambiguous) {
            ambiguous = true;
            continue;
        }
        if (!best || candidate->distance < best->distance) {
            best = std::move(candidate);
            ambiguous = false;
        } else if (std::abs(candidate->distance - best->distance) <= 1e-5 &&
                   candidate->object_id != best->object_id) {
            ambiguous = true;
        }
        ++proposal.candidates_processed;
        if (progress) progress(proposal.candidates_processed, proposal.candidates_total);
        if (request.selected_ids.empty() && best) break;
    }
    if (!best || ambiguous)
        return fail(ambiguous ? CutGeometryStatus::Ambiguous : CutGeometryStatus::NoChange,
            ambiguous ? "Several Scissors hits are equally close; select one path first" :
                (unsupported ? "The hit path has no editable source geometry" :
                    "Click directly on an editable path node or segment"));
    proposal.scissors_edit = ScissorsGeometryEdit{
        best->object_id, best->subpath_id, best->segment_id,
        best->parameter, best->endpoint_node};
    proposal.status = CutGeometryStatus::Success;
    proposal.message = "Scissors topology proposal is ready to commit";
    return proposal;
}

CutGeometryProposal compute_smart_trim_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress) {
    CutGeometryProposal proposal;
    proposal.operation = CutGeometryOperation::SmartTrim;
    proposal.base_snapshot = request.snapshot;
    proposal.max_approximation_error_mm = request.max_approximation_error_mm;
    const auto fail = [&proposal](CutGeometryStatus status, const char* message) {
        proposal.status = status;
        proposal.message = message;
        proposal.smart_trim_edit.reset();
        return std::move(proposal);
    };
    if (!request.snapshot || request.snapshot->is_content_locked())
        return fail(CutGeometryStatus::Invalid, "Document snapshot is unavailable or locked");
    if (request.points_world.size() != 1 || !request.points_world.front().is_finite() ||
        !std::isfinite(request.hit_tolerance_mm) || request.hit_tolerance_mm <= 0.0)
        return fail(CutGeometryStatus::Invalid, "Smart Trim point or hit tolerance is invalid");
    if (request.selected_ids.size() > 4096)
        return fail(CutGeometryStatus::BudgetExceeded, "Selected target count exceeds the edit budget");
    if (stop.stop_requested())
        return fail(CutGeometryStatus::Cancelled, "Smart Trim was cancelled");

    const auto point_world = request.points_world.front();
    std::vector<document::ObjectId> hit_candidates = request.selected_ids;
    if (hit_candidates.empty()) {
        hit_candidates = topmost_path_candidates(*request.snapshot,
            {point_world.x - request.hit_tolerance_mm,
             point_world.y - request.hit_tolerance_mm,
             request.hit_tolerance_mm * 2.0, request.hit_tolerance_mm * 2.0});
    }
    if (hit_candidates.size() > 4096)
        return fail(CutGeometryStatus::BudgetExceeded,
            "Smart Trim candidate count exceeds the geometry budget");
    std::optional<ScissorsHit> hit;
    bool ambiguous_hit = false;
    bool unsupported_hit = false;
    for (const auto id : hit_candidates) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Smart Trim was cancelled");
        bool candidate_ambiguous = false;
        auto candidate = nearest_scissors_hit(*request.snapshot, id, point_world,
            request.hit_tolerance_mm, candidate_ambiguous, unsupported_hit);
        if (!candidate) continue;
        if (candidate_ambiguous) {
            ambiguous_hit = true;
            continue;
        }
        if (!hit || candidate->distance < hit->distance) {
            hit = std::move(candidate);
            ambiguous_hit = false;
        } else if (std::abs(candidate->distance - hit->distance) <= 1e-5 &&
                   candidate->object_id != hit->object_id) {
            ambiguous_hit = true;
        }
        if (request.selected_ids.empty() && hit) break;
    }
    if (!hit)
        return fail(unsupported_hit ? CutGeometryStatus::Unsupported : CutGeometryStatus::NoChange,
            unsupported_hit ? "The hit path has no editable source geometry" :
                "Click directly on an editable path segment");
    if (ambiguous_hit)
        return fail(CutGeometryStatus::Ambiguous,
            "Several paths are equally close; select one path first");

    const auto target_value = request.snapshot->objects().get(hit->object_id);
    const auto* target = target_value
        ? std::get_if<document::PathObject>(&*target_value) : nullptr;
    const auto* target_owner = request.snapshot->hierarchy().find(hit->object_id);
    if (!target || !target_owner || !target->authoring_source || !target->has_stroke ||
        target->has_fill || target_owner->mask ||
        !request.snapshot->hierarchy().editable(hit->object_id))
        return fail(CutGeometryStatus::Unsupported,
            "Smart Trim edits editable stroke-only paths; filled paths are left unchanged");
    const auto target_subpath_it = std::find_if(target->authoring_source->subpaths.begin(),
        target->authoring_source->subpaths.end(), [&](const auto& subpath) {
            return subpath.id == hit->subpath_id;
        });
    if (target_subpath_it == target->authoring_source->subpaths.end() ||
        target_subpath_it->segments.empty())
        return fail(CutGeometryStatus::Unsupported,
            "The clicked subpath has no editable segments");
    const auto& target_subpath = *target_subpath_it;
    const double click_station = static_cast<double>(hit->segment_index) + hit->parameter;

    struct OtherCurve {
        document::ObjectId object_id{document::kInvalidObjectId};
        document::SubpathId subpath_id{};
        size_t segment_index{0};
        geometry::CurveSegment curve{geometry::LineSegment{}};
        geometry::Rect2D bounds{};
    };
    constexpr size_t kMaximumOtherSegments = 100000;
    constexpr size_t kMaximumCurvePairs = 20000;
    std::vector<OtherCurve> other_curves;
    other_curves.reserve(1024);
    size_t total_segments = 0;
    size_t total_path_elements = 0;
    const auto all_paths = all_editable_path_ids(*request.snapshot);
    if (all_paths.size() > 4096)
        return fail(CutGeometryStatus::BudgetExceeded,
            "Smart Trim document path count exceeds the geometry budget");
    const std::size_t progress_total = all_paths.size() +
        target_subpath.segments.size();
    std::size_t progress_completed = 0;
    if (progress) progress(0, progress_total);
    for (const auto id : all_paths) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Smart Trim was cancelled");
        const auto object = request.snapshot->objects().get(id);
        const auto* source = object ? std::get_if<document::PathObject>(&*object) : nullptr;
        const auto* owner = request.snapshot->hierarchy().find(id);
        if (!source || !owner || owner->mask) continue;
        if (source->path.size() > 200000 ||
            total_path_elements > 1000000 - source->path.size())
            return fail(CutGeometryStatus::BudgetExceeded,
                "Smart Trim source geometry exceeds the 1,000,000-element work budget");
        total_path_elements += source->path.size();
        std::optional<document::EditablePath> converted;
        const document::EditablePath* editable = source->authoring_source.get();
        if (!editable) {
            converted = document::editable_path_from_path2d(source->path, nullptr);
            if (!converted) continue;
            editable = &*converted;
        }
        for (const auto& subpath : editable->subpaths) {
            for (size_t segment = 0; segment < subpath.segments.size(); ++segment) {
                if (stop.stop_requested())
                    return fail(CutGeometryStatus::Cancelled, "Smart Trim was cancelled");
                if (++total_segments > kMaximumOtherSegments)
                    return fail(CutGeometryStatus::BudgetExceeded,
                        "Smart Trim segment budget exceeded");
                if (id == hit->object_id && subpath.id == hit->subpath_id &&
                    segment == hit->segment_index) continue;
                if (id == hit->object_id && subpath.id == hit->subpath_id) {
                    const size_t count = subpath.segments.size();
                    const size_t delta = segment > hit->segment_index
                        ? segment - hit->segment_index : hit->segment_index - segment;
                    if (delta == 1 || (subpath.closed && delta + 1 == count)) continue;
                }
                auto curve = segment_curve_world(subpath, segment, owner->world);
                other_curves.push_back({id, subpath.id, segment, curve,
                                        geometry::curve_bounds(curve)});
            }
        }
        if (progress) progress(++progress_completed, progress_total);
    }
    if (other_curves.empty())
        return fail(CutGeometryStatus::NoChange,
            "No other editable path segment can bound this interval");

    const geometry::CurveQueryOptions query_options{1e-3, 1e-4, 16, 1024};
    const auto clicked_target_curve = segment_curve_world(
        target_subpath, hit->segment_index, target_owner->world);
    const auto clicked_target_point = geometry::curve_point_at(
        clicked_target_curve, hit->parameter);
    const double contact_tolerance_mm = std::max(1e-5, query_options.tolerance_mm);
    const geometry::Rect2D clicked_contact_bounds{
        clicked_target_point.x - contact_tolerance_mm,
        clicked_target_point.y - contact_tolerance_mm,
        contact_tolerance_mm * 2.0, contact_tolerance_mm * 2.0};
    bool clicked_on_contact = false;
    std::size_t contact_candidates = 0;
    struct Event {
        double station{0.0};
        document::SmartTrimBoundary boundary{};
        geometry::Point2D point{};
    };
    std::vector<Event> events;
    if (target_subpath.segments.size() > kMaximumCurvePairs / other_curves.size())
        return fail(CutGeometryStatus::BudgetExceeded,
            "Smart Trim curve-pair comparison budget exceeded");
    size_t pairs = 0;
    for (const auto& other : other_curves) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Smart Trim was cancelled");
        if (!other.bounds.intersects(clicked_contact_bounds)) continue;
        if (++contact_candidates > kMaximumCurvePairs)
            return fail(CutGeometryStatus::BudgetExceeded,
                "Smart Trim contact-query budget exceeded");
        try {
            const auto nearest = geometry::nearest_point_on_curve(
                other.curve, clicked_target_point, contact_tolerance_mm, 4096);
            if (nearest.status == geometry::GeometryOperationStatus::Success &&
                nearest.point.is_finite() && std::isfinite(nearest.distance) &&
                nearest.distance <= contact_tolerance_mm)
                clicked_on_contact = true;
        } catch (...) {
            return fail(CutGeometryStatus::Invalid,
                "Nearest-contact query failed; Smart Trim left the document unchanged");
        }
    }
    for (size_t segment = 0; segment < target_subpath.segments.size(); ++segment) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Smart Trim was cancelled");
        const auto target_curve = segment_curve_world(
            target_subpath, segment, target_owner->world);
        const auto target_bounds = geometry::curve_bounds(target_curve);
        for (const auto& other : other_curves) {
            if (!target_bounds.intersects(other.bounds)) continue;
            if (++pairs > kMaximumCurvePairs)
                return fail(CutGeometryStatus::BudgetExceeded,
                    "Smart Trim intersection pair budget exceeded");
            geometry::CurveIntersectionResult intersections;
            try {
                intersections = geometry::find_curve_intersections(
                    target_curve, other.curve, query_options);
            } catch (...) {
                return fail(CutGeometryStatus::Invalid,
                    "Intersection solver threw during Smart Trim; document unchanged");
            }
            if (intersections.status == geometry::CurveQueryStatus::Overlap)
                return fail(CutGeometryStatus::Ambiguous,
                    "Coincident segments make this trim interval ambiguous");
            if (intersections.status == geometry::CurveQueryStatus::BudgetExceeded ||
                intersections.status == geometry::CurveQueryStatus::Indeterminate)
                return fail(CutGeometryStatus::BudgetExceeded,
                    "Intersection solver could not resolve this interval within budget");
            if (intersections.status == geometry::CurveQueryStatus::InvalidInput)
                return fail(CutGeometryStatus::Invalid,
                    "Intersection solver rejected invalid source geometry");
            for (const auto& point : intersections.points) {
                if (!std::isfinite(point.first_parameter) ||
                    point.first_parameter < 0.0 || point.first_parameter > 1.0 ||
                    !point.point.is_finite() || !std::isfinite(point.residual_mm) ||
                    point.residual_mm > query_options.max_residual_mm)
                    return fail(CutGeometryStatus::Invalid,
                        "Intersection residual exceeded the Smart Trim tolerance");
                double station = static_cast<double>(segment) + point.first_parameter;
                if (target_subpath.closed &&
                    station >= static_cast<double>(target_subpath.segments.size()) - 1e-9)
                    station = 0.0;
                events.push_back({station,
                    {target_subpath.segments[segment].id, point.first_parameter}, point.point});
                if (events.size() > 4096)
                    return fail(CutGeometryStatus::BudgetExceeded,
                        "Smart Trim intersection count budget exceeded");
            }
        }
        if (progress) progress(++progress_completed, progress_total);
    }
    if (clicked_on_contact)
        return fail(CutGeometryStatus::NoChange,
            "Click between intersections, away from tangent contacts or crossing points");
    if (events.size() < 2)
        return fail(CutGeometryStatus::NoChange,
            "This path has fewer than two resolved intersections");
    std::sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        return left.station < right.station;
    });
    const double station_period = static_cast<double>(target_subpath.segments.size());
    std::vector<Event> unique_events;
    unique_events.reserve(events.size());
    for (const auto& event : events) {
        if (!unique_events.empty() &&
            std::abs(event.station - unique_events.back().station) <= 1e-7 &&
            event.point.distance_to(unique_events.back().point) <= query_options.tolerance_mm)
            continue;
        unique_events.push_back(event);
    }
    if (target_subpath.closed && unique_events.size() > 1 &&
        unique_events.front().station <= 1e-7 &&
        station_period - unique_events.back().station <= 1e-7 &&
        unique_events.front().point.distance_to(unique_events.back().point) <=
            query_options.tolerance_mm)
        unique_events.pop_back();
    if (unique_events.size() < 2)
        return fail(CutGeometryStatus::Ambiguous,
            "Intersections collapse to one point at the current tolerance");
    for (const auto& event : unique_events) {
        const double direct = std::abs(event.station - click_station);
        const double circular = target_subpath.closed
            ? std::min(direct, station_period - std::min(direct, station_period)) : direct;
        if (circular <= 1e-6)
            return fail(CutGeometryStatus::NoChange,
                "Click between intersections rather than on an intersection");
    }

    size_t left_index = 0;
    size_t right_index = 0;
    bool wraps = false;
    const auto next = std::upper_bound(unique_events.begin(), unique_events.end(), click_station,
        [](double station, const Event& event) { return station < event.station; });
    if (!target_subpath.closed) {
        if (next == unique_events.begin() || next == unique_events.end())
            return fail(CutGeometryStatus::NoChange,
                "The clicked segment is not bounded by two intersections");
        right_index = static_cast<size_t>(next - unique_events.begin());
        left_index = right_index - 1;
    } else if (next == unique_events.begin() || next == unique_events.end()) {
        left_index = unique_events.size() - 1;
        right_index = 0;
        wraps = true;
    } else {
        right_index = static_cast<size_t>(next - unique_events.begin());
        left_index = right_index - 1;
    }
    const auto& left = unique_events[left_index];
    const auto& right = unique_events[right_index];
    proposal.smart_trim_edit = SmartTrimGeometryEdit{
        hit->object_id, hit->subpath_id, left.boundary.segment_id,
        left.boundary.parameter, right.boundary.segment_id,
        right.boundary.parameter, wraps};
    if (request.preview_only) {
        constexpr std::size_t kMaximumPreviewPoints = 250000;
        CutGeometryPreviewContour removed_interval;
        removed_interval.removed = true;
        removed_interval.points_world.reserve(256);
        const auto append_interval = [&](double start_station, double end_station) {
            double station = start_station;
            while (station < end_station - 1e-10) {
                if (stop.stop_requested()) return false;
                const auto segment_index = std::min(
                    static_cast<std::size_t>(std::floor(station)),
                    target_subpath.segments.size() - 1);
                const double segment_end = std::min(
                    end_station, static_cast<double>(segment_index + 1));
                const double first_parameter = std::clamp(
                    station - static_cast<double>(segment_index), 0.0, 1.0);
                const double last_parameter = std::clamp(
                    segment_end - static_cast<double>(segment_index), 0.0, 1.0);
                if (last_parameter <= first_parameter + 1e-12) {
                    station = segment_end;
                    continue;
                }
                const auto curve = segment_curve_world(
                    target_subpath, segment_index, target_owner->world);
                const std::size_t samples = std::holds_alternative<geometry::LineSegment>(curve)
                    ? 1 : 16;
                for (std::size_t sample = 0; sample <= samples; ++sample) {
                    const double fraction = static_cast<double>(sample) /
                        static_cast<double>(samples);
                    const auto point = geometry::curve_point_at(curve,
                        first_parameter + (last_parameter - first_parameter) * fraction);
                    if (!point.is_finite()) return false;
                    if (removed_interval.points_world.empty() ||
                        point.distance_to(removed_interval.points_world.back()) > 1e-9)
                        removed_interval.points_world.push_back(point);
                    if (removed_interval.points_world.size() > kMaximumPreviewPoints)
                        return false;
                }
                station = segment_end;
            }
            return true;
        };
        const bool preview_ok = wraps
            ? append_interval(left.station, station_period) &&
                append_interval(0.0, right.station)
            : append_interval(left.station, right.station);
        if (!preview_ok || removed_interval.points_world.size() < 2)
            return fail(stop.stop_requested() ? CutGeometryStatus::Cancelled
                                              : CutGeometryStatus::BudgetExceeded,
                "Smart Trim preview could not represent the removed interval within its budget");
        proposal.preview_contours.push_back(std::move(removed_interval));
    }
    proposal.candidates_total = pairs;
    proposal.candidates_processed = pairs;
    proposal.status = CutGeometryStatus::Success;
    proposal.message = "Smart Trim topology proposal is ready to commit";
    return proposal;
}

} // namespace

CutGeometryProposal compute_cut_geometry(const CutGeometryRequest& request,
                                         std::stop_token stop,
                                         const CutGeometryProgress& progress) {
    CutGeometryProposal proposal;
    proposal.operation = request.operation;
    proposal.base_snapshot = request.snapshot;
    proposal.max_approximation_error_mm = request.max_approximation_error_mm;
    const auto fail = [&proposal](CutGeometryStatus status, const char* message) {
        proposal.status = status;
        proposal.message = message;
        proposal.replacements.clear();
        return std::move(proposal);
    };
    if (!request.snapshot || request.snapshot->is_content_locked())
        return fail(CutGeometryStatus::Invalid, "Document snapshot is unavailable or locked");
    if (!std::isfinite(request.max_approximation_error_mm) ||
        request.max_approximation_error_mm < 0.0 ||
        request.max_approximation_error_mm > kCutGeometryDefaultToleranceMm)
        return fail(CutGeometryStatus::Invalid,
            "Maximum approximation error must be within 0..0.05 mm");
    if (request.selected_ids.size() > 4096)
        return fail(CutGeometryStatus::BudgetExceeded, "Selected target count exceeds the edit budget");
    if (stop.stop_requested())
        return fail(CutGeometryStatus::Cancelled, "Geometry edit was cancelled");
    if (request.operation == CutGeometryOperation::TrimObjects)
        return compute_trim_objects_geometry(request, stop, progress);

    if (request.points_world.empty() || request.points_world.size() > 4096)
        return fail(request.points_world.size() > 4096
                ? CutGeometryStatus::BudgetExceeded : CutGeometryStatus::Invalid,
            "Gesture point count is outside the supported range");
    if (std::any_of(request.points_world.begin(), request.points_world.end(),
            [](const auto& point) { return !point.is_finite(); }))
        return fail(CutGeometryStatus::Invalid, "Gesture contains a non-finite point");
    if (request.operation == CutGeometryOperation::SmartFill)
        return compute_smart_fill_geometry(request, stop, progress);
    if (request.operation == CutGeometryOperation::ShapeBuilder)
        return compute_shape_builder_geometry(request, stop, progress);
    if (request.operation == CutGeometryOperation::Knife &&
        (request.points_world.size() < 2 || !std::isfinite(request.gap_mm) ||
         request.gap_mm < 0.0 || request.gap_mm > 1.0 ||
         !std::isfinite(request.separate_offset_mm) || request.separate_offset_mm < 0.0))
        return fail(CutGeometryStatus::Invalid, "Knife options or gesture are invalid");
    if (request.operation == CutGeometryOperation::VectorEraser &&
        (!std::isfinite(request.diameter_mm) || request.diameter_mm <= 0.0 ||
         request.diameter_mm > 1000.0))
        return fail(CutGeometryStatus::Invalid, "Vector Eraser diameter is outside 0..1000 mm");
    if (request.operation == CutGeometryOperation::Scissors)
        return compute_scissors_geometry(request, stop, progress);
    if (request.operation == CutGeometryOperation::SmartTrim)
        return compute_smart_trim_geometry(request, stop, progress);
    if (request.operation != CutGeometryOperation::Knife &&
        request.operation != CutGeometryOperation::VectorEraser)
        return fail(CutGeometryStatus::Invalid, "Unknown geometry operation");

    const bool selected = !request.selected_ids.empty();
    if (selected && !selected_paths_support_cut(*request.snapshot,
            request.selected_ids, request.operation == CutGeometryOperation::Knife))
        return fail(CutGeometryStatus::Unsupported,
            "Every selected target must be visible, editable vector geometry supported by this tool");

    double min_x = request.points_world.front().x;
    double max_x = min_x;
    double min_y = request.points_world.front().y;
    double max_y = min_y;
    for (const auto& point : request.points_world) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }

    std::vector<document::ObjectId> candidate_ids = request.selected_ids;
    if (!selected) {
        if (request.operation == CutGeometryOperation::Knife) {
            const double pad = std::max(0.01, request.gap_mm * 0.5);
            candidate_ids = topmost_path_candidates(*request.snapshot,
                {min_x - pad, min_y - pad,
                 std::max(0.01, max_x - min_x + 2.0 * pad),
                 std::max(0.01, max_y - min_y + 2.0 * pad)});
        } else {
            const double radius = request.diameter_mm * 0.5;
            candidate_ids = topmost_path_candidates(*request.snapshot,
                {min_x - radius, min_y - radius,
                 max_x - min_x + 2.0 * radius, max_y - min_y + 2.0 * radius});
        }
    }
    if (candidate_ids.size() > 4096)
        return fail(CutGeometryStatus::BudgetExceeded, "Candidate target count exceeds the edit budget");
    proposal.candidates_total = candidate_ids.size();
    if (progress) progress(0, proposal.candidates_total);
    if (candidate_ids.empty())
        return fail(CutGeometryStatus::NoChange, "No visible editable path intersects the gesture");

    constexpr std::size_t kMaxOutputElements = 1'000'000;
    constexpr std::size_t kMaxInputElements = 1'000'000;
    constexpr std::size_t kMaxTargetElements = 200'000;
    std::size_t output_elements = 0;
    std::size_t input_elements = 0;
    for (const auto id : candidate_ids) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Geometry edit was cancelled");
        ++proposal.candidates_processed;
        const auto object = request.snapshot->objects().get(id);
        const auto* source = object ? std::get_if<document::PathObject>(&*object) : nullptr;
        const auto* owner = request.snapshot->hierarchy().find(id);
        if (!source || !owner || owner->mask || !request.snapshot->hierarchy().editable(id)) {
            if (selected)
                return fail(CutGeometryStatus::Unsupported, "A selected target is no longer editable");
            continue;
        }
        if (request.operation == CutGeometryOperation::Knife && !source->has_fill)
            continue;
        if (request.operation == CutGeometryOperation::VectorEraser &&
            !source->has_fill && !source->has_stroke)
            continue;

        const auto source_elements = source->path.size();
        if (source_elements > kMaxTargetElements ||
            input_elements > kMaxInputElements - source_elements)
            return fail(CutGeometryStatus::BudgetExceeded,
                "Geometry input exceeds the 1,000,000-element work budget");
        input_elements += source_elements;

        const auto world_path = source->path.transformed(owner->world);
        geometry::PathCutResult result;
        if (request.operation == CutGeometryOperation::Knife) {
            result = geometry::cut_path_by_polyline(
                world_path, request.points_world, request.gap_mm);
        } else if (source->has_fill) {
            result = geometry::erase_path(world_path, request.points_world,
                request.diameter_mm, request.round_nib);
        } else {
            result = geometry::erase_open_path_stroke(world_path, request.points_world,
                request.diameter_mm, source->stroke_width_mm, request.round_nib);
        }
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Geometry edit was cancelled");
        if (!result.success ||
            (request.operation == CutGeometryOperation::Knife && result.pieces.size() < 2)) {
            if (progress && (proposal.candidates_processed % 8 == 0 ||
                             proposal.candidates_processed == proposal.candidates_total))
                progress(proposal.candidates_processed, proposal.candidates_total);
            continue;
        }
        if (!std::isfinite(result.approximation_error_bound_mm) ||
            result.approximation_error_bound_mm < 0.0)
            return fail(CutGeometryStatus::Invalid,
                "Geometry kernel returned an invalid approximation bound");
        proposal.approximation_error_bound_mm = std::max(
            proposal.approximation_error_bound_mm,
            result.approximation_error_bound_mm);
        if (proposal.approximation_error_bound_mm >
            request.max_approximation_error_mm)
            return fail(CutGeometryStatus::ToleranceNotMet,
                "Approximation bound exceeds the requested tolerance; document unchanged");

        CutGeometryReplacement replacement;
        replacement.target_id = id;
        if (result.status != geometry::PathCutStatus::EmptyResult) {
            const auto primary = result.pieces.empty() ? std::size_t{0}
                : largest_component_index(result.pieces);
            replacement.pieces.reserve(result.pieces.size());
            for (std::size_t index = 0; index < result.pieces.size(); ++index) {
                output_elements += result.pieces[index].size();
                if (output_elements > kMaxOutputElements)
                    return fail(CutGeometryStatus::BudgetExceeded,
                        "Geometry output exceeds the 1,000,000-element budget");
                const auto piece_id = index == primary ? id : document::kInvalidObjectId;
                auto piece = make_piece(*source, piece_id,
                    result.pieces.size() > 1
                        ? source->name + " (Mảnh " + std::to_string(index + 1) + ")"
                        : source->name,
                    result.pieces[index], *owner,
                    request.operation == CutGeometryOperation::Knife &&
                            request.separate_offset_mm > 0.0 && result.pieces.size() == 2
                        ? geometry::Transform2D::translation(
                            (index == primary ? -1.0 : 1.0) * request.separate_offset_mm * 0.5, 0.0)
                        : geometry::Transform2D::identity());
                if (piece.path.empty())
                    return fail(CutGeometryStatus::Unsupported,
                        "A result piece could not be converted to editable path data");
                replacement.pieces.push_back(std::move(piece));
            }
        }
        proposal.replacements.push_back(std::move(replacement));
        if (progress && (proposal.candidates_processed % 8 == 0 ||
                         proposal.candidates_processed == proposal.candidates_total))
            progress(proposal.candidates_processed, proposal.candidates_total);
        if (!selected) break;
    }
    if (proposal.replacements.empty())
        return fail(CutGeometryStatus::NoChange, "No eligible path was affected by the gesture");
    proposal.status = CutGeometryStatus::Success;
    proposal.message = "Geometry proposal is ready to commit";
    return proposal;
}

bool DocumentWorkflow::scissors_split_at_point(const geometry::Point2D& point_world,
                                                double hit_tolerance_mm) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || !point_world.is_finite() ||
        !std::isfinite(hit_tolerance_mm) || hit_tolerance_mm <= 0.0) return false;

    const auto& selected = root_.vector_controller().selection().selected_ids();
    std::vector<document::ObjectId> candidates;
    if (!selected.empty()) {
        candidates = selected;
    } else {
        const geometry::Rect2D hit_bounds{point_world.x - hit_tolerance_mm,
                                          point_world.y - hit_tolerance_mm,
                                          hit_tolerance_mm * 2.0,
                                          hit_tolerance_mm * 2.0};
        candidates = topmost_path_candidates(*snapshot, hit_bounds);
    }

    std::optional<ScissorsHit> hit;
    bool ambiguous = false;
    bool unsupported = false;
    for (const auto id : candidates) {
        bool candidate_ambiguous = false;
        auto candidate = nearest_scissors_hit(*snapshot, id, point_world, hit_tolerance_mm,
                                              candidate_ambiguous, unsupported);
        if (!candidate) continue;
        if (candidate_ambiguous) {
            ambiguous = true;
            continue;
        }
        if (!hit || candidate->distance < hit->distance) {
            hit = std::move(candidate);
            ambiguous = false;
        } else if (std::abs(candidate->distance - hit->distance) <= 1e-5 &&
                   candidate->object_id != hit->object_id) {
            ambiguous = true;
        }
        if (selected.empty() && hit) break; // preserve topmost paint order
    }
    if (!hit || ambiguous) {
        return false;
    }

    auto command = std::make_unique<document::ScissorsPathCommand>(
        hit->object_id, hit->subpath_id, hit->segment_id, hit->parameter,
        hit->endpoint_node);
    const auto result = root_.document().execute_command(std::move(command));
    if (!result.success) return false;
    root_.vector_controller().set_selection({hit->object_id}, *root_.document().current_snapshot());
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::smart_trim_at_point(const geometry::Point2D& point_world,
                                            double hit_tolerance_mm,
                                            std::string* error) {
    const auto fail = [error](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (error) error->clear();
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || !point_world.is_finite() ||
        !std::isfinite(hit_tolerance_mm) || hit_tolerance_mm <= 0.0)
        return fail("Invalid point, tolerance, or locked document");

    const auto& selection = root_.vector_controller().selection().selected_ids();
    std::vector<document::ObjectId> hit_candidates;
    if (!selection.empty()) {
        hit_candidates = selection;
    } else {
        const geometry::Rect2D hit_bounds{point_world.x - hit_tolerance_mm,
            point_world.y - hit_tolerance_mm, hit_tolerance_mm * 2.0,
            hit_tolerance_mm * 2.0};
        hit_candidates = topmost_path_candidates(*snapshot, hit_bounds);
    }
    std::optional<ScissorsHit> hit;
    bool ambiguous_hit = false;
    bool unsupported_hit = false;
    for (const auto id : hit_candidates) {
        bool candidate_ambiguous = false;
        auto candidate = nearest_scissors_hit(*snapshot, id, point_world,
            hit_tolerance_mm, candidate_ambiguous, unsupported_hit);
        if (!candidate) continue;
        if (candidate_ambiguous) {
            ambiguous_hit = true;
            continue;
        }
        if (!hit || candidate->distance < hit->distance) {
            hit = std::move(candidate);
            ambiguous_hit = false;
        } else if (std::abs(candidate->distance - hit->distance) <= 1e-5 &&
                   candidate->object_id != hit->object_id) {
            ambiguous_hit = true;
        }
        if (selection.empty() && hit) break;
    }
    if (!hit) return fail(unsupported_hit
        ? "The hit path has no editable source geometry"
        : "Click directly on an editable path segment");
    if (ambiguous_hit) return fail("Several paths are equally close; select one path first");

    const auto target_value = snapshot->objects().get(hit->object_id);
    const auto* target = target_value ? std::get_if<document::PathObject>(&*target_value) : nullptr;
    const auto* target_owner = snapshot->hierarchy().find(hit->object_id);
    if (!target || !target_owner || !target->authoring_source || !target->has_stroke ||
        target->has_fill || target_owner->mask || !snapshot->hierarchy().editable(hit->object_id))
        return fail("Smart Trim edits editable stroke-only paths; filled paths are left unchanged");
    const auto target_subpath_it = std::find_if(target->authoring_source->subpaths.begin(),
        target->authoring_source->subpaths.end(), [&](const auto& subpath) {
            return subpath.id == hit->subpath_id;
        });
    if (target_subpath_it == target->authoring_source->subpaths.end() ||
        target_subpath_it->segments.empty())
        return fail("The clicked subpath has no editable segments");
    const auto& target_subpath = *target_subpath_it;
    const double click_station = static_cast<double>(hit->segment_index) + hit->parameter;

    struct OtherCurve {
        document::ObjectId object_id{document::kInvalidObjectId};
        document::SubpathId subpath_id{};
        size_t segment_index{0};
        geometry::CurveSegment curve{geometry::LineSegment{}};
        geometry::Rect2D bounds{};
    };
    constexpr size_t kMaximumOtherSegments = 100000;
    constexpr size_t kMaximumCurvePairs = 200000;
    std::vector<OtherCurve> other_curves;
    other_curves.reserve(1024);
    size_t total_segments = 0;
    for (const auto id : all_editable_path_ids(*snapshot)) {
        const auto object = snapshot->objects().get(id);
        const auto* source = object ? std::get_if<document::PathObject>(&*object) : nullptr;
        const auto* owner = snapshot->hierarchy().find(id);
        if (!source || !owner || owner->mask) continue;
        std::optional<document::EditablePath> converted;
        const document::EditablePath* editable = source->authoring_source.get();
        if (!editable) {
            converted = document::editable_path_from_path2d(source->path, nullptr);
            if (!converted) continue;
            editable = &*converted;
        }
        for (const auto& subpath : editable->subpaths) {
            for (size_t segment = 0; segment < subpath.segments.size(); ++segment) {
                if (++total_segments > kMaximumOtherSegments)
                    return fail("Smart Trim segment budget exceeded");
                if (id == hit->object_id && subpath.id == hit->subpath_id &&
                    segment == hit->segment_index) continue;
                if (id == hit->object_id && subpath.id == hit->subpath_id) {
                    const size_t count = subpath.segments.size();
                    const size_t delta = segment > hit->segment_index
                        ? segment - hit->segment_index : hit->segment_index - segment;
                    if (delta == 1 || (subpath.closed && delta + 1 == count)) continue;
                }
                auto curve = segment_curve_world(subpath, segment, owner->world);
                other_curves.push_back({id, subpath.id, segment, curve,
                                        geometry::curve_bounds(curve)});
            }
        }
    }
    if (other_curves.empty())
        return fail("No other editable path segment can bound this interval");

    const geometry::CurveQueryOptions query_options{1e-3, 1e-4, 16, 2048};
    struct Event {
        double station{0.0};
        document::SmartTrimBoundary boundary{};
        geometry::Point2D point{};
    };
    std::vector<Event> events;
    if (!other_curves.empty() && target_subpath.segments.size() >
            kMaximumCurvePairs / other_curves.size())
        return fail("Smart Trim curve-pair comparison budget exceeded");
    size_t pairs = 0;
    for (size_t segment = 0; segment < target_subpath.segments.size(); ++segment) {
        const auto target_curve = segment_curve_world(target_subpath, segment, target_owner->world);
        const auto target_bounds = geometry::curve_bounds(target_curve);
        for (const auto& other : other_curves) {
            if (!target_bounds.intersects(other.bounds)) continue;
            if (++pairs > kMaximumCurvePairs)
                return fail("Smart Trim intersection pair budget exceeded");
            const auto intersections = geometry::find_curve_intersections(
                target_curve, other.curve, query_options);
            if (intersections.status == geometry::CurveQueryStatus::Overlap)
                return fail("Coincident segments make this trim interval ambiguous");
            if (intersections.status == geometry::CurveQueryStatus::BudgetExceeded ||
                intersections.status == geometry::CurveQueryStatus::Indeterminate)
                return fail("Intersection solver could not resolve this interval within budget");
            if (intersections.status == geometry::CurveQueryStatus::InvalidInput)
                return fail("Intersection solver rejected invalid source geometry");
            for (const auto& point : intersections.points) {
                if (!std::isfinite(point.first_parameter) ||
                    point.first_parameter < 0.0 || point.first_parameter > 1.0 ||
                    !point.point.is_finite() || !std::isfinite(point.residual_mm) ||
                    point.residual_mm > query_options.max_residual_mm)
                    return fail("Intersection residual exceeded the Smart Trim tolerance");
                double station = static_cast<double>(segment) + point.first_parameter;
                if (target_subpath.closed &&
                    station >= static_cast<double>(target_subpath.segments.size()) - 1e-9)
                    station = 0.0;
                events.push_back({station,
                    {target_subpath.segments[segment].id, point.first_parameter}, point.point});
                if (events.size() > 4096)
                    return fail("Smart Trim intersection count budget exceeded");
            }
        }
    }
    if (events.size() < 2)
        return fail("This path has fewer than two resolved intersections");
    std::sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        return left.station < right.station;
    });
    const double station_period = static_cast<double>(target_subpath.segments.size());
    std::vector<Event> unique_events;
    unique_events.reserve(events.size());
    for (const auto& event : events) {
        if (!unique_events.empty() &&
            std::abs(event.station - unique_events.back().station) <= 1e-7 &&
            event.point.distance_to(unique_events.back().point) <= query_options.tolerance_mm)
            continue;
        unique_events.push_back(event);
    }
    if (target_subpath.closed && unique_events.size() > 1 &&
        unique_events.front().station <= 1e-7 &&
        station_period - unique_events.back().station <= 1e-7 &&
        unique_events.front().point.distance_to(unique_events.back().point) <=
            query_options.tolerance_mm)
        unique_events.pop_back();
    if (unique_events.size() < 2)
        return fail("Intersections collapse to one point at the current tolerance");
    for (const auto& event : unique_events) {
        const double direct = std::abs(event.station - click_station);
        const double circular = target_subpath.closed
            ? std::min(direct, station_period - std::min(direct, station_period)) : direct;
        if (circular <= 1e-6)
            return fail("Click between intersections rather than on an intersection");
    }

    size_t left_index = 0;
    size_t right_index = 0;
    bool wraps = false;
    const auto next = std::upper_bound(unique_events.begin(), unique_events.end(), click_station,
        [](double station, const Event& event) { return station < event.station; });
    if (!target_subpath.closed) {
        if (next == unique_events.begin() || next == unique_events.end())
            return fail("The clicked segment is not bounded by two intersections");
        right_index = static_cast<size_t>(next - unique_events.begin());
        left_index = right_index - 1;
    } else if (next == unique_events.begin()) {
        left_index = unique_events.size() - 1;
        right_index = 0;
        wraps = true;
    } else if (next == unique_events.end()) {
        left_index = unique_events.size() - 1;
        right_index = 0;
        wraps = true;
    } else {
        right_index = static_cast<size_t>(next - unique_events.begin());
        left_index = right_index - 1;
    }
    const auto& left = unique_events[left_index];
    const auto& right = unique_events[right_index];
    auto command = std::make_unique<document::SmartTrimPathCommand>(
        hit->object_id, hit->subpath_id, left.boundary, right.boundary, wraps);
    if (!root_.document().execute_command(std::move(command)).success)
        return fail("The document rejected this Smart Trim topology edit");
    root_.vector_controller().set_selection(
        {hit->object_id}, *root_.document().current_snapshot());
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::resolve_smart_fill_face(
    const geometry::Point2D& point_world, geometry::Path2D& face,
    std::vector<document::ObjectId>& source_ids,
    document::ObjectId& style_source_id, std::string* error) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) {
        if (error) *error = "Document snapshot is unavailable";
        return false;
    }
    CutGeometryRequest request;
    request.operation = CutGeometryOperation::SmartFill;
    request.snapshot = snapshot;
    request.selected_ids = root_.vector_controller().selection().selected_ids();
    request.points_world = {point_world};
    auto proposal = compute_smart_fill_geometry(request, {});
    if (proposal.status != CutGeometryStatus::Success || proposal.additions.size() != 1) {
        if (error) *error = proposal.message.empty()
            ? "Smart Fill face resolution failed" : std::move(proposal.message);
        return false;
    }
    if (error) error->clear();
    face = std::move(proposal.additions.front().object.path);
    source_ids = std::move(proposal.source_ids);
    style_source_id = proposal.style_source_id;
    return true;
}

bool DocumentWorkflow::smart_fill_at_point(const geometry::Point2D& point_world,
                                            std::string* error) {
    geometry::Path2D face;
    std::vector<document::ObjectId> source_ids;
    document::ObjectId style_source = document::kInvalidObjectId;
    if (!resolve_smart_fill_face(point_world, face, source_ids, style_source, error)) return false;
    const auto snapshot = root_.document().current_snapshot();
    const auto style_value = snapshot ? snapshot->objects().get(style_source) : std::nullopt;
    const auto* style = style_value ? std::get_if<document::PathObject>(&*style_value) : nullptr;
    const auto* owner = snapshot ? snapshot->hierarchy().find(style_source) : nullptr;
    const auto editable = document::editable_path_from_path2d(face, nullptr);
    if (!style || !owner || !editable) {
        if (error) *error = "Could not create an editable Smart Fill object";
        return false;
    }
    auto result = *style;
    result.id = document::generate_object_id();
    if (result.id == document::kInvalidObjectId) {
        if (error) *error = "Object identity allocator is exhausted";
        return false;
    }
    result.name = "Smart Fill";
    result.path = std::move(face);
    result.transform = geometry::Transform2D::identity();
    result.contour_spec.clear();
    result.set_authoring_source(*editable);
    const auto new_id = result.id;
    if (!root_.document().execute_command(std::make_unique<document::AddObjectCommand>(
            std::move(result), owner->layer)).success) {
        if (error) *error = "The document rejected the Smart Fill object";
        return false;
    }
    root_.vector_controller().set_selection({new_id}, *root_.document().current_snapshot());
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::shape_builder_apply_faces(
    const std::vector<geometry::Point2D>& seed_points, bool erase_faces,
    std::string* error) {
    const auto fail = [error](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (error) error->clear();
    if (seed_points.empty() || seed_points.size() > 64)
        return fail("Shape Builder requires 1 to 64 selected faces");
    std::vector<geometry::Path2D> selected_faces;
    std::vector<document::ObjectId> source_ids;
    document::ObjectId style_source = document::kInvalidObjectId;
    for (const auto point : seed_points) {
        geometry::Path2D face;
        std::vector<document::ObjectId> face_sources;
        document::ObjectId face_style = document::kInvalidObjectId;
        if (!resolve_smart_fill_face(point, face, face_sources, face_style, error)) return false;
        if (source_ids.empty()) {
            source_ids = std::move(face_sources);
            style_source = face_style;
        } else if (source_ids != face_sources) {
            return fail("The source set changed while Shape Builder was active");
        }
        selected_faces.push_back(std::move(face));
    }
    constexpr geometry::BooleanOptions options{0.01, 1e-6, 16, 200000};
    geometry::Path2D combined = selected_faces.front();
    std::vector<geometry::Path2D> merged_components{selected_faces.front()};
    for (size_t index = 1; index < selected_faces.size(); ++index) {
        auto result = geometry::boolean_paths(combined, selected_faces[index],
            geometry::BooleanOp::Union, options);
        if (!result.success || result.path.empty())
            return fail("Could not combine selected faces within the geometry budget");
        merged_components = std::move(result.components);
        if (merged_components.empty()) merged_components.push_back(result.path);
        combined = std::move(result.path);
    }
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked()) return fail("Document changed or became locked");
    if (!erase_faces) {
        const auto style_value = snapshot->objects().get(style_source);
        const auto* style = style_value ? std::get_if<document::PathObject>(&*style_value) : nullptr;
        const auto* owner = snapshot->hierarchy().find(style_source);
        if (!style || !owner || !snapshot->hierarchy().editable(style_source))
            return fail("Shape Builder style source is stale or locked");
        auto composite = std::make_unique<document::CompositeCommand>("Shape Builder Merge");
        std::vector<document::ObjectId> created_ids;
        for (auto& component : merged_components) {
            const auto editable = document::editable_path_from_path2d(component, nullptr);
            if (!editable) return fail("A merged face could not be represented as an editable path");
            auto created = *style;
            created.id = document::generate_object_id();
            created.name = "Shape Builder";
            created.path = std::move(component);
            created.transform = geometry::Transform2D::identity();
            created.contour_spec.clear();
            created.set_authoring_source(*editable);
            created_ids.push_back(created.id);
            composite->add_command(std::make_unique<document::AddObjectCommand>(
                std::move(created), owner->layer));
        }
        if (created_ids.empty() || !root_.document().execute_command(std::move(composite)).success)
            return fail("The document rejected the Shape Builder merge");
        root_.vector_controller().set_selection(std::move(created_ids), *root_.document().current_snapshot());
        sync_status_bar();
        return true;
    }

    auto composite = std::make_unique<document::CompositeCommand>("Shape Builder Erase Faces");
    std::vector<document::ObjectId> remaining_ids;
    size_t changed_sources = 0;
    for (const auto id : source_ids) {
        const auto value = snapshot->objects().get(id);
        const auto* source = value ? std::get_if<document::PathObject>(&*value) : nullptr;
        const auto* owner = snapshot->hierarchy().find(id);
        if (!source || !owner || owner->mask || !snapshot->hierarchy().editable(id) || !source->has_fill)
            return fail("A Shape Builder source became unavailable before erase");
        auto difference = geometry::boolean_paths(source->path.transformed(owner->world),
            combined, geometry::BooleanOp::Difference, options);
        if (!difference.success) return fail("A source face could not be erased within the geometry budget");
        if (difference.status == geometry::BooleanStatus::NoChange) {
            remaining_ids.push_back(id);
            continue;
        }
        auto components = boolean_components(std::move(difference));
        std::vector<document::ObjectVariant> replacements;
        const size_t primary = components.empty() ? 0 : largest_component_index(components);
        for (size_t index = 0; index < components.size(); ++index) {
            auto piece = make_piece(*source,
                index == primary ? id : document::generate_object_id(),
                components.size() > 1 ? source->name + " (Face " + std::to_string(index + 1) + ")"
                                      : source->name,
                components[index], *owner);
            if (piece.path.empty()) return fail("A remaining source component is not editable");
            remaining_ids.push_back(piece.id);
            replacements.emplace_back(std::move(piece));
        }
        composite->add_command(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
            id, std::move(replacements), "Shape Builder Erase"));
        ++changed_sources;
    }
    if (changed_sources == 0) return fail("The selected face does not overlap an editable source");
    if (!root_.document().execute_command(std::move(composite)).success)
        return fail("The document rejected the atomic Shape Builder erase");
    root_.vector_controller().set_selection(std::move(remaining_ids), *root_.document().current_snapshot());
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::cut_objects_by_line(
    const geometry::Point2D& p1,
    const geometry::Point2D& p2,
    double separate_offset_mm,
    double gap_mm
) {
    return cut_objects_by_polyline({p1, p2}, separate_offset_mm, gap_mm);
}

bool DocumentWorkflow::cut_objects_by_polyline(
    const std::vector<geometry::Point2D>& points,
    double separate_offset_mm,
    double gap_mm
) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || points.size() < 2) return false;
    if (std::any_of(points.begin(), points.end(), [](const auto& point) {
            return !std::isfinite(point.x) || !std::isfinite(point.y);
        }) ||
        !std::isfinite(separate_offset_mm) || separate_offset_mm < 0.0 ||
        !std::isfinite(gap_mm) || gap_mm < 0.0) return false;

    double min_x = points.front().x;
    double max_x = points.front().x;
    double min_y = points.front().y;
    double max_y = points.front().y;
    for (const auto& point : points) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }

    const auto& sel = root_.vector_controller().selection().selected_ids();
    std::vector<document::ObjectId> candidate_ids;
    if (!sel.empty()) {
        // An explicit multi-selection is one edit request. Reject the whole
        // request before geometry work if any member is stale, locked, masked,
        // unsupported, or not fillable; never silently edit only a subset.
        if (!selected_paths_support_cut(*snapshot, sel, true)) return false;
        candidate_ids = sel;
    } else {
        // Resolve in reverse paint order. The first path that the actual cut
        // can divide is the only implicit target.
        geometry::Rect2D cut_bounds;
        const double pad = std::max(0.01, gap_mm * 0.5);
        cut_bounds.x = min_x - pad;
        cut_bounds.y = min_y - pad;
        cut_bounds.width = std::max(0.01, max_x - min_x + 2.0 * pad);
        cut_bounds.height = std::max(0.01, max_y - min_y + 2.0 * pad);

        candidate_ids = topmost_path_candidates(*snapshot, cut_bounds);
    }

    if (candidate_ids.empty()) return false;

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    std::vector<document::ObjectId> all_new_ids;

    for (const auto id : candidate_ids) {
        const auto obj_var = snapshot->objects().get(id);
        if (!obj_var) continue;
        const auto* po = std::get_if<document::PathObject>(&*obj_var);
        const auto* owner = snapshot->hierarchy().find(id);
        if (!po || !po->has_fill || !owner || owner->mask ||
            !snapshot->hierarchy().editable(id)) {
            continue;
        }

        const auto world_path = po->path.transformed(owner->world);
        auto cut_res = geometry::cut_path_by_polyline(world_path, points, gap_mm);
        if (!cut_res.success || cut_res.pieces.size() < 2) continue;

        std::vector<document::ObjectVariant> pieces;
        std::vector<document::ObjectId> piece_ids;
        pieces.reserve(cut_res.pieces.size());
        piece_ids.reserve(cut_res.pieces.size());
        const size_t primary = largest_component_index(cut_res.pieces);
        for (size_t index = 0; index < cut_res.pieces.size(); ++index) {
            const double offset_x = (separate_offset_mm > 0.0 && cut_res.pieces.size() == 2)
                ? (index == primary ? -1.0 : 1.0) * separate_offset_mm * 0.5 : 0.0;
            auto piece = make_piece(*po, index == primary ? id : document::generate_object_id(),
                po->name + " (Mảnh " + std::to_string(index + 1) + ")",
                cut_res.pieces[index], *owner,
                geometry::Transform2D::translation(offset_x, 0.0));
            if (piece.path.empty()) {
                pieces.clear();
                break;
            }
            piece_ids.push_back(piece.id);
            pieces.push_back(std::move(piece));
        }
        if (pieces.size() != cut_res.pieces.size()) continue;
        all_new_ids.insert(all_new_ids.end(), piece_ids.begin(), piece_ids.end());

        cmds.push_back(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
            id, std::move(pieces), "Knife Cut"));
        if (sel.empty()) break;
    }

    if (cmds.empty()) return false;

    if (cmds.size() == 1) {
        if (!root_.document().execute_command(std::move(cmds.front())).success) return false;
    } else {
        auto composite = std::make_unique<document::CompositeCommand>("Knife Cut Multiple");
        for (auto& cmd : cmds) {
            composite->add_command(std::move(cmd));
        }
        if (!root_.document().execute_command(std::move(composite)).success) return false;
    }

    root_.vector_controller().selection().clear();
    for (const auto nid : all_new_ids) {
        root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
    }
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::erase_objects_by_stroke(
    const std::vector<geometry::Point2D>& stroke,
    double diameter_mm,
    bool round_nib
) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || stroke.empty() ||
        !std::isfinite(diameter_mm) || diameter_mm <= 0.0) return false;
    double min_x = stroke.front().x, max_x = stroke.front().x;
    double min_y = stroke.front().y, max_y = stroke.front().y;
    for (const auto& point : stroke) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }

    const auto& sel = root_.vector_controller().selection().selected_ids();
    std::vector<document::ObjectId> candidate_ids;
    if (!sel.empty()) {
        if (!selected_paths_support_cut(*snapshot, sel, false)) return false;
        candidate_ids = sel;
    } else {
        const double radius = diameter_mm * 0.5;
        const geometry::Rect2D bounds{min_x - radius, min_y - radius,
                                      max_x - min_x + 2.0 * radius,
                                      max_y - min_y + 2.0 * radius};
        candidate_ids = topmost_path_candidates(*snapshot, bounds);
    }

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    std::vector<document::ObjectId> all_new_ids;

    for (const auto id : candidate_ids) {
        const auto obj_var = snapshot->objects().get(id);
        if (!obj_var) continue;
        const auto* po = std::get_if<document::PathObject>(&*obj_var);
        const auto* owner = snapshot->hierarchy().find(id);
        if (!po || (!po->has_fill && !po->has_stroke) || !owner || owner->mask ||
            !snapshot->hierarchy().editable(id)) {
            continue;
        }

        const auto world_path = po->path.transformed(owner->world);
        auto erase_res = po->has_fill
            ? geometry::erase_path(world_path, stroke, diameter_mm, round_nib)
            : geometry::erase_open_path_stroke(world_path, stroke, diameter_mm,
                                                po->stroke_width_mm, round_nib);
        if (!erase_res.success) continue;
        if (erase_res.status == geometry::PathCutStatus::EmptyResult) {
            // A stroke that covers the complete filled object is a valid edit.
            // An empty replacement removes it and remains undoable through the
            // same snapshot-backed command as a partial erase.
            cmds.push_back(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
                id, std::vector<document::ObjectVariant>{}, "Vector Eraser"));
            if (sel.empty()) break;
            continue;
        }
        if (erase_res.status != geometry::PathCutStatus::Success || erase_res.pieces.empty())
            continue;

        std::vector<document::ObjectVariant> pieces;
        const size_t primary = largest_component_index(erase_res.pieces);
        bool pieces_valid = true;
        std::vector<document::ObjectId> piece_ids;
        piece_ids.reserve(erase_res.pieces.size());
        for (size_t index = 0; index < erase_res.pieces.size(); ++index) {
            auto piece = make_piece(*po,
                index == primary ? id : document::generate_object_id(),
                erase_res.pieces.size() > 1
                    ? po->name + " (Mảnh " + std::to_string(index + 1) + ")" : po->name,
                erase_res.pieces[index], *owner);
            if (piece.path.empty()) {
                pieces_valid = false;
                break;
            }
            piece_ids.push_back(piece.id);
            pieces.push_back(std::move(piece));
        }
        if (!pieces_valid || pieces.size() != erase_res.pieces.size()) continue;
        all_new_ids.insert(all_new_ids.end(), piece_ids.begin(), piece_ids.end());

        cmds.push_back(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
            id, std::move(pieces), "Vector Eraser"));
        if (sel.empty()) break;
    }

    if (cmds.empty()) return false;

    if (cmds.size() == 1) {
        if (!root_.document().execute_command(std::move(cmds.front())).success) return false;
    } else {
        auto composite = std::make_unique<document::CompositeCommand>("Vector Eraser Multiple");
        for (auto& cmd : cmds) {
            composite->add_command(std::move(cmd));
        }
        if (!root_.document().execute_command(std::move(composite)).success) return false;
    }

    root_.vector_controller().selection().clear();
    for (const auto nid : all_new_ids) {
        root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
    }
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::trim_objects_source_target(
    document::ObjectId target_id,
    const std::vector<document::ObjectId>& source_ids,
    bool keep_sources
) {
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->is_content_locked() || source_ids.empty()) return false;

    const auto target_var = snapshot->objects().get(target_id);
    if (!target_var) return false;
    const auto* target = std::get_if<document::PathObject>(&*target_var);
    const auto* target_owner = snapshot->hierarchy().find(target_id);
    if (!target || !target->has_fill || !target_owner || target_owner->mask ||
        !snapshot->hierarchy().editable(target_id)) return false;

    std::vector<geometry::Path2D> source_paths;
    std::unordered_set<document::ObjectId> unique_sources;
    for (const auto sid : source_ids) {
        if (sid == target_id || !unique_sources.insert(sid).second) return false;
        const auto src_var = snapshot->objects().get(sid);
        if (!src_var) return false;
        const auto* src_po = std::get_if<document::PathObject>(&*src_var);
        const auto* source_owner = snapshot->hierarchy().find(sid);
        if (!src_po || !src_po->has_fill || !source_owner || source_owner->mask ||
            !snapshot->hierarchy().editable(sid)) return false;
        source_paths.push_back(src_po->path.transformed(source_owner->world));
    }
    if (source_paths.empty()) return false;

    const auto target_world_path = target->path.transformed(target_owner->world);
    auto trim_res = geometry::trim_target_with_sources(target_world_path, source_paths);
    if (!trim_res.success) return false;

    std::vector<document::ObjectVariant> pieces;
    auto trimmed_components = output_components(std::move(trim_res));
    // Full removal requires a separate preview/confirmation surface; do not
    // turn a trim into an unreviewed delete.
    if (trimmed_components.empty()) return false;
    const size_t primary = largest_component_index(trimmed_components);
    for (size_t index = 0; index < trimmed_components.size(); ++index) {
        auto piece = make_piece(*target,
            index == primary ? target_id : document::generate_object_id(),
            trimmed_components.size() > 1
                ? target->name + " (Mảnh " + std::to_string(index + 1) + ")" : target->name,
            trimmed_components[index], *target_owner);
        if (piece.path.empty()) return false;
        pieces.push_back(std::move(piece));
    }

    std::vector<document::ObjectId> result_ids;
    result_ids.reserve(pieces.size());
    for (const auto& piece : pieces)
        result_ids.push_back(std::visit([](const auto& object) { return object.id; }, piece));

    auto replace_cmd = std::make_unique<document::ReplaceObjectWithPiecesCommand>(
        target_id, std::move(pieces), "Trim Objects");

    bool ok = false;
    if (keep_sources) {
        ok = root_.document().execute_command(std::move(replace_cmd)).success;
    } else {
        auto composite = std::make_unique<document::CompositeCommand>("Trim Objects");
        composite->add_command(std::move(replace_cmd));
        for (const auto sid : source_ids) {
            composite->add_command(std::make_unique<document::RemoveObjectCommand>(sid));
        }
        ok = root_.document().execute_command(std::move(composite)).success;
    }

    if (ok) {
        root_.vector_controller().selection().clear();
        for (const auto id : result_ids)
            root_.vector_controller().selection().select(id, vector::SelectionMode::Add);
        sync_status_bar();
    }
    return ok;
}

bool DocumentWorkflow::apply_cut_geometry_proposal(CutGeometryProposal proposal,
                                                    std::string* error) {
    const auto fail = [error](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    if (error) error->clear();
    if (proposal.status != CutGeometryStatus::Success)
        return fail(proposal.message.empty() ? "Geometry proposal did not succeed" : proposal.message);
    if (!std::isfinite(proposal.approximation_error_bound_mm) ||
        proposal.approximation_error_bound_mm < 0.0 ||
        !std::isfinite(proposal.max_approximation_error_mm) ||
        proposal.max_approximation_error_mm < 0.0 ||
        proposal.max_approximation_error_mm > kCutGeometryDefaultToleranceMm ||
        proposal.approximation_error_bound_mm > proposal.max_approximation_error_mm)
        return fail("Geometry proposal failed its approximation tolerance check");
    const auto current = root_.document().current_snapshot();
    if (!current || !proposal.base_snapshot || current != proposal.base_snapshot ||
        current->revision() != proposal.base_snapshot->revision())
        return fail("Geometry proposal is stale because the document changed");
    if (current->is_content_locked())
        return fail("Geometry proposal cannot be committed to the current document");

    if (proposal.operation == CutGeometryOperation::Scissors) {
        if (!proposal.scissors_edit || proposal.smart_trim_edit ||
            !proposal.replacements.empty() || !proposal.additions.empty() ||
            !proposal.remove_ids.empty() || !proposal.preserve_selection_ids.empty())
            return fail("Scissors proposal has an invalid payload");
        const auto& edit = *proposal.scissors_edit;
        const auto target = current->objects().get(edit.object_id);
        const auto* path = target ? std::get_if<document::PathObject>(&*target) : nullptr;
        const auto* owner = current->hierarchy().find(edit.object_id);
        if (!path || !path->authoring_source || !owner || owner->mask ||
            !current->hierarchy().editable(edit.object_id))
            return fail("Scissors proposal target is no longer editable");
        auto command = std::make_unique<document::ScissorsPathCommand>(
            edit.object_id, edit.subpath_id, edit.segment_id, edit.parameter,
            edit.endpoint_node);
        const auto result = root_.document().execute_command(std::move(command));
        if (!result.success)
            return fail(result.error_message.empty()
                ? "Document rejected the Scissors edit" : result.error_message);
        root_.vector_controller().set_selection(
            {edit.object_id}, *root_.document().current_snapshot());
        sync_status_bar();
        return true;
    }

    if (proposal.operation == CutGeometryOperation::SmartTrim) {
        if (!proposal.smart_trim_edit || proposal.scissors_edit ||
            !proposal.replacements.empty() || !proposal.additions.empty() ||
            !proposal.remove_ids.empty() || !proposal.preserve_selection_ids.empty())
            return fail("Smart Trim proposal has an invalid payload");
        const auto& edit = *proposal.smart_trim_edit;
        const auto target = current->objects().get(edit.object_id);
        const auto* path = target ? std::get_if<document::PathObject>(&*target) : nullptr;
        const auto* owner = current->hierarchy().find(edit.object_id);
        if (!path || !path->authoring_source || !path->has_stroke || path->has_fill ||
            !owner || owner->mask || !current->hierarchy().editable(edit.object_id))
            return fail("Smart Trim proposal target is no longer editable");
        auto command = std::make_unique<document::SmartTrimPathCommand>(
            edit.object_id, edit.subpath_id,
            document::SmartTrimBoundary{edit.start_segment, edit.start_parameter},
            document::SmartTrimBoundary{edit.end_segment, edit.end_parameter},
            edit.wraps_closed_subpath);
        const auto result = root_.document().execute_command(std::move(command));
        if (!result.success)
            return fail(result.error_message.empty()
                ? "Document rejected the Smart Trim edit" : result.error_message);
        root_.vector_controller().set_selection(
            {edit.object_id}, *root_.document().current_snapshot());
        sync_status_bar();
        return true;
    }

    const bool knife_or_eraser = proposal.operation == CutGeometryOperation::Knife ||
        proposal.operation == CutGeometryOperation::VectorEraser;
    const bool trim_objects = proposal.operation == CutGeometryOperation::TrimObjects;
    const bool smart_fill = proposal.operation == CutGeometryOperation::SmartFill;
    const bool shape_builder = proposal.operation == CutGeometryOperation::ShapeBuilder;
    if ((!knife_or_eraser && !trim_objects && !smart_fill && !shape_builder) ||
        proposal.scissors_edit || proposal.smart_trim_edit)
        return fail("Geometry proposal contains an unsupported operation");
    if (knife_or_eraser && (proposal.replacements.empty() ||
        !proposal.additions.empty() || !proposal.remove_ids.empty() ||
        !proposal.preserve_selection_ids.empty() || !proposal.source_ids.empty() ||
        proposal.style_source_id != document::kInvalidObjectId))
        return fail("Cut proposal contains an invalid payload");
    if (trim_objects && (proposal.replacements.size() != 1 ||
        !proposal.additions.empty() || !proposal.preserve_selection_ids.empty() ||
        !proposal.source_ids.empty() ||
        proposal.style_source_id != document::kInvalidObjectId ||
        (proposal.keep_sources && !proposal.remove_ids.empty()) ||
        (!proposal.keep_sources && proposal.remove_ids.empty())))
        return fail("Trim Objects proposal contains an invalid payload");
    if (smart_fill && (!proposal.replacements.empty() ||
        proposal.additions.size() != 1 || !proposal.remove_ids.empty() ||
        !proposal.preserve_selection_ids.empty() || proposal.source_ids.empty() ||
        proposal.source_ids.size() > 64 ||
        proposal.style_source_id == document::kInvalidObjectId ||
        std::find(proposal.source_ids.begin(), proposal.source_ids.end(),
                  proposal.style_source_id) == proposal.source_ids.end()))
        return fail("Smart Fill proposal contains an invalid payload");
    if (smart_fill) {
        std::unordered_set<document::ObjectId> source_ids;
        for (const auto id : proposal.source_ids) {
            const auto value = current->objects().get(id);
            const auto* path = value ? std::get_if<document::PathObject>(&*value) : nullptr;
            const auto* owner = current->hierarchy().find(id);
            if (id == document::kInvalidObjectId || !source_ids.insert(id).second ||
                !path || !path->has_fill || !owner || owner->mask ||
                !current->hierarchy().editable(id))
                return fail("Smart Fill proposal contains a stale or invalid source");
        }
    }
    if (shape_builder && ((proposal.erase_faces &&
            (proposal.replacements.empty() || !proposal.additions.empty())) ||
        (!proposal.erase_faces &&
            (!proposal.replacements.empty() || proposal.additions.empty())) ||
        !proposal.remove_ids.empty() || !proposal.source_ids.empty() ||
        proposal.style_source_id != document::kInvalidObjectId))
        return fail("Shape Builder proposal contains an invalid payload");

    std::unordered_set<document::ObjectId> target_ids;
    std::unordered_set<document::ObjectId> output_ids;
    std::vector<document::ObjectId> resulting_ids;
    std::vector<std::unique_ptr<document::DocumentCommand>> commands;
    commands.reserve(proposal.replacements.size() + proposal.additions.size() +
                     proposal.remove_ids.size());
    const std::string command_name = proposal.operation == CutGeometryOperation::Knife
        ? "Knife Cut" : proposal.operation == CutGeometryOperation::VectorEraser
            ? "Vector Eraser" : trim_objects ? "Trim Objects"
                : smart_fill ? "Smart Fill"
                    : proposal.erase_faces ? "Shape Builder Erase Faces"
                                           : "Shape Builder Merge";
    for (auto& replacement : proposal.replacements) {
        if (replacement.target_id == document::kInvalidObjectId ||
            !target_ids.insert(replacement.target_id).second)
            return fail("Geometry proposal contains a duplicate or invalid target");
        const auto* owner = current->hierarchy().find(replacement.target_id);
        const auto target = current->objects().get(replacement.target_id);
        if (!owner || owner->mask || !current->hierarchy().editable(replacement.target_id) ||
            !target || !std::holds_alternative<document::PathObject>(*target))
            return fail("Geometry proposal target is no longer editable");
        const auto& target_path = std::get<document::PathObject>(*target);
        if ((proposal.operation == CutGeometryOperation::Knife && !target_path.has_fill) ||
            (proposal.operation == CutGeometryOperation::VectorEraser &&
                !target_path.has_fill && !target_path.has_stroke) ||
            ((trim_objects || shape_builder) && !target_path.has_fill))
            return fail("Geometry proposal target no longer supports this operation");
        if (replacement.pieces.empty() &&
            !(shape_builder && proposal.erase_faces))
            return fail("Geometry proposal would remove a target without explicit erase semantics");

        std::vector<document::ObjectVariant> pieces;
        pieces.reserve(replacement.pieces.size());
        for (auto& piece : replacement.pieces) {
            if (!piece.transform.is_finite() || piece.path.empty() ||
                piece.path.size() > 200000 || !piece.authoring_source)
                return fail("Geometry proposal contains invalid path data");
            if (piece.id == document::kInvalidObjectId) {
                do {
                    piece.id = document::generate_object_id();
                } while (piece.id != document::kInvalidObjectId &&
                         (current->objects().get(piece.id) || output_ids.contains(piece.id)));
            }
            if (piece.id == document::kInvalidObjectId)
                return fail("Object identity allocator is exhausted");
            if (piece.id != replacement.target_id &&
                current->objects().get(piece.id))
                return fail("Geometry proposal output ID collides with an existing object");
            if (!output_ids.insert(piece.id).second)
                return fail("Geometry proposal contains duplicate output IDs");
            resulting_ids.push_back(piece.id);
            pieces.emplace_back(std::move(piece));
        }
        commands.push_back(std::make_unique<document::ReplaceObjectWithPiecesCommand>(
            replacement.target_id, std::move(pieces), command_name));
    }

    for (auto& addition : proposal.additions) {
        if (addition.layer >= current->page().layers.size() ||
            addition.object.path.empty() || !addition.object.transform.is_finite() ||
            addition.object.path.size() > 200000 || !addition.object.has_fill ||
            !addition.object.authoring_source)
            return fail("Geometry proposal contains an invalid new path");
        auto& object = addition.object;
        if (object.id == document::kInvalidObjectId) {
            do {
                object.id = document::generate_object_id();
            } while (object.id != document::kInvalidObjectId &&
                     (current->objects().get(object.id) || output_ids.contains(object.id)));
        }
        if (object.id == document::kInvalidObjectId ||
            current->objects().get(object.id) || !output_ids.insert(object.id).second)
            return fail("Geometry proposal new path identity is invalid or colliding");
        resulting_ids.push_back(object.id);
        commands.push_back(std::make_unique<document::AddObjectCommand>(
            std::move(object), addition.layer));
    }

    for (const auto id : proposal.remove_ids) {
        if (id == document::kInvalidObjectId || target_ids.contains(id) ||
            output_ids.contains(id) || !target_ids.insert(id).second)
            return fail("Geometry proposal contains a duplicate or invalid removal target");
        const auto value = current->objects().get(id);
        const auto* path = value ? std::get_if<document::PathObject>(&*value) : nullptr;
        const auto* owner = current->hierarchy().find(id);
        if (!path || !path->has_fill || !owner || owner->mask ||
            !current->hierarchy().editable(id))
            return fail("Geometry proposal removal target is no longer editable");
        commands.push_back(std::make_unique<document::RemoveObjectCommand>(id));
    }
    for (const auto id : proposal.preserve_selection_ids) {
        if (!current->objects().get(id) || target_ids.contains(id) ||
            !output_ids.insert(id).second)
            return fail("Geometry proposal contains an invalid preserved selection ID");
        resulting_ids.push_back(id);
    }

    if (commands.empty())
        return fail("Geometry proposal has no document commands to commit");

    document::CommandResult result;
    if (commands.size() == 1) {
        result = root_.document().execute_command(std::move(commands.front()));
    } else {
        auto composite = std::make_unique<document::CompositeCommand>(command_name);
        for (auto& command : commands) composite->add_command(std::move(command));
        result = root_.document().execute_command(std::move(composite));
    }
    if (!result.success)
        return fail(result.error_message.empty()
            ? "Document rejected the geometry proposal" : result.error_message);

    root_.vector_controller().selection().clear();
    for (const auto id : resulting_ids)
        root_.vector_controller().selection().select(id, vector::SelectionMode::Add);
    sync_status_bar();
    return true;
}

} // namespace pikdraw::desktop
