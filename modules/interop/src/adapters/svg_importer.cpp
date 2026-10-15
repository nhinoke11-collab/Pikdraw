#include "pikdraw/interop/adapters/svg_importer.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>

namespace pikdraw::interop {

namespace {

std::string get_attr(const std::string& tag, const std::string& attr_name) {
    std::regex re(attr_name + R"(\s*=\s*["']([^"']*)["'])");
    std::smatch match;
    if (std::regex_search(tag, match, re) && match.size() > 1) {
        return match.str(1);
    }
    return "";
}

double parse_double(const std::string& s, double def = 0.0) {
    if (s.empty()) return def;
    try {
        size_t idx = 0;
        double val = std::stod(s, &idx);
        return val;
    } catch (...) {
        return def;
    }
}

color::ColorValue parse_color(const std::string& color_str, color::ColorValue def) {
    if (color_str.empty()) return def;
    if (color_str == "none") return color::RGBColor{0, 0, 0, 0};
    if (color_str.front() == '#' && (color_str.length() == 7 || color_str.length() == 4)) {
        if (color_str.length() == 7) {
            int r = std::stoi(color_str.substr(1, 2), nullptr, 16);
            int g = std::stoi(color_str.substr(3, 2), nullptr, 16);
            int b = std::stoi(color_str.substr(5, 2), nullptr, 16);
            return color::RGBColor{r / 255.0, g / 255.0, b / 255.0, 1.0};
        }
    }
    return def;
}

void parse_svg_path_data(const std::string& d, geometry::Path2D& path) {
    std::stringstream ss(d);
    char cmd;
    double x, y, cx, cy, c1x, c1y, c2x, c2y;
    double cur_x = 0, cur_y = 0;

    while (ss >> cmd) {
        if (cmd == 'M' || cmd == 'm') {
            if (ss >> x >> y) {
                if (cmd == 'm') { x += cur_x; y += cur_y; }
                path.move_to(x, y);
                cur_x = x; cur_y = y;
            }
        } else if (cmd == 'L' || cmd == 'l') {
            if (ss >> x >> y) {
                if (cmd == 'l') { x += cur_x; y += cur_y; }
                path.line_to(x, y);
                cur_x = x; cur_y = y;
            }
        } else if (cmd == 'Q' || cmd == 'q') {
            if (ss >> cx >> cy >> x >> y) {
                if (cmd == 'q') { cx += cur_x; cy += cur_y; x += cur_x; y += cur_y; }
                path.quad_to(cx, cy, x, y);
                cur_x = x; cur_y = y;
            }
        } else if (cmd == 'C' || cmd == 'c') {
            if (ss >> c1x >> c1y >> c2x >> c2y >> x >> y) {
                if (cmd == 'c') { c1x += cur_x; c1y += cur_y; c2x += cur_x; c2y += cur_y; x += cur_x; y += cur_y; }
                path.cubic_to(c1x, c1y, c2x, c2y, x, y);
                cur_x = x; cur_y = y;
            }
        } else if (cmd == 'Z' || cmd == 'z') {
            path.close();
        }
    }
}

} // anonymous namespace

SvgImportResult SvgImporter::import_from_string(const std::string& svg_xml) {
    SvgImportResult res;
    res.page = document::Page::make_default();

    // Check for unsupported tags
    std::vector<std::string> unsupported_tags = {"<filter", "<mask", "<clipPath", "<script", "<foreignObject"};
    for (const auto& tag : unsupported_tags) {
        if (svg_xml.find(tag) != std::string::npos) {
            res.report.add_issue(FidelitySeverity::Unsupported, tag, "Advanced Filter/Mask",
                                 "Feature not supported in M1 SVG subset; rejected without silent flattening");
        }
    }

    // Parse <svg> root dimensions
    std::regex svg_root_re(R"(<svg[^>]*>)");
    std::smatch svg_match;
    if (std::regex_search(svg_xml, svg_match, svg_root_re)) {
        std::string root_tag = svg_match.str();
        double w = parse_double(get_attr(root_tag, "width"), 20000.0);
        double h = parse_double(get_attr(root_tag, "height"), 1000.0);
        res.page.width_mm = w;
        res.page.height_mm = h;
    }

    // Parse <rect ...>
    std::regex rect_re(R"(<rect\s+([^>]*)/?>)");
    auto rect_begin = std::sregex_iterator(svg_xml.begin(), svg_xml.end(), rect_re);
    auto rect_end = std::sregex_iterator();
    for (auto it = rect_begin; it != rect_end; ++it) {
        std::string attrs = it->str(1);
        double x = parse_double(get_attr(attrs, "x"));
        double y = parse_double(get_attr(attrs, "y"));
        double w = parse_double(get_attr(attrs, "width"), 10.0);
        double h = parse_double(get_attr(attrs, "height"), 10.0);

        document::PathObject po;
        po.id = document::generate_object_id();
        po.name = "Imported Rect";
        po.path = geometry::Path2D::make_rect(x, y, w, h);
        po.fill = parse_color(get_attr(attrs, "fill"), color::RGBColor{0.2, 0.4, 0.8, 1.0});
        po.stroke = parse_color(get_attr(attrs, "stroke"), color::RGBColor{0.0, 0.0, 0.0, 1.0});
        po.stroke_width_mm = parse_double(get_attr(attrs, "stroke-width"), 1.0);
        res.objects.push_back(std::move(po));
    }

    // Parse <path ...>
    std::regex path_re(R"(<path\s+([^>]*)/?>)");
    auto path_begin = std::sregex_iterator(svg_xml.begin(), svg_xml.end(), path_re);
    auto path_end = std::sregex_iterator();
    for (auto it = path_begin; it != path_end; ++it) {
        std::string attrs = it->str(1);
        std::string d = get_attr(attrs, "d");
        if (!d.empty()) {
            document::PathObject po;
            po.id = document::generate_object_id();
            po.name = "Imported Path";
            parse_svg_path_data(d, po.path);
            po.fill = parse_color(get_attr(attrs, "fill"), color::RGBColor{0.2, 0.4, 0.8, 1.0});
            po.stroke = parse_color(get_attr(attrs, "stroke"), color::RGBColor{0.0, 0.0, 0.0, 1.0});
            po.stroke_width_mm = parse_double(get_attr(attrs, "stroke-width"), 1.0);
            res.objects.push_back(std::move(po));
        }
    }

    // Parse <text ...>text</text>
    std::regex text_re(R"(<text\s+([^>]*)>([^<]*)</text>)");
    auto text_begin = std::sregex_iterator(svg_xml.begin(), svg_xml.end(), text_re);
    auto text_end = std::sregex_iterator();
    for (auto it = text_begin; it != text_end; ++it) {
        std::string attrs = it->str(1);
        std::string content = it->str(2);

        document::TextObject to;
        to.id = document::generate_object_id();
        to.name = "Imported Text";
        to.text = content;
        double x = parse_double(get_attr(attrs, "x"));
        double y = parse_double(get_attr(attrs, "y"));
        to.position = {x, y};
        std::string family = get_attr(attrs, "font-family");
        if (!family.empty()) to.font.family_name = family;
        double fsize = parse_double(get_attr(attrs, "font-size"), 12.0);
        to.font.size_pt = fsize;
        to.fill = parse_color(get_attr(attrs, "fill"), color::RGBColor{0.9, 0.9, 0.9, 1.0});
        res.objects.push_back(std::move(to));
    }

    res.success = true;
    return res;
}

SvgImportResult SvgImporter::import_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        SvgImportResult res;
        res.success = false;
        res.error_message = "Cannot open SVG file: " + path;
        return res;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return import_from_string(buffer.str());
}

} // namespace pikdraw::interop
