#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/geometry/dimensions/dimension_geometry.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace pikdraw::document {

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

    // Escape text (convert semicolons to %3B, newlines to %0A)
    std::string escaped_text;
    for (char c : ann.text) {
        if (c == ';') escaped_text += "%3B";
        else if (c == '\n') escaped_text += "%0A";
        else if (c == '%') escaped_text += "%25";
        else escaped_text += c;
    }
    ss << ";text=" << escaped_text;
    return ss.str();
}

std::optional<AnnotationObject> deserialize_annotation_spec(
    std::string_view spec, std::string* error_reason) {
    if (error_reason) error_reason->clear();
    const auto fail = [&](const char* reason) -> std::optional<AnnotationObject> {
        if (error_reason) *error_reason = reason;
        return std::nullopt;
    };
    if (spec != "annotation:v1" && !spec.starts_with("annotation:v1;")) {
        return fail("Unsupported annotation specification version");
    }

    std::unordered_map<std::string, std::string> kv;
    size_t start = 0;
    while (start < spec.size()) {
        auto next = spec.find(';', start);
        if (next == std::string_view::npos) next = spec.size();
        auto token = spec.substr(start, next - start);
        auto eq = token.find('=');
        if (eq != std::string_view::npos) {
            kv.emplace(std::string(token.substr(0, eq)), std::string(token.substr(eq + 1)));
        }
        start = next + 1;
    }

    auto get_int = [&](const std::string& k, int def) -> int {
        auto it = kv.find(k);
        if (it != kv.end()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return def;
    };
    auto get_double = [&](const std::string& k, double def) -> double {
        auto it = kv.find(k);
        if (it != kv.end()) {
            try { return std::stod(it->second); } catch (...) {}
        }
        return def;
    };

    AnnotationObject ann;
    ann.kind = static_cast<AnnotationKind>(get_int("kind", 0));
    ann.anchor.source_object_id = static_cast<ObjectId>(get_int("anc_obj", 0));
    ann.anchor.anchor_pt.x = get_double("anc_x", 0.0);
    ann.anchor.anchor_pt.y = get_double("anc_y", 0.0);
    ann.anchor.state = static_cast<AssociationState>(get_int("anc_st", 3));
    if (get_int("anc_has_el", 0) != 0) {
        ann.anchor.element_id = static_cast<uint64_t>(get_int("anc_el", 0));
    }
    ann.anchor.parameter = get_double("anc_param", 0.0);
    ann.landing_point.x = get_double("land_x", 0.0);
    ann.landing_point.y = get_double("land_y", 0.0);
    ann.text_point.x = get_double("txt_x", 0.0);
    ann.text_point.y = get_double("txt_y", 0.0);

    const int pts_count = get_int("pts_cnt", 0);
    for (int i = 0; i < pts_count; ++i) {
        const std::string prefix = "pt" + std::to_string(i);
        geometry::Point2D pt{get_double(prefix + "_x", 0.0), get_double(prefix + "_y", 0.0)};
        ann.leader_points.push_back(pt);
    }

    ann.style.text_size_mm = get_double("t_sz", 2.5);
    ann.style.line_width_mm = get_double("l_w", 0.25);
    ann.style.arrow_size_mm = get_double("arr_sz", 2.0);
    ann.style.arrow_type = static_cast<geometry::ArrowType>(get_int("arr_t", 0));
    ann.style.has_landing = (get_int("has_land", 1) != 0);
    ann.style.landing_length_mm = get_double("land_len", 4.0);
    ann.style.gap_mm = get_double("gap", 1.0);

    auto text_it = kv.find("text");
    if (text_it != kv.end()) {
        std::string unescaped;
        const std::string& raw = text_it->second;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '%' && i + 2 < raw.size()) {
                if (raw.substr(i, 3) == "%3B") { unescaped += ';'; i += 2; continue; }
                if (raw.substr(i, 3) == "%0A") { unescaped += '\n'; i += 2; continue; }
                if (raw.substr(i, 3) == "%25") { unescaped += '%'; i += 2; continue; }
            }
            unescaped += raw[i];
        }
        ann.text = unescaped;
    }

    return ann;
}

std::string serialize_detail_view_spec(const DetailViewObject& dv) {
    std::ostringstream ss;
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
       << ";lbl=" << dv.label;
    return ss.str();
}

std::optional<DetailViewObject> deserialize_detail_view_spec(
    std::string_view spec, std::string* error_reason) {
    if (error_reason) error_reason->clear();
    const auto fail = [&](const char* reason) -> std::optional<DetailViewObject> {
        if (error_reason) *error_reason = reason;
        return std::nullopt;
    };
    if (spec != "detail_view:v1" && !spec.starts_with("detail_view:v1;")) {
        return fail("Unsupported detail view specification version");
    }

    std::unordered_map<std::string, std::string> kv;
    size_t start = 0;
    while (start < spec.size()) {
        auto next = spec.find(';', start);
        if (next == std::string_view::npos) next = spec.size();
        auto token = spec.substr(start, next - start);
        auto eq = token.find('=');
        if (eq != std::string_view::npos) {
            kv.emplace(std::string(token.substr(0, eq)), std::string(token.substr(eq + 1)));
        }
        start = next + 1;
    }

    auto get_int = [&](const std::string& k, int def) -> int {
        auto it = kv.find(k);
        if (it != kv.end()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return def;
    };
    auto get_double = [&](const std::string& k, double def) -> double {
        auto it = kv.find(k);
        if (it != kv.end()) {
            try { return std::stod(it->second); } catch (...) {}
        }
        return def;
    };

    DetailViewObject dv;
    dv.marker_id = static_cast<ObjectId>(get_int("m_id", 0));
    dv.label_id = static_cast<ObjectId>(get_int("l_id", 0));
    dv.source_page_id = static_cast<uint64_t>(get_int("p_id", 1));
    dv.source_region.x = get_double("sx", 0.0);
    dv.source_region.y = get_double("sy", 0.0);
    dv.source_region.width = get_double("sw", 20.0);
    dv.source_region.height = get_double("sh", 20.0);
    dv.frame_rect.x = get_double("fx", 40.0);
    dv.frame_rect.y = get_double("fy", 40.0);
    dv.frame_rect.width = get_double("fw", 60.0);
    dv.frame_rect.height = get_double("fh", 60.0);
    dv.scale_ratio = get_double("scale", 2.0);
    dv.shape = static_cast<DetailViewShape>(get_int("shape", 0));
    dv.show_connector = (get_int("conn", 1) != 0);
    dv.line_width_mm = get_double("l_w", 0.25);
    dv.text_size_mm = get_double("t_sz", 2.5);

    auto lbl_it = kv.find("lbl");
    if (lbl_it != kv.end()) {
        dv.label = lbl_it->second;
    }

    return dv;
}

AnnotationRegistry& AnnotationRegistry::instance() {
    static AnnotationRegistry s_instance;
    return s_instance;
}

void AnnotationRegistry::set(ObjectId id, AnnotationObject ann) {
    annotations_[id] = std::move(ann);
}

void AnnotationRegistry::remove(ObjectId id) {
    annotations_.erase(id);
}

std::optional<AnnotationObject> AnnotationRegistry::get(ObjectId id) const {
    const auto it = annotations_.find(id);
    if (it != annotations_.end()) return it->second;
    return std::nullopt;
}

bool AnnotationRegistry::contains(ObjectId id) const {
    return annotations_.contains(id);
}

void AnnotationRegistry::clear() {
    annotations_.clear();
}

DetailViewRegistry& DetailViewRegistry::instance() {
    static DetailViewRegistry s_instance;
    return s_instance;
}

void DetailViewRegistry::set(ObjectId id, DetailViewObject dv) {
    views_[id] = std::move(dv);
}

void DetailViewRegistry::remove(ObjectId id) {
    views_.erase(id);
}

std::optional<DetailViewObject> DetailViewRegistry::get(ObjectId id) const {
    const auto it = views_.find(id);
    if (it != views_.end()) return it->second;
    return std::nullopt;
}

bool DetailViewRegistry::contains(ObjectId id) const {
    return views_.contains(id);
}

void DetailViewRegistry::clear() {
    views_.clear();
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
    const int int_scale = static_cast<int>(std::round(dv.scale_ratio));
    if (std::abs(dv.scale_ratio - int_scale) < 1e-4) {
        layout.formatted_label = dv.label + " · " + std::to_string(int_scale) + ":1";
    } else {
        std::ostringstream ss;
        ss.precision(1);
        ss << std::fixed << dv.scale_ratio;
        layout.formatted_label = dv.label + " · " + ss.str() + ":1";
    }

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
            const double sr = (std::min)(dv.source_region.width, dv.source_region.height) * 0.5;
            const double fr = (std::min)(dv.frame_rect.width, dv.frame_rect.height) * 0.5;

            const double p1_x = scx + ux * sr;
            const double p1_y = scy + uy * sr;
            const double p2_x = fcx - ux * fr;
            const double p2_y = fcy - uy * fr;

            layout.connector_path.move_to(geometry::Point2D{p1_x, p1_y});
            layout.connector_path.line_to(geometry::Point2D{p2_x, p2_y});
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

    // Append arrow elements
    for (const auto& el : layout.arrow_path.elements()) {
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
