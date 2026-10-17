#include "pikdraw/automation/automation_service.hpp"
#include "automation_service_internal.hpp"

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace pikdraw::automation {
using namespace detail;
namespace {
bool make_alignment_deltas(const document::DocumentSnapshot& snapshot, const AlignObjects& request,
                           std::vector<std::pair<uint64_t, geometry::Transform2D>>& deltas,
                           std::string& error) {
    const auto reference = snapshot.get_object(request.reference_id);
    if (!reference) { error = "alignment reference object does not exist."; return false; }
    const auto reference_bounds = object_rect(*reference);
    const auto valid_reference = geometry::validate_rect(reference_bounds);
    if (valid_reference != geometry::GeometryStatus::Ok) {
        error = "alignment reference bounds are unavailable or outside the supported geometry range.";
        return false;
    }
    bool changes_geometry = false;
    for (const auto id : request.target_ids) {
        const auto value = snapshot.get_object(id);
        if (!value) { error = "alignment target object does not exist."; return false; }
        const auto bounds = object_rect(*value);
        if (geometry::validate_rect(bounds) != geometry::GeometryStatus::Ok) {
            error = "alignment target bounds are unavailable or outside the supported geometry range.";
            return false;
        }
        double dx = 0.0, dy = 0.0;
        switch (request.mode) {
        case AlignmentMode::Left: dx = reference_bounds.x - bounds.x; break;
        case AlignmentMode::CenterX:
            dx = reference_bounds.x + reference_bounds.width * 0.5 -
                 (bounds.x + bounds.width * 0.5); break;
        case AlignmentMode::Right: dx = reference_bounds.right() - bounds.right(); break;
        case AlignmentMode::Top: dy = reference_bounds.y - bounds.y; break;
        case AlignmentMode::CenterY:
            dy = reference_bounds.y + reference_bounds.height * 0.5 -
                 (bounds.y + bounds.height * 0.5); break;
        case AlignmentMode::Bottom: dy = reference_bounds.bottom() - bounds.bottom(); break;
        default: error = "alignment mode is unsupported."; return false;
        }
        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            error = "alignment produced a non-finite translation."; return false;
        }
        changes_geometry = changes_geometry || dx != 0.0 || dy != 0.0;
        deltas.emplace_back(id, geometry::Transform2D::translation(dx, dy));
    }
    if (!changes_geometry) { error = "alignment would not change any target geometry."; return false; }
    return true;
}

bool make_distribution_deltas(const document::DocumentSnapshot& snapshot,
                              const DistributeObjects& request,
                              std::vector<std::pair<uint64_t, geometry::Transform2D>>& deltas,
                              std::string& error) {
    struct Entry { uint64_t id; geometry::Rect2D bounds; };
    std::vector<Entry> entries;
    entries.reserve(request.target_ids.size());
    const bool horizontal = request.axis == DistributionAxis::Horizontal;
    if (!horizontal && request.axis != DistributionAxis::Vertical) {
        error = "distribution axis is unsupported."; return false;
    }
    for (const auto id : request.target_ids) {
        const auto value = snapshot.get_object(id);
        if (!value) { error = "distribution target object does not exist."; return false; }
        const auto bounds = object_rect(*value);
        if (geometry::validate_rect(bounds) != geometry::GeometryStatus::Ok) {
            error = "distribution target bounds are unavailable or outside the supported geometry range.";
            return false;
        }
        entries.push_back({id, bounds});
    }
    std::sort(entries.begin(), entries.end(), [horizontal](const Entry& lhs, const Entry& rhs) {
        const double a = horizontal ? lhs.bounds.x : lhs.bounds.y;
        const double b = horizontal ? rhs.bounds.x : rhs.bounds.y;
        return a == b ? lhs.id < rhs.id : a < b;
    });
    const auto position = [horizontal](const Entry& entry) {
        return horizontal ? entry.bounds.x : entry.bounds.y;
    };
    const auto extent = [horizontal](const Entry& entry) {
        return horizontal ? entry.bounds.width : entry.bounds.height;
    };
    const double start = position(entries.front());
    const double end = position(entries.back()) + extent(entries.back());
    double total_extent = 0.0;
    for (const auto& entry : entries) total_extent += extent(entry);
    const double gap = (end - start - total_extent) / static_cast<double>(entries.size() - 1);
    if (!std::isfinite(gap) || gap < -1e-9) {
        error = "distribution anchors do not leave non-overlapping space for the requested objects.";
        return false;
    }
    double cursor = start;
    bool changes_geometry = false;
    for (size_t index = 0; index < entries.size(); ++index) {
        const double offset = index == 0 || index + 1 == entries.size()
            ? 0.0 : cursor - position(entries[index]);
        const auto& entry = entries[index];
        const auto delta = horizontal
            ? geometry::Transform2D::translation(offset, 0.0)
            : geometry::Transform2D::translation(0.0, offset);
        if (geometry::validate_rect(delta.map_rect(entry.bounds)) != geometry::GeometryStatus::Ok) {
            error = "distributed bounds exceed the supported geometry range."; return false;
        }
        changes_geometry = changes_geometry || offset != 0.0;
        deltas.emplace_back(entry.id, delta);
        cursor += extent(entry) + gap;
    }
    if (!changes_geometry) { error = "distribution would not change any target geometry."; return false; }
    return true;
}

std::optional<document::ObjectVariant> clone_for_duplicate(
    const document::ObjectVariant& source, double offset_x_mm, double offset_y_mm,
    std::string& error) {
    return std::visit([&](const auto& value) -> std::optional<document::ObjectVariant> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::GroupObject> ||
                      std::is_same_v<T, document::ImageObject>) {
            error = "Duplicate currently supports path and text objects; groups and images are unsupported.";
            return std::nullopt;
        } else {
            auto clone = value;
            clone.id = document::generate_object_id();
            if (clone.id == document::kInvalidObjectId) {
                error = "Object identity allocator is exhausted.";
                return std::nullopt;
            }
            clone.transform = geometry::Transform2D::translation(offset_x_mm, offset_y_mm) * clone.transform;
            if (!clone.transform.is_finite()) {
                error = "Duplicate offset produces a non-finite transform.";
                return std::nullopt;
            }
            if constexpr (std::is_same_v<T, document::PathObject>) {
                if (value.authoring_source) {
                    auto source_path = *value.authoring_source;
                    for (auto& subpath : source_path.subpaths) {
                        subpath.id = document::generate_path_element_id<document::SubpathId>();
                        if (!subpath.id.valid()) { error = "Path identity allocator is exhausted."; return std::nullopt; }
                        for (auto& node : subpath.nodes) {
                            node.id = document::generate_path_element_id<document::NodeId>();
                            if (!node.id.valid()) { error = "Path identity allocator is exhausted."; return std::nullopt; }
                        }
                        for (auto& segment : subpath.segments) {
                            segment.id = document::generate_path_element_id<document::SegmentId>();
                            if (!segment.id.valid()) { error = "Path identity allocator is exhausted."; return std::nullopt; }
                        }
                    }
                    try { clone.set_authoring_source(std::move(source_path)); }
                    catch (const std::exception& exception) { error = exception.what(); return std::nullopt; }
                }
            }
            return document::ObjectVariant(std::move(clone));
        }
    }, source);
}

std::optional<document::ObjectVariant> make_object(const CreateObject& request,
                                                   std::string& error) {
    if (request.kind == CreateObjectKind::Rectangle) {
        auto object = vector::ShapeFactory::create_rectangle(
            request.bounds_mm, request.fill, request.stroke, request.stroke_width_mm);
        if (object.id == document::kInvalidObjectId) { error = "Object identity allocator is exhausted."; return std::nullopt; }
        if (!request.name.empty()) object.name = request.name;
        return document::ObjectVariant(std::move(object));
    }
    if (request.kind == CreateObjectKind::Ellipse) {
        auto object = vector::ShapeFactory::create_ellipse(
            {request.bounds_mm.x + request.bounds_mm.width / 2.0,
             request.bounds_mm.y + request.bounds_mm.height / 2.0},
            request.bounds_mm.width / 2.0, request.bounds_mm.height / 2.0,
            request.fill, request.stroke, request.stroke_width_mm);
        if (object.id == document::kInvalidObjectId) { error = "Object identity allocator is exhausted."; return std::nullopt; }
        if (!request.name.empty()) object.name = request.name;
        return document::ObjectVariant(std::move(object));
    }
    if (request.kind == CreateObjectKind::Text) {
        document::TextObject text;
        text.id = document::generate_object_id();
        if (text.id == document::kInvalidObjectId) { error = "Object identity allocator is exhausted."; return std::nullopt; }
        text.name = request.name.empty() ? "AI text" : request.name;
        text.text = request.text;
        text.font = request.font;
        text.fill = request.fill;
        text.alignment = request.text_alignment;
        text.underline = request.underline;
        text.position = request.position_mm;
        return document::ObjectVariant(std::move(text));
    }

    document::EditablePath source;
    document::EditableSubpath subpath;
    subpath.id = document::generate_path_element_id<document::SubpathId>();
    subpath.closed = request.kind == CreateObjectKind::Polyline && request.path_closed;
    const bool closed = subpath.closed;
    if (!subpath.id.valid()) { error = "Path identity allocator is exhausted."; return std::nullopt; }
    subpath.nodes.reserve(request.path_points_mm.size());
    for (const auto point : request.path_points_mm) {
        const auto id = document::generate_path_element_id<document::NodeId>();
        if (!id.valid()) { error = "Path identity allocator is exhausted."; return std::nullopt; }
        subpath.nodes.push_back(document::PathNode{id, point});
    }
    const size_t segment_count = subpath.closed ? subpath.nodes.size() : subpath.nodes.size() - 1;
    subpath.segments.reserve(segment_count);
    for (size_t index = 0; index < segment_count; ++index) {
        const auto id = document::generate_path_element_id<document::SegmentId>();
        if (!id.valid()) { error = "Path identity allocator is exhausted."; return std::nullopt; }
        subpath.segments.push_back(document::EditableSegment{id, document::PathSegmentKind::Line});
    }
    source.subpaths.push_back(std::move(subpath));
    document::PathObject path;
    path.id = document::generate_object_id();
    if (path.id == document::kInvalidObjectId) { error = "Object identity allocator is exhausted."; return std::nullopt; }
    path.name = request.name.empty()
        ? (request.kind == CreateObjectKind::Line ? "Line" : "Polyline") : request.name;
    path.fill = request.fill;
    path.stroke = request.stroke;
    path.stroke_width_mm = request.stroke_width_mm;
    path.has_fill = closed;
    path.has_stroke = true;
    try { path.set_authoring_source(std::move(source)); }
    catch (const std::exception& exception) { error = exception.what(); return std::nullopt; }
    return document::ObjectVariant(std::move(path));
}

} // namespace

BatchValidation AutomationService::validate_batch(const std::vector<BatchOperation>& operations) const {
    BatchValidation result;
    if (operations.empty() || operations.size() > 256) {
        result.diagnostics.emplace_back("operations must contain 1 to 256 supported design operations.");
        return result;
    }
    const auto snapshot = document_.current_snapshot();
    if (!snapshot) {
        result.diagnostics.emplace_back("No committed document snapshot is available.");
        return result;
    }
    std::unordered_set<uint64_t> targeted_ids;
    for (size_t index = 0; index < operations.size(); ++index) {
        const auto prefix = "operations[" + std::to_string(index) + "]: ";
        const auto& operation = operations[index];
        if (const auto* create = std::get_if<CreateObject>(&operation)) {
            const auto validation = validate_objects({*create});
            for (const auto& diagnostic : validation.diagnostics) {
                const auto split = diagnostic.find(": ");
                result.diagnostics.emplace_back(prefix + (split == std::string::npos
                    ? diagnostic : diagnostic.substr(split + 2)));
            }
            continue;
        }

        const auto validate_targets = [&](const std::vector<uint64_t>& ids,
                                          bool allow_image,
                                          bool allow_group,
                                          std::string_view operation_name) {
            if (ids.empty() || ids.size() > 256) {
                result.diagnostics.emplace_back(prefix + std::string(operation_name) +
                    " requires 1 to 256 target_ids.");
                return;
            }
            std::unordered_set<uint64_t> local_ids;
            for (const auto id : ids) {
                if (id == document::kInvalidObjectId || !local_ids.insert(id).second) {
                    result.diagnostics.emplace_back(prefix + "target_ids must be nonzero and unique.");
                    return;
                }
                if (!targeted_ids.insert(id).second) {
                    result.diagnostics.emplace_back(prefix + "an object may be targeted only once per batch.");
                    return;
                }
                const auto layer = root_layer_index(*snapshot, id);
                const auto object = snapshot->get_object(id);
                if (!layer || !object) {
                    result.diagnostics.emplace_back(prefix + "target object " + std::to_string(id) +
                        " is not a top-level object in this document.");
                    return;
                }
                if (snapshot->page().layers[*layer].locked || object_locked(*object)) {
                    result.diagnostics.emplace_back(prefix + "target object " + std::to_string(id) +
                        " is locked or belongs to a locked layer.");
                    return;
                }
                if (!allow_group && !supported_edit_target(*object)) {
                    result.diagnostics.emplace_back(prefix + "group targets are not supported by " +
                        std::string(operation_name) + ".");
                    return;
                }
                if (!allow_image && std::holds_alternative<document::ImageObject>(*object)) {
                    result.diagnostics.emplace_back(prefix + "image targets are not supported by " +
                        std::string(operation_name) + ".");
                    return;
                }
            }
        };

        if (const auto* transform = std::get_if<TransformObjects>(&operation)) {
            if (!std::isfinite(transform->translate_x_mm) || !std::isfinite(transform->translate_y_mm) ||
                !std::isfinite(transform->scale_x) || !std::isfinite(transform->scale_y) ||
                transform->scale_x < 0.0001 || transform->scale_x > 10000.0 ||
                transform->scale_y < 0.0001 || transform->scale_y > 10000.0 ||
                !std::isfinite(transform->rotation_degrees) ||
                std::abs(transform->rotation_degrees) > 360000.0 ||
                !transform->pivot_mm.is_finite() ||
                geometry::validate_point(transform->pivot_mm) != geometry::GeometryStatus::Ok) {
                result.diagnostics.emplace_back(prefix + "transform values must be finite, use positive scale within limits, and keep the pivot in range.");
                continue;
            }
            const auto delta = transform_delta(*transform);
            if (!delta.is_finite() || !delta.is_invertible()) {
                result.diagnostics.emplace_back(prefix + "transform matrix is singular or non-finite.");
                continue;
            }
            if (transform_is_identity(delta)) {
                result.diagnostics.emplace_back(prefix + "object.transform must change the target geometry.");
                continue;
            }
            validate_targets(transform->target_ids, true, false, "object.transform");
            for (const auto id : transform->target_ids) {
                const auto object = snapshot->get_object(id);
                if (!object || !root_layer_index(*snapshot, id) || !supported_edit_target(*object)) continue;
                const auto bounds = delta.map_rect(object_rect(*object));
                if (geometry::validate_rect(bounds) != geometry::GeometryStatus::Ok) {
                    result.diagnostics.emplace_back(prefix + "transformed bounds exceed the supported geometry range.");
                    break;
                }
            }
        } else if (const auto* duplicate = std::get_if<DuplicateObjects>(&operation)) {
            if (!std::isfinite(duplicate->offset_x_mm) || !std::isfinite(duplicate->offset_y_mm)) {
                result.diagnostics.emplace_back(prefix + "duplicate offsets must be finite millimeter values.");
                continue;
            }
            if (duplicate->target_ids.empty() || duplicate->target_ids.size() > 64) {
                result.diagnostics.emplace_back(prefix + "object.duplicate requires 1 to 64 target_ids.");
                continue;
            }
            validate_targets(duplicate->target_ids, false, false, "object.duplicate");
            const auto offset = geometry::Transform2D::translation(duplicate->offset_x_mm,
                                                                    duplicate->offset_y_mm);
            for (const auto id : duplicate->target_ids) {
                const auto object = snapshot->get_object(id);
                if (!object || !root_layer_index(*snapshot, id) ||
                    !supported_edit_target(*object) || std::holds_alternative<document::ImageObject>(*object)) continue;
                if (geometry::validate_rect(offset.map_rect(object_rect(*object))) !=
                    geometry::GeometryStatus::Ok) {
                    result.diagnostics.emplace_back(prefix + "duplicate bounds exceed the supported geometry range.");
                    break;
                }
            }
        } else if (const auto* remove = std::get_if<DeleteObjects>(&operation)) {
            validate_targets(remove->target_ids, true, false, "object.delete");
        } else if (const auto* style = std::get_if<SetObjectStyle>(&operation)) {
            validate_targets(style->target_ids, false, false, "object.style");
            if (!style->fill && !style->stroke && !style->stroke_width_mm)
                result.diagnostics.emplace_back(prefix + "object.style requires at least one style field.");
            if ((style->fill && !style->fill->is_valid()) || (style->stroke && !style->stroke->is_valid()) ||
                (style->stroke_width_mm && (!std::isfinite(*style->stroke_width_mm) || *style->stroke_width_mm < 0.0)))
                result.diagnostics.emplace_back(prefix + "fill/stroke colors and stroke width are invalid.");
            if (style->stroke || style->stroke_width_mm) {
                for (const auto id : style->target_ids) {
                    const auto object = snapshot->get_object(id);
                    if (object && std::holds_alternative<document::TextObject>(*object)) {
                        result.diagnostics.emplace_back(prefix + "text objects support fill changes only.");
                        break;
                    }
                }
            }
        } else if (const auto* group = std::get_if<GroupObjects>(&operation)) {
            if (group->target_ids.size() < 2 || group->target_ids.size() > 256 || group->name.size() > 256) {
                result.diagnostics.emplace_back(prefix + "object.group requires 2 to 256 targets and a name up to 256 UTF-8 bytes.");
                continue;
            }
            validate_targets(group->target_ids, true, false, "object.group");
            std::optional<size_t> shared_layer;
            for (const auto id : group->target_ids) {
                const auto layer = root_layer_index(*snapshot, id);
                const auto object = snapshot->get_object(id);
                if (!layer || !object || !supported_edit_target(*object)) continue;
                if (shared_layer && *shared_layer != *layer) {
                    result.diagnostics.emplace_back(prefix + "group targets must belong to one layer.");
                    break;
                }
                shared_layer = layer;
            }
        } else if (const auto* ungroup = std::get_if<UngroupObjects>(&operation)) {
            if (ungroup->target_ids.empty() || ungroup->target_ids.size() > 256) {
                result.diagnostics.emplace_back(prefix + "object.ungroup requires 1 to 256 group IDs.");
                continue;
            }
            validate_targets(ungroup->target_ids, true, true, "object.ungroup");
            for (const auto id : ungroup->target_ids) {
                const auto object = snapshot->get_object(id);
                if (object && !std::holds_alternative<document::GroupObject>(*object)) {
                    result.diagnostics.emplace_back(prefix + "object.ungroup targets must all be groups.");
                    break;
                }
            }
        } else if (const auto* align = std::get_if<AlignObjects>(&operation)) {
            if (align->target_ids.empty() || align->target_ids.size() > 255 ||
                align->reference_id == document::kInvalidObjectId) {
                result.diagnostics.emplace_back(prefix + "object.align requires a reference and 1 to 255 target IDs.");
                continue;
            }
            std::vector<uint64_t> all_ids{align->reference_id};
            all_ids.insert(all_ids.end(), align->target_ids.begin(), align->target_ids.end());
            validate_targets(all_ids, true, false, "object.align");
            std::vector<std::pair<uint64_t, geometry::Transform2D>> deltas;
            std::string diagnostic;
            if (!make_alignment_deltas(*snapshot, *align, deltas, diagnostic)) {
                result.diagnostics.emplace_back(prefix + diagnostic);
            } else {
                for (const auto& [id, delta] : deltas) {
                    const auto object = snapshot->get_object(id);
                    if (object && geometry::validate_rect(delta.map_rect(object_rect(*object))) !=
                        geometry::GeometryStatus::Ok) {
                        result.diagnostics.emplace_back(prefix + "aligned bounds exceed the supported geometry range.");
                        break;
                    }
                }
            }
        } else if (const auto* distribute = std::get_if<DistributeObjects>(&operation)) {
            if (distribute->target_ids.size() < 3 || distribute->target_ids.size() > 256) {
                result.diagnostics.emplace_back(prefix + "object.distribute requires 3 to 256 target IDs.");
                continue;
            }
            validate_targets(distribute->target_ids, true, false, "object.distribute");
            std::vector<std::pair<uint64_t, geometry::Transform2D>> deltas;
            std::string diagnostic;
            if (!make_distribution_deltas(*snapshot, *distribute, deltas, diagnostic)) {
                result.diagnostics.emplace_back(prefix + diagnostic);
            }
        }
    }
    result.valid = result.diagnostics.empty();
    return result;
}

Result AutomationService::apply_batch(uint64_t expected_epoch, uint64_t expected_revision,
                                      const std::vector<BatchOperation>& operations) {
    const auto revision_before = document_.revision();
    if (expected_epoch != document_.epoch())
        return {Status::EpochMismatch, "The document lifecycle changed.", revision_before, revision_before, {}, {}, {}};
    if (expected_revision != revision_before)
        return {Status::RevisionConflict, "The document revision changed.", revision_before, revision_before, {}, {}, {}};
    const auto validation = validate_batch(operations);
    if (!validation.valid) {
        const auto& diagnostic = validation.diagnostics.front();
        const auto status = diagnostic.find("unsupported") != std::string::npos ||
                            diagnostic.find("not supported") != std::string::npos
            ? Status::UnsupportedCapability
            : diagnostic.find("locked") != std::string::npos ||
              diagnostic.find("not a top-level") != std::string::npos
                ? Status::TargetNotFound : Status::InvalidArgument;
        return {status, diagnostic, revision_before, revision_before, {}, {}, {}};
    }

    auto& writer = document_.writer();
    writer.begin_transaction("AI: Apply Design Batch");
    Result result{Status::Ok, {}, revision_before, revision_before, {}, {}, {}};
    const auto fail_transaction = [&](const std::string& message) {
        writer.rollback_transaction();
        return Result{Status::InternalError, message, revision_before, revision_before, {}, {}, {}};
    };
    for (size_t operation_index = 0; operation_index < operations.size(); ++operation_index) {
        const auto& operation = operations[operation_index];
        if (const auto* create = std::get_if<CreateObject>(&operation)) {
            std::string error;
            auto object = make_object(*create, error);
            if (!object) return fail_transaction(error);
            const auto object_id = std::visit([](const auto& value) { return value.id; }, *object);
            const auto staged = writer.stage_command(std::make_unique<document::AddObjectCommand>(
                std::move(*object), create->layer_index));
            if (!staged.success) return fail_transaction(staged.error_message);
            result.created_ids.push_back(object_id);
            result.created_operation_indices.push_back(operation_index);
            result.affected_ids.push_back(object_id);
        } else if (const auto* transform = std::get_if<TransformObjects>(&operation)) {
            const auto delta = transform_delta(*transform);
            for (const auto id : transform->target_ids) {
                const auto staged = writer.stage_command(std::make_unique<document::TransformObjectCommand>(id, delta));
                if (!staged.success) return fail_transaction(staged.error_message);
                result.affected_ids.push_back(id);
            }
        } else if (const auto* duplicate = std::get_if<DuplicateObjects>(&operation)) {
            for (const auto id : duplicate->target_ids) {
                const auto source = document_.objects().get(id);
                if (!source) return fail_transaction("Duplicate source disappeared after validation.");
                std::string error;
                auto clone = clone_for_duplicate(*source, duplicate->offset_x_mm,
                                                  duplicate->offset_y_mm, error);
                if (!clone) return fail_transaction(error);
                const auto clone_id = std::visit([](const auto& value) { return value.id; }, *clone);
                const auto layer = root_layer_index(*document_.current_snapshot(), id);
                if (!layer) return fail_transaction("Duplicate source is no longer a top-level object.");
                const auto staged = writer.stage_command(std::make_unique<document::AddObjectCommand>(
                    std::move(*clone), *layer));
                if (!staged.success) return fail_transaction(staged.error_message);
                result.created_ids.push_back(clone_id);
                result.created_operation_indices.push_back(operation_index);
                result.affected_ids.push_back(clone_id);
            }
        } else if (const auto* remove = std::get_if<DeleteObjects>(&operation)) {
            for (const auto id : remove->target_ids) {
                const auto staged = writer.stage_command(std::make_unique<document::RemoveObjectCommand>(id));
                if (!staged.success) return fail_transaction(staged.error_message);
                result.affected_ids.push_back(id);
            }
        } else if (const auto* style = std::get_if<SetObjectStyle>(&operation)) {
            for (const auto id : style->target_ids) {
                auto patch = *style;
                patch.target_ids = {id};
                const auto staged = writer.stage_command(std::make_unique<SetObjectStyleCommand>(std::move(patch)));
                if (!staged.success) return fail_transaction(staged.error_message);
                result.affected_ids.push_back(id);
            }
        } else if (const auto* group_op = std::get_if<GroupObjects>(&operation)) {
            document::GroupObject group;
            group.id = document::generate_object_id();
            if (group.id == document::kInvalidObjectId)
                return fail_transaction("Object identity allocator is exhausted.");
            group.name = group_op->name.empty() ? "AI group" : group_op->name;
            const auto staged = writer.stage_command(std::make_unique<GroupTopLevelObjectsCommand>(
                group, group_op->target_ids));
            if (!staged.success) return fail_transaction(staged.error_message);
            result.created_ids.push_back(group.id);
            result.created_operation_indices.push_back(operation_index);
            result.affected_ids.push_back(group.id);
        } else if (const auto* ungroup = std::get_if<UngroupObjects>(&operation)) {
            const auto staged = writer.stage_command(std::make_unique<UngroupTopLevelObjectsCommand>(
                ungroup->target_ids));
            if (!staged.success) return fail_transaction(staged.error_message);
            result.affected_ids.insert(result.affected_ids.end(), ungroup->target_ids.begin(),
                                       ungroup->target_ids.end());
        } else if (const auto* align = std::get_if<AlignObjects>(&operation)) {
            std::vector<std::pair<uint64_t, geometry::Transform2D>> deltas;
            std::string error;
            if (!make_alignment_deltas(*document_.current_snapshot(), *align, deltas, error))
                return fail_transaction(error);
            for (const auto& [id, delta] : deltas) {
                if (transform_is_identity(delta)) continue;
                const auto staged = writer.stage_command(std::make_unique<document::TransformObjectCommand>(id, delta));
                if (!staged.success) return fail_transaction(staged.error_message);
                result.affected_ids.push_back(id);
            }
        } else if (const auto* distribute = std::get_if<DistributeObjects>(&operation)) {
            std::vector<std::pair<uint64_t, geometry::Transform2D>> deltas;
            std::string error;
            if (!make_distribution_deltas(*document_.current_snapshot(), *distribute, deltas, error))
                return fail_transaction(error);
            for (const auto& [id, delta] : deltas) {
                if (transform_is_identity(delta)) continue;
                const auto staged = writer.stage_command(std::make_unique<document::TransformObjectCommand>(id, delta));
                if (!staged.success) return fail_transaction(staged.error_message);
                result.affected_ids.push_back(id);
            }
        }
    }
    const auto committed = writer.commit_transaction();
    if (!committed.success)
        return {Status::InternalError, committed.error_message, revision_before, document_.revision(), {}, {}, {}};
    result.revision_after = document_.revision();
    return result;
}

DocumentValidation AutomationService::validate_document(double safe_margin_mm,
    const std::set<uint64_t>& safe_margin_exempt_ids) const {
    DocumentValidation result;
    const auto snapshot = document_.current_snapshot();
    if (!snapshot) {
        result.diagnostics.push_back({"NO_SNAPSHOT", "error", 0,
                                      "No committed document snapshot is available."});
        return result;
    }
    if (!std::isfinite(safe_margin_mm) || safe_margin_mm < 0.0 ||
        safe_margin_mm * 2.0 >= snapshot->page().width_mm ||
        safe_margin_mm * 2.0 >= snapshot->page().height_mm) {
        result.diagnostics.push_back({"INVALID_SAFE_MARGIN", "error", 0,
                                      "safe_margin_mm must be finite and leave a positive page interior."});
        return result;
    }
    for (size_t layer_index = 0; layer_index < snapshot->page().layers.size(); ++layer_index) {
        for (const auto id : snapshot->page().layers[layer_index].object_ids) {
            const auto value = snapshot->get_object(id);
            if (!value) continue;
            geometry::Rect2D bounds{};
            bool has_bounds = true;
            bool check_safe_margin = true;
            if (const auto* path = std::get_if<document::PathObject>(&*value)) {
                bounds = path->bounds();
            } else if (const auto* text = std::get_if<document::TextObject>(&*value)) {
                try {
                    const auto metrics = measure_text(text->text, text->font);
                    const auto transform = text->transform *
                        geometry::Transform2D::translation(text->position.x, text->position.y) *
                        geometry::Transform2D::scale(1.0, -1.0);
                    bounds = transform.map_rect(metrics.bounds_mm);
                    if (metrics.missing_glyph_count > 0)
                        result.diagnostics.push_back({"MISSING_GLYPH", "error", id,
                            "Text shaping contains missing glyphs for the requested text."});
                    if (metrics.is_fallback)
                        result.diagnostics.push_back({"FONT_FALLBACK", "warning", id, metrics.warning});
                } catch (const std::exception& error) {
                    has_bounds = false;
                    result.diagnostics.push_back({"TEXT_LAYOUT_UNAVAILABLE", "not_assessed", id,
                        std::string("Text metrics could not be computed: ") + error.what()});
                }
            } else if (const auto* image = std::get_if<document::ImageObject>(&*value)) {
                bounds = image->bounds();
            } else {
                has_bounds = false;
                check_safe_margin = false;
                result.diagnostics.push_back({"GROUP_BOUNDS_UNSUPPORTED", "not_assessed", id,
                    "Group bounds are not computed by the automation validator."});
            }
            if (!has_bounds) continue;
            const double right = bounds.x + bounds.width;
            const double top = bounds.y + bounds.height;
            if (!bounds.is_finite() || bounds.x < 0.0 || bounds.y < 0.0 ||
                right > snapshot->page().width_mm || top > snapshot->page().height_mm)
                result.diagnostics.push_back({"OUTSIDE_PAGE", "error", id,
                    "Object geometry exceeds the page bounds."});
            if (check_safe_margin && !safe_margin_exempt_ids.contains(id) && safe_margin_mm > 0.0 &&
                (bounds.x < safe_margin_mm || bounds.y < safe_margin_mm ||
                 right > snapshot->page().width_mm - safe_margin_mm ||
                 top > snapshot->page().height_mm - safe_margin_mm))
                result.diagnostics.push_back({"SAFE_MARGIN", "error", id,
                    "Object geometry exceeds the requested safe margin."});
        }
    }
    result.valid = std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const auto& diagnostic) { return diagnostic.severity == "error" ||
                                            diagnostic.severity == "not_assessed"; });
    return result;
}

} // namespace pikdraw::automation
