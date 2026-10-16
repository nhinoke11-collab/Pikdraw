#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <limits>

namespace pikdraw::interop {

namespace {

std::string color_to_hex(const color::ColorValue& val) {
    auto dev = color::to_preview_rgba(val).to_device();
    std::stringstream ss;
    ss << "#"
       << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(dev.r)
       << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(dev.g)
       << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(dev.b);
    return ss.str();
}

std::string format_path_d(const geometry::Path2D& path) {
    std::stringstream ss;
    for (const auto& el : path.elements()) {
        if (el.verb == geometry::PathVerb::MoveTo) {
            ss << "M " << el.pt.x << " " << el.pt.y << " ";
        } else if (el.verb == geometry::PathVerb::LineTo) {
            ss << "L " << el.pt.x << " " << el.pt.y << " ";
        } else if (el.verb == geometry::PathVerb::QuadTo) {
            ss << "Q " << el.ctrl1.x << " " << el.ctrl1.y << " " << el.pt.x << " " << el.pt.y << " ";
        } else if (el.verb == geometry::PathVerb::CubicTo) {
            ss << "C " << el.ctrl1.x << " " << el.ctrl1.y << " "
               << el.ctrl2.x << " " << el.ctrl2.y << " "
               << el.pt.x << " " << el.pt.y << " ";
        } else if (el.verb == geometry::PathVerb::Close) {
            ss << "Z ";
        }
    }
    return ss.str();
}

} // anonymous namespace

std::string SvgExporter::export_to_string(
    const document::DocumentSnapshot& snapshot,
    const SvgExportOptions& options) {
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (options.include_xml_header) {
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    }

    const auto& page = snapshot.page();
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
       << "width=\"" << page.width_mm << "mm\" "
       << "height=\"" << page.height_mm << "mm\" "
       << "viewBox=\"0 0 " << page.width_mm << " " << page.height_mm << "\">\n";

    for (const auto& [id, obj_var] : snapshot.objects().all()) {
        std::visit([&](const auto& obj) {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                if (!obj.visible) return;
                ss << "  <path d=\"" << format_path_d(obj.path) << "\" ";
                if (obj.has_fill) {
                    ss << "fill=\"" << color_to_hex(obj.fill) << "\" ";
                } else {
                    ss << "fill=\"none\" ";
                }
                if (obj.has_stroke) {
                    ss << "stroke=\"" << color_to_hex(obj.stroke) << "\" "
                       << "stroke-width=\"" << obj.stroke_width_mm << "\" ";
                }
                if (obj.transform.a != 1.0 || obj.transform.d != 1.0 ||
                    obj.transform.tx != 0.0 || obj.transform.ty != 0.0) {
                    ss << "transform=\"matrix("
                       << obj.transform.a << " " << obj.transform.b << " "
                       << obj.transform.c << " " << obj.transform.d << " "
                       << obj.transform.tx << " " << obj.transform.ty << ")\" ";
                }
                ss << "/>\n";
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                if (!obj.visible) return;
                ss << "  <text x=\"" << obj.position.x << "\" y=\"" << obj.position.y << "\" "
                   << "font-family=\"" << obj.font.family_name << "\" "
                   // The exported viewBox uses millimeters as user units.
                   << "font-size=\"" << obj.font.size_pt * 25.4 / 72.0 << "\" "
                   << "fill=\"" << color_to_hex(obj.fill) << "\">"
                   << obj.text << "</text>\n";
            }
        }, obj_var);
    }

    ss << "</svg>\n";
    return ss.str();
}

bool SvgExporter::export_to_file(
    const std::string& path,
    const document::DocumentSnapshot& snapshot,
    const SvgExportOptions& options) {
    std::ofstream out(path);
    if (!out) return false;
    out << export_to_string(snapshot, options);
    return out.good();
}

} // namespace pikdraw::interop
