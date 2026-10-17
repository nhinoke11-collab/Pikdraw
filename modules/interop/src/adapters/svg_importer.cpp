#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "svg_path_parser.hpp"
#include "svg_xml_cursor.hpp"
#include "../path_adapter/svg_path_adapter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace pikdraw::interop {
namespace {

constexpr double kPxToMm = 25.4 / 96.0;
constexpr size_t kMaxTotalPathElements = 100000;
constexpr size_t kMaxSvgGroupDepth = 128;

struct SvgLength { double value{0}; std::string_view unit; };

bool ParseLength(std::string_view source, SvgLength& output) {
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.front())) != 0) source.remove_prefix(1);
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.back())) != 0) source.remove_suffix(1);
    size_t consumed = 0;
    if (!ParseSvgNumber(source, output.value, consumed)) return false;
    output.unit = source.substr(consumed);
    return output.unit.empty() || output.unit == "px" || output.unit == "mm" || output.unit == "cm" ||
           output.unit == "in" || output.unit == "pt" || output.unit == "pc";
}

double LengthToMm(const SvgLength& length) {
    if (length.unit.empty() || length.unit == "px") return length.value * kPxToMm;
    if (length.unit == "mm") return length.value;
    if (length.unit == "cm") return length.value * 10.0;
    if (length.unit == "in") return length.value * 25.4;
    if (length.unit == "pt") return length.value * 25.4 / 72.0;
    return length.value * 25.4 / 6.0;
}

bool LengthToUserUnits(const SvgLength& length, double& value) {
    if (length.unit.empty() || length.unit == "px") value = length.value;
    else value = LengthToMm(length) / kPxToMm;
    return std::isfinite(value);
}

bool AttributesAllowed(const XmlTag& tag, std::initializer_list<std::string_view> allowed) {
    for (const auto& [name, value] : tag.attributes) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) return false;
    }
    return true;
}

bool RootAttributesAllowed(const XmlTag& tag) {
    for (const auto& [name, value] : tag.attributes) {
        if (name.starts_with("xmlns:") && name.size() > 6 && name.size() <= 70 &&
            value.size() <= 512 && IsValidUtf8(value)) continue;
        if (name != "xmlns" && name != "id" && name != "version" && name != "width" &&
            name != "height" && name != "viewBox" && name != "preserveAspectRatio") return false;
    }
    return true;
}

std::string Attribute(const XmlTag& tag, std::string_view name, std::string fallback = {}) {
    const auto found = tag.attributes.find(std::string(name));
    return found == tag.attributes.end() ? std::move(fallback) : found->second;
}

bool ParseUserLength(std::string_view source, double fallback, double& value, bool positive = false) {
    if (source.empty()) { value = fallback; return !positive || value > 0; }
    SvgLength length;
    return ParseLength(source, length) && LengthToUserUnits(length, value) && std::isfinite(value) &&
           (!positive || value > 0) && std::abs(value) <= 1.0e9;
}

bool ParseViewBox(std::string_view source, std::array<double, 4>& values) {
    return ParseSvgNumberList(source, values) && values[2] > 0 && values[3] > 0 &&
           std::abs(values[0]) <= 1.0e9 && std::abs(values[1]) <= 1.0e9 &&
           values[2] <= 1.0e9 && values[3] <= 1.0e9;
}

bool ParseColor(std::string_view source, color::ColorValue& value) {
    std::string normalized(source);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    if (normalized == "black") value = color::RGBColor{0, 0, 0, 1};
    else if (normalized == "white") value = color::RGBColor{1, 1, 1, 1};
    else if (normalized == "red") value = color::RGBColor{1, 0, 0, 1};
    else if (normalized == "green") value = color::RGBColor{0, 0.5019607843137255, 0, 1};
    else if (normalized == "blue") value = color::RGBColor{0, 0, 1, 1};
    else if (normalized == "transparent") value = color::RGBColor{0, 0, 0, 0};
    else if (normalized.size() == 4 && normalized.front() == '#') {
        const int r = hex(normalized[1]), g = hex(normalized[2]), b = hex(normalized[3]);
        if (r < 0 || g < 0 || b < 0) return false;
        value = color::RGBColor{r / 15.0, g / 15.0, b / 15.0, 1};
    } else if ((normalized.size() == 7 || normalized.size() == 9) && normalized.front() == '#') {
        int channels[4]{0, 0, 0, 255};
        for (size_t i = 0; i < (normalized.size() == 9 ? 4u : 3u); ++i) {
            const int high = hex(normalized[1 + i * 2]), low = hex(normalized[2 + i * 2]);
            if (high < 0 || low < 0) return false;
            channels[i] = high * 16 + low;
        }
        value = color::RGBColor{channels[0] / 255.0, channels[1] / 255.0,
                                channels[2] / 255.0, channels[3] / 255.0};
    } else return false;
    return value.is_valid();
}

bool ParsePaint(const XmlTag& tag, std::string_view key, std::string_view default_value,
                color::ColorValue& value, bool& enabled) {
    const std::string paint = Attribute(tag, key, std::string(default_value));
    enabled = paint != "none";
    return !enabled || ParseColor(paint, value);
}

geometry::Path2D TransformPath(const geometry::Path2D& source, const geometry::Transform2D& transform) {
    geometry::Path2D result;
    result.set_winding_rule(source.winding_rule());
    for (const auto& element : source.elements()) {
        const auto endpoint = transform.map_point(element.pt);
        if (element.verb == geometry::PathVerb::MoveTo) result.move_to(endpoint);
        else if (element.verb == geometry::PathVerb::LineTo) result.line_to(endpoint);
        else if (element.verb == geometry::PathVerb::QuadTo) {
            const auto control = transform.map_point(element.ctrl1);
            result.quad_to(control.x, control.y, endpoint.x, endpoint.y);
        } else if (element.verb == geometry::PathVerb::CubicTo) {
            const auto first = transform.map_point(element.ctrl1);
            const auto second = transform.map_point(element.ctrl2);
            result.cubic_to(first.x, first.y, second.x, second.y, endpoint.x, endpoint.y);
        } else if (element.verb == geometry::PathVerb::Close) result.close();
        else return {};
    }
    return result;
}

SvgImportResult Failure(std::string diagnostic, std::string feature = "unsupported_svg_subset") {
    SvgImportResult result;
    result.page = document::Page::make_default();
    result.error_message = std::move(diagnostic);
    result.report.add_issue(FidelitySeverity::Unsupported, "svg", std::move(feature), result.error_message);
    return result;
}

bool ParseSourceId(const XmlTag& tag, uint32_t ordinal, std::string& id,
                   std::unordered_set<std::string>& used) {
    id = Attribute(tag, "id");
    if (id.empty()) id = "svg-element-" + std::to_string(ordinal);
    if (id.size() > 128 || !IsValidUtf8(id) || !used.insert(id).second) return false;
    return true;
}

} // namespace

namespace {

SvgImportResult ImportSvgString(const std::string& input) {
    if (input.empty() || input.size() > SvgImporter::kMaxSourceBytes) return Failure("SVG source exceeds the bounded input profile", "resource_limit");
    std::string_view source(input);
    if (source.starts_with("\xef\xbb\xbf")) source.remove_prefix(3);
    if (!IsValidUtf8(source)) return Failure("SVG source is not valid UTF-8", "invalid_encoding");

    XmlCursor cursor(source);
    if (!cursor.SkipDocumentPreamble()) return Failure("SVG XML preamble is malformed or prohibited", "xml_security");
    XmlTag root;
    if (!cursor.ParseTag(root) || root.closing || root.name != "svg" || !RootAttributesAllowed(root)) {
        return Failure("SVG root or root attributes are outside the supported profile", "unsupported_root");
    }
    const std::string namespace_uri = Attribute(root, "xmlns");
    if (!namespace_uri.empty() && namespace_uri != "http://www.w3.org/2000/svg") {
        return Failure("SVG namespace is not supported", "unsupported_namespace");
    }
    const std::string inkscape_namespace = Attribute(root, "xmlns:inkscape");
    if (!inkscape_namespace.empty() &&
        inkscape_namespace != "http://www.inkscape.org/namespaces/inkscape") {
        return Failure("Inkscape layer namespace is not supported", "unsupported_namespace");
    }
    const std::string version = Attribute(root, "version", "1.1");
    if (version != "1.0" && version != "1.1") return Failure("Only static SVG 1.0/1.1 is in the supported profile", "unsupported_version");

    SvgLength width_length, height_length;
    if (!ParseLength(Attribute(root, "width"), width_length) || !ParseLength(Attribute(root, "height"), height_length)) {
        return Failure("SVG requires explicit width and height with supported units", "unsupported_units");
    }
    const double width_mm = LengthToMm(width_length), height_mm = LengthToMm(height_length);
    if (!std::isfinite(width_mm) || !std::isfinite(height_mm) || width_mm <= 0 || height_mm <= 0 ||
        width_mm > SvgImporter::kMaxPageMillimeters || height_mm > SvgImporter::kMaxPageMillimeters) {
        return Failure("SVG page dimensions exceed the supported range", "resource_limit");
    }

    geometry::Transform2D root_transform{kPxToMm, 0, 0, kPxToMm, 0, 0};
    const std::string view_box_text = Attribute(root, "viewBox");
    if (!view_box_text.empty()) {
        std::array<double, 4> view_box{};
        if (!ParseViewBox(view_box_text, view_box)) return Failure("SVG viewBox is malformed or outside the supported range", "invalid_viewbox");
        const double sx = width_mm / view_box[2], sy = height_mm / view_box[3];
        const std::string aspect = Attribute(root, "preserveAspectRatio", "xMidYMid meet");
        if (aspect == "none") {
            if (std::abs(sx - sy) > 1e-12) return Failure("Non-uniform viewBox scaling is not supported", "unsupported_viewbox_transform");
            root_transform = {sx, 0, 0, sy, -view_box[0] * sx, -view_box[1] * sy};
        } else if (aspect == "xMidYMid meet" || aspect == "xMidYMid") {
            const double scale = std::min(sx, sy);
            root_transform = {scale, 0, 0, scale,
                (width_mm - view_box[2] * scale) / 2.0 - view_box[0] * scale,
                (height_mm - view_box[3] * scale) / 2.0 - view_box[1] * scale};
        } else return Failure("Only default xMidYMid meet or uniform none viewBox mapping is supported", "unsupported_viewbox_transform");
    } else if (Attribute(root, "preserveAspectRatio") != "") {
        return Failure("preserveAspectRatio requires viewBox", "unsupported_viewbox_transform");
    }

    SvgImportResult result;
    result.page = document::Page::make_default();
    result.page.width_mm = width_mm;
    result.page.height_mm = height_mm;
    result.page.layers.clear();
    std::unordered_set<std::string> source_ids;
    struct GroupFrame {
        document::ObjectId id{document::kInvalidObjectId};
        std::string source_id;
        std::string name;
        bool is_layer{false};
        size_t layer_index{0};
        std::vector<document::ObjectId> children;
    };
    std::vector<GroupFrame> group_stack;
    std::optional<size_t> default_layer_index;
    const auto ensure_default_layer = [&] {
        if (default_layer_index) return;
        default_layer_index = result.page.layers.size();
        result.page.layers.push_back(document::Layer{"Layer 1", true, false, {}});
        std::string synthetic_id = "svg-default-layer";
        uint32_t suffix = 1;
        while (!source_ids.insert(synthetic_id).second) {
            synthetic_id = "svg-default-layer-" + std::to_string(suffix++);
        }
        result.layer_source_ids.push_back(std::move(synthetic_id));
    };
    const auto add_to_container = [&](document::ObjectId id) {
        if (group_stack.empty()) {
            ensure_default_layer();
            result.page.layers[*default_layer_index].object_ids.push_back(id);
        } else if (group_stack.back().is_layer) {
            result.page.layers[group_stack.back().layer_index].object_ids.push_back(id);
        } else {
            group_stack.back().children.push_back(id);
        }
    };
    const auto close_group = [&]() {
        if (group_stack.empty()) return false;
        GroupFrame frame = std::move(group_stack.back());
        group_stack.pop_back();
        if (frame.is_layer) return true;
        document::GroupObject object;
        object.id = frame.id;
        object.name = std::move(frame.name);
        object.children_ids = std::move(frame.children);
        result.objects.emplace_back(std::move(object));
        result.source_ids.push_back(frame.source_id);
        add_to_container(frame.id);
        return true;
    };
    size_t total_path_elements = 0;
    uint32_t ordinal = 0;
    bool root_closed = root.self_closing;
    while (!root_closed) {
        if (!cursor.SkipMisc()) return Failure("Malformed XML comment or prohibited declaration", "xml_security");
        if (cursor.AtEnd() || !cursor.AtTagStart()) return Failure("Unexpected XML content in SVG document", "malformed_xml");
        XmlTag tag;
        if (!cursor.ParseTag(tag)) return Failure("Malformed SVG tag or attribute", "malformed_xml");
        if (tag.closing) {
            if (tag.name == "g") {
                if (!close_group()) return Failure("SVG group closing tag has no matching group", "malformed_xml");
                continue;
            }
            if (tag.name != "svg" || !group_stack.empty()) {
                return Failure("Unexpected or unbalanced SVG closing tag", "malformed_xml");
            }
            root_closed = true;
            break;
        }
        if (++ordinal > SvgImporter::kMaxObjects) return Failure("SVG object count exceeds the supported profile", "resource_limit");
        std::string source_id;
        if (!ParseSourceId(tag, ordinal, source_id, source_ids)) return Failure("SVG element IDs are invalid or duplicated", "invalid_source_id");
        bool element_close_already_consumed = false;

        if (tag.name == "g") {
            if (!AttributesAllowed(tag, {"id", "inkscape:groupmode", "inkscape:label"})) {
                return Failure("SVG group uses unsupported attributes", "unsupported_group_feature");
            }
            const std::string group_mode = Attribute(tag, "inkscape:groupmode");
            const std::string label = Attribute(tag, "inkscape:label");
            if (label.size() > 128) return Failure("SVG group or layer label exceeds the supported profile", "resource_limit");
            if ((!group_mode.empty() || !label.empty()) &&
                inkscape_namespace != "http://www.inkscape.org/namespaces/inkscape") {
                return Failure("Inkscape layer metadata has no recognized namespace", "unsupported_namespace");
            }
            if (!group_mode.empty() && group_mode != "layer") {
                return Failure("Inkscape group mode is unsupported", "unsupported_layer_feature");
            }
            const bool is_layer = group_mode == "layer";
            if (is_layer && !group_stack.empty()) {
                return Failure("Nested SVG layers are outside the supported profile", "nested_layer_unsupported");
            }
            if (!is_layer && group_stack.size() >= kMaxSvgGroupDepth) {
                return Failure("SVG group nesting exceeds the supported profile", "resource_limit");
            }
            GroupFrame frame;
            frame.id = document::generate_object_id();
            frame.source_id = source_id;
            frame.name = label.empty() ? source_id : label;
            frame.is_layer = is_layer;
            if (is_layer) {
                frame.layer_index = result.page.layers.size();
                result.page.layers.push_back(document::Layer{frame.name, true, false, {}});
                result.layer_source_ids.push_back(source_id);
            }
            group_stack.push_back(std::move(frame));
            if (tag.self_closing && !close_group()) {
                return Failure("SVG empty group could not be closed", "malformed_xml");
            }
            continue;
        } else if (tag.name == "rect") {
            if (!AttributesAllowed(tag, {"id", "x", "y", "width", "height", "fill", "stroke", "stroke-width"})) {
                return Failure("SVG rectangle uses unsupported attributes", "unsupported_rectangle_feature");
            }
            double x = 0, y = 0, w = 0, h = 0;
            if (!ParseUserLength(Attribute(tag, "x"), 0, x) || !ParseUserLength(Attribute(tag, "y"), 0, y) ||
                !ParseUserLength(Attribute(tag, "width"), 0, w, true) || !ParseUserLength(Attribute(tag, "height"), 0, h, true)) {
                return Failure("SVG rectangle dimensions are malformed", "invalid_geometry");
            }
            geometry::Path2D source_path = geometry::Path2D::make_rect(x, y, w, h);
            document::PathObject object;
            object.id = document::generate_object_id();
            object.name = "Imported Rectangle";
            object.path = TransformPath(source_path, root_transform);
            if (object.path.empty() || !ParsePaint(tag, "fill", "black", object.fill, object.has_fill) ||
                !ParsePaint(tag, "stroke", "none", object.stroke, object.has_stroke)) {
                return Failure("SVG rectangle paint or geometry is unsupported", "unsupported_paint");
            }
            SvgLength stroke;
            if (!ParseLength(Attribute(tag, "stroke-width", "1"), stroke) || !LengthToUserUnits(stroke, object.stroke_width_mm)) {
                return Failure("SVG stroke width is malformed", "unsupported_units");
            }
            object.stroke_width_mm *= std::abs(root_transform.a);
            if (!std::isfinite(object.stroke_width_mm) || object.stroke_width_mm < 0) return Failure("SVG stroke width is invalid", "invalid_geometry");
            std::string issue;
            auto source = adapters::SvgPathAdapter::from_path2d(object.path, &issue);
            if (source) {
                object.set_authoring_source(std::move(*source));
            }
            total_path_elements += object.path.size();
            const auto id = object.id;
            result.objects.emplace_back(std::move(object));
            result.source_ids.push_back(source_id);
            add_to_container(id);
        } else if (tag.name == "path") {
            if (!AttributesAllowed(tag, {"id", "d", "fill", "stroke", "stroke-width", "fill-rule"})) {
                return Failure("SVG path uses unsupported attributes", "unsupported_path_feature");
            }
            document::PathObject object;
            object.id = document::generate_object_id();
            object.name = "Imported Path";
            if (!ParseSvgPath(Attribute(tag, "d"), root_transform, object.path, SvgImporter::kMaxPathElements)) {
                return Failure("SVG path data is malformed or uses an unsupported command", "unsupported_path_command");
            }
            const std::string rule = Attribute(tag, "fill-rule", "nonzero");
            if (rule == "evenodd") object.path.set_winding_rule(geometry::WindingRule::EvenOdd);
            else if (rule != "nonzero") return Failure("SVG fill-rule is unsupported", "unsupported_fill_rule");
            if (!ParsePaint(tag, "fill", "black", object.fill, object.has_fill) ||
                !ParsePaint(tag, "stroke", "none", object.stroke, object.has_stroke)) {
                return Failure("SVG path paint is unsupported", "unsupported_paint");
            }
            SvgLength stroke;
            if (!ParseLength(Attribute(tag, "stroke-width", "1"), stroke) || !LengthToUserUnits(stroke, object.stroke_width_mm)) {
                return Failure("SVG stroke width is malformed", "unsupported_units");
            }
            object.stroke_width_mm *= std::abs(root_transform.a);
            std::string issue;
            auto source = adapters::SvgPathAdapter::from_path2d(object.path, &issue);
            if (source) {
                object.set_authoring_source(std::move(*source));
            }
            const auto id = object.id;
            total_path_elements += object.path.size();
            result.objects.emplace_back(std::move(object));
            result.source_ids.push_back(source_id);
            add_to_container(id);
        } else if (tag.name == "text") {
            if (!AttributesAllowed(tag, {"id", "x", "y", "fill", "font-family", "font-size", "font-weight", "font-style"}) || tag.self_closing) {
                return Failure("SVG text uses unsupported attributes or empty-element syntax", "unsupported_text_feature");
            }
            double x = 0, y = 0, font_size = 16;
            if (!ParseUserLength(Attribute(tag, "x"), 0, x) || !ParseUserLength(Attribute(tag, "y"), 0, y) ||
                !ParseUserLength(Attribute(tag, "font-size", "16"), 16, font_size, true)) {
                return Failure("SVG text metrics are malformed", "unsupported_units");
            }
            std::string text;
            if (!cursor.ReadText(text)) return Failure("SVG text is malformed or too large", "resource_limit");
            XmlTag close;
            if (!cursor.ParseTag(close) || !close.closing || close.name != "text") return Failure("Nested SVG text markup is not supported", "unsupported_text_markup");
            element_close_already_consumed = true;
            document::TextObject object;
            object.id = document::generate_object_id();
            object.name = "Imported Text";
            object.text = std::move(text);
            const auto position = root_transform.map_point({x, y});
            object.position = position;
            object.font.family_name = Attribute(tag, "font-family", "sans-serif");
            if (object.font.family_name.size() > 256) return Failure("SVG font family is too long", "resource_limit");
            const std::string weight = Attribute(tag, "font-weight", "400");
            if (weight == "normal") object.font.weight = 400;
            else if (weight == "bold") object.font.weight = 700;
            else {
                unsigned value = 0;
                const auto parsed = std::from_chars(weight.data(), weight.data() + weight.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != weight.data() + weight.size() || value < 100 || value > 900 || value % 100 != 0) {
                    return Failure("SVG font weight is unsupported", "unsupported_font_weight");
                }
                object.font.weight = static_cast<uint16_t>(value);
            }
            const std::string style = Attribute(tag, "font-style", "normal");
            if (style != "normal" && style != "italic") return Failure("SVG font style is unsupported", "unsupported_font_style");
            object.font.italic = style == "italic";
            object.font.size_pt = font_size * std::abs(root_transform.a) * 72.0 / 25.4;
            bool has_fill = true;
            if (!ParsePaint(tag, "fill", "black", object.fill, has_fill)) return Failure("SVG text paint is unsupported", "unsupported_paint");
            if (!has_fill) object.fill = color::RGBColor{0, 0, 0, 0};
            result.report.add_issue(FidelitySeverity::Warning, source_id, "font_resolution",
                                    "Font family is retained; rendered appearance is not verified");
            const auto id = object.id;
            result.objects.emplace_back(std::move(object));
            result.source_ids.push_back(source_id);
            add_to_container(id);
        } else {
            return Failure("SVG contains an element outside the supported hierarchy subset", "unsupported_element");
        }

        if (total_path_elements > kMaxTotalPathElements) return Failure("SVG total path complexity exceeds the supported profile", "resource_limit");
        if (!tag.self_closing && !element_close_already_consumed) {
            if (!cursor.SkipMisc()) return Failure("Malformed XML inside SVG element", "malformed_xml");
            XmlTag close;
            if (!cursor.ParseTag(close) || !close.closing || close.name != tag.name) {
                return Failure("SVG element content or closing tag is unsupported", "malformed_xml");
            }
        }
    }
    if (result.page.layers.empty()) {
        ensure_default_layer();
    }
    if (!cursor.SkipMisc() || !cursor.AtEnd()) return Failure("Trailing XML content is not supported", "malformed_xml");
    result.success = true;
    return result;
}

} // namespace

SvgImportResult SvgImporter::import_from_string(const std::string& input) {
    try {
        return ImportSvgString(input);
    } catch (...) {
        return Failure("SVG parser allocation or codec failure", "resource_limit");
    }
}
SvgImportResult SvgImporter::import_from_file(const std::string& path) {
    try {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return Failure("Cannot read selected SVG file", "source_read_failed");
    const auto end = input.tellg();
    if (end <= 0 || static_cast<uint64_t>(end) > kMaxSourceBytes) return Failure("SVG source exceeds the bounded input profile", "resource_limit");
    std::string bytes(static_cast<size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return Failure("Cannot read selected SVG file", "source_read_failed");
    return import_from_string(bytes);
    } catch (...) {
        return Failure("SVG file read allocation failure", "resource_limit");
    }
}

} // namespace pikdraw::interop
