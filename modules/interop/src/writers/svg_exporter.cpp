#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "../path_adapter/svg_path_adapter.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <utility>

namespace pikdraw::interop {
namespace {
std::string xml_escape(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
    case '&': escaped += "&amp;"; break;
    case '<': escaped += "&lt;"; break;
    case '>': escaped += "&gt;"; break;
    case '"': escaped += "&quot;"; break;
    case '\'': escaped += "&apos;"; break;
    default: escaped.push_back(character); break;
    }
  }
  return escaped;
}

std::string color_to_hex(const color::ColorValue &value) {
  const auto device = color::to_preview_rgba(value).to_device();
  std::ostringstream stream;
  stream << '#' << std::hex << std::setfill('0') << std::setw(2)
         << static_cast<int>(device.r) << std::setw(2)
         << static_cast<int>(device.g) << std::setw(2)
         << static_cast<int>(device.b);
  return stream.str();
}

double color_alpha(const color::ColorValue &value) {
  return color::to_preview_rgba(value).to_device().a / 255.0;
}

bool has_transform(const geometry::Transform2D &value) {
  return value.a != 1.0 || value.b != 0.0 || value.c != 0.0 ||
         value.d != 1.0 || value.tx != 0.0 || value.ty != 0.0;
}

void warn_preview_color(SvgExportResult &result, document::ObjectId id,
                        const color::ColorValue &value,
                        const char *property) {
  if (value.color_space() != color::ColorSpace::Rgb ||
      value.overprint().fill || value.overprint().stroke) {
    result.report.add_issue(
        FidelitySeverity::Warning, std::to_string(id), property,
        "Source separation/overprint was converted to an RGB SVG preview color");
  }
}

void write_transform(std::ostringstream &stream,
                     const geometry::Transform2D &transform) {
  if (!has_transform(transform))
    return;
  stream << " transform=\"matrix(" << transform.a << ' ' << transform.b
         << ' ' << transform.c << ' ' << transform.d << ' ' << transform.tx
         << ' ' << transform.ty << ")\"";
}
} // namespace

SvgExportResult SvgExporter::export_with_report(
    const document::DocumentSnapshot &snapshot,
    const SvgExportOptions &options) {
  SvgExportResult result;
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  if (options.include_xml_header)
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

  const auto &page = snapshot.page();
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
         << page.width_mm << "mm\" height=\"" << page.height_mm
         << "mm\" viewBox=\"0 0 " << page.width_mm << ' ' << page.height_mm
         << "\">\n";
  for (const auto &[id, object] : snapshot.objects().all()) {
    if (const auto *path = std::get_if<document::PathObject>(&object)) {
      if (!path->visible)
        continue;
      std::string issue;
      std::optional<document::EditablePath> migrated;
      const document::EditablePath *source = path->authoring_source.get();
      if (!source) {
        migrated = adapters::SvgPathAdapter::from_path2d(path->path, &issue);
        if (migrated)
          source = &*migrated;
      }
      const auto data = source
                            ? adapters::SvgPathAdapter::to_path_data(*source,
                                                                     &issue)
                            : std::optional<std::string>{};
      if (!data) {
        result.report.add_issue(FidelitySeverity::Unsupported,
                                "object " + std::to_string(id),
                                "Editable path", issue);
        continue;
      }
      stream << "  <path d=\"" << *data << '"';
      if (path->has_fill) {
        stream << " fill=\"" << color_to_hex(path->fill) << '"';
        const auto alpha = color_alpha(path->fill);
        if (alpha < 1.0)
          stream << " fill-opacity=\"" << alpha << '"';
        warn_preview_color(result, id, path->fill, "fill color");
      } else {
        stream << " fill=\"none\"";
      }
      if (path->has_stroke) {
        stream << " stroke=\"" << color_to_hex(path->stroke)
               << "\" stroke-width=\"" << path->stroke_width_mm << '"';
        const auto alpha = color_alpha(path->stroke);
        if (alpha < 1.0)
          stream << " stroke-opacity=\"" << alpha << '"';
        warn_preview_color(result, id, path->stroke, "stroke color");
      }
      write_transform(stream, path->transform);
      stream << "/>\n";
      continue;
    }
    if (const auto *text = std::get_if<document::TextObject>(&object)) {
      if (!text->visible)
        continue;
      stream << "  <text x=\"" << text->position.x << "\" y=\""
             << text->position.y << "\" font-family=\""
             << xml_escape(text->font.family_name) << "\" font-size=\""
             << text->font.size_pt * 25.4 / 72.0 << "\" fill=\"" << color_to_hex(text->fill)
             << '"';
      const auto alpha = color_alpha(text->fill);
      if (alpha < 1.0)
        stream << " fill-opacity=\"" << alpha << '"';
      write_transform(stream, text->transform);
      stream << '>' << xml_escape(text->text) << "</text>\n";
      warn_preview_color(result, id, text->fill, "text color");
      continue;
    }
    result.report.add_issue(FidelitySeverity::Unsupported,
                            "object " + std::to_string(id), "Group object",
                            "SVG group hierarchy and inherited transforms are not exported by this subset");
  }
  if (result.report.has_unsupported()) {
    result.error_message =
        "SVG export stopped because one or more objects cannot be represented without loss";
    return result;
  }
  stream << "</svg>\n";
  result.svg = stream.str();
  result.success = true;
  return result;
}

std::string SvgExporter::export_to_string(
    const document::DocumentSnapshot &snapshot,
    const SvgExportOptions &options) {
  auto result = export_with_report(snapshot, options);
  return result.success ? std::move(result.svg) : std::string{};
}

bool SvgExporter::export_to_file(const std::string &path,
                                 const document::DocumentSnapshot &snapshot,
                                 const SvgExportOptions &options) {
  auto result = export_with_report(snapshot, options);
  if (!result.success)
    return false;
  std::ofstream output(path, std::ios::binary);
  if (!output)
    return false;
  output << result.svg;
  return output.good();
}
} // namespace pikdraw::interop
