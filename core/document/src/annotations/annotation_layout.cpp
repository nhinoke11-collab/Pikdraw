#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/geometry/dimensions/dimension_geometry.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace pikdraw::document {

namespace {
constexpr size_t kMaxAnnotationSpecBytes = 1024u * 1024u;
constexpr size_t kMaxAnnotationTextBytes = 64u * 1024u;
constexpr size_t kMaxLeaderPoints = 4096;
constexpr size_t kMaxLabelBytes = 256;
constexpr double kMaxAnnotationCoordinate = 1.0e12;

using SpecFields = std::unordered_map<std::string, std::string>;

bool set_error(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

bool parse_spec_fields(std::string_view spec, std::string_view prefix,
                       SpecFields& fields, std::string* error) {
    if (spec.size() > kMaxAnnotationSpecBytes)
        return set_error(error, "Annotation specification exceeds 1 MiB");
    if (spec != prefix &&
        (spec.size() <= prefix.size() || !spec.starts_with(prefix) ||
         spec[prefix.size()] != ';'))
        return set_error(error, "Unsupported annotation specification version");

    size_t start = spec == prefix ? spec.size() : prefix.size() + 1;
    while (start < spec.size()) {
        const auto next = spec.find(';', start);
        const auto end = next == std::string_view::npos ? spec.size() : next;
        const auto token = spec.substr(start, end - start);
        const auto eq = token.find('=');
        if (token.empty() || eq == std::string_view::npos || eq == 0)
            return set_error(error, "Malformed annotation specification field");
        std::string key(token.substr(0, eq));
        const std::string value(token.substr(eq + 1));
        if (!fields.emplace(key, value).second)
            return set_error(error, "Duplicate annotation specification field");
        start = next == std::string_view::npos ? spec.size() : next + 1;
        if (next != std::string_view::npos && start == spec.size())
            return set_error(error, "Trailing annotation specification delimiter");
    }
    return true;
}

bool has_only_fields(const SpecFields& fields,
                     const std::unordered_set<std::string>& allowed,
                     std::string* error, bool allow_point_fields = false) {
    for (const auto& [key, unused] : fields) {
        (void)unused;
        if (allowed.contains(key)) continue;
        if (allow_point_fields && key.size() >= 5 && key.starts_with("pt")) {
            const auto suffix_pos = key.size() - 2;
            const auto suffix = key.substr(suffix_pos);
            if (suffix == "_x" || suffix == "_y") {
                const auto digits = std::string_view(key).substr(2, key.size() - 4);
                if (!digits.empty() && std::all_of(digits.begin(), digits.end(),
                    [](char c) { return c >= '0' && c <= '9'; }))
                    continue;
            }
        }
        return set_error(error, "Unknown annotation specification field");
    }
    return true;
}

template <typename T>
bool parse_integer(const SpecFields& fields, const char* key, T& value,
                   std::string* error) {
    const auto it = fields.find(key);
    if (it == fields.end()) return set_error(error, "Missing annotation specification field");
    const auto* begin = it->second.data();
    const auto* end = begin + it->second.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        return set_error(error, "Invalid integer in annotation specification");
    return true;
}

bool parse_double(const SpecFields& fields, const char* key, double& value,
                  std::string* error) {
    const auto it = fields.find(key);
    if (it == fields.end()) return set_error(error, "Missing annotation specification field");
    const auto* begin = it->second.data();
    const auto* end = begin + it->second.size();
    const auto result = std::from_chars(begin, end, value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
        return set_error(error, "Invalid finite number in annotation specification");
    return true;
}

bool parse_bool(const SpecFields& fields, const char* key, bool& value,
                std::string* error) {
    int raw = 0;
    if (!parse_integer(fields, key, raw, error)) return false;
    if (raw != 0 && raw != 1)
        return set_error(error, "Invalid boolean in annotation specification");
    value = raw != 0;
    return true;
}

std::string encode_field(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (c == '%' || c == ';' || c == '\n' || c == '\r') {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0f]);
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

bool decode_field(std::string_view input, std::string& output,
                  size_t max_bytes, std::string* error) {
    output.clear();
    output.reserve(std::min(input.size(), max_bytes));
    const auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%') {
            if (i + 2 >= input.size())
                return set_error(error, "Malformed percent escape in annotation text");
            const int hi = hex_value(input[i + 1]);
            const int lo = hex_value(input[i + 2]);
            if (hi < 0 || lo < 0)
                return set_error(error, "Malformed percent escape in annotation text");
            output.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            output.push_back(input[i]);
        }
        if (output.size() > max_bytes)
            return set_error(error, "Annotation text exceeds its size limit");
    }
    return true;
}

bool valid_utf8_text(std::string_view value) noexcept {
    size_t i = 0;
    while (i < value.size()) {
        const auto lead = static_cast<uint8_t>(value[i]);
        if (lead == 0) return false;
        if (lead <= 0x7f) {
            ++i;
            continue;
        }
        size_t continuation_count = 0;
        uint8_t second_min = 0x80;
        uint8_t second_max = 0xbf;
        if (lead >= 0xc2 && lead <= 0xdf) {
            continuation_count = 1;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            continuation_count = 2;
            if (lead == 0xe0) second_min = 0xa0;
            if (lead == 0xed) second_max = 0x9f;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            continuation_count = 3;
            if (lead == 0xf0) second_min = 0x90;
            if (lead == 0xf4) second_max = 0x8f;
        } else {
            return false;
        }
        if (continuation_count > value.size() - i - 1) return false;
        const auto second = static_cast<uint8_t>(value[i + 1]);
        if (second < second_min || second > second_max) return false;
        for (size_t j = 2; j <= continuation_count; ++j) {
            const auto byte = static_cast<uint8_t>(value[i + j]);
            if (byte < 0x80 || byte > 0xbf) return false;
        }
        i += continuation_count + 1;
    }
    return true;
}

bool finite_point(const geometry::Point2D& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           std::abs(point.x) <= kMaxAnnotationCoordinate &&
           std::abs(point.y) <= kMaxAnnotationCoordinate;
}

bool finite_rect(const geometry::Rect2D& rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height) &&
           std::abs(rect.x) <= kMaxAnnotationCoordinate &&
           std::abs(rect.y) <= kMaxAnnotationCoordinate &&
           rect.width > 0.0 && rect.height > 0.0 &&
           rect.width <= kMaxAnnotationCoordinate &&
           rect.height <= kMaxAnnotationCoordinate &&
           std::isfinite(rect.right()) && std::isfinite(rect.bottom()) &&
           std::abs(rect.right()) <= kMaxAnnotationCoordinate &&
           std::abs(rect.bottom()) <= kMaxAnnotationCoordinate;
}

bool nearly_equal(double a, double b) {
    return std::abs(a - b) <= 1.0e-6 * (std::max)({1.0, std::abs(a), std::abs(b)});
}
} // namespace

geometry::Rect2D AnnotationObject::bounds() const {
    const auto layout = evaluate_annotation_layout(*this);
    return transform.map_rect(layout.bounds);
}

geometry::Rect2D DetailViewObject::bounds() const {
    const auto layout = evaluate_detail_view_layout(*this);
    return transform.map_rect(layout.bounds);
}

std::string serialize_annotation_spec(const AnnotationObject& ann) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(std::numeric_limits<double>::max_digits10);
    ss << "annotation:v1"
       << ";kind=" << static_cast<int>(ann.kind)
       << ";anc_obj=" << ann.anchor.source_object_id
       << ";anc_x=" << ann.anchor.anchor_pt.x
       << ";anc_y=" << ann.anchor.anchor_pt.y
       << ";anc_st=" << static_cast<int>(ann.anchor.state)
       << ";anc_el=" << (ann.anchor.element_id ? *ann.anchor.element_id : 0)
       << ";anc_has_el=" << (ann.anchor.element_id ? 1 : 0)
       << ";anc_param=" << ann.anchor.parameter
       << ";land_x=" << ann.landing_point.x
       << ";land_y=" << ann.landing_point.y
       << ";txt_x=" << ann.text_point.x
       << ";txt_y=" << ann.text_point.y
       << ";pts_cnt=" << ann.leader_points.size();

    for (size_t i = 0; i < ann.leader_points.size(); ++i) {
        ss << ";pt" << i << "_x=" << ann.leader_points[i].x
           << ";pt" << i << "_y=" << ann.leader_points[i].y;
    }

    const auto& s = ann.style;
    ss << ";t_sz=" << s.text_size_mm
       << ";l_w=" << s.line_width_mm
       << ";arr_sz=" << s.arrow_size_mm
       << ";arr_t=" << static_cast<int>(s.arrow_type)
       << ";has_land=" << (s.has_landing ? 1 : 0)
       << ";land_len=" << s.landing_length_mm
       << ";gap=" << s.gap_mm;

    ss << ";text=" << encode_field(ann.text)
       << ";font=" << encode_field(ann.style.font_family);
    return ss.str();
}

std::optional<AnnotationObject> deserialize_annotation_spec(
    std::string_view spec, std::string* error_reason) {
    if (error_reason) error_reason->clear();
    const auto fail = [&](const char* reason) -> std::optional<AnnotationObject> {
        if (error_reason) *error_reason = reason;
        return std::nullopt;
    };
    SpecFields kv;
    if (!parse_spec_fields(spec, "annotation:v1", kv, error_reason))
        return std::nullopt;
    static const std::unordered_set<std::string> allowed{
        "kind", "anc_obj", "anc_x", "anc_y", "anc_st", "anc_el",
        "anc_has_el", "anc_param", "land_x", "land_y", "txt_x", "txt_y",
        "pts_cnt", "t_sz", "l_w", "arr_sz", "arr_t", "has_land",
        "land_len", "gap", "text", "font"};
    if (!has_only_fields(kv, allowed, error_reason, true)) return std::nullopt;
    AnnotationObject ann;
    int kind = 0, state = 0, has_element = 0, arrow_type = 0, point_count = 0;
    uint64_t source_id = 0, element_id = 0;
    if (!parse_integer(kv, "kind", kind, error_reason) ||
        !parse_integer(kv, "anc_obj", source_id, error_reason) ||
        !parse_double(kv, "anc_x", ann.anchor.anchor_pt.x, error_reason) ||
        !parse_double(kv, "anc_y", ann.anchor.anchor_pt.y, error_reason) ||
        !parse_integer(kv, "anc_st", state, error_reason) ||
        !parse_integer(kv, "anc_el", element_id, error_reason) ||
        !parse_integer(kv, "anc_has_el", has_element, error_reason) ||
        !parse_double(kv, "anc_param", ann.anchor.parameter, error_reason) ||
        !parse_double(kv, "land_x", ann.landing_point.x, error_reason) ||
        !parse_double(kv, "land_y", ann.landing_point.y, error_reason) ||
        !parse_double(kv, "txt_x", ann.text_point.x, error_reason) ||
        !parse_double(kv, "txt_y", ann.text_point.y, error_reason) ||
        !parse_integer(kv, "pts_cnt", point_count, error_reason) ||
        !parse_double(kv, "t_sz", ann.style.text_size_mm, error_reason) ||
        !parse_double(kv, "l_w", ann.style.line_width_mm, error_reason) ||
        !parse_double(kv, "arr_sz", ann.style.arrow_size_mm, error_reason) ||
        !parse_integer(kv, "arr_t", arrow_type, error_reason) ||
        !parse_bool(kv, "has_land", ann.style.has_landing, error_reason) ||
        !parse_double(kv, "land_len", ann.style.landing_length_mm, error_reason) ||
        !parse_double(kv, "gap", ann.style.gap_mm, error_reason))
        return std::nullopt;
    if (kind < 0 || kind > static_cast<int>(AnnotationKind::PlainNote) ||
        state < 0 || state > static_cast<int>(AssociationState::Fixed) ||
        arrow_type < 0 || arrow_type > static_cast<int>(geometry::ArrowType::Open) ||
        (has_element != 0 && has_element != 1) || point_count < 0 ||
        static_cast<size_t>(point_count) > kMaxLeaderPoints) {
        return fail("Annotation enum or point count is out of range");
    }
    ann.kind = static_cast<AnnotationKind>(kind);
    ann.anchor.source_object_id = source_id;
    ann.anchor.state = static_cast<AssociationState>(state);
    if (has_element) ann.anchor.element_id = element_id;
    ann.style.arrow_type = static_cast<geometry::ArrowType>(arrow_type);
    if (kv.size() != allowed.size() - (kv.contains("font") ? 0u : 1u) +
                         static_cast<size_t>(point_count) * 2u)
        return fail("Annotation point fields do not match the declared count");
    ann.leader_points.reserve(static_cast<size_t>(point_count));
    for (int i = 0; i < point_count; ++i) {
        const std::string prefix = "pt" + std::to_string(i);
        geometry::Point2D pt;
        const auto x = prefix + "_x";
        const auto y = prefix + "_y";
        if (!parse_double(kv, x.c_str(), pt.x, error_reason) ||
            !parse_double(kv, y.c_str(), pt.y, error_reason))
            return std::nullopt;
        ann.leader_points.push_back(pt);
    }
    const auto text_it = kv.find("text");
    if (text_it == kv.end() ||
        !decode_field(text_it->second, ann.text, kMaxAnnotationTextBytes, error_reason))
        return fail("Missing or invalid annotation text");
    const auto font_it = kv.find("font");
    if (font_it != kv.end() &&
        !decode_field(font_it->second, ann.style.font_family, kMaxLabelBytes, error_reason))
        return std::nullopt;
    std::string validation_error;
    if (!validate_annotation_object(ann, &validation_error))
        return fail(validation_error.c_str());
    return ann;
}

bool validate_annotation_object(const AnnotationObject& ann,
                                std::string* error_reason) {
    if (error_reason) error_reason->clear();
    if (ann.kind != AnnotationKind::LeaderNote &&
        ann.kind != AnnotationKind::PlainNote)
        return set_error(error_reason, "Unsupported annotation kind");
    if (static_cast<uint8_t>(ann.anchor.state) >
        static_cast<uint8_t>(AssociationState::Fixed))
        return set_error(error_reason, "Invalid annotation anchor state");
    if ((ann.anchor.state == AssociationState::Valid ||
         ann.anchor.state == AssociationState::Partial) &&
        ann.anchor.source_object_id == kInvalidObjectId)
        return set_error(error_reason, "Associated annotation anchor has no source object");
    if (!finite_point(ann.anchor.anchor_pt) || !finite_point(ann.landing_point) ||
        !finite_point(ann.text_point) || !std::isfinite(ann.anchor.parameter) ||
        ann.anchor.parameter < 0.0 || ann.anchor.parameter > 1.0)
        return set_error(error_reason, "Annotation anchor or placement is out of range");
    if (ann.leader_points.size() > kMaxLeaderPoints)
        return set_error(error_reason, "Annotation has too many leader points");
    for (const auto& point : ann.leader_points) {
        if (!finite_point(point))
            return set_error(error_reason, "Annotation leader point is not finite");
    }
    if (ann.text.size() > kMaxAnnotationTextBytes ||
        !valid_utf8_text(ann.text) || ann.style.font_family.empty() ||
        ann.style.font_family.size() > kMaxLabelBytes ||
        !valid_utf8_text(ann.style.font_family))
        return set_error(error_reason, "Annotation text or font identity exceeds its limit");
    if (!std::isfinite(ann.style.text_size_mm) || ann.style.text_size_mm <= 0.0 ||
        ann.style.text_size_mm > 1000.0 ||
        !std::isfinite(ann.style.line_width_mm) || ann.style.line_width_mm <= 0.0 ||
        ann.style.line_width_mm > 1000.0 ||
        !std::isfinite(ann.style.arrow_size_mm) || ann.style.arrow_size_mm <= 0.0 ||
        ann.style.arrow_size_mm > 1000.0 ||
        !std::isfinite(ann.style.landing_length_mm) ||
        ann.style.landing_length_mm < 0.0 || ann.style.landing_length_mm > 1.0e6 ||
        !std::isfinite(ann.style.gap_mm) || ann.style.gap_mm < 0.0 ||
        ann.style.gap_mm > 1.0e6 ||
        static_cast<uint8_t>(ann.style.arrow_type) >
            static_cast<uint8_t>(geometry::ArrowType::Open))
        return set_error(error_reason, "Annotation style is out of range");
    return true;
}

std::string serialize_detail_view_spec(const DetailViewObject& dv) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(std::numeric_limits<double>::max_digits10);
    ss << "detail_view:v1"
       << ";m_id=" << dv.marker_id
       << ";l_id=" << dv.label_id
       << ";p_id=" << dv.source_page_id
       << ";sx=" << dv.source_region.x
       << ";sy=" << dv.source_region.y
       << ";sw=" << dv.source_region.width
       << ";sh=" << dv.source_region.height
       << ";fx=" << dv.frame_rect.x
       << ";fy=" << dv.frame_rect.y
       << ";fw=" << dv.frame_rect.width
       << ";fh=" << dv.frame_rect.height
       << ";scale=" << dv.scale_ratio
       << ";shape=" << static_cast<int>(dv.shape)
       << ";conn=" << (dv.show_connector ? 1 : 0)
       << ";l_w=" << dv.line_width_mm
       << ";t_sz=" << dv.text_size_mm
       << ";lbl=" << encode_field(dv.label);
    return ss.str();
}

std::optional<DetailViewObject> deserialize_detail_view_spec(
    std::string_view spec, std::string* error_reason) {
    if (error_reason) error_reason->clear();
    const auto fail = [&](const char* reason) -> std::optional<DetailViewObject> {
        if (error_reason) *error_reason = reason;
        return std::nullopt;
    };
    SpecFields kv;
    if (!parse_spec_fields(spec, "detail_view:v1", kv, error_reason))
        return std::nullopt;
    static const std::unordered_set<std::string> allowed{
        "m_id", "l_id", "p_id", "sx", "sy", "sw", "sh", "fx", "fy",
        "fw", "fh", "scale", "shape", "conn", "l_w", "t_sz", "lbl"};
    if (!has_only_fields(kv, allowed, error_reason)) return std::nullopt;
    DetailViewObject dv;
    int shape = 0;
    if (!parse_integer(kv, "m_id", dv.marker_id, error_reason) ||
        !parse_integer(kv, "l_id", dv.label_id, error_reason) ||
        !parse_integer(kv, "p_id", dv.source_page_id, error_reason) ||
        !parse_double(kv, "sx", dv.source_region.x, error_reason) ||
        !parse_double(kv, "sy", dv.source_region.y, error_reason) ||
        !parse_double(kv, "sw", dv.source_region.width, error_reason) ||
        !parse_double(kv, "sh", dv.source_region.height, error_reason) ||
        !parse_double(kv, "fx", dv.frame_rect.x, error_reason) ||
        !parse_double(kv, "fy", dv.frame_rect.y, error_reason) ||
        !parse_double(kv, "fw", dv.frame_rect.width, error_reason) ||
        !parse_double(kv, "fh", dv.frame_rect.height, error_reason) ||
        !parse_double(kv, "scale", dv.scale_ratio, error_reason) ||
        !parse_integer(kv, "shape", shape, error_reason) ||
        !parse_bool(kv, "conn", dv.show_connector, error_reason) ||
        !parse_double(kv, "l_w", dv.line_width_mm, error_reason) ||
        !parse_double(kv, "t_sz", dv.text_size_mm, error_reason))
        return std::nullopt;
    if (shape < 0 || shape > static_cast<int>(DetailViewShape::Rectangle))
        return fail("Invalid detail view shape");
    dv.shape = static_cast<DetailViewShape>(shape);
    const auto label_it = kv.find("lbl");
    if (label_it == kv.end() ||
        !decode_field(label_it->second, dv.label, kMaxLabelBytes, error_reason))
        return fail("Missing or invalid detail view label");
    std::string validation_error;
    if (!validate_detail_view_object(dv, &validation_error))
        return fail(validation_error.c_str());
    return dv;
}

bool validate_detail_view_object(const DetailViewObject& dv,
                                 std::string* error_reason) {
    if (error_reason) error_reason->clear();
    if (dv.source_page_id == 0)
        return set_error(error_reason, "Detail view source page ID is invalid");
    if (!finite_rect(dv.source_region) || !finite_rect(dv.frame_rect))
        return set_error(error_reason, "Detail view region or frame is invalid");
    if (!std::isfinite(dv.scale_ratio) || dv.scale_ratio < 0.1 ||
        dv.scale_ratio > 50.0)
        return set_error(error_reason, "Detail view scale must be between 0.1 and 50");
    if (dv.shape != DetailViewShape::Circle &&
        dv.shape != DetailViewShape::Rectangle)
        return set_error(error_reason, "Invalid detail view shape");
    if (dv.shape == DetailViewShape::Circle &&
        (!nearly_equal(dv.source_region.width, dv.source_region.height) ||
         !nearly_equal(dv.frame_rect.width, dv.frame_rect.height)))
        return set_error(error_reason, "Circle detail view regions must remain circular");
    if (!nearly_equal(dv.frame_rect.width, dv.source_region.width * dv.scale_ratio) ||
        !nearly_equal(dv.frame_rect.height, dv.source_region.height * dv.scale_ratio))
        return set_error(error_reason, "Detail view frame does not match its uniform scale");
    if (dv.label.empty() || dv.label.size() > kMaxLabelBytes ||
        !valid_utf8_text(dv.label) ||
        !std::isfinite(dv.line_width_mm) || dv.line_width_mm <= 0.0 ||
        dv.line_width_mm > 1000.0 || !std::isfinite(dv.text_size_mm) ||
        dv.text_size_mm <= 0.0 || dv.text_size_mm > 1000.0)
        return set_error(error_reason, "Detail view style or label is invalid");
    return true;
}

std::optional<double> parse_detail_scale_ratio(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    const auto parse_value = [](std::string_view part) -> std::optional<double> {
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.remove_prefix(1);
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.remove_suffix(1);
        std::string normalized(part);
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        double value = 0.0;
        const auto parsed = std::from_chars(normalized.data(), normalized.data() + normalized.size(),
                                            value, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != normalized.data() + normalized.size() ||
            !std::isfinite(value) || value <= 0.0) return std::nullopt;
        return value;
    };
    const auto colon = text.find(':');
    const auto numerator = parse_value(text.substr(0, colon));
    if (!numerator) return std::nullopt;
    double ratio = *numerator;
    if (colon != std::string_view::npos) {
        if (text.find(':', colon + 1) != std::string_view::npos) return std::nullopt;
        const auto denominator = parse_value(text.substr(colon + 1));
        if (!denominator) return std::nullopt;
        ratio /= *denominator;
    }
    if (!std::isfinite(ratio) || ratio < 0.1 || ratio > 50.0) return std::nullopt;
    return ratio;
}

std::string format_detail_scale_ratio(double ratio) {
    if (!std::isfinite(ratio) || ratio <= 0.0) return "—";
    const bool reduction = ratio < 1.0;
    const double value = reduction ? 1.0 / ratio : ratio;
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::setprecision(6) << value;
    return reduction ? "1:" + ss.str() : ss.str() + ":1";
}

AnnotationLayout evaluate_annotation_layout(const AnnotationObject& ann) {
    AnnotationLayout layout;
    layout.formatted_text = ann.text;
    layout.text_size_mm = ann.style.text_size_mm;

    double min_x = ann.anchor.anchor_pt.x;
    double max_x = ann.anchor.anchor_pt.x;
    double min_y = ann.anchor.anchor_pt.y;
    double max_y = ann.anchor.anchor_pt.y;

    const auto update_bounds = [&](double x, double y) {
        min_x = (std::min)(min_x, x);
        max_x = (std::max)(max_x, x);
        min_y = (std::min)(min_y, y);
        max_y = (std::max)(max_y, y);
    };

    if (ann.kind == AnnotationKind::PlainNote) {
        layout.text_origin = ann.text_point;
        const double approx_char_w = ann.style.text_size_mm * 0.6;
        const double approx_line_h = ann.style.text_size_mm * 1.3;
        
        // Count lines and max line length
        size_t max_line_len = 0;
        size_t line_count = 1;
        size_t curr_len = 0;
        for (char c : ann.text) {
            if (c == '\n') {
                line_count++;
                curr_len = 0;
            } else {
                curr_len++;
                max_line_len = (std::max)(max_line_len, curr_len);
            }
        }
        if (max_line_len == 0) max_line_len = ann.text.size();
        const double tw = (std::max)(10.0, max_line_len * approx_char_w);
        const double th = (std::max)(ann.style.text_size_mm, line_count * approx_line_h);

        layout.text_box = geometry::Rect2D{ann.text_point.x, ann.text_point.y, tw, th};
        layout.bounds = layout.text_box;
        return layout;
    }

    // Leader note
    std::vector<geometry::Point2D> pts;
    pts.push_back(ann.anchor.anchor_pt);
    for (const auto& p : ann.leader_points) {
        pts.push_back(p);
    }

    // Arrowhead at anchor pointing in direction of first line segment
    if (pts.size() >= 2) {
        const auto p0 = pts[0];
        const auto p1 = pts[1];
        const double dx = p1.x - p0.x;
        const double dy = p1.y - p0.y;
        const double len = std::hypot(dx, dy);
        geometry::DimensionArrow arrow;
        arrow.tip = p0;
        arrow.direction = (len > 1e-9) ? geometry::Point2D{-dx / len, -dy / len} : geometry::Point2D{1.0, 0.0};
        layout.arrow_path = geometry::make_arrow_path(arrow, ann.style.arrow_type, ann.style.arrow_size_mm);
    }

    // Leader path
    layout.leader_path.move_to(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) {
        layout.leader_path.line_to(pts[i]);
        update_bounds(pts[i].x, pts[i].y);
    }

    // Landing bar
    geometry::Point2D landing_end = pts.back();
    if (ann.style.has_landing) {
        // Landing extends horizontally towards text
        const double dir = (ann.text_point.x >= pts.back().x) ? 1.0 : -1.0;
        landing_end.x += dir * ann.style.landing_length_mm;
        layout.leader_path.line_to(landing_end);
        update_bounds(landing_end.x, landing_end.y);
    }

    // Text position
    const double approx_char_w = ann.style.text_size_mm * 0.6;
    const double approx_line_h = ann.style.text_size_mm * 1.3;
    size_t max_line_len = 0;
    size_t line_count = 1;
    size_t curr_len = 0;
    for (char c : ann.text) {
        if (c == '\n') {
            line_count++;
            curr_len = 0;
        } else {
            curr_len++;
            max_line_len = (std::max)(max_line_len, curr_len);
        }
    }
    if (max_line_len == 0) max_line_len = ann.text.size();
    const double tw = (std::max)(5.0, max_line_len * approx_char_w);
    const double th = (std::max)(ann.style.text_size_mm, line_count * approx_line_h);

    const double gap = ann.style.gap_mm;
    if (ann.text_point.x >= pts.back().x) {
        layout.text_origin = geometry::Point2D{landing_end.x + gap, landing_end.y - th * 0.5};
    } else {
        layout.text_origin = geometry::Point2D{landing_end.x - gap - tw, landing_end.y - th * 0.5};
    }
    layout.text_box = geometry::Rect2D{layout.text_origin.x, layout.text_origin.y, tw, th};

    update_bounds(layout.text_box.x, layout.text_box.y);
    update_bounds(layout.text_box.right(), layout.text_box.bottom());

    constexpr double kMargin = 2.0;
    layout.bounds = geometry::Rect2D{
        min_x - kMargin, min_y - kMargin,
        (max_x - min_x) + 2.0 * kMargin,
        (max_y - min_y) + 2.0 * kMargin};

    return layout;
}

DetailViewLayout evaluate_detail_view_layout(const DetailViewObject& dv) {
    DetailViewLayout layout;

    // Formatted label e.g. "DETAIL A · 2:1" or "DETAIL A · 3:1"
    layout.formatted_label = dv.label + " · " + format_detail_scale_ratio(dv.scale_ratio);

    // Source marker geometry
    if (dv.shape == DetailViewShape::Circle) {
        const double r = (std::min)(dv.source_region.width, dv.source_region.height) * 0.5;
        const double cx = dv.source_region.x + dv.source_region.width * 0.5;
        const double cy = dv.source_region.y + dv.source_region.height * 0.5;
        layout.marker_path = geometry::Path2D::make_ellipse(cx, cy, r, r);
    } else {
        layout.marker_path = geometry::Path2D::make_rect(
            dv.source_region.x, dv.source_region.y, dv.source_region.width, dv.source_region.height);
    }

    // Target frame geometry
    if (dv.shape == DetailViewShape::Circle) {
        const double r = (std::min)(dv.frame_rect.width, dv.frame_rect.height) * 0.5;
        const double cx = dv.frame_rect.x + dv.frame_rect.width * 0.5;
        const double cy = dv.frame_rect.y + dv.frame_rect.height * 0.5;
        layout.frame_path = geometry::Path2D::make_ellipse(cx, cy, r, r);
    } else {
        layout.frame_path = geometry::Path2D::make_rect(
            dv.frame_rect.x, dv.frame_rect.y, dv.frame_rect.width, dv.frame_rect.height);
    }

    // Connector line from marker edge to frame edge
    if (dv.show_connector) {
        const double scx = dv.source_region.x + dv.source_region.width * 0.5;
        const double scy = dv.source_region.y + dv.source_region.height * 0.5;
        const double fcx = dv.frame_rect.x + dv.frame_rect.width * 0.5;
        const double fcy = dv.frame_rect.y + dv.frame_rect.height * 0.5;

        const double dx = fcx - scx;
        const double dy = fcy - scy;
        const double dist = std::hypot(dx, dy);

        if (dist > 1e-4) {
            const double ux = dx / dist;
            const double uy = dy / dist;
            const auto ray_to_edge = [ux, uy](double width, double height, DetailViewShape shape) {
                if (shape == DetailViewShape::Circle) return width * 0.5;
                const double hx = width * 0.5;
                const double hy = height * 0.5;
                const double tx = std::abs(ux) > 1e-12 ? hx / std::abs(ux)
                                                       : std::numeric_limits<double>::infinity();
                const double ty = std::abs(uy) > 1e-12 ? hy / std::abs(uy)
                                                       : std::numeric_limits<double>::infinity();
                return (std::min)(tx, ty);
            };
            const double sr = ray_to_edge(dv.source_region.width, dv.source_region.height, dv.shape);
            const double fr = ray_to_edge(dv.frame_rect.width, dv.frame_rect.height, dv.shape);
            if (std::isfinite(sr) && std::isfinite(fr) && sr + fr < dist) {
                const double p1_x = scx + ux * sr;
                const double p1_y = scy + uy * sr;
                const double p2_x = fcx - ux * fr;
                const double p2_y = fcy - uy * fr;
                layout.connector_path.move_to(geometry::Point2D{p1_x, p1_y});
                layout.connector_path.line_to(geometry::Point2D{p2_x, p2_y});
            }
        }
    }

    // Label position centered below target frame
    const double approx_char_w = dv.text_size_mm * 0.6;
    const double label_w = layout.formatted_label.size() * approx_char_w;
    const double label_h = dv.text_size_mm * 1.4;

    const double fcx = dv.frame_rect.x + dv.frame_rect.width * 0.5;
    const double f_bottom = dv.frame_rect.bottom();
    const double label_x = fcx - label_w * 0.5;
    const double label_y = f_bottom + 3.0; // 3 mm margin below frame

    layout.label_origin = geometry::Point2D{label_x, label_y};
    layout.label_box = geometry::Rect2D{label_x, label_y, label_w, label_h};

    // Source to frame transform: maps source_region center to frame_rect center with scale_ratio
    const double scx = dv.source_region.x + dv.source_region.width * 0.5;
    const double scy = dv.source_region.y + dv.source_region.height * 0.5;
    const double fcx_target = dv.frame_rect.x + dv.frame_rect.width * 0.5;
    const double fcy_target = dv.frame_rect.y + dv.frame_rect.height * 0.5;

    layout.source_to_frame_transform =
        geometry::Transform2D::translation(fcx_target, fcy_target) *
        geometry::Transform2D::scale(dv.scale_ratio, dv.scale_ratio) *
        geometry::Transform2D::translation(-scx, -scy);

    // Total bounding box enclosing source, frame, connector, and label
    const double min_x = (std::min)({dv.source_region.x, dv.frame_rect.x, layout.label_box.x});
    const double max_x = (std::max)({dv.source_region.right(), dv.frame_rect.right(), layout.label_box.right()});
    const double min_y = (std::min)({dv.source_region.y, dv.frame_rect.y, layout.label_box.y});
    const double max_y = (std::max)({dv.source_region.bottom(), dv.frame_rect.bottom(), layout.label_box.bottom()});

    constexpr double kMargin = 2.0;
    layout.bounds = geometry::Rect2D{
        min_x - kMargin, min_y - kMargin,
        (max_x - min_x) + 2.0 * kMargin,
        (max_y - min_y) + 2.0 * kMargin};

    return layout;
}

PathObject annotation_to_path_object(
    const AnnotationObject& ann, const AnnotationLayout& layout) {
    PathObject obj;
    obj.id = ann.id;
    obj.name = ann.name;
    obj.transform = ann.transform;
    obj.stroke = ann.style.color;
    obj.stroke_width_mm = ann.style.line_width_mm > 0.0 ? ann.style.line_width_mm : 0.25;
    obj.fill = ann.style.color;
    obj.has_stroke = true;
    obj.has_fill = false;
    obj.visible = ann.visible;
    obj.locked = ann.locked;
    obj.contour_spec = serialize_annotation_spec(ann);

    // Keep the path-backed adapter structurally renderable for a plain note;
    // text itself is rendered from the persisted annotation specification.
    if (ann.kind == AnnotationKind::PlainNote)
        obj.path.move_to(layout.text_origin);

    // Append leader lines
    for (const auto& el : layout.leader_path.elements()) {
        if (el.verb == geometry::PathVerb::MoveTo) {
            obj.path.move_to(el.pt);
        } else if (el.verb == geometry::PathVerb::LineTo) {
            obj.path.line_to(el.pt);
        } else if (el.verb == geometry::PathVerb::Close) {
            obj.path.close();
        }
    }

    return obj;
}

PathObject detail_view_to_path_object(
    const DetailViewObject& dv, const DetailViewLayout& layout) {
    PathObject obj;
    obj.id = dv.id;
    obj.name = dv.name;
    obj.transform = dv.transform;
    obj.stroke = dv.color;
    obj.stroke_width_mm = dv.line_width_mm > 0.0 ? dv.line_width_mm : 0.25;
    obj.fill = dv.color;
    obj.has_stroke = true;
    obj.has_fill = false;
    obj.visible = dv.visible;
    obj.locked = dv.locked;
    obj.contour_spec = serialize_detail_view_spec(dv);

    // Append marker path
    for (const auto& el : layout.marker_path.elements()) {
        if (el.verb == geometry::PathVerb::MoveTo) {
            obj.path.move_to(el.pt);
        } else if (el.verb == geometry::PathVerb::LineTo) {
            obj.path.line_to(el.pt);
        } else if (el.verb == geometry::PathVerb::ArcTo) {
            obj.path.arc_to(el.rx, el.ry, el.rotation_deg, el.large_arc, el.sweep, el.pt.x, el.pt.y);
        } else if (el.verb == geometry::PathVerb::Close) {
            obj.path.close();
        }
    }

    // Append frame path
    for (const auto& el : layout.frame_path.elements()) {
        if (el.verb == geometry::PathVerb::MoveTo) {
            obj.path.move_to(el.pt);
        } else if (el.verb == geometry::PathVerb::LineTo) {
            obj.path.line_to(el.pt);
        } else if (el.verb == geometry::PathVerb::ArcTo) {
            obj.path.arc_to(el.rx, el.ry, el.rotation_deg, el.large_arc, el.sweep, el.pt.x, el.pt.y);
        } else if (el.verb == geometry::PathVerb::Close) {
            obj.path.close();
        }
    }

    // Append connector path
    for (const auto& el : layout.connector_path.elements()) {
        if (el.verb == geometry::PathVerb::MoveTo) {
            obj.path.move_to(el.pt);
        } else if (el.verb == geometry::PathVerb::LineTo) {
            obj.path.line_to(el.pt);
        } else if (el.verb == geometry::PathVerb::Close) {
            obj.path.close();
        }
    }

    return obj;
}

} // namespace pikdraw::document
