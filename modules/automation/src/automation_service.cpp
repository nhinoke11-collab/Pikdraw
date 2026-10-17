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

} // namespace pikdraw::automation
