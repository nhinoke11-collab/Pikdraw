#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "../path_adapter/svg_path_adapter.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

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
  return std::visit([](const auto &source) {
    using T = std::decay_t<decltype(source)>;
    if constexpr (std::is_same_v<T, color::SpotColor>)
      return source.alternate_rgb.a;
    else
      return source.a;
  }, value.payload());
}

bool is_unit_opacity(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

void report_unsupported_appearance(SvgExportResult &result,
                                   document::ObjectId id,
                                   const document::Appearance &appearance) {
  const auto element = "object " + std::to_string(id);
  if (appearance.schema_version != document::kAppearanceSchemaVersion ||
      appearance.schema_minor != document::kAppearanceSchemaMinor) {
    result.report.add_issue(FidelitySeverity::Unsupported, element,
                            "Appearance schema",
                            "SVG export cannot preserve an unknown appearance schema");
  }
  if (!is_unit_opacity(appearance.object_opacity) ||
      !is_unit_opacity(appearance.fill_opacity) ||
      !is_unit_opacity(appearance.stroke_opacity)) {
    result.report.add_issue(FidelitySeverity::Unsupported, element,
                            "Opacity",
                            "SVG opacity values must be finite and within [0, 1]");
  }
  if (appearance.blend_mode != document::BlendMode::Normal) {
    result.report.add_issue(FidelitySeverity::Unsupported, element,
                            "Object blend mode",
                            "This SVG export subset has no verified mapping for non-Normal PikDraw compositing modes");
  }
  if (!appearance.effects.empty()) {
    result.report.add_issue(FidelitySeverity::Unsupported, element,
                            "Appearance effects",
                            "This SVG export subset has no verified mapping for PikDraw effect nodes, including disabled nodes");
  }
}

void preflight_appearance_export(
    const document::DocumentSnapshot &snapshot, SvgExportResult &result) {
  for (const auto &[id, object] : snapshot.objects().all()) {
    if (const auto *path = std::get_if<document::PathObject>(&object)) {
      if (path->visible)
        report_unsupported_appearance(result, id, path->appearance);
    } else if (const auto *text = std::get_if<document::TextObject>(&object)) {
      if (text->visible)
        report_unsupported_appearance(result, id, text->appearance);
    }
  }
}

void write_paint_opacity(std::ostringstream &stream, const char *attribute,
                         const color::ColorValue &color,
                         double component_opacity) {
  const double combined = color_alpha(color) * component_opacity;
  if (combined < 1.0)
    stream << ' ' << attribute << "=\"" << combined << '"';
}

void write_object_opacity(std::ostringstream &stream, double opacity) {
  if (opacity < 1.0)
    stream << " opacity=\"" << opacity << '"';
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
  preflight_appearance_export(snapshot, result);
  if (result.report.has_unsupported()) {
    result.error_message =
        "SVG export stopped before output because an appearance value has no verified mapping";
    return result;
  }

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
        write_paint_opacity(stream, "fill-opacity", path->fill,
                            path->appearance.fill_opacity);
        warn_preview_color(result, id, path->fill, "fill color");
      } else {
        stream << " fill=\"none\"";
      }
      if (path->has_stroke) {
        stream << " stroke=\"" << color_to_hex(path->stroke)
               << "\" stroke-width=\"" << path->stroke_width_mm << '"';
        write_paint_opacity(stream, "stroke-opacity", path->stroke,
                            path->appearance.stroke_opacity);
        warn_preview_color(result, id, path->stroke, "stroke color");
      }
      write_object_opacity(stream, path->appearance.object_opacity);
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
      write_paint_opacity(stream, "fill-opacity", text->fill,
                          text->appearance.fill_opacity);
      write_object_opacity(stream, text->appearance.object_opacity);
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
