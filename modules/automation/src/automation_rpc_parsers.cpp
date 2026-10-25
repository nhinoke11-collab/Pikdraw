#include "automation_rpc_internal.hpp"

#include "pikdraw/typography/layout/text_layout.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace pikdraw::automation {

bool only_fields(const JsonValue& value, std::initializer_list<std::string_view> allowed) {
    const auto* object = value.object();
    if (!object) return false;
    for (const auto& [key, ignored] : *object) {
        (void)ignored;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return false;
    }
    return true;
}

std::optional<geometry::units::LengthUnit> parse_physical_length_unit(
    std::string_view suffix) noexcept {
    const auto unit = geometry::units::parse_length_unit_suffix(suffix);
    if (!unit || !geometry::units::is_physical_length_unit(*unit)) return std::nullopt;
    return unit;
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
                    "rotation_degrees", "pivot_x_mm", "pivot_y_mm", "translate_x", "translate_y",
                    "pivot_x", "pivot_y", "length_unit"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 256, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                const bool typed_lengths = field(value, "translate_x") || field(value, "translate_y") ||
                    field(value, "pivot_x") || field(value, "pivot_y");
                const bool mm_lengths = field(value, "translate_x_mm") || field(value, "translate_y_mm") ||
                    field(value, "pivot_x_mm") || field(value, "pivot_y_mm");
                const auto* unit_text = string_field(value, "length_unit");
                const bool has_unit = field(value, "length_unit") != nullptr;
                if (typed_lengths != has_unit || (typed_lengths && mm_lengths)) {
                    diagnostic = prefix + "typed transform lengths require length_unit and cannot mix with *_mm fields.";
                    return false;
                }
                auto unit = geometry::units::LengthUnit::Millimeter;
                if (typed_lengths) {
                    const auto parsed = unit_text ? parse_physical_length_unit(*unit_text) : std::nullopt;
                    if (!parsed) {
                        diagnostic = prefix + "length_unit must be mm, cm, m, in, ft, yd, pt or pc; px needs DPI.";
                        return false;
                    }
                    unit = *parsed;
                }
                TransformObjects transform;
                transform.target_ids = std::move(target_ids);
                const auto read_length = [&](std::string_view mm_key, std::string_view typed_key, double& target) {
                    if (!typed_lengths) return read_optional_number(mm_key, target);
                    if (!field(value, typed_key)) return true;
                    const auto* number = number_field(value, typed_key);
                    if (!number || !std::isfinite(*number) ||
                        !geometry::units::convert_length_to_mm(*number, unit, target)) {
                        diagnostic = prefix + std::string(typed_key) + " must be a finite length in length_unit.";
                        return false;
                    }
                    return true;
                };
                if (!read_length("translate_x_mm", "translate_x", transform.translate_x_mm) ||
                    !read_length("translate_y_mm", "translate_y", transform.translate_y_mm) ||
                    !read_optional_number("scale_x", transform.scale_x) ||
                    !read_optional_number("scale_y", transform.scale_y) ||
                    !read_optional_number("rotation_degrees", transform.rotation_degrees) ||
                    !read_length("pivot_x_mm", "pivot_x", transform.pivot_mm.x) ||
                    !read_length("pivot_y_mm", "pivot_y", transform.pivot_mm.y)) return false;
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
                    "op", "target_ids", "offset_x_mm", "offset_y_mm",
                    "offset_x", "offset_y", "length_unit"};
                if (!reject_unknown(allowed)) return false;
                if (!parse_target_ids(value, target_ids, 64, target_error)) {
                    diagnostic = prefix + target_error; return false;
                }
                const bool typed_lengths = field(value, "offset_x") || field(value, "offset_y");
                const bool mm_lengths = field(value, "offset_x_mm") || field(value, "offset_y_mm");
                const auto* unit_text = string_field(value, "length_unit");
                const bool has_unit = field(value, "length_unit") != nullptr;
                if (typed_lengths != has_unit || (typed_lengths && mm_lengths)) {
                    diagnostic = prefix + "typed duplicate offsets require length_unit and cannot mix with *_mm fields.";
                    return false;
                }
                auto unit = geometry::units::LengthUnit::Millimeter;
                if (typed_lengths) {
                    const auto parsed = unit_text ? parse_physical_length_unit(*unit_text) : std::nullopt;
                    if (!parsed) {
                        diagnostic = prefix + "length_unit must be mm, cm, m, in, ft, yd, pt or pc; px needs DPI.";
                        return false;
                    }
                    unit = *parsed;
                }
                DuplicateObjects duplicate;
                duplicate.target_ids = std::move(target_ids);
                const auto read_offset = [&](std::string_view mm_key, std::string_view typed_key, double& target) {
                    if (!typed_lengths) return read_optional_number(mm_key, target);
                    if (!field(value, typed_key)) return true;
                    const auto* number = number_field(value, typed_key);
                    if (!number || !std::isfinite(*number) ||
                        !geometry::units::convert_length_to_mm(*number, unit, target)) {
                        diagnostic = prefix + std::string(typed_key) + " must be a finite length in length_unit.";
                        return false;
                    }
                    return true;
                };
                if (!read_offset("offset_x_mm", "offset_x", duplicate.offset_x_mm) ||
                    !read_offset("offset_y_mm", "offset_y", duplicate.offset_y_mm)) return false;
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
                    "op", "target_ids", "fill", "stroke", "stroke_width_mm",
                    "stroke_width", "length_unit"};
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
                const bool typed_width = field(value, "stroke_width") != nullptr;
                const bool has_unit = field(value, "length_unit") != nullptr;
                if (typed_width != has_unit || (typed_width && field(value, "stroke_width_mm"))) {
                    diagnostic = prefix + "typed stroke_width requires length_unit and cannot mix with stroke_width_mm.";
                    return false;
                }
                if (typed_width) {
                    const auto* unit_text = string_field(value, "length_unit");
                    const auto unit = unit_text ? parse_physical_length_unit(*unit_text) : std::nullopt;
                    const auto* number = number_field(value, "stroke_width");
                    if (!unit || !number || !std::isfinite(*number) ||
                        !geometry::units::convert_length_to_mm(*number, *unit, stroke_width)) {
                        diagnostic = prefix + "length_unit and stroke_width must be valid finite physical lengths; px needs DPI.";
                        return false;
                    }
                    style.stroke_width_mm = stroke_width;
                } else {
                    if (!read_optional_number("stroke_width_mm", stroke_width)) return false;
                    if (field(value, "stroke_width_mm")) style.stroke_width_mm = stroke_width;
                }
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
            "op", "client_ref", "name", "layer_index", "fill", "stroke",
            "stroke_width_mm", "stroke_width", "length_unit"};
        std::set<std::string, std::less<>> allowed = common_create_fields;
        if (*op == "object.create_rectangle" || *op == "object.create_ellipse") {
            allowed.insert({"x_mm", "y_mm", "width_mm", "height_mm", "x", "y", "width", "height"});
        } else if (*op == "object.create_text") {
            allowed.insert({"x_mm", "y_mm", "text", "font_family", "font_postscript_name",
                "font_weight", "font_italic", "font_size_pt", "alignment", "underline", "x", "y"});
        } else {
            allowed.insert({"points_mm", "points", "closed"});
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

        const bool has_length_unit = field(value, "length_unit") != nullptr;
        const auto* length_unit_text = string_field(value, "length_unit");
        geometry::units::LengthUnit unit = geometry::units::LengthUnit::Millimeter;
        if (has_length_unit) {
            const auto parsed_unit = length_unit_text
                ? parse_physical_length_unit(*length_unit_text) : std::nullopt;
            if (!parsed_unit) {
                diagnostic = prefix + "length_unit must be mm, cm, m, in, ft, yd, pt or pc; px needs an explicit DPI basis.";
                return false;
            }
            unit = *parsed_unit;
        }
        const bool has_typed_geometry = create.kind == CreateObjectKind::Rectangle ||
                create.kind == CreateObjectKind::Ellipse
            ? (field(value, "x") || field(value, "y") || field(value, "width") || field(value, "height"))
            : create.kind == CreateObjectKind::Text
                ? (field(value, "x") || field(value, "y"))
                : field(value, "points") != nullptr;
        const bool has_mm_geometry = field(value, "x_mm") || field(value, "y_mm") ||
            field(value, "width_mm") || field(value, "height_mm") || field(value, "points_mm");
        if (has_length_unit != has_typed_geometry || (has_length_unit && has_mm_geometry)) {
            diagnostic = prefix + "use either explicit *_mm/points_mm fields or typed geometry fields with length_unit; do not mix them.";
            return false;
        }
        const auto read_typed_length = [&](std::string_view key, double& target) {
            const auto* number = number_field(value, key);
            if (!number || !std::isfinite(*number) ||
                !geometry::units::convert_length_to_mm(*number, unit, target)) {
                diagnostic = prefix + std::string(key) + " must be a finite length in length_unit.";
                return false;
            }
            return true;
        };
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
        if ((has_length_unit && field(value, "stroke_width_mm")) ||
            (!has_length_unit && field(value, "stroke_width"))) {
            diagnostic = prefix + "stroke_width_mm cannot be mixed with typed stroke_width; typed stroke_width requires length_unit.";
            return false;
        }
        if (has_length_unit && field(value, "stroke_width")) {
            if (!read_typed_length("stroke_width", create.stroke_width_mm)) return false;
        } else if (!read_optional_number("stroke_width_mm", create.stroke_width_mm)) {
            return false;
        }
        if (create.kind == CreateObjectKind::Text) {
            if (field(value, "width_mm") || field(value, "height_mm") ||
                field(value, "width") || field(value, "height") || field(value, "stroke") ||
                field(value, "stroke_width_mm") || field(value, "stroke_width")) {
                diagnostic = prefix + "text objects do not accept shape dimensions or stroke fields.";
                return false;
            }
            const auto* text = string_field(value, "text");
            const auto* family = string_field(value, "font_family");
            double x_mm = 0.0, y_mm = 0.0;
            bool position_valid = false;
            if (has_length_unit) {
                position_valid = read_typed_length("x", x_mm) && read_typed_length("y", y_mm);
            } else {
                const auto* x = number_field(value, "x_mm");
                const auto* y = number_field(value, "y_mm");
                position_valid = x && y && std::isfinite(*x) && std::isfinite(*y);
                if (position_valid) { x_mm = *x; y_mm = *y; }
            }
            if (!text || !family || !position_valid) {
                if (!has_length_unit)
                    diagnostic = prefix + "text requires text, font_family and finite x_mm/y_mm or x/y with length_unit.";
                return false;
            }
            create.text = *text;
            create.position_mm = {x_mm, y_mm};
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
                field(value, "height_mm") || field(value, "x") || field(value, "y") ||
                field(value, "width") || field(value, "height") || field(value, "text") || field(value, "font_family") ||
                field(value, "font_postscript_name") || field(value, "font_weight") ||
                field(value, "font_italic") || field(value, "font_size_pt") ||
                field(value, "alignment") || field(value, "underline")) {
                diagnostic = prefix + "path objects use points_mm or points and do not accept box or text fields.";
                return false;
            }
            const auto* points = field(value, has_length_unit ? "points" : "points_mm");
            if (!points || !points->array() || points->array()->size() < 2 || points->array()->size() > 4096) {
                diagnostic = prefix + "points_mm or points must contain 2 to 4096 point objects.";
                return false;
            }
            create.path_points_mm.reserve(points->array()->size());
            for (const auto& point_value : *points->array()) {
                if (!point_value.object() ||
                    !(has_length_unit ? only_fields(point_value, {"x", "y"})
                                      : only_fields(point_value, {"x_mm", "y_mm"}))) {
                    diagnostic = prefix + (has_length_unit
                        ? "each typed point must contain only x and y."
                        : "each point must contain only x_mm and y_mm.");
                    return false;
                }
                geometry::Point2D point;
                if (has_length_unit) {
                    const auto* x = number_field(point_value, "x");
                    const auto* y = number_field(point_value, "y");
                    if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y) ||
                        !geometry::units::convert_length_to_mm(*x, unit, point.x) ||
                        !geometry::units::convert_length_to_mm(*y, unit, point.y)) {
                        diagnostic = prefix + "each typed point requires finite x/y in length_unit.";
                        return false;
                    }
                } else {
                    const auto* x = number_field(point_value, "x_mm");
                    const auto* y = number_field(point_value, "y_mm");
                    if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y)) {
                        diagnostic = prefix + "each point requires finite x_mm and y_mm.";
                        return false;
                    }
                    point = {*x, *y};
                }
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
            double x_mm = 0.0, y_mm = 0.0, width_mm = 0.0, height_mm = 0.0;
            bool dimensions_valid = false;
            if (has_length_unit) {
                dimensions_valid = read_typed_length("x", x_mm) && read_typed_length("y", y_mm) &&
                    read_typed_length("width", width_mm) && read_typed_length("height", height_mm);
            } else {
                const auto* x = number_field(value, "x_mm");
                const auto* y = number_field(value, "y_mm");
                const auto* width = number_field(value, "width_mm");
                const auto* height = number_field(value, "height_mm");
                dimensions_valid = x && y && width && height && std::isfinite(*x) &&
                    std::isfinite(*y) && std::isfinite(*width) && std::isfinite(*height);
                if (dimensions_valid) { x_mm = *x; y_mm = *y; width_mm = *width; height_mm = *height; }
            }
            if (!dimensions_valid) {
                if (!has_length_unit)
                    diagnostic = prefix + "shape requires finite x/y/width/height in mm or length_unit.";
                return false;
            }
            create.bounds_mm = {x_mm, y_mm, width_mm, height_mm};
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

} // namespace pikdraw::automation
