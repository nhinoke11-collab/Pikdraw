#include "pikdraw/automation/automation_service.hpp"

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
namespace {

bool valid_shape(const CreateObject& request) {
    return request.bounds_mm.is_finite() && request.bounds_mm.width > 0.0 &&
           request.bounds_mm.height > 0.0 && request.fill.is_valid() &&
           request.stroke.is_valid() && std::isfinite(request.stroke_width_mm) &&
           request.stroke_width_mm >= 0.0;
}

std::optional<size_t> root_layer_index(const document::DocumentSnapshot& snapshot,
                                       uint64_t object_id) {
    for (size_t index = 0; index < snapshot.page().layers.size(); ++index) {
        const auto& ids = snapshot.page().layers[index].object_ids;
        if (std::find(ids.begin(), ids.end(), object_id) != ids.end()) return index;
    }
    return std::nullopt;
}

bool object_locked(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) { return value.locked; }, object);
}

bool transform_is_identity(const geometry::Transform2D& transform) {
    constexpr double tolerance = 1e-12;
    return std::abs(transform.a - 1.0) <= tolerance &&
           std::abs(transform.b) <= tolerance && std::abs(transform.c) <= tolerance &&
           std::abs(transform.d - 1.0) <= tolerance && std::abs(transform.tx) <= tolerance &&
           std::abs(transform.ty) <= tolerance;
}

bool supported_edit_target(const document::ObjectVariant& object) {
    return !std::holds_alternative<document::GroupObject>(object);
}

geometry::Rect2D object_rect(const document::ObjectVariant& object) {
    try {
        return std::visit([](const auto& value) -> geometry::Rect2D {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::ImageObject>) return value.bounds();
            if constexpr (std::is_same_v<T, document::TextObject>) {
                const auto layout = typography::TextLayout::layout_text(value.text, value.font);
                const auto transform = value.transform *
                    geometry::Transform2D::translation(value.position.x, value.position.y) *
                    geometry::Transform2D::scale(1.0, -1.0);
                return transform.map_rect(layout.bounds());
            }
            return {};
        }, object);
    } catch (...) {
        const auto invalid = std::numeric_limits<double>::quiet_NaN();
        return {invalid, invalid, invalid, invalid};
    }
}

class SetObjectStyleCommand final : public document::DocumentCommand {
public:
    explicit SetObjectStyleCommand(SetObjectStyle patch) : patch_(std::move(patch)) {}
    std::string_view name() const noexcept override { return "Set Object Style"; }

    document::CommandResult execute(document::Document& doc) override {
        const auto original = doc.objects().get(patch_.target_ids.front());
        if (!original) return document::CommandResult::fail("Style target no longer exists");
        auto updated = *original;
        const bool supported = std::visit([&](auto& object) {
            using T = std::decay_t<decltype(object)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                if (patch_.fill) { object.fill = *patch_.fill; object.has_fill = true; }
                if (patch_.stroke) { object.stroke = *patch_.stroke; object.has_stroke = true; }
                if (patch_.stroke_width_mm) object.stroke_width_mm = *patch_.stroke_width_mm;
                return true;
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                if (patch_.fill) object.fill = *patch_.fill;
                return !patch_.stroke && !patch_.stroke_width_mm;
            } else {
                return false;
            }
        }, updated);
        if (!supported) return document::CommandResult::fail("Style is unsupported for this object type");
        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(doc.page(), doc.objects().set(patch_.target_ids.front(), std::move(updated)));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_)
            return document::CommandResult::fail("Style command has no committed state to restore");
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    SetObjectStyle patch_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class GroupTopLevelObjectsCommand final : public document::DocumentCommand {
public:
    GroupTopLevelObjectsCommand(document::GroupObject group, std::vector<uint64_t> children)
        : group_(std::move(group)), children_(std::move(children)) {}
    std::string_view name() const noexcept override { return "Group Objects"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_ || children_.size() < 2 || group_.id == document::kInvalidObjectId ||
            doc.objects().contains(group_.id))
            return document::CommandResult::fail("A group requires two existing objects and a new ID");
        auto page = doc.page();
        auto objects = doc.objects();
        const std::unordered_set<uint64_t> selected(children_.begin(), children_.end());
        if (selected.size() != children_.size())
            return document::CommandResult::fail("Group target IDs must be unique");
        size_t owner_layer = page.layers.size();
        std::vector<size_t> positions;
        for (size_t layer_index = 0; layer_index < page.layers.size(); ++layer_index) {
            const auto& layer = page.layers[layer_index];
            for (size_t position = 0; position < layer.object_ids.size(); ++position) {
                if (!selected.contains(layer.object_ids[position])) continue;
                if (owner_layer == page.layers.size()) owner_layer = layer_index;
                if (owner_layer != layer_index)
                    return document::CommandResult::fail("Objects must be in the same layer to group");
                positions.push_back(position);
            }
        }
        if (positions.size() != children_.size())
            return document::CommandResult::fail("Only top-level sibling objects can be grouped");
        auto& layer = page.layers[owner_layer];
        if (layer.locked) return document::CommandResult::fail("The target layer is locked");
        const auto insertion = *std::min_element(positions.begin(), positions.end());
        std::vector<uint64_t> remaining;
        std::vector<uint64_t> ordered_children;
        remaining.reserve(layer.object_ids.size() - children_.size() + 1);
        ordered_children.reserve(children_.size());
        for (size_t position = 0; position < layer.object_ids.size(); ++position) {
            const auto id = layer.object_ids[position];
            if (position == insertion) remaining.push_back(group_.id);
            if (selected.contains(id)) ordered_children.push_back(id);
            else remaining.push_back(id);
        }
        group_.children_ids = std::move(ordered_children);
        layer.object_ids = std::move(remaining);
        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), objects.set(group_.id, group_));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_)
            return document::CommandResult::fail("Group command has no committed state to restore");
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    document::GroupObject group_;
    std::vector<uint64_t> children_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class UngroupTopLevelObjectsCommand final : public document::DocumentCommand {
public:
    explicit UngroupTopLevelObjectsCommand(std::vector<uint64_t> groups) : groups_(std::move(groups)) {}
    std::string_view name() const noexcept override { return "Ungroup Objects"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_ || groups_.empty())
            return document::CommandResult::fail("At least one top-level group is required");
        const std::unordered_set<uint64_t> selected(groups_.begin(), groups_.end());
        if (selected.size() != groups_.size())
            return document::CommandResult::fail("Group target IDs must be unique");
        auto page = doc.page();
        auto objects = doc.objects();
        std::unordered_set<uint64_t> found;
        for (auto& layer : page.layers) {
            std::vector<uint64_t> roots;
            roots.reserve(layer.object_ids.size());
            for (const auto id : layer.object_ids) {
                if (!selected.contains(id)) { roots.push_back(id); continue; }
                const auto object = objects.get(id);
                const auto* group = object ? std::get_if<document::GroupObject>(&*object) : nullptr;
                if (!group) return document::CommandResult::fail("Only top-level groups can be ungrouped");
                if (layer.locked || group->locked)
                    return document::CommandResult::fail("A selected group or its layer is locked");
                roots.insert(roots.end(), group->children_ids.begin(), group->children_ids.end());
                found.insert(id);
                objects = objects.remove(id);
            }
            layer.object_ids = std::move(roots);
        }
        if (found.size() != selected.size())
            return document::CommandResult::fail("Only top-level groups can be ungrouped");
        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_)
            return document::CommandResult::fail("Ungroup command has no committed state to restore");
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<uint64_t> groups_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

geometry::Transform2D transform_delta(const TransformObjects& request) {
    const auto& pivot = request.pivot_mm;
    return geometry::Transform2D::translation(request.translate_x_mm, request.translate_y_mm) *
        geometry::Transform2D::translation(pivot.x, pivot.y) *
        geometry::Transform2D::rotation_degrees(request.rotation_degrees) *
        geometry::Transform2D::scale(request.scale_x, request.scale_y) *
        geometry::Transform2D::translation(-pivot.x, -pivot.y);
}

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

std::pair<geometry::Rect2D, ObjectSummary::BoundsQuality>
object_bounds(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) -> std::pair<geometry::Rect2D, ObjectSummary::BoundsQuality> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::PathObject>) {
            return {value.bounds(), ObjectSummary::BoundsQuality::SourceGeometry};
        } else if constexpr (std::is_same_v<T, document::TextObject>) {
            return {value.bounds(), ObjectSummary::BoundsQuality::TextApproximation};
        } else if constexpr (std::is_same_v<T, document::ImageObject>) {
            return {value.bounds(), ObjectSummary::BoundsQuality::SourceGeometry};
        } else {
            return {{}, ObjectSummary::BoundsQuality::Unsupported};
        }
    }, object);
}

const char* object_type(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) -> const char* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::PathObject>) return "path";
        if constexpr (std::is_same_v<T, document::TextObject>) return "text";
        if constexpr (std::is_same_v<T, document::GroupObject>) return "group";
        return "image";
    }, object);
}

} // namespace

std::vector<Capability> AutomationService::capabilities() const {
    return {
        {"document.inspect", "{}", {"document.read"}, false},
        {"objects.query", "{}", {"document.read"}, false},
        {"objects.get", "{object_id,expected_epoch,expected_revision}", {"document.read"}, false},
        {"font.query", "{family_name,size_pt,weight,italic}", {"document.read"}, false},
        {"text.measure", "{text,family_name,size_pt,weight,italic,max_width_mm,line_spacing_ratio,alignment}",
         {"document.read"}, false},
        {"object.create_rectangle", "{x_mm,y_mm,width_mm,height_mm,layer_index,fill,stroke,stroke_width_mm}",
         {"document.edit"}, true},
        {"batch.validate", "{operations:[typed create,transform,style,group,ungroup,align,distribute,duplicate,delete]}",
         {"document.read"}, false},
        {"batch.apply", "{request_id,idempotency_key,expected_epoch,expected_revision,operations:[typed create,transform,style,group,ungroup,align,distribute,duplicate,delete]}",
         {"document.edit"}, true},
        {"history.undo", "{request_id,idempotency_key,expected_epoch,expected_revision}", {"document.edit"}, true},
        {"history.redo", "{request_id,idempotency_key,expected_epoch,expected_revision}", {"document.edit"}, true},
        {"document.validate", "{safe_margin_mm,exclude_safe_margin_ids}", {"document.read"}, false},
    };
}

DocumentState AutomationService::inspect() const {
    const auto snapshot = document_.current_snapshot();
    if (!snapshot) return {};
    return {snapshot->document_id(), snapshot->epoch(), snapshot->revision(),
            snapshot->page().width_mm, snapshot->page().height_mm,
            snapshot->object_count(), snapshot->page().layers.size(), document_.is_dirty()};
}

std::vector<ObjectSummary> AutomationService::query_objects() const {
    const auto snapshot = document_.current_snapshot();
    std::vector<ObjectSummary> result;
    if (!snapshot) return result;
    result.reserve(snapshot->object_count());
    for (size_t layer_index = 0; layer_index < snapshot->page().layers.size(); ++layer_index) {
        for (const auto id : snapshot->page().layers[layer_index].object_ids) {
            const auto object = snapshot->get_object(id);
            if (!object) continue;
            const auto [bounds, bounds_quality] = object_bounds(*object);
            const auto name = std::visit([](const auto& value) { return value.name; }, *object);
            result.push_back({id, object_type(*object), name, bounds, bounds_quality, layer_index});
        }
    }
    return result;
}

Result AutomationService::create_rectangle(const CreateRectangle& request) {
    return create_rectangles(request.expected_epoch, request.expected_revision, {request});
}

BatchValidation AutomationService::validate_rectangles(const std::vector<CreateRectangle>& requests) const {
    std::vector<CreateObject> objects;
    objects.reserve(requests.size());
    for (const auto& request : requests) {
        CreateObject object;
        object.kind = CreateObjectKind::Rectangle;
        object.expected_epoch = request.expected_epoch;
        object.expected_revision = request.expected_revision;
        object.layer_index = request.layer_index;
        object.bounds_mm = request.bounds_mm;
        object.fill = request.fill;
        object.stroke = request.stroke;
        object.stroke_width_mm = request.stroke_width_mm;
        objects.push_back(std::move(object));
    }
    return validate_objects(objects);
}

BatchValidation AutomationService::validate_objects(const std::vector<CreateObject>& requests) const {
    BatchValidation result;
    if (requests.empty()) {
        result.diagnostics.emplace_back("operations must contain at least one supported object.");
        return result;
    }
    if (requests.size() > 256) {
        result.diagnostics.emplace_back("A batch is limited to 256 object operations.");
        return result;
    }
    const auto snapshot = document_.current_snapshot();
    if (!snapshot) {
        result.diagnostics.emplace_back("No committed document snapshot is available.");
        return result;
    }
    for (size_t index = 0; index < requests.size(); ++index) {
        const auto& request = requests[index];
        const auto prefix = "operations[" + std::to_string(index) + "]: ";
        const bool is_text = request.kind == CreateObjectKind::Text;
        const bool is_path = request.kind == CreateObjectKind::Line ||
                             request.kind == CreateObjectKind::Polyline;
        if (is_text && (!request.position_mm.is_finite() || request.text.empty() ||
                        request.text.size() > 8192 || request.name.size() > 256 ||
                        request.font.family_name.empty() || request.font.family_name.size() > 256 ||
                        request.font.postscript_name.size() > 256 ||
                        request.font.weight < 100 || request.font.weight > 900 ||
                        !std::isfinite(request.font.size_pt) || request.font.size_pt <= 0.0 ||
                        request.font.size_pt > 10000.0 || !request.fill.is_valid())) {
            result.diagnostics.emplace_back(prefix + "text, font identity, position, or fill is invalid.");
        } else if (is_path && (request.name.size() > 256 || request.path_points_mm.size() < 2 ||
                   request.path_points_mm.size() > 4096 ||
                   (request.kind == CreateObjectKind::Line && request.path_closed) ||
                   (request.kind == CreateObjectKind::Line && request.path_points_mm.size() != 2) ||
                   (request.path_closed && request.path_points_mm.size() < 3) ||
                   !request.fill.is_valid() || !request.stroke.is_valid() ||
                   !std::isfinite(request.stroke_width_mm) || request.stroke_width_mm < 0.0)) {
            result.diagnostics.emplace_back(prefix + "line/polyline topology, coordinates, colors, or stroke width are invalid.");
        } else if (is_path) {
            bool invalid_points = false;
            for (size_t point_index = 0; point_index < request.path_points_mm.size(); ++point_index) {
                const auto point = request.path_points_mm[point_index];
                if (!point.is_finite() || geometry::validate_point(point) != geometry::GeometryStatus::Ok ||
                    (point_index > 0 && point == request.path_points_mm[point_index - 1])) {
                    invalid_points = true;
                    break;
                }
            }
            if (request.path_closed && request.path_points_mm.back() == request.path_points_mm.front())
                invalid_points = true;
            if (invalid_points) {
                result.diagnostics.emplace_back(prefix + "path points must be finite, in range, and every line segment must have nonzero length.");
            }
        } else if (!is_text && !valid_shape(request)) {
            result.diagnostics.emplace_back(prefix + "shape dimensions, colors, or stroke width are invalid.");
        } else if (is_text) {
            try {
                const auto metrics = measure_text(request.text, request.font);
                if (metrics.missing_glyph_count > 0)
                    result.diagnostics.emplace_back(prefix + "font shaping produced missing glyphs.");
            } catch (const std::exception& error) {
                result.diagnostics.emplace_back(prefix + "text layout failed: " + error.what());
            }
        }
        if (request.layer_index >= snapshot->page().layers.size()) {
            result.diagnostics.emplace_back(prefix + "layer_index does not exist.");
        } else if (snapshot->page().layers[request.layer_index].locked) {
            result.diagnostics.emplace_back(prefix + "target layer is locked.");
        }
    }
    result.valid = result.diagnostics.empty();
    return result;
}

Result AutomationService::create_rectangles(uint64_t expected_epoch, uint64_t expected_revision,
                                             const std::vector<CreateRectangle>& requests) {
    std::vector<CreateObject> objects;
    objects.reserve(requests.size());
    for (const auto& request : requests) {
        CreateObject object;
        object.kind = CreateObjectKind::Rectangle;
        object.expected_epoch = request.expected_epoch;
        object.expected_revision = request.expected_revision;
        object.layer_index = request.layer_index;
        object.bounds_mm = request.bounds_mm;
        object.fill = request.fill;
        object.stroke = request.stroke;
        object.stroke_width_mm = request.stroke_width_mm;
        objects.push_back(std::move(object));
    }
    return create_objects(expected_epoch, expected_revision, objects);
}

Result AutomationService::create_objects(uint64_t expected_epoch, uint64_t expected_revision,
                                          const std::vector<CreateObject>& requests) {
    std::vector<BatchOperation> operations;
    operations.reserve(requests.size());
    for (const auto& request : requests) operations.emplace_back(request);
    return apply_batch(expected_epoch, expected_revision, operations);
}

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

Result AutomationService::undo(uint64_t expected_epoch, uint64_t expected_revision) {
    const auto revision_before = document_.revision();
    if (expected_epoch != document_.epoch())
        return {Status::EpochMismatch, "The document lifecycle changed.", revision_before, revision_before, {}, {}, {}};
    if (expected_revision != revision_before)
        return {Status::RevisionConflict, "The document revision changed.", revision_before, revision_before, {}, {}, {}};
    if (!document_.can_undo())
        return {Status::InvalidArgument, "There is no committed action to undo.", revision_before, revision_before, {}, {}, {}};
    if (!document_.undo())
        return {Status::BusyInteraction, "Undo could not run while the document writer is busy.",
                revision_before, document_.revision(), {}, {}, {}};
    return {Status::Ok, {}, revision_before, document_.revision(), {}, {}, {}};
}

Result AutomationService::redo(uint64_t expected_epoch, uint64_t expected_revision) {
    const auto revision_before = document_.revision();
    if (expected_epoch != document_.epoch())
        return {Status::EpochMismatch, "The document lifecycle changed.", revision_before, revision_before, {}, {}, {}};
    if (expected_revision != revision_before)
        return {Status::RevisionConflict, "The document revision changed.", revision_before, revision_before, {}, {}, {}};
    if (!document_.can_redo())
        return {Status::InvalidArgument, "There is no committed action to redo.", revision_before, revision_before, {}, {}, {}};
    if (!document_.redo())
        return {Status::BusyInteraction, "Redo could not run while the document writer is busy.",
                revision_before, document_.revision(), {}, {}, {}};
    return {Status::Ok, {}, revision_before, document_.revision(), {}, {}, {}};
}

TextMeasurement AutomationService::measure_text(std::string_view text,
    const typography::FontIdentity& font, const typography::LayoutOptions& options) const {
    if (text.size() > 8192) throw std::invalid_argument("Text is limited to 8192 UTF-8 bytes.");
    const auto layout = typography::TextLayout::layout_text(text, font, options);
    TextMeasurement result;
    result.requested_font = font;
    result.bounds_mm = layout.bounds();
    result.line_count = layout.line_count();
    for (const auto& line : layout.lines()) {
        result.actual_font = line.run.font;
        result.is_fallback = result.is_fallback || line.run.is_fallback;
        if (result.warning.empty() && line.run.is_fallback)
            result.warning = line.run.warning_message;
        result.glyph_count += line.run.glyphs.size();
        for (const auto& glyph : line.run.glyphs)
            if (glyph.glyph_id == 0) ++result.missing_glyph_count;
    }
    return result;
}

typography::ResolvedFont AutomationService::query_font(
    const typography::FontIdentity& font) const {
    return typography::FontResolver::instance().resolve(font);
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
