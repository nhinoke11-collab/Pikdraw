#include "pikdraw/automation/automation_rpc.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace pikdraw::automation {
namespace {

std::string id_string(uint64_t value) { return std::to_string(value); }

std::string random_handle() {
    std::array<uint32_t, 4> words{};
    std::random_device random;
    for (auto& word : words) word = random();
    std::ostringstream output;
    output << "ses_" << std::hex << std::setfill('0');
    for (const auto word : words) output << std::setw(8) << word;
    return output.str();
}

const JsonValue* field(const JsonValue& value, std::string_view name) { return value.get(name); }
const std::string* string_field(const JsonValue& value, std::string_view name) {
    const auto* item = field(value, name); return item ? item->string() : nullptr;
}
const double* number_field(const JsonValue& value, std::string_view name) {
    const auto* item = field(value, name); return item ? item->number() : nullptr;
}

bool only_fields(const JsonValue& value, std::initializer_list<std::string_view> allowed) {
    const auto* object = value.object();
    if (!object) return false;
    for (const auto& [key, ignored] : *object) {
        (void)ignored;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return false;
    }
    return true;
}

JsonValue rect_json(const geometry::Rect2D& bounds) {
    return JsonValue::Object{{"x_mm", bounds.x}, {"y_mm", bounds.y},
                             {"width_mm", bounds.width}, {"height_mm", bounds.height}};
}

JsonValue color_json(const color::ColorValue& value) {
    JsonValue::Object channels;
    std::string model;
    std::visit([&](const auto& color_value) {
        using T = std::decay_t<decltype(color_value)>;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            model = "rgb"; channels = {{"r", color_value.r}, {"g", color_value.g},
                                       {"b", color_value.b}, {"alpha", color_value.a}};
        } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
            model = "cmyk"; channels = {{"c", color_value.c}, {"m", color_value.m},
                                        {"y", color_value.y}, {"k", color_value.k}, {"alpha", color_value.a}};
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
            model = "spot"; channels = {{"name", color_value.spot_name}, {"tint", color_value.tint},
                {"r_alternate", color_value.alternate_rgb.r}, {"g_alternate", color_value.alternate_rgb.g},
                {"b_alternate", color_value.alternate_rgb.b}, {"alpha", color_value.alternate_rgb.a},
                {"overprint", color_value.overprint}};
        } else {
            model = "grayscale"; channels = {{"gray", color_value.gray}, {"alpha", color_value.a}};
        }
    }, value.payload());
    return JsonValue::Object{{"model", model}, {"channels", std::move(channels)},
        {"overprint_fill", value.overprint().fill}, {"overprint_stroke", value.overprint().stroke}};
}

[[maybe_unused]] std::string bounds_quality(ObjectSummary::BoundsQuality quality) {
    switch (quality) {
    case ObjectSummary::BoundsQuality::SourceGeometry: return "source_geometry";
    case ObjectSummary::BoundsQuality::TextApproximation: return "text_approximation";
    default: return "unsupported";
    }
}

JsonValue object_json(const document::ObjectVariant& value, size_t layer_index) {
    return std::visit([&](const auto& object) -> JsonValue {
        using T = std::decay_t<decltype(object)>;
        JsonValue::Object result{{"id", id_string(object.id)}, {"name", object.name},
                                 {"layer_index", static_cast<double>(layer_index)}};
        result["transform"] = JsonValue::Object{{"a", object.transform.a}, {"b", object.transform.b},
            {"c", object.transform.c}, {"d", object.transform.d},
            {"tx_mm", object.transform.tx}, {"ty_mm", object.transform.ty}};
        if constexpr (std::is_same_v<T, document::PathObject>) {
            result["type"] = "path";
            result["bounds"] = rect_json(object.bounds());
            result["bounds_quality"] = "source_geometry";
            result["fill"] = color_json(object.fill);
            result["stroke"] = color_json(object.stroke);
            result["stroke_width_mm"] = object.stroke_width_mm;
            result["has_fill"] = object.has_fill;
            result["has_stroke"] = object.has_stroke;
            result["visible"] = object.visible; result["locked"] = object.locked;
            JsonValue::Array elements;
            for (const auto& element : object.path.elements()) {
                elements.emplace_back(JsonValue::Object{
                    {"verb", static_cast<double>(element.verb)},
                    {"x_mm", element.pt.x}, {"y_mm", element.pt.y},
                    {"control1_x_mm", element.ctrl1.x}, {"control1_y_mm", element.ctrl1.y},
                    {"control2_x_mm", element.ctrl2.x}, {"control2_y_mm", element.ctrl2.y},
                    {"rx_mm", element.rx}, {"ry_mm", element.ry},
                    {"rotation_degrees", element.rotation_deg},
                    {"large_arc", element.large_arc}, {"sweep", element.sweep}});
            }
            result["path_elements"] = std::move(elements);
            result["path_representation"] = object.authoring_source ? "editable_path_and_adapter" : "path_adapter_only";
            if (object.authoring_source) {
                const auto node_mode = [](document::PathNodeMode mode) -> const char* {
                    switch (mode) {
                    case document::PathNodeMode::Corner: return "corner";
                    case document::PathNodeMode::Cusp: return "cusp";
                    case document::PathNodeMode::Smooth: return "smooth";
                    case document::PathNodeMode::Symmetric: return "symmetric";
                    case document::PathNodeMode::AutoSmooth: return "auto_smooth";
                    }
                    return "unknown";
                };
                const auto segment_kind = [](document::PathSegmentKind kind) -> const char* {
                    switch (kind) {
                    case document::PathSegmentKind::Line: return "line";
                    case document::PathSegmentKind::QuadraticBezier: return "quadratic_bezier";
                    case document::PathSegmentKind::CubicBezier: return "cubic_bezier";
                    case document::PathSegmentKind::EllipticArc: return "elliptic_arc";
                    }
                    return "unknown";
                };
                JsonValue::Array subpaths;
                for (const auto& subpath : object.authoring_source->subpaths) {
                    JsonValue::Array nodes;
                    for (const auto& node : subpath.nodes) {
                        nodes.emplace_back(JsonValue::Object{{"id", id_string(node.id.value())},
                            {"x_mm", node.anchor.x}, {"y_mm", node.anchor.y},
                            {"mode", node_mode(node.mode)},
                            {"incoming_handle", node.has_incoming_handle
                                ? JsonValue(JsonValue::Object{{"x_mm", node.incoming_handle.x}, {"y_mm", node.incoming_handle.y}})
                                : JsonValue()},
                            {"outgoing_handle", node.has_outgoing_handle
                                ? JsonValue(JsonValue::Object{{"x_mm", node.outgoing_handle.x}, {"y_mm", node.outgoing_handle.y}})
                                : JsonValue()}});
                    }
                    JsonValue::Array segments;
                    for (const auto& segment : subpath.segments) {
                        JsonValue::Object item{{"id", id_string(segment.id.value())},
                            {"kind", segment_kind(segment.kind)}};
                        if (segment.kind == document::PathSegmentKind::QuadraticBezier)
                            item["control"] = JsonValue::Object{{"x_mm", segment.quadratic_control.x},
                                                                {"y_mm", segment.quadratic_control.y}};
                        if (segment.kind == document::PathSegmentKind::EllipticArc)
                            item["arc"] = JsonValue::Object{{"center_x_mm", segment.arc.center.x},
                                {"center_y_mm", segment.arc.center.y},
                                {"axis_u_x_mm", segment.arc.axis_u.x}, {"axis_u_y_mm", segment.arc.axis_u.y},
                                {"axis_v_x_mm", segment.arc.axis_v.x}, {"axis_v_y_mm", segment.arc.axis_v.y},
                                {"start_angle_radians", segment.arc.start_angle_rad},
                                {"sweep_angle_radians", segment.arc.sweep_angle_rad}};
                        segments.emplace_back(std::move(item));
                    }
                    subpaths.emplace_back(JsonValue::Object{{"id", id_string(subpath.id.value())},
                        {"closed", subpath.closed}, {"nodes", std::move(nodes)},
                        {"segments", std::move(segments)}});
                }
                result["editable_path"] = JsonValue::Object{
                    {"winding_rule", object.authoring_source->winding_rule == geometry::WindingRule::EvenOdd
                        ? "even_odd" : "non_zero"},
                    {"subpaths", std::move(subpaths)}};
            }
        } else if constexpr (std::is_same_v<T, document::TextObject>) {
            result["type"] = "text"; result["text"] = object.text;
            result["font_family"] = object.font.family_name;
            result["font_postscript_name"] = object.font.postscript_name;
            result["font_weight"] = static_cast<double>(object.font.weight);
            result["font_italic"] = object.font.italic;
            result["font_size_pt"] = object.font.size_pt;
            result["alignment"] = object.alignment == typography::TextAlignment::Left ? "left" :
                object.alignment == typography::TextAlignment::Center ? "center" : "right";
            result["underline"] = object.underline;
            result["fill"] = color_json(object.fill);
            result["position_x_mm"] = object.position.x; result["position_y_mm"] = object.position.y;
            result["visible"] = object.visible; result["locked"] = object.locked;
            try {
                const auto layout = typography::TextLayout::layout_text(object.text, object.font);
                const auto transform = object.transform *
                    geometry::Transform2D::translation(object.position.x, object.position.y) *
                    geometry::Transform2D::scale(1.0, -1.0);
                result["bounds"] = rect_json(transform.map_rect(layout.bounds()));
                result["bounds_quality"] = "typography_metrics";
                JsonValue::Array lines;
                size_t missing_glyphs = 0;
                bool fallback = false;
                for (const auto& line : layout.lines()) {
                    fallback = fallback || line.run.is_fallback;
                    for (const auto& glyph : line.run.glyphs)
                        if (glyph.glyph_id == 0) ++missing_glyphs;
                    lines.emplace_back(JsonValue::Object{{"glyph_count", static_cast<double>(line.run.glyphs.size())},
                        {"advance_mm", line.run.total_advance_mm}, {"actual_family", line.run.font.family_name},
                        {"actual_postscript_name", line.run.font.postscript_name},
                        {"fallback", line.run.is_fallback}, {"warning", line.run.warning_message}});
                }
                result["lines"] = std::move(lines);
                result["missing_glyph_count"] = static_cast<double>(missing_glyphs);
                result["font_fallback"] = fallback;
            } catch (const std::exception& error) {
                result["bounds_quality"] = "unsupported";
                result["text_metrics_error"] = error.what();
            }
        } else if constexpr (std::is_same_v<T, document::GroupObject>) {
            result["type"] = "group"; result["bounds_quality"] = "unsupported";
            JsonValue::Array children;
            for (auto id : object.children_ids) children.emplace_back(id_string(id));
            result["children_ids"] = std::move(children);
            result["visible"] = object.visible; result["locked"] = object.locked;
        } else {
            result["type"] = "image"; result["bounds"] = rect_json(object.bounds());
            result["bounds_quality"] = "source_geometry";
            result["width_px"] = static_cast<double>(object.width_px);
            result["height_px"] = static_cast<double>(object.height_px);
            result["visible"] = object.visible; result["locked"] = object.locked;
        }
        return result;
    }, value);
}

bool parse_color(const JsonValue* input, color::ColorValue& color_out) {
    if (!input) return true;
    const auto* model = string_field(*input, "model");
    const auto* channels = field(*input, "channels");
    if (!model || *model != "rgb" || !channels || !channels->object()) return false;
    const auto read = [&](std::string_view key, double fallback, double& out) {
        const auto* value = number_field(*channels, key);
        out = value ? *value : fallback;
        return std::isfinite(out) && out >= 0.0 && out <= 1.0;
    };
    color::RGBColor rgb;
    if (!read("r", rgb.r, rgb.r) || !read("g", rgb.g, rgb.g) ||
        !read("b", rgb.b, rgb.b) || !read("alpha", rgb.a, rgb.a)) return false;
    color_out = color::ColorValue{rgb};
    return true;
}

bool parse_target_ids(const JsonValue& value, std::vector<uint64_t>& ids,
                      size_t maximum, std::string& diagnostic) {
    const auto* values = field(value, "target_ids");
    if (!values || !values->array() || values->array()->empty() || values->array()->size() > maximum) {
        diagnostic = "target_ids must be an array containing 1 to " + std::to_string(maximum) + " object IDs.";
        return false;
    }
    std::unordered_set<uint64_t> seen;
    for (const auto& item : *values->array()) {
        if (!item.string() || item.string()->empty()) {
            diagnostic = "Every target_id must be an unsigned decimal string.";
            return false;
        }
        uint64_t id = 0;
        const auto& text = *item.string();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || id == 0) {
            diagnostic = "Every target_id must be a nonzero unsigned decimal string.";
            return false;
        }
        if (!seen.insert(id).second) {
            diagnostic = "target_ids must be unique.";
            return false;
        }
        ids.push_back(id);
    }
    return true;
}

bool parse_object_id(const JsonValue& value, std::string_view field_name, uint64_t& id,
                     std::string& diagnostic) {
    const auto* text = string_field(value, field_name);
    if (!text || text->empty()) {
        diagnostic = std::string(field_name) + " must be a nonzero unsigned decimal string.";
        return false;
    }
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size() || id == 0) {
        diagnostic = std::string(field_name) + " must be a nonzero unsigned decimal string.";
        return false;
    }
    return true;
}

struct ObjectLocation {
    size_t layer_index{0};
    bool top_level{false};
    std::optional<uint64_t> parent_group_id;
};

std::optional<ObjectLocation> find_object_location(const document::DocumentSnapshot& snapshot,
                                                    uint64_t target_id) {
    for (size_t layer_index = 0; layer_index < snapshot.page().layers.size(); ++layer_index) {
        std::vector<std::pair<uint64_t, std::optional<uint64_t>>> pending;
        for (const auto id : snapshot.page().layers[layer_index].object_ids)
            pending.emplace_back(id, std::nullopt);
        std::unordered_set<uint64_t> visited;
        while (!pending.empty()) {
            const auto [id, parent] = pending.back();
            pending.pop_back();
            if (!visited.insert(id).second) continue;
            if (id == target_id) return ObjectLocation{layer_index, !parent.has_value(), parent};
            const auto value = snapshot.get_object(id);
            const auto* group = value ? std::get_if<document::GroupObject>(&*value) : nullptr;
            if (!group) continue;
            for (const auto child : group->children_ids) pending.emplace_back(child, id);
        }
    }
    return std::nullopt;
}

bool parse_object_operations(const JsonValue* input, uint64_t epoch, uint64_t revision,
                             std::vector<BatchOperation>& parsed_result,
                             std::vector<std::string>& client_refs, std::string& diagnostic) {
    const auto* raw_operations = input ? input->array() : nullptr;
    if (!raw_operations || raw_operations->empty() || raw_operations->size() > 256) {
        diagnostic = "operations must be an array containing 1 to 256 supported design operations.";
        return false;
    }
    std::set<std::string, std::less<>> seen_refs;
    std::vector<BatchOperation> parsed_operations;
    std::vector<std::string> parsed_client_refs;
    parsed_operations.reserve(raw_operations->size());
    parsed_client_refs.reserve(raw_operations->size());
    for (size_t index = 0; index < raw_operations->size(); ++index) {
        const auto& value = (*raw_operations)[index];
        const auto* object = value.object();
        const auto prefix = "operations[" + std::to_string(index) + "]: ";
        if (!object) { diagnostic = prefix + "must be an object."; return false; }
        const auto* op = string_field(value, "op");
        if (!op) { diagnostic = prefix + "op is required."; return false; }

        const auto reject_unknown = [&](const std::set<std::string, std::less<>>& allowed) {
            for (const auto& [key, ignored] : *object) {
                (void)ignored;
                if (!allowed.contains(key)) {
                    diagnostic = prefix + "unknown field '" + key + "'.";
                    return false;
                }
            }
            return true;
        };
        const auto read_optional_number = [&](std::string_view key, double& target) {
            if (!field(value, key)) return true;
            const auto* number = number_field(value, key);
            if (!number || !std::isfinite(*number)) {
                diagnostic = prefix + std::string(key) + " must be a finite number.";
                return false;
            }
            target = *number;
            return true;
        };
        const bool is_create = *op == "object.create_rectangle" || *op == "object.create_ellipse" ||
            *op == "object.create_text" || *op == "object.create_line" || *op == "object.create_polyline";
        if (!is_create) {
            std::vector<uint64_t> target_ids;
            std::string target_error;
            if (*op == "object.transform") {
                const std::set<std::string, std::less<>> allowed{
                    "op", "target_ids", "translate_x_mm", "translate_y_mm", "scale_x", "scale_y",
                    "rotation_degrees", "pivot_x_mm", "pivot_y_mm"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 256, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                TransformObjects transform;
                transform.target_ids = std::move(target_ids);
                if (!read_optional_number("translate_x_mm", transform.translate_x_mm) ||
                    !read_optional_number("translate_y_mm", transform.translate_y_mm) ||
                    !read_optional_number("scale_x", transform.scale_x) ||
                    !read_optional_number("scale_y", transform.scale_y) ||
                    !read_optional_number("rotation_degrees", transform.rotation_degrees) ||
                    !read_optional_number("pivot_x_mm", transform.pivot_mm.x) ||
                    !read_optional_number("pivot_y_mm", transform.pivot_mm.y)) return false;
                if (!std::isfinite(transform.translate_x_mm) || !std::isfinite(transform.translate_y_mm) ||
                    !std::isfinite(transform.scale_x) || !std::isfinite(transform.scale_y) ||
                    !std::isfinite(transform.rotation_degrees) || !transform.pivot_mm.is_finite()) {
                    diagnostic = prefix + "transform fields must be finite numbers."; return false;
                }
                parsed_operations.emplace_back(std::move(transform));
                parsed_client_refs.emplace_back();
                continue;
            }
            if (*op == "object.duplicate") {
                const std::set<std::string, std::less<>> allowed{
                    "op", "target_ids", "offset_x_mm", "offset_y_mm"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 64, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                DuplicateObjects duplicate;
                duplicate.target_ids = std::move(target_ids);
                if (!read_optional_number("offset_x_mm", duplicate.offset_x_mm) ||
                    !read_optional_number("offset_y_mm", duplicate.offset_y_mm)) return false;
                if (!std::isfinite(duplicate.offset_x_mm) || !std::isfinite(duplicate.offset_y_mm)) {
                    diagnostic = prefix + "duplicate offsets must be finite numbers."; return false;
                }
                parsed_operations.emplace_back(std::move(duplicate));
                parsed_client_refs.emplace_back();
                continue;
            }
            if (*op == "object.delete") {
                const std::set<std::string, std::less<>> allowed{"op", "target_ids"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 256, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                parsed_operations.emplace_back(DeleteObjects{std::move(target_ids)});
                parsed_client_refs.emplace_back();
                continue;
            }
            if (*op == "object.style") {
                const std::set<std::string, std::less<>> allowed{
                    "op", "target_ids", "fill", "stroke", "stroke_width_mm"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 256, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                SetObjectStyle style;
                style.target_ids = std::move(target_ids);
                if (field(value, "fill")) {
                    color::ColorValue fill;
                    if (!parse_color(field(value, "fill"), fill)) {
                        diagnostic = prefix + "fill must be a validated RGB source color."; return false;
                    }
                    style.fill = fill;
                }
                if (field(value, "stroke")) {
                    color::ColorValue stroke;
                    if (!parse_color(field(value, "stroke"), stroke)) {
                        diagnostic = prefix + "stroke must be a validated RGB source color."; return false;
                    }
                    style.stroke = stroke;
                }
                double stroke_width = 0.0;
                if (!read_optional_number("stroke_width_mm", stroke_width)) return false;
                if (field(value, "stroke_width_mm")) style.stroke_width_mm = stroke_width;
                parsed_operations.emplace_back(std::move(style));
                parsed_client_refs.emplace_back();
                continue;
            }
            if (*op == "object.group") {
                const std::set<std::string, std::less<>> allowed{
                    "op", "target_ids", "client_ref", "name"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 256, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                GroupObjects group;
                group.target_ids = std::move(target_ids);
                if (field(value, "name") && !string_field(value, "name")) {
                    diagnostic = prefix + "name must be a string."; return false;
                }
                if (const auto* name = string_field(value, "name")) {
                    if (name->size() > 256) { diagnostic = prefix + "name exceeds 256 UTF-8 bytes."; return false; }
                    group.name = *name;
                }
                std::string client_ref;
                if (field(value, "client_ref") && !string_field(value, "client_ref")) {
                    diagnostic = prefix + "client_ref must be a string."; return false;
                }
                if (const auto* ref = string_field(value, "client_ref")) {
                    if (ref->empty() || ref->size() > 64 || !seen_refs.insert(*ref).second) {
                        diagnostic = prefix + "client_ref must be unique and 1 to 64 characters."; return false;
                    }
                    client_ref = *ref;
                }
                parsed_operations.emplace_back(std::move(group));
                parsed_client_refs.push_back(std::move(client_ref));
                continue;
            }
            if (*op == "object.ungroup") {
                const std::set<std::string, std::less<>> allowed{"op", "target_ids"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 256, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                parsed_operations.emplace_back(UngroupObjects{std::move(target_ids)});
                parsed_client_refs.emplace_back();
                continue;
            }
            if (*op == "object.align") {
                const std::set<std::string, std::less<>> allowed{
                    "op", "reference_id", "target_ids", "mode"};
                if (!reject_unknown(allowed)) return false;
                AlignObjects align;
                std::string target_error;
                if (!parse_object_id(value, "reference_id", align.reference_id, target_error) ||
                    !parse_target_ids(value, align.target_ids, 255, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                const auto* mode = string_field(value, "mode");
                if (!mode) { diagnostic = prefix + "mode is required and must be a string."; return false; }
                if (*mode == "left") align.mode = AlignmentMode::Left;
                else if (*mode == "center_x") align.mode = AlignmentMode::CenterX;
                else if (*mode == "right") align.mode = AlignmentMode::Right;
                else if (*mode == "top") align.mode = AlignmentMode::Top;
                else if (*mode == "center_y") align.mode = AlignmentMode::CenterY;
                else if (*mode == "bottom") align.mode = AlignmentMode::Bottom;
                else { diagnostic = prefix + "mode must be left, center_x, right, top, center_y or bottom."; return false; }
                parsed_operations.emplace_back(std::move(align));
                parsed_client_refs.emplace_back();
                continue;
            }
            if (*op == "object.distribute") {
                const std::set<std::string, std::less<>> allowed{"op", "target_ids", "axis"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 256, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                const auto* axis = string_field(value, "axis");
                if (!axis) { diagnostic = prefix + "axis is required and must be a string."; return false; }
                DistributionAxis parsed_axis;
                if (*axis == "horizontal") parsed_axis = DistributionAxis::Horizontal;
                else if (*axis == "vertical") parsed_axis = DistributionAxis::Vertical;
                else { diagnostic = prefix + "axis must be horizontal or vertical."; return false; }
                parsed_operations.emplace_back(DistributeObjects{std::move(target_ids), parsed_axis});
                parsed_client_refs.emplace_back();
                continue;
            }
            diagnostic = prefix + "unsupported op '" + *op + "'.";
            return false;
        }

        const std::set<std::string, std::less<>> common_create_fields{
            "op", "client_ref", "name", "layer_index", "fill", "stroke", "stroke_width_mm"};
        std::set<std::string, std::less<>> allowed = common_create_fields;
        if (*op == "object.create_rectangle" || *op == "object.create_ellipse") {
            allowed.insert({"x_mm", "y_mm", "width_mm", "height_mm"});
        } else if (*op == "object.create_text") {
            allowed.insert({"x_mm", "y_mm", "text", "font_family", "font_postscript_name",
                "font_weight", "font_italic", "font_size_pt", "alignment", "underline"});
        } else {
            allowed.insert({"points_mm", "closed"});
        }
        if (!reject_unknown(allowed)) return false;

        CreateObject create;
        create.expected_epoch = epoch;
        create.expected_revision = revision;
        if (*op == "object.create_rectangle") create.kind = CreateObjectKind::Rectangle;
        else if (*op == "object.create_ellipse") create.kind = CreateObjectKind::Ellipse;
        else if (*op == "object.create_text") create.kind = CreateObjectKind::Text;
        else if (*op == "object.create_line") create.kind = CreateObjectKind::Line;
        else create.kind = CreateObjectKind::Polyline;
        if (field(value, "name") && !string_field(value, "name")) {
            diagnostic = prefix + "name must be a string.";
            return false;
        }
        if (const auto* name = string_field(value, "name")) {
            if (name->size() > 256) { diagnostic = prefix + "name exceeds 256 UTF-8 bytes."; return false; }
            create.name = *name;
        }
        double layer_value = 0.0;
        if (!read_optional_number("layer_index", layer_value)) return false;
        if (field(value, "layer_index")) {
            if (layer_value < 0.0 || layer_value > 4095.0 || std::floor(layer_value) != layer_value) {
                diagnostic = prefix + "layer_index must be an integer in [0, 4095]."; return false;
            }
            create.layer_index = static_cast<size_t>(layer_value);
        }
        if (!parse_color(field(value, "fill"), create.fill) ||
            !parse_color(field(value, "stroke"), create.stroke)) {
            diagnostic = prefix + "fill and stroke currently accept validated RGB source colors.";
            return false;
        }
        if (!read_optional_number("stroke_width_mm", create.stroke_width_mm)) return false;
        if (create.kind == CreateObjectKind::Text) {
            if (field(value, "width_mm") || field(value, "height_mm") ||
                field(value, "stroke") || field(value, "stroke_width_mm")) {
                diagnostic = prefix + "text objects do not accept shape dimensions or stroke fields.";
                return false;
            }
            const auto* text = string_field(value, "text");
            const auto* family = string_field(value, "font_family");
            const auto* x = number_field(value, "x_mm");
            const auto* y = number_field(value, "y_mm");
            if (!text || !family || !x || !y) {
                diagnostic = prefix + "text requires text, font_family and x_mm/y_mm."; return false;
            }
            create.text = *text;
            create.position_mm = {*x, *y};
            create.font.family_name = *family;
            create.font.postscript_name.clear();
            if (field(value, "font_postscript_name") && !string_field(value, "font_postscript_name")) {
                diagnostic = prefix + "font_postscript_name must be a string."; return false;
            }
            if (const auto* postscript = string_field(value, "font_postscript_name"))
                create.font.postscript_name = *postscript;
            double weight_value = static_cast<double>(create.font.weight);
            if (!read_optional_number("font_weight", weight_value)) return false;
            if (field(value, "font_weight")) {
                if (weight_value < 100.0 || weight_value > 900.0 || std::floor(weight_value) != weight_value) {
                    diagnostic = prefix + "font_weight must be an integer in [100, 900]."; return false;
                }
                create.font.weight = static_cast<uint16_t>(weight_value);
            }
            if (const auto* italic = field(value, "font_italic")) {
                if (!italic->boolean()) { diagnostic = prefix + "font_italic must be a boolean."; return false; }
                create.font.italic = *italic->boolean();
            }
            if (!read_optional_number("font_size_pt", create.font.size_pt)) return false;
            if (field(value, "alignment") && !string_field(value, "alignment")) {
                diagnostic = prefix + "alignment must be a string."; return false;
            }
            if (const auto* alignment = string_field(value, "alignment")) {
                if (*alignment == "left") create.text_alignment = typography::TextAlignment::Left;
                else if (*alignment == "center") create.text_alignment = typography::TextAlignment::Center;
                else if (*alignment == "right") create.text_alignment = typography::TextAlignment::Right;
                else { diagnostic = prefix + "alignment must be left, center or right."; return false; }
            }
            if (const auto* underline = field(value, "underline")) {
                if (!underline->boolean()) { diagnostic = prefix + "underline must be a boolean."; return false; }
                create.underline = *underline->boolean();
            }
        } else if (create.kind == CreateObjectKind::Line || create.kind == CreateObjectKind::Polyline) {
            if (field(value, "x_mm") || field(value, "y_mm") || field(value, "width_mm") ||
                field(value, "height_mm") || field(value, "text") || field(value, "font_family") ||
                field(value, "font_postscript_name") || field(value, "font_weight") ||
                field(value, "font_italic") || field(value, "font_size_pt") ||
                field(value, "alignment") || field(value, "underline")) {
                diagnostic = prefix + "path objects use points_mm and do not accept box or text fields.";
                return false;
            }
            const auto* points = field(value, "points_mm");
            if (!points || !points->array() || points->array()->size() < 2 || points->array()->size() > 4096) {
                diagnostic = prefix + "points_mm must contain 2 to 4096 point objects.";
                return false;
            }
            create.path_points_mm.reserve(points->array()->size());
            for (const auto& point_value : *points->array()) {
                if (!point_value.object() || !only_fields(point_value, {"x_mm", "y_mm"})) {
                    diagnostic = prefix + "each point must contain only x_mm and y_mm.";
                    return false;
                }
                geometry::Point2D point;
                const auto* x = number_field(point_value, "x_mm");
                const auto* y = number_field(point_value, "y_mm");
                if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y)) {
                    diagnostic = prefix + "each point requires finite x_mm and y_mm.";
                    return false;
                }
                point = {*x, *y};
                create.path_points_mm.push_back(point);
            }
            if (create.kind == CreateObjectKind::Line && field(value, "closed")) {
                diagnostic = prefix + "a line is always open; use a polyline to create a closed path.";
                return false;
            }
            if (const auto* closed = field(value, "closed")) {
                if (!closed->boolean()) { diagnostic = prefix + "closed must be a boolean."; return false; }
                create.path_closed = *closed->boolean();
            }
        } else {
            if (field(value, "text") || field(value, "font_family") ||
                field(value, "font_postscript_name") || field(value, "font_weight") ||
                field(value, "font_italic") || field(value, "font_size_pt") ||
                field(value, "alignment") || field(value, "underline")) {
                diagnostic = prefix + "shape objects do not accept text or font fields.";
                return false;
            }
            const auto* x = number_field(value, "x_mm");
            const auto* y = number_field(value, "y_mm");
            const auto* width = number_field(value, "width_mm");
            const auto* height = number_field(value, "height_mm");
            if (!x || !y || !width || !height) {
                diagnostic = prefix + "shape requires finite x/y/width/height in mm."; return false;
            }
            create.bounds_mm = {*x, *y, *width, *height};
        }
        std::string client_ref;
        if (const auto* ref = string_field(value, "client_ref")) {
            if (ref->empty() || ref->size() > 64 || !seen_refs.insert(*ref).second) {
                diagnostic = prefix + "client_ref must be unique and 1 to 64 characters."; return false;
            }
            client_ref = *ref;
        }
        parsed_operations.emplace_back(std::move(create));
        parsed_client_refs.push_back(std::move(client_ref));
    }
    parsed_result = std::move(parsed_operations);
    client_refs = std::move(parsed_client_refs);
    return true;
}

bool read_revision_pair(const JsonValue& request, uint64_t& epoch, uint64_t& revision) {
    const auto* epoch_text = string_field(request, "expected_epoch");
    const auto* revision_text = string_field(request, "expected_revision");
    if (!epoch_text || !revision_text) return false;
    const auto parsed_epoch = std::from_chars(epoch_text->data(), epoch_text->data() + epoch_text->size(), epoch);
    const auto parsed_revision = std::from_chars(revision_text->data(), revision_text->data() + revision_text->size(), revision);
    return parsed_epoch.ec == std::errc{} && parsed_epoch.ptr == epoch_text->data() + epoch_text->size() &&
           parsed_revision.ec == std::errc{} && parsed_revision.ptr == revision_text->data() + revision_text->size();
}

bool parse_font(const JsonValue& request, typography::FontIdentity& font,
                std::string& diagnostic) {
    const auto* family = string_field(request, "font_family");
    if (!family || family->empty() || family->size() > 256) {
        diagnostic = "font_family is required and limited to 256 UTF-8 bytes.";
        return false;
    }
    font.family_name = *family;
    font.postscript_name.clear();
    if (const auto* postscript = string_field(request, "font_postscript_name")) {
        if (postscript->size() > 256) { diagnostic = "font_postscript_name is too long."; return false; }
        font.postscript_name = *postscript;
    }
    if (const auto* size = number_field(request, "font_size_pt")) font.size_pt = *size;
    if (const auto* weight = number_field(request, "font_weight")) {
        if (*weight < 100.0 || *weight > 900.0 || std::floor(*weight) != *weight) {
            diagnostic = "font_weight must be an integer in [100, 900]."; return false;
        }
        font.weight = static_cast<uint16_t>(*weight);
    }
    if (const auto* italic = field(request, "font_italic")) {
        if (!italic->boolean()) { diagnostic = "font_italic must be a boolean."; return false; }
        font.italic = *italic->boolean();
    }
    if (!std::isfinite(font.size_pt) || font.size_pt <= 0.0 || font.size_pt > 10000.0) {
        diagnostic = "font_size_pt must be finite and in (0, 10000]."; return false;
    }
    return true;
}

} // namespace

AutomationRpc::AutomationRpc(AutomationService& service, std::set<std::string> granted_scopes)
    : service_(service), granted_scopes_(std::move(granted_scopes)) {}

bool AutomationRpc::owns_session(std::string_view session_handle) const noexcept {
    return sessions_.find(std::string(session_handle)) != sessions_.end();
}

bool AutomationRpc::has_scope(std::string_view session_handle, std::string_view scope) const {
    const auto it = sessions_.find(std::string(session_handle));
    if (it == sessions_.end()) return false;
    const auto state = service_.inspect();
    return it->second.document_id == state.document_id && it->second.epoch == state.epoch &&
           it->second.scopes.contains(std::string(scope));
}

JsonValue AutomationRpc::error(std::string code, std::string message) const {
    return JsonValue::Object{{"status", "error"}, {"code", std::move(code)}, {"message", std::move(message)}};
}

bool AutomationRpc::authorize(const JsonValue& request, std::string_view required_scope,
                              JsonValue& failure) const {
    const auto* session = string_field(request, "session_handle");
    if (!session) { failure = error("SESSION_REQUIRED", "Connect to this PikDraw instance first."); return false; }
    const auto it = sessions_.find(*session);
    if (it == sessions_.end()) { failure = error("SESSION_EXPIRED", "The session handle is unknown or expired."); return false; }
    const auto state = service_.inspect();
    if (it->second.document_id != state.document_id || it->second.epoch != state.epoch) {
        failure = error("SESSION_EXPIRED", "The document identity or epoch changed after this session was created.");
        return false;
    }
    if (!it->second.scopes.contains(std::string(required_scope))) {
        failure = error("SCOPE_DENIED", "This session does not have the required permission."); return false;
    }
    return true;
}

JsonValue AutomationRpc::handle_connect(const JsonValue& request) {
    if (!only_fields(request, {"operation", "scopes"}))
        return error("INVALID_ARGUMENT", "session.create contains an unknown field.");
    std::set<std::string> requested{"document.read"};
    if (const auto* scopes = field(request, "scopes")) {
        if (!scopes->array()) return error("INVALID_ARGUMENT", "scopes must be an array of strings.");
        requested.clear();
        for (const auto& value : *scopes->array()) {
            if (!value.string()) return error("INVALID_ARGUMENT", "Every scope must be a string.");
            requested.insert(*value.string());
        }
    }
    for (const auto& scope : requested) {
        if (scope != "document.read" && scope != "document.create" &&
            scope != "document.edit" && scope != "file.read" && scope != "file.write")
            return error("INVALID_ARGUMENT", "Unknown scope: " + scope);
        if (!granted_scopes_.contains(scope))
            return error("SCOPE_DENIED", "The PikDraw instance did not grant scope: " + scope);
    }
    if (!requested.contains("document.read") &&
        (requested.contains("document.create") || requested.contains("document.edit") ||
         requested.contains("file.read") || requested.contains("file.write")))
        return error("INVALID_ARGUMENT", "Write and file scopes require document.read.");
    if (requested.contains("file.write") && !requested.contains("file.read"))
        return error("INVALID_ARGUMENT", "file.write requires file.read.");
    if (sessions_.size() >= 8) return error("RESOURCE_LIMIT", "Too many automation sessions are active.");
    const auto state = service_.inspect();
    const auto handle = random_handle();
    sessions_.emplace(handle, Session{requested, state.document_id, state.epoch, {}});
    return JsonValue::Object{{"status", "ok"}, {"session_handle", handle},
        {"granted_scopes", [&] { JsonValue::Array a; for (const auto& s : requested) a.emplace_back(s); return JsonValue(std::move(a)); }()},
        {"document_id", state.document_id}, {"epoch", id_string(state.epoch)}};
}

JsonValue AutomationRpc::handle(const JsonValue& request) {
    const auto* operation = string_field(request, "operation");
    if (!operation) return error("INVALID_ARGUMENT", "operation is required.");
    if (*operation == "session.create") return handle_connect(request);
    if (*operation == "session.disconnect") {
        if (!only_fields(request, {"operation", "session_handle"}))
            return error("INVALID_ARGUMENT", "session.disconnect contains an unknown field.");
        const auto* session = string_field(request, "session_handle");
        if (!session || sessions_.erase(*session) == 0) return error("SESSION_EXPIRED", "The session is unknown.");
        return JsonValue::Object{{"status", "ok"}, {"disconnected", true}};
    }
    if (*operation == "capabilities") {
        if (!only_fields(request, {"operation", "session_handle"}))
            return error("INVALID_ARGUMENT", "capabilities contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        JsonValue::Array capabilities;
        for (const auto& capability : service_.capabilities()) {
            bool allowed = true;
        for (const auto& scope : capability.required_scopes) {
                JsonValue ignored;
                if (!authorize(request, scope, ignored)) { allowed = false; break; }
            }
            if (!allowed) continue;
            capabilities.emplace_back(JsonValue::Object{{"id", capability.id},
                {"input_schema", capability.input_schema}, {"writes_document", capability.writes_document}});
        }
        capabilities.emplace_back(JsonValue::Object{{"id", "request.get"},
            {"input_schema", "{idempotency_key}"}, {"writes_document", false}});
        if (authorize(request, "document.create", failure))
            capabilities.emplace_back(JsonValue::Object{{"id", "document.create"},
                {"input_schema", "{page_width_mm,page_height_mm,title}"}, {"writes_document", true}});
        failure = {};
        if (authorize(request, "file.read", failure))
            capabilities.emplace_back(JsonValue::Object{{"id", "file.verify_reopen"},
                {"input_schema", "{file_name}"}, {"writes_document", false}});
        failure = {};
        if (authorize(request, "file.write", failure))
            capabilities.emplace_back(JsonValue::Object{{"id", "file.save"},
                {"input_schema", "{file_name}"}, {"writes_document", false}});
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"capabilities", std::move(capabilities)}};
    }
    if (*operation == "document.inspect") {
        if (!only_fields(request, {"operation", "session_handle"}))
            return error("INVALID_ARGUMENT", "document.inspect contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto state = service_.inspect();
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", state.document_id}, {"epoch", id_string(state.epoch)},
            {"revision", id_string(state.revision)}, {"page_width_mm", state.page_width_mm},
            {"page_height_mm", state.page_height_mm}, {"object_count", static_cast<double>(state.object_count)},
            {"layer_count", static_cast<double>(state.layer_count)}, {"dirty", state.dirty}};
    }
    if (*operation == "font.query") {
        if (!only_fields(request, {"operation", "session_handle", "font_family", "font_postscript_name",
                                  "font_weight", "font_italic", "font_size_pt"}))
            return error("INVALID_ARGUMENT", "font.query contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        typography::FontIdentity requested;
        std::string diagnostic;
        if (!parse_font(request, requested, diagnostic)) return error("INVALID_ARGUMENT", diagnostic);
        try {
            const auto resolved = service_.query_font(requested);
            return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
                {"requested_family", resolved.requested.family_name},
                {"requested_postscript_name", resolved.requested.postscript_name},
                {"actual_family", resolved.actual.family_name},
                {"actual_postscript_name", resolved.actual.postscript_name},
                {"weight", static_cast<double>(resolved.actual.weight)},
                {"italic", resolved.actual.italic}, {"size_pt", resolved.actual.size_pt},
                {"fallback", resolved.is_fallback}, {"warning", resolved.warning_message}};
        } catch (const std::exception& exception) {
            return error("UNSUPPORTED_CAPABILITY", std::string("Font resolution failed: ") + exception.what());
        }
    }
    if (*operation == "text.measure") {
        if (!only_fields(request, {"operation", "session_handle", "text", "font_family",
            "font_postscript_name", "font_weight", "font_italic", "font_size_pt",
            "max_width_mm", "line_spacing_ratio", "alignment"}))
            return error("INVALID_ARGUMENT", "text.measure contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto* text = string_field(request, "text");
        typography::FontIdentity font;
        std::string diagnostic;
        if (!text || !parse_font(request, font, diagnostic))
            return error("INVALID_ARGUMENT", text ? diagnostic : "text is required.");
        typography::LayoutOptions options;
        if (const auto* width = number_field(request, "max_width_mm")) options.max_width_mm = *width;
        if (const auto* spacing = number_field(request, "line_spacing_ratio")) options.line_spacing_ratio = *spacing;
        if (const auto* alignment = string_field(request, "alignment")) {
            if (*alignment == "left") options.alignment = typography::TextAlignment::Left;
            else if (*alignment == "center") options.alignment = typography::TextAlignment::Center;
            else if (*alignment == "right") options.alignment = typography::TextAlignment::Right;
            else return error("INVALID_ARGUMENT", "alignment must be left, center or right.");
        }
        try {
            const auto measured = service_.measure_text(*text, font, options);
            return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
                {"requested_family", measured.requested_font.family_name},
                {"requested_postscript_name", measured.requested_font.postscript_name},
                {"actual_family", measured.actual_font.family_name},
                {"actual_postscript_name", measured.actual_font.postscript_name},
                {"font_size_pt", font.size_pt}, {"fallback", measured.is_fallback},
                {"warning", measured.warning}, {"line_count", static_cast<double>(measured.line_count)},
                {"glyph_count", static_cast<double>(measured.glyph_count)},
                {"missing_glyph_count", static_cast<double>(measured.missing_glyph_count)},
                {"bounds_mm", rect_json(measured.bounds_mm)}};
        } catch (const std::exception& exception) {
            return error("UNSUPPORTED_CAPABILITY", std::string("Text layout failed: ") + exception.what());
        }
    }
    if (*operation == "document.validate") {
        if (!only_fields(request, {"operation", "session_handle", "safe_margin_mm", "safe_margin_exempt_ids"}))
            return error("INVALID_ARGUMENT", "document.validate contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        double margin = 0.0;
        if (const auto* value = number_field(request, "safe_margin_mm")) margin = *value;
        std::set<uint64_t> exempt_ids;
        if (const auto* values = field(request, "safe_margin_exempt_ids")) {
            if (!values->array() || values->array()->size() > 1024)
                return error("INVALID_ARGUMENT", "safe_margin_exempt_ids must be an array with at most 1024 IDs.");
            for (const auto& value : *values->array()) {
                const auto* text_id = value.string();
                uint64_t id = 0;
                if (!text_id) return error("INVALID_ARGUMENT", "Every exempt object ID must be a decimal string.");
                const auto parsed = std::from_chars(text_id->data(), text_id->data() + text_id->size(), id);
                if (parsed.ec != std::errc{} || parsed.ptr != text_id->data() + text_id->size())
                    return error("INVALID_ARGUMENT", "Exempt object IDs must be unsigned decimal strings.");
                exempt_ids.insert(id);
            }
        }
        const auto state = service_.inspect();
        const auto validation = service_.validate_document(margin, exempt_ids);
        JsonValue::Array diagnostics;
        for (const auto& item : validation.diagnostics)
            diagnostics.emplace_back(JsonValue::Object{{"code", item.code}, {"severity", item.severity},
                {"object_id", id_string(item.object_id)}, {"message", item.message}});
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", state.document_id}, {"epoch", id_string(state.epoch)},
            {"revision", id_string(state.revision)}, {"valid", validation.valid},
            {"diagnostics", std::move(diagnostics)}};
    }
    if (*operation == "objects.query") {
        if (!only_fields(request, {"operation", "session_handle", "offset", "limit",
                                   "expected_epoch", "expected_revision"}))
            return error("INVALID_ARGUMENT", "objects.query contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto state = service_.inspect();
        const auto read_unsigned = [&](std::string_view name, uint64_t fallback, uint64_t& out) {
            const auto* text = string_field(request, name);
            if (!text) { out = fallback; return true; }
            const auto parsed = std::from_chars(text->data(), text->data() + text->size(), out);
            return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size();
        };
        uint64_t offset = 0;
        uint64_t expected_epoch = state.epoch;
        uint64_t expected_revision = state.revision;
        const bool has_epoch = string_field(request, "expected_epoch") != nullptr;
        const bool has_revision = string_field(request, "expected_revision") != nullptr;
        if (!read_unsigned("offset", 0, offset) ||
            !read_unsigned("expected_epoch", state.epoch, expected_epoch) ||
            !read_unsigned("expected_revision", state.revision, expected_revision))
            return error("INVALID_ARGUMENT", "offset, expected_epoch and expected_revision must be unsigned decimal strings.");
        if (offset > 0 && (!has_epoch || !has_revision))
            return error("INVALID_ARGUMENT", "Paged queries after the first page require expected_epoch and expected_revision.");
        if (expected_epoch != state.epoch || expected_revision != state.revision)
            return error("REVISION_CONFLICT", "The document changed between query pages; inspect and restart paging.");
        uint64_t limit = 100;
        if (const auto* value = number_field(request, "limit")) {
            if (*value < 1.0 || *value > 100.0 || std::floor(*value) != *value)
                return error("INVALID_ARGUMENT", "limit must be an integer in [1, 100].");
            limit = static_cast<uint64_t>(*value);
        }
        const auto snapshot = service_.snapshot();
        if (!snapshot) return error("DOCUMENT_CLOSED", "No committed document snapshot is available.");
        JsonValue::Array objects;
        const auto summaries = service_.query_objects();
        if (offset > summaries.size()) return error("INVALID_ARGUMENT", "offset is outside the current object list.");
        const auto end = std::min<uint64_t>(static_cast<uint64_t>(summaries.size()), offset + limit);
        for (uint64_t index = offset; index < end; ++index) {
            const auto& summary = summaries[static_cast<size_t>(index)];
            const auto object = snapshot->get_object(summary.id);
            if (object) objects.emplace_back(object_json(*object, summary.layer_index));
        }
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", snapshot->document_id()}, {"epoch", id_string(snapshot->epoch())},
            {"revision", id_string(snapshot->revision())}, {"offset", id_string(offset)},
            {"limit", static_cast<double>(limit)}, {"next_offset", end < summaries.size() ? id_string(end) : ""},
            {"objects", std::move(objects)}};
    }
    if (*operation == "objects.get") {
        if (!only_fields(request, {"operation", "session_handle", "object_id",
                                   "expected_epoch", "expected_revision"}))
            return error("INVALID_ARGUMENT", "objects.get contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        uint64_t epoch = 0, revision = 0, object_id = 0;
        std::string diagnostic;
        if (!read_revision_pair(request, epoch, revision) ||
            !parse_object_id(request, "object_id", object_id, diagnostic))
            return error("INVALID_ARGUMENT", diagnostic.empty()
                ? "expected_epoch and expected_revision must be unsigned decimal strings."
                : std::move(diagnostic));
        const auto state = service_.inspect();
        if (state.epoch != epoch) return error("EPOCH_MISMATCH", "The document lifecycle changed.");
        if (state.revision != revision) return error("REVISION_CONFLICT", "The document revision changed.");
        const auto snapshot = service_.snapshot();
        if (!snapshot) return error("DOCUMENT_CLOSED", "No committed document snapshot is available.");
        const auto object = snapshot->get_object(object_id);
        const auto location = find_object_location(*snapshot, object_id);
        if (!object || !location) return error("OBJECT_NOT_FOUND", "No committed object matches object_id.");
        auto result = *object_json(*object, location->layer_index).object();
        result["status"] = "ok";
        result["api_version"] = std::string(kApiVersion);
        result["document_id"] = snapshot->document_id();
        result["epoch"] = id_string(snapshot->epoch());
        result["revision"] = id_string(snapshot->revision());
        result["top_level"] = location->top_level;
        if (location->parent_group_id)
            result["parent_group_id"] = id_string(*location->parent_group_id);
        return result;
    }
    if (*operation == "request.get") {
        if (!only_fields(request, {"operation", "session_handle", "idempotency_key"}))
            return error("INVALID_ARGUMENT", "request.get contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        const auto* session_handle = string_field(request, "session_handle");
        const auto* key = string_field(request, "idempotency_key");
        if (!session_handle || !key || key->empty())
            return error("INVALID_ARGUMENT", "session_handle and idempotency_key are required.");
        const auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        const auto receipt = session->second.receipts.find(*key);
        if (receipt == session->second.receipts.end())
            return error("REQUEST_NOT_FOUND", "No committed receipt matches this idempotency key in the current session.");
        return receipt->second.response;
    }
    if (*operation == "history.undo" || *operation == "history.redo") {
        const auto& action = *operation == "history.undo" ? std::string("undo") : std::string("redo");
        if (!only_fields(request, {"operation", "session_handle", "request_id", "idempotency_key",
                                   "expected_epoch", "expected_revision"}))
            return error("INVALID_ARGUMENT", "history." + action + " contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.edit", failure)) return failure;
        const auto* session_handle = string_field(request, "session_handle");
        const auto* request_id = string_field(request, "request_id");
        const auto* idempotency_key = string_field(request, "idempotency_key");
        if (!session_handle || !request_id || request_id->empty() || request_id->size() > 128 ||
            !idempotency_key || idempotency_key->empty() || idempotency_key->size() > 128)
            return error("INVALID_ARGUMENT", "request_id and idempotency_key (1 to 128 characters) are required for every write.");
        auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        JsonValue::Object payload = *request.object();
        payload.erase("session_handle"); payload.erase("request_id"); payload.erase("idempotency_key");
        const auto canonical_payload = JsonValue(std::move(payload)).serialize();
        const auto prior = session->second.receipts.find(*idempotency_key);
        if (prior != session->second.receipts.end()) {
            if (prior->second.canonical_payload != canonical_payload)
                return error("IDEMPOTENCY_CONFLICT", "This idempotency_key was already used with a different write payload.");
            return prior->second.response;
        }
        if (session->second.receipts.size() >= 128)
            return error("RESOURCE_LIMIT", "This session has reached its 128 committed write receipt limit.");
        uint64_t epoch = 0, revision = 0;
        if (!read_revision_pair(request, epoch, revision))
            return error("INVALID_ARGUMENT", "expected_epoch and expected_revision must be unsigned decimal strings.");
        const auto result = action == "undo" ? service_.undo(epoch, revision) : service_.redo(epoch, revision);
        if (!result.ok()) {
            const char* code = result.status == Status::RevisionConflict ? "REVISION_CONFLICT" :
                result.status == Status::EpochMismatch ? "EPOCH_MISMATCH" :
                result.status == Status::BusyInteraction ? "BUSY_INTERACTION" :
                result.status == Status::InternalError ? "INTERNAL_ERROR" : "INVALID_ARGUMENT";
            return error(code, result.diagnostic);
        }
        JsonValue receipt = JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"request_id", *request_id}, {"idempotency_key", *idempotency_key},
            {"document_id", service_.inspect().document_id}, {"epoch", id_string(epoch)},
            {"action", action}, {"revision_before", id_string(result.revision_before)},
            {"revision_after", id_string(result.revision_after)}, {"dirty", service_.inspect().dirty}};
        session->second.receipts.emplace(*idempotency_key, Receipt{canonical_payload, receipt});
        return receipt;
    }
    if (*operation == "batch.validate") {
        JsonValue failure;
        if (!authorize(request, "document.read", failure)) return failure;
        for (const auto& [key, ignored] : *request.object()) {
            (void)ignored;
            if (key != "operation" && key != "session_handle" && key != "expected_epoch" &&
                key != "expected_revision" && key != "operations")
                return error("INVALID_ARGUMENT", "Unknown batch.validate field: " + key);
        }
        uint64_t epoch = 0, revision = 0;
        if (!read_revision_pair(request, epoch, revision))
            return error("INVALID_ARGUMENT", "expected_epoch and expected_revision must be unsigned decimal strings.");
        const auto state = service_.inspect();
        if (state.epoch != epoch) return error("EPOCH_MISMATCH", "The document lifecycle changed.");
        if (state.revision != revision) return error("REVISION_CONFLICT", "The document revision changed.");
        std::vector<BatchOperation> operations;
        std::vector<std::string> client_refs;
        std::string diagnostic;
        if (!parse_object_operations(field(request, "operations"), epoch, revision,
                                     operations, client_refs, diagnostic))
            return error("INVALID_ARGUMENT", std::move(diagnostic));
        const auto validation = service_.validate_batch(operations);
        JsonValue::Array diagnostics;
        for (const auto& item : validation.diagnostics) diagnostics.emplace_back(item);
        return JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"document_id", state.document_id}, {"epoch", id_string(state.epoch)},
            {"revision", id_string(state.revision)}, {"valid", validation.valid},
            {"operation_count", static_cast<double>(operations.size())}, {"diagnostics", std::move(diagnostics)}};
    }
    if (*operation == "batch.apply") {
        JsonValue failure;
        if (!authorize(request, "document.edit", failure)) return failure;
        for (const auto& [key, ignored] : *request.object()) {
            (void)ignored;
            if (key != "operation" && key != "session_handle" && key != "request_id" &&
                key != "idempotency_key" && key != "expected_epoch" &&
                key != "expected_revision" && key != "operations")
                return error("INVALID_ARGUMENT", "Unknown batch.apply field: " + key);
        }
        const auto* session_handle = string_field(request, "session_handle");
        const auto* request_id = string_field(request, "request_id");
        const auto* idempotency_key = string_field(request, "idempotency_key");
        if (!session_handle || !request_id || request_id->empty() || request_id->size() > 128 ||
            !idempotency_key || idempotency_key->empty() || idempotency_key->size() > 128)
            return error("INVALID_ARGUMENT", "request_id and idempotency_key (1 to 128 characters) are required for every write.");
        auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        JsonValue::Object payload = request.object() ? *request.object() : JsonValue::Object{};
        payload.erase("session_handle"); payload.erase("request_id"); payload.erase("idempotency_key");
        const auto canonical_payload = JsonValue(std::move(payload)).serialize();
        if (canonical_payload.size() > 64 * 1024)
            return error("RESOURCE_LIMIT", "A batch payload is limited to 64 KiB.");
        const auto prior = session->second.receipts.find(*idempotency_key);
        if (prior != session->second.receipts.end()) {
            if (prior->second.canonical_payload != canonical_payload)
                return error("IDEMPOTENCY_CONFLICT", "This idempotency_key was already used with a different write payload.");
            return prior->second.response;
        }
        if (session->second.receipts.size() >= 128)
            return error("RESOURCE_LIMIT", "This session has reached its 128 committed write receipt limit.");
        uint64_t epoch = 0, revision = 0;
        if (!read_revision_pair(request, epoch, revision))
            return error("INVALID_ARGUMENT", "expected_epoch and expected_revision must be unsigned decimal strings.");
        std::vector<BatchOperation> operations;
        std::vector<std::string> client_refs;
        std::string diagnostic;
        if (!parse_object_operations(field(request, "operations"), epoch, revision,
                                     operations, client_refs, diagnostic))
            return error("INVALID_ARGUMENT", std::move(diagnostic));
        const auto result = service_.apply_batch(epoch, revision, operations);
        if (!result.ok()) {
            const char* code = result.status == Status::RevisionConflict ? "REVISION_CONFLICT" :
                result.status == Status::EpochMismatch ? "EPOCH_MISMATCH" :
                result.status == Status::TargetNotFound ? "TARGET_NOT_FOUND" :
                result.status == Status::UnsupportedCapability ? "UNSUPPORTED_CAPABILITY" :
                result.status == Status::BusyInteraction ? "BUSY_INTERACTION" :
                result.status == Status::InternalError ? "INTERNAL_ERROR" : "INVALID_ARGUMENT";
            return error(code, result.diagnostic);
        }
        JsonValue::Array ids;
        JsonValue::Array affected_ids;
        JsonValue::Object client_ref_map;
        for (size_t index = 0; index < result.created_ids.size(); ++index) {
            const auto id = id_string(result.created_ids[index]);
            ids.emplace_back(id);
            if (index < result.created_operation_indices.size()) {
                const auto operation_index = result.created_operation_indices[index];
                if (operation_index < client_refs.size() && !client_refs[operation_index].empty())
                    client_ref_map[client_refs[operation_index]] = id;
            }
        }
        for (const auto id : result.affected_ids) affected_ids.emplace_back(id_string(id));
        JsonValue receipt = JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"request_id", *request_id}, {"idempotency_key", *idempotency_key},
            {"document_id", service_.inspect().document_id}, {"epoch", id_string(epoch)},
            {"revision_before", id_string(result.revision_before)},
            {"revision_after", id_string(result.revision_after)}, {"created_ids", std::move(ids)},
            {"affected_ids", std::move(affected_ids)},
            {"client_ref_to_id", std::move(client_ref_map)}};
        session->second.receipts.emplace(*idempotency_key, Receipt{canonical_payload, receipt});
        return receipt;
    }
    if (*operation == "object.create_rectangle") {
        if (!only_fields(request, {"operation", "session_handle", "request_id", "idempotency_key",
            "expected_epoch", "expected_revision", "x_mm", "y_mm", "width_mm", "height_mm",
            "layer_index", "fill", "stroke", "stroke_width_mm"}))
            return error("INVALID_ARGUMENT", "object.create_rectangle contains an unknown field.");
        JsonValue failure;
        if (!authorize(request, "document.edit", failure)) return failure;
        const auto* session_handle = string_field(request, "session_handle");
        const auto* request_id = string_field(request, "request_id");
        const auto* idempotency_key = string_field(request, "idempotency_key");
        if (!session_handle || !request_id || request_id->empty() || request_id->size() > 128 ||
            !idempotency_key || idempotency_key->empty() || idempotency_key->size() > 128)
            return error("INVALID_ARGUMENT", "request_id and idempotency_key (1 to 128 characters) are required for every write.");
        auto session = sessions_.find(*session_handle);
        if (session == sessions_.end()) return error("SESSION_EXPIRED", "The session handle is unknown or expired.");
        JsonValue::Object payload = request.object() ? *request.object() : JsonValue::Object{};
        payload.erase("session_handle"); payload.erase("request_id"); payload.erase("idempotency_key");
        const auto canonical_payload = JsonValue(std::move(payload)).serialize();
        if (canonical_payload.size() > 16 * 1024)
            return error("RESOURCE_LIMIT", "A write payload is limited to 16 KiB.");
        const auto prior = session->second.receipts.find(*idempotency_key);
        if (prior != session->second.receipts.end()) {
            if (prior->second.canonical_payload != canonical_payload)
                return error("IDEMPOTENCY_CONFLICT", "This idempotency_key was already used with a different write payload.");
            return prior->second.response;
        }
        if (session->second.receipts.size() >= 128)
            return error("RESOURCE_LIMIT", "This session has reached its 128 committed write receipt limit; disconnect and create a new session.");
        const auto* epoch_text = string_field(request, "expected_epoch");
        const auto* revision_text = string_field(request, "expected_revision");
        const auto* x = number_field(request, "x_mm");
        const auto* y = number_field(request, "y_mm");
        const auto* width = number_field(request, "width_mm");
        const auto* height = number_field(request, "height_mm");
        if (!epoch_text || !revision_text || !x || !y || !width || !height)
            return error("INVALID_ARGUMENT", "expected_epoch, expected_revision and finite mm dimensions are required.");
        uint64_t epoch = 0, revision = 0;
        const auto ep = std::from_chars(epoch_text->data(), epoch_text->data() + epoch_text->size(), epoch);
        const auto rev = std::from_chars(revision_text->data(), revision_text->data() + revision_text->size(), revision);
        if (ep.ec != std::errc{} || ep.ptr != epoch_text->data() + epoch_text->size() ||
            rev.ec != std::errc{} || rev.ptr != revision_text->data() + revision_text->size())
            return error("INVALID_ARGUMENT", "Epoch and revision must be unsigned decimal strings.");
        CreateRectangle create;
        create.expected_epoch = epoch; create.expected_revision = revision;
        create.bounds_mm = {*x, *y, *width, *height};
        if (const auto* layer = number_field(request, "layer_index")) {
            if (*layer < 0.0 || *layer > 4095.0 || std::floor(*layer) != *layer)
                return error("INVALID_ARGUMENT", "layer_index must be an integer in [0, 4095].");
            create.layer_index = static_cast<size_t>(*layer);
        }
        if (!parse_color(field(request, "fill"), create.fill) ||
            !parse_color(field(request, "stroke"), create.stroke))
            return error("INVALID_ARGUMENT", "The current create_rectangle capability accepts valid RGB source colors.");
        if (const auto* stroke_width = number_field(request, "stroke_width_mm")) create.stroke_width_mm = *stroke_width;
        const auto result = service_.create_rectangle(create);
        if (!result.ok()) {
            const char* code = result.status == Status::RevisionConflict ? "REVISION_CONFLICT" :
                result.status == Status::EpochMismatch ? "EPOCH_MISMATCH" :
                result.status == Status::TargetNotFound ? "TARGET_NOT_FOUND" : "INVALID_ARGUMENT";
            return error(code, result.diagnostic);
        }
        JsonValue::Array ids; for (const auto id : result.created_ids) ids.emplace_back(id_string(id));
        JsonValue receipt = JsonValue::Object{{"status", "ok"}, {"api_version", std::string(kApiVersion)},
            {"request_id", *request_id}, {"idempotency_key", *idempotency_key},
            {"revision_before", id_string(result.revision_before)}, {"revision_after", id_string(result.revision_after)},
            {"created_ids", std::move(ids)}};
        session->second.receipts.emplace(*idempotency_key, Receipt{canonical_payload, receipt});
        return receipt;
    }
    return error("UNSUPPORTED_CAPABILITY", "No handler is registered for this operation.");
}

} // namespace pikdraw::automation
