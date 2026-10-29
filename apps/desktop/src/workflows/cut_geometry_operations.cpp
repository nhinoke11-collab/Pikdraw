#include "pikdraw/desktop/workflows/cut_geometry_job.hpp"

#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/geometry/flattening/flattening.hpp"
#include "pikdraw/geometry/operations/path_cut.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace pikdraw::desktop {
namespace {

constexpr std::size_t kMaximumBooleanTargets = 64;
constexpr std::size_t kMaximumVisitedNodes = 100000;
constexpr std::size_t kMaximumPathElements = 200000;
constexpr std::size_t kMaximumTotalElements = 1000000;
constexpr std::size_t kMaximumPreviewPoints = 250000;
constexpr double kBooleanFlattenToleranceMm = 0.005;
constexpr double kPointClassificationToleranceMm = 0.001;
constexpr double kPreviewFlattenToleranceMm = 0.025;

geometry::BooleanOptions boolean_options() {
    return {kBooleanFlattenToleranceMm, 1e-6, 16, kMaximumPathElements};
}

std::vector<document::ObjectId> all_visible_editable_paths(
    const document::DocumentSnapshot& snapshot, bool& exceeded_limit) {
    std::vector<document::ObjectId> result;
    std::unordered_set<document::ObjectId> visited;
    exceeded_limit = false;
    const auto visit = [&](const auto& self, document::ObjectId id) -> void {
        if (exceeded_limit) return;
        if (!visited.insert(id).second) return;
        if (visited.size() > kMaximumVisitedNodes) {
            exceeded_limit = true;
            return;
        }
        const auto* owner = snapshot.hierarchy().find(id);
        if (!owner || owner->mask || !snapshot.hierarchy().editable(id)) return;
        const auto value = snapshot.objects().get(id);
        if (!value) return;
        if (const auto* group = std::get_if<document::GroupObject>(&*value)) {
            if (group->clip) return;
            for (const auto child : group->children_ids) {
                self(self, child);
                if (exceeded_limit) return;
            }
            return;
        }
        if (const auto* path = std::get_if<document::PathObject>(&*value);
            path && path->has_fill) {
            result.push_back(id);
            exceeded_limit = result.size() > kMaximumBooleanTargets;
        }
    };
    for (const auto& layer : snapshot.page().layers) {
        if (!layer.visible || layer.locked) continue;
        for (const auto id : layer.object_ids) {
            visit(visit, id);
            if (exceeded_limit) break;
        }
        if (exceeded_limit) break;
    }
    return result;
}

enum class PointRelation { Outside, Inside, Boundary, Invalid };

PointRelation point_in_filled_path(const geometry::Path2D& path,
                                   geometry::Point2D point,
                                   double tolerance_mm = 1e-4) {
    const auto flattened = geometry::flatten_path_checked(path,
        geometry::FlatteningOptions{kPointClassificationToleranceMm, 16,
                                    kMaximumPathElements});
    if (flattened.status != geometry::FlatteningStatus::Success)
        return PointRelation::Invalid;
    int winding = 0;
    bool parity = false;
    const double tolerance_squared = tolerance_mm * tolerance_mm;
    for (const auto& subpath : flattened.subpaths) {
        if (!subpath.is_closed || subpath.points.size() < 3) continue;
        for (std::size_t index = 0; index < subpath.points.size(); ++index) {
            const auto a = subpath.points[index];
            const auto b = subpath.points[(index + 1) % subpath.points.size()];
            const auto segment = b - a;
            const double length_squared = segment.x * segment.x + segment.y * segment.y;
            const double parameter = length_squared <= 1e-24 ? 0.0 : std::clamp(
                ((point.x - a.x) * segment.x + (point.y - a.y) * segment.y) /
                    length_squared, 0.0, 1.0);
            const auto nearest = a + segment * parameter;
            const double distance = nearest.distance_to(point);
            if (distance * distance <= tolerance_squared) return PointRelation::Boundary;
            const double cross = segment.x * (point.y - a.y) -
                                 segment.y * (point.x - a.x);
            if (a.y <= point.y) {
                if (b.y > point.y && cross > 0.0) {
                    ++winding;
                    parity = !parity;
                }
            } else if (b.y <= point.y && cross < 0.0) {
                --winding;
                parity = !parity;
            }
        }
    }
    const bool inside = path.winding_rule() == geometry::WindingRule::EvenOdd
        ? parity : winding != 0;
    return inside ? PointRelation::Inside : PointRelation::Outside;
}

std::vector<geometry::Path2D> boolean_components(geometry::BooleanResult&& result) {
    if (!result.success) return {};
    if (!result.components.empty()) return std::move(result.components);
    if (!result.path.empty()) return {std::move(result.path)};
    return {};
}

std::optional<geometry::Path2D> component_containing_point(
    const std::vector<geometry::Path2D>& components,
    geometry::Point2D point, bool& ambiguous) {
    std::optional<geometry::Path2D> selected;
    for (const auto& component : components) {
        const auto relation = point_in_filled_path(component, point, 0.001);
        if (relation == PointRelation::Boundary || relation == PointRelation::Invalid) {
            ambiguous = true;
            return std::nullopt;
        }
        if (relation != PointRelation::Inside) continue;
        if (selected) {
            ambiguous = true;
            return std::nullopt;
        }
        selected = component;
    }
    return selected;
}

std::size_t largest_component_index(const std::vector<geometry::Path2D>& paths) {
    std::size_t selected = 0;
    double largest = -1.0;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto bounds = paths[index].bounds();
        const double area = bounds.width * bounds.height;
        if (std::isfinite(area) && area > largest) {
            largest = area;
            selected = index;
        }
    }
    return selected;
}

document::PathObject make_piece(
    const document::PathObject& source, document::ObjectId id,
    const std::string& name, const geometry::Path2D& world_path,
    const document::SceneHierarchy::Entry& owner) {
    auto piece = source;
    piece.id = id;
    piece.name = name;
    const auto parent_inverse = owner.parent_world.inverted();
    if (!parent_inverse) {
        piece.path.clear();
        return piece;
    }
    piece.path = world_path.transformed(*parent_inverse);
    piece.transform = geometry::Transform2D::identity();
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

bool append_preview_contours(CutGeometryProposal& proposal,
                             const geometry::Path2D& world_path,
                             bool removed, std::size_t& total_points) {
    const auto flattened = geometry::flatten_path_checked(world_path,
        geometry::FlatteningOptions{kPreviewFlattenToleranceMm, 16,
                                    kMaximumPathElements});
    if (flattened.status != geometry::FlatteningStatus::Success)
        return false;
    for (const auto& subpath : flattened.subpaths) {
        if (subpath.points.size() < 2) continue;
        if (subpath.points.size() > kMaximumPreviewPoints - total_points)
            return false;
        total_points += subpath.points.size();
        proposal.preview_contours.push_back(
            {subpath.points, subpath.is_closed, removed});
    }
    return true;
}

struct FaceResolution {
    CutGeometryStatus status{CutGeometryStatus::Invalid};
    geometry::Path2D face;
    std::vector<document::ObjectId> source_ids;
    document::ObjectId style_source{document::kInvalidObjectId};
    std::size_t style_layer{0};
    double approximation_bound_mm{0.0};
    std::string message;
};

FaceResolution resolve_smart_fill_face(
    const document::DocumentSnapshot& snapshot,
    const geometry::Point2D& point_world,
    const std::vector<document::ObjectId>& selected_ids,
    std::stop_token stop, const CutGeometryProgress& progress) {
    FaceResolution result;
    const auto fail = [&result](CutGeometryStatus status, std::string message) {
        result.status = status;
        result.message = std::move(message);
        result.face.clear();
        result.source_ids.clear();
        result.style_source = document::kInvalidObjectId;
        return result;
    };
    if (snapshot.is_content_locked() || !point_world.is_finite())
        return fail(CutGeometryStatus::Invalid, "Invalid point or locked document");
    if (stop.stop_requested())
        return fail(CutGeometryStatus::Cancelled, "Smart Fill was cancelled");

    const bool has_selection = !selected_ids.empty();
    bool candidate_overflow = false;
    const auto candidates = has_selection ? selected_ids
        : all_visible_editable_paths(snapshot, candidate_overflow);
    if (candidate_overflow)
        return fail(CutGeometryStatus::BudgetExceeded,
            "Smart Fill candidate count exceeds the 64-path geometry budget");
    if (candidates.empty())
        return fail(CutGeometryStatus::NoChange,
            "No editable closed filled paths are available");
    if (candidates.size() > kMaximumBooleanTargets)
        return fail(CutGeometryStatus::BudgetExceeded,
            "Smart Fill source count exceeds the 64-path budget");

    std::vector<geometry::Path2D> world_paths;
    world_paths.reserve(candidates.size());
    result.source_ids.reserve(candidates.size());
    std::size_t total_elements = 0;
    if (progress) progress(0, candidates.size());
    std::size_t candidates_processed = 0;
    std::unordered_set<document::ObjectId> seen_candidates;
    for (const auto id : candidates) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Smart Fill was cancelled");
        if (id == document::kInvalidObjectId || !seen_candidates.insert(id).second)
            return fail(CutGeometryStatus::Invalid,
                "Smart Fill source list contains a duplicate or invalid identity");
        const auto value = snapshot.objects().get(id);
        const auto* path = value ? std::get_if<document::PathObject>(&*value) : nullptr;
        const auto* owner = snapshot.hierarchy().find(id);
        if (!path || !owner || owner->mask || !snapshot.hierarchy().editable(id) ||
            !path->has_fill) {
            if (has_selection)
                return fail(CutGeometryStatus::Unsupported,
                    "Select only visible, unlocked filled paths for Smart Fill");
            continue;
        }
        std::optional<document::EditablePath> converted;
        const document::EditablePath* source = path->authoring_source.get();
        if (!source) {
            if (path->path.size() > kMaximumPathElements)
                return fail(CutGeometryStatus::BudgetExceeded,
                    "Smart Fill source exceeds the 200,000-element path budget");
            converted = document::editable_path_from_path2d(path->path, nullptr);
            if (!converted) {
                if (has_selection)
                    return fail(CutGeometryStatus::Unsupported,
                        "A selected source has no readable closed path data");
                continue;
            }
            source = &*converted;
        }
        if (source->subpaths.empty() ||
            std::any_of(source->subpaths.begin(), source->subpaths.end(),
                [](const auto& subpath) { return !subpath.closed; })) {
            if (has_selection)
                return fail(CutGeometryStatus::Unsupported,
                    "Smart Fill requires closed source contours");
            continue;
        }
        std::size_t source_segments = 0;
        for (const auto& subpath : source->subpaths) {
            if (subpath.segments.size() > kMaximumPathElements - source_segments)
                return fail(CutGeometryStatus::BudgetExceeded,
                    "Smart Fill editable segment count exceeds the geometry budget");
            source_segments += subpath.segments.size();
        }
        if (source->subpaths.size() > 256 || path->path.size() > kMaximumPathElements ||
            total_elements > kMaximumTotalElements - path->path.size())
            return fail(CutGeometryStatus::BudgetExceeded,
                "Smart Fill geometry element budget exceeded");
        total_elements += path->path.size();
        auto world = path->path.transformed(owner->world);
        if (world.empty() || !world.bounds().is_finite()) {
            if (has_selection)
                return fail(CutGeometryStatus::Invalid,
                    "A selected source has invalid world geometry");
            continue;
        }
        if (result.style_source == document::kInvalidObjectId) {
            result.style_source = id;
            result.style_layer = owner->layer;
        }
        result.source_ids.push_back(id);
        world_paths.push_back(std::move(world));
        ++candidates_processed;
        if (progress) progress(candidates_processed, candidates.size());
    }
    if (world_paths.empty() || result.style_source == document::kInvalidObjectId)
        return fail(CutGeometryStatus::NoChange,
            "No visible, unlocked closed filled paths are available");

    const auto options = boolean_options();
    geometry::Path2D region = world_paths.front();
    for (std::size_t index = 1; index < world_paths.size(); ++index) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Smart Fill was cancelled");
        auto combined = geometry::boolean_paths(region, world_paths[index],
            geometry::BooleanOp::Union, options);
        if (!combined.success || combined.path.empty())
            return fail(combined.status == geometry::BooleanStatus::WorkBudgetExceeded
                    ? CutGeometryStatus::BudgetExceeded : CutGeometryStatus::Unsupported,
                "Could not build the closed-path arrangement within budget");
        result.approximation_bound_mm += combined.approximation_error_bound_mm;
        region = std::move(combined.path);
    }
    const auto in_union = point_in_filled_path(region, point_world, 0.001);
    if (in_union == PointRelation::Boundary)
        return fail(CutGeometryStatus::Ambiguous,
            "Click inside a face, away from its boundary");
    if (in_union == PointRelation::Invalid)
        return fail(CutGeometryStatus::Invalid,
            "Could not resolve the point against the path arrangement");
    if (in_union != PointRelation::Inside)
        return fail(CutGeometryStatus::NoChange,
            "The click is outside the closed-path arrangement");

    for (const auto& boundary : world_paths) {
        if (stop.stop_requested())
            return fail(CutGeometryStatus::Cancelled, "Smart Fill was cancelled");
        const auto relation = point_in_filled_path(boundary, point_world, 0.001);
        if (relation == PointRelation::Boundary)
            return fail(CutGeometryStatus::Ambiguous,
                "The click lies on a source boundary; choose an interior point");
        if (relation == PointRelation::Invalid)
            return fail(CutGeometryStatus::Invalid,
                "Could not resolve the point against a source boundary");
        const auto operation = relation == PointRelation::Inside
            ? geometry::BooleanOp::Intersection : geometry::BooleanOp::Difference;
        auto split = geometry::boolean_paths(region, boundary, operation, options);
        if (!split.success)
            return fail(split.status == geometry::BooleanStatus::WorkBudgetExceeded
                    ? CutGeometryStatus::BudgetExceeded : CutGeometryStatus::Unsupported,
                "Could not resolve the selected face within the geometry budget");
        result.approximation_bound_mm += split.approximation_error_bound_mm;
        bool ambiguous = false;
        auto component = component_containing_point(boolean_components(std::move(split)),
                                                     point_world, ambiguous);
        if (ambiguous)
            return fail(CutGeometryStatus::Ambiguous,
                "A face boundary or intersection is ambiguous at this point");
        if (!component)
            return fail(CutGeometryStatus::NoChange,
                "The selected face is empty or could not be resolved");
        region = std::move(*component);
    }
    result.face = std::move(region);
    result.status = CutGeometryStatus::Success;
    result.message = "Smart Fill face proposal is ready";
    return result;
}

bool valid_tolerance(double tolerance) {
    return std::isfinite(tolerance) && tolerance >= 0.0 &&
        tolerance <= kCutGeometryDefaultToleranceMm;
}

CutGeometryProposal failure(const CutGeometryRequest& request,
                            CutGeometryStatus status, std::string message) {
    CutGeometryProposal proposal;
    proposal.operation = request.operation;
    proposal.base_snapshot = request.snapshot;
    proposal.max_approximation_error_mm = request.max_approximation_error_mm;
    proposal.status = status;
    proposal.message = std::move(message);
    return proposal;
}

} // namespace

CutGeometryProposal compute_trim_objects_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress) {
    if (!request.snapshot || request.snapshot->is_content_locked())
        return failure(request, CutGeometryStatus::Invalid,
            "Document snapshot is unavailable or locked");
    if (!valid_tolerance(request.max_approximation_error_mm))
        return failure(request, CutGeometryStatus::Invalid,
            "Maximum approximation error must be within 0..0.05 mm");
    if (request.target_id == document::kInvalidObjectId || request.source_ids.empty())
        return failure(request, CutGeometryStatus::Invalid,
            "Trim Objects requires one Target and at least one Source");
    if (request.source_ids.size() > kMaximumBooleanTargets)
        return failure(request, CutGeometryStatus::BudgetExceeded,
            "Trim Objects supports at most 64 Source paths per operation");
    if (stop.stop_requested())
        return failure(request, CutGeometryStatus::Cancelled, "Trim Objects was cancelled");

    const auto target_value = request.snapshot->objects().get(request.target_id);
    const auto* target = target_value
        ? std::get_if<document::PathObject>(&*target_value) : nullptr;
    const auto* target_owner = request.snapshot->hierarchy().find(request.target_id);
    if (!target || !target->has_fill || !target_owner || target_owner->mask ||
        !request.snapshot->hierarchy().editable(request.target_id))
        return failure(request, CutGeometryStatus::Unsupported,
            "Trim Target must be a visible, editable filled path");
    if (target->path.size() > kMaximumPathElements)
        return failure(request, CutGeometryStatus::BudgetExceeded,
            "Trim Target exceeds the 200,000-element path budget");

    std::vector<geometry::Path2D> source_paths;
    source_paths.reserve(request.source_ids.size());
    std::unordered_set<document::ObjectId> seen;
    std::size_t total_elements = target->path.size();
    const std::size_t total_candidates = request.source_ids.size() + 1;
    if (progress) progress(0, total_candidates);
    for (const auto id : request.source_ids) {
        if (stop.stop_requested())
            return failure(request, CutGeometryStatus::Cancelled,
                "Trim Objects was cancelled");
        if (id == request.target_id || id == document::kInvalidObjectId ||
            !seen.insert(id).second)
            return failure(request, CutGeometryStatus::Invalid,
                "Trim Source list contains a duplicate or invalid identity");
        const auto value = request.snapshot->objects().get(id);
        const auto* source = value ? std::get_if<document::PathObject>(&*value) : nullptr;
        const auto* owner = request.snapshot->hierarchy().find(id);
        if (!source || !source->has_fill || !owner || owner->mask ||
            !request.snapshot->hierarchy().editable(id))
            return failure(request, CutGeometryStatus::Unsupported,
                "Every Trim Source must be a visible, editable filled path");
        if (source->path.size() > kMaximumPathElements ||
            total_elements > kMaximumTotalElements - source->path.size())
            return failure(request, CutGeometryStatus::BudgetExceeded,
                "Trim geometry exceeds the 1,000,000-element work budget");
        total_elements += source->path.size();
        auto world = source->path.transformed(owner->world);
        if (world.empty() || !world.bounds().is_finite())
            return failure(request, CutGeometryStatus::Invalid,
                "Trim Source has invalid world geometry");
        source_paths.push_back(std::move(world));
        if (progress) progress(source_paths.size(), total_candidates);
    }

    auto result = geometry::trim_target_with_sources(
        target->path.transformed(target_owner->world), source_paths, boolean_options());
    if (progress) progress(total_candidates, total_candidates);
    if (!result.success) {
        const auto status = result.status == geometry::BooleanStatus::NoChange
            ? CutGeometryStatus::NoChange
            : result.status == geometry::BooleanStatus::WorkBudgetExceeded
                ? CutGeometryStatus::BudgetExceeded
                : result.status == geometry::BooleanStatus::ToleranceNotMet
                    ? CutGeometryStatus::ToleranceNotMet : CutGeometryStatus::Unsupported;
        return failure(request, status,
            result.error_message ? result.error_message : "Trim geometry could not be resolved");
    }
    if (result.status == geometry::BooleanStatus::Empty || result.path.empty())
        return failure(request, CutGeometryStatus::NoChange,
            "Trim would remove the full Target; document unchanged");
    if (!std::isfinite(result.approximation_error_bound_mm) ||
        result.approximation_error_bound_mm < 0.0)
        return failure(request, CutGeometryStatus::Invalid,
            "Trim kernel returned an invalid approximation bound");
    if (result.approximation_error_bound_mm > request.max_approximation_error_mm)
        return failure(request, CutGeometryStatus::ToleranceNotMet,
            "Trim approximation bound exceeds the requested tolerance");

    const double approximation_error_bound_mm = result.approximation_error_bound_mm;
    auto components = boolean_components(std::move(result));
    if (components.empty())
        return failure(request, CutGeometryStatus::NoChange,
            "Trim produced no editable Target components");
    const auto primary = largest_component_index(components);
    CutGeometryProposal proposal;
    proposal.operation = CutGeometryOperation::TrimObjects;
    proposal.base_snapshot = request.snapshot;
    proposal.max_approximation_error_mm = request.max_approximation_error_mm;
    proposal.approximation_error_bound_mm = approximation_error_bound_mm;
    proposal.keep_sources = request.keep_sources;
    proposal.candidates_total = request.source_ids.size() + 1;
    proposal.candidates_processed = proposal.candidates_total;
    CutGeometryReplacement replacement;
    replacement.target_id = request.target_id;
    std::size_t output_elements = 0;
    std::size_t preview_points = 0;
    replacement.pieces.reserve(components.size());
    for (std::size_t index = 0; index < components.size(); ++index) {
        output_elements += components[index].size();
        if (output_elements > kMaximumTotalElements)
            return failure(request, CutGeometryStatus::BudgetExceeded,
                "Trim output exceeds the 1,000,000-element work budget");
        auto piece = make_piece(*target,
            index == primary ? request.target_id : document::kInvalidObjectId,
            components.size() > 1
                ? target->name + " (Trim " + std::to_string(index + 1) + ")"
                : target->name,
            components[index], *target_owner);
        if (piece.path.empty())
            return failure(request, CutGeometryStatus::Unsupported,
                "A Trim result could not be represented as an editable path");
        if (!append_preview_contours(proposal, components[index], false, preview_points))
            return failure(request, CutGeometryStatus::BudgetExceeded,
                "Trim preview exceeds the 250,000-point display budget");
        replacement.pieces.push_back(std::move(piece));
    }
    proposal.replacements.push_back(std::move(replacement));
    if (!request.keep_sources) {
        for (const auto& source_path : source_paths) {
            if (!append_preview_contours(proposal, source_path, true, preview_points))
                return failure(request, CutGeometryStatus::BudgetExceeded,
                    "Trim preview exceeds the 250,000-point display budget");
        }
        proposal.remove_ids = request.source_ids;
    }
    proposal.status = CutGeometryStatus::Success;
    proposal.message = "Trim Objects proposal is ready to commit";
    return proposal;
}

CutGeometryProposal compute_smart_fill_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress) {
    if (!request.snapshot || request.points_world.size() != 1 ||
        !request.points_world.front().is_finite())
        return failure(request, CutGeometryStatus::Invalid,
            "Smart Fill requires one finite face point");
    if (!valid_tolerance(request.max_approximation_error_mm))
        return failure(request, CutGeometryStatus::Invalid,
            "Maximum approximation error must be within 0..0.05 mm");
    auto face = resolve_smart_fill_face(*request.snapshot, request.points_world.front(),
                                        request.selected_ids, stop, progress);
    if (face.status != CutGeometryStatus::Success)
        return failure(request, face.status, std::move(face.message));
    if (face.approximation_bound_mm > request.max_approximation_error_mm)
        return failure(request, CutGeometryStatus::ToleranceNotMet,
            "Smart Fill approximation bound exceeds the requested tolerance");
    if (face.face.size() > kMaximumPathElements)
        return failure(request, CutGeometryStatus::BudgetExceeded,
            "Smart Fill output exceeds the 200,000-element path budget");
    const auto style_value = request.snapshot->objects().get(face.style_source);
    const auto* style = style_value
        ? std::get_if<document::PathObject>(&*style_value) : nullptr;
    const auto editable = document::editable_path_from_path2d(face.face, nullptr);
    if (!style || !editable)
        return failure(request, CutGeometryStatus::Unsupported,
            "Could not create an editable Smart Fill object");
    auto addition = *style;
    addition.id = document::kInvalidObjectId;
    addition.name = "Smart Fill";
    addition.path = std::move(face.face);
    addition.transform = geometry::Transform2D::identity();
    addition.contour_spec.clear();
    addition.set_authoring_source(*editable);

    CutGeometryProposal proposal;
    proposal.operation = CutGeometryOperation::SmartFill;
    proposal.base_snapshot = request.snapshot;
    proposal.max_approximation_error_mm = request.max_approximation_error_mm;
    proposal.approximation_error_bound_mm = face.approximation_bound_mm;
    proposal.candidates_total = face.source_ids.size();
    proposal.candidates_processed = proposal.candidates_total;
    proposal.source_ids = std::move(face.source_ids);
    proposal.style_source_id = face.style_source;
    std::size_t preview_points = 0;
    if (!append_preview_contours(proposal, addition.path, false, preview_points))
        return failure(request, CutGeometryStatus::BudgetExceeded,
            "Smart Fill preview exceeds the 250,000-point display budget");
    proposal.additions.push_back({std::move(addition), face.style_layer});
    proposal.status = CutGeometryStatus::Success;
    proposal.message = "Smart Fill proposal is ready to commit";
    return proposal;
}

CutGeometryProposal compute_shape_builder_geometry(
    const CutGeometryRequest& request, std::stop_token stop,
    const CutGeometryProgress& progress) {
    if (!request.snapshot || request.points_world.empty() ||
        request.points_world.size() > kMaximumBooleanTargets ||
        std::any_of(request.points_world.begin(), request.points_world.end(),
            [](const auto& point) { return !point.is_finite(); }))
        return failure(request, request.points_world.size() > kMaximumBooleanTargets
                ? CutGeometryStatus::BudgetExceeded : CutGeometryStatus::Invalid,
            "Shape Builder requires 1 to 64 finite face points");
    if (!valid_tolerance(request.max_approximation_error_mm))
        return failure(request, CutGeometryStatus::Invalid,
            "Maximum approximation error must be within 0..0.05 mm");

    std::vector<geometry::Path2D> selected_faces;
    selected_faces.reserve(request.points_world.size());
    std::vector<document::ObjectId> source_ids;
    document::ObjectId style_source = document::kInvalidObjectId;
    std::size_t style_layer = 0;
    double face_bound = 0.0;
    for (const auto point : request.points_world) {
        if (stop.stop_requested())
            return failure(request, CutGeometryStatus::Cancelled,
                "Shape Builder was cancelled");
        auto face = resolve_smart_fill_face(*request.snapshot, point,
                                            request.selected_ids, stop, progress);
        if (face.status != CutGeometryStatus::Success)
            return failure(request, face.status, std::move(face.message));
        if (source_ids.empty()) {
            source_ids = std::move(face.source_ids);
            style_source = face.style_source;
            style_layer = face.style_layer;
        } else if (source_ids != face.source_ids) {
            return failure(request, CutGeometryStatus::Stale,
                "The source set changed while Shape Builder was active");
        }
        face_bound = std::max(face_bound, face.approximation_bound_mm);
        selected_faces.push_back(std::move(face.face));
    }
    const auto options = boolean_options();
    geometry::Path2D combined = selected_faces.front();
    std::vector<geometry::Path2D> merged_components{selected_faces.front()};
    double merge_bound = face_bound;
    for (std::size_t index = 1; index < selected_faces.size(); ++index) {
        if (stop.stop_requested())
            return failure(request, CutGeometryStatus::Cancelled,
                "Shape Builder was cancelled");
        auto merged = geometry::boolean_paths(combined, selected_faces[index],
            geometry::BooleanOp::Union, options);
        if (!merged.success || merged.path.empty())
            return failure(request,
                merged.status == geometry::BooleanStatus::WorkBudgetExceeded
                    ? CutGeometryStatus::BudgetExceeded : CutGeometryStatus::Unsupported,
                "Could not combine selected faces within the geometry budget");
        merge_bound += merged.approximation_error_bound_mm;
        merged_components = std::move(merged.components);
        if (merged_components.empty()) merged_components.push_back(merged.path);
        combined = std::move(merged.path);
    }
    if (merge_bound > request.max_approximation_error_mm)
        return failure(request, CutGeometryStatus::ToleranceNotMet,
            "Shape Builder approximation bound exceeds the requested tolerance");

    CutGeometryProposal proposal;
    proposal.operation = CutGeometryOperation::ShapeBuilder;
    proposal.base_snapshot = request.snapshot;
    proposal.max_approximation_error_mm = request.max_approximation_error_mm;
    proposal.erase_faces = request.erase_faces;
    proposal.candidates_total = source_ids.size() + request.points_world.size();
    std::size_t preview_points = 0;
    if (!request.erase_faces) {
        const auto style_value = request.snapshot->objects().get(style_source);
        const auto* style = style_value
            ? std::get_if<document::PathObject>(&*style_value) : nullptr;
        if (!style || !request.snapshot->hierarchy().editable(style_source))
            return failure(request, CutGeometryStatus::Stale,
                "Shape Builder style source is stale or locked");
        std::size_t output_elements = 0;
        for (auto& component : merged_components) {
            if (stop.stop_requested())
                return failure(request, CutGeometryStatus::Cancelled,
                    "Shape Builder was cancelled");
            output_elements += component.size();
            if (output_elements > kMaximumTotalElements ||
                component.size() > kMaximumPathElements)
                return failure(request, CutGeometryStatus::BudgetExceeded,
                    "Shape Builder output exceeds the geometry budget");
            const auto editable = document::editable_path_from_path2d(component, nullptr);
            if (!editable)
                return failure(request, CutGeometryStatus::Unsupported,
                    "A merged face could not be represented as an editable path");
            auto addition = *style;
            addition.id = document::kInvalidObjectId;
            addition.name = "Shape Builder";
            addition.path = std::move(component);
            addition.transform = geometry::Transform2D::identity();
            addition.contour_spec.clear();
            addition.set_authoring_source(*editable);
            if (!append_preview_contours(proposal, addition.path, false, preview_points))
                return failure(request, CutGeometryStatus::BudgetExceeded,
                    "Shape Builder preview exceeds the 250,000-point display budget");
            proposal.additions.push_back({std::move(addition), style_layer});
        }
        proposal.approximation_error_bound_mm = merge_bound;
        proposal.candidates_processed = proposal.candidates_total;
        proposal.status = CutGeometryStatus::Success;
        proposal.message = "Shape Builder merge proposal is ready to commit";
        return proposal;
    }

    double maximum_output_bound = merge_bound;
    std::size_t total_output_elements = 0;
    if (!append_preview_contours(proposal, combined, true, preview_points))
        return failure(request, CutGeometryStatus::BudgetExceeded,
            "Shape Builder preview exceeds the 250,000-point display budget");
    for (const auto id : source_ids) {
        if (stop.stop_requested())
            return failure(request, CutGeometryStatus::Cancelled,
                "Shape Builder was cancelled");
        const auto value = request.snapshot->objects().get(id);
        const auto* source = value ? std::get_if<document::PathObject>(&*value) : nullptr;
        const auto* owner = request.snapshot->hierarchy().find(id);
        if (!source || !owner || owner->mask || !source->has_fill ||
            !request.snapshot->hierarchy().editable(id))
            return failure(request, CutGeometryStatus::Stale,
                "A Shape Builder source became unavailable before erase");
        auto difference = geometry::boolean_paths(
            source->path.transformed(owner->world), combined,
            geometry::BooleanOp::Difference, options);
        if (!difference.success)
            return failure(request,
                difference.status == geometry::BooleanStatus::WorkBudgetExceeded
                    ? CutGeometryStatus::BudgetExceeded : CutGeometryStatus::Unsupported,
                "A source face could not be erased within the geometry budget");
        if (difference.status == geometry::BooleanStatus::NoChange) {
            proposal.preserve_selection_ids.push_back(id);
            ++proposal.candidates_processed;
            continue;
        }
        const double source_bound = merge_bound + difference.approximation_error_bound_mm;
        maximum_output_bound = std::max(maximum_output_bound, source_bound);
        auto components = boolean_components(std::move(difference));
        CutGeometryReplacement replacement;
        replacement.target_id = id;
        const std::size_t primary = components.empty() ? 0 : largest_component_index(components);
        replacement.pieces.reserve(components.size());
        for (std::size_t index = 0; index < components.size(); ++index) {
            total_output_elements += components[index].size();
            if (components[index].size() > kMaximumPathElements ||
                total_output_elements > kMaximumTotalElements)
                return failure(request, CutGeometryStatus::BudgetExceeded,
                    "Shape Builder output exceeds the geometry budget");
            auto piece = make_piece(*source,
                index == primary ? id : document::kInvalidObjectId,
                components.size() > 1
                    ? source->name + " (Face " + std::to_string(index + 1) + ")"
                    : source->name,
                components[index], *owner);
            if (piece.path.empty())
                return failure(request, CutGeometryStatus::Unsupported,
                    "A remaining source component is not editable");
            if (!append_preview_contours(proposal, components[index], false, preview_points))
                return failure(request, CutGeometryStatus::BudgetExceeded,
                    "Shape Builder preview exceeds the 250,000-point display budget");
            replacement.pieces.push_back(std::move(piece));
        }
        proposal.replacements.push_back(std::move(replacement));
        ++proposal.candidates_processed;
    }
    if (proposal.replacements.empty())
        return failure(request, CutGeometryStatus::NoChange,
            "The selected faces do not overlap editable source paths");
    if (maximum_output_bound > request.max_approximation_error_mm)
        return failure(request, CutGeometryStatus::ToleranceNotMet,
            "Shape Builder approximation bound exceeds the requested tolerance");
    proposal.approximation_error_bound_mm = maximum_output_bound;
    proposal.status = CutGeometryStatus::Success;
    proposal.message = "Shape Builder erase proposal is ready to commit";
    return proposal;
}

} // namespace pikdraw::desktop
