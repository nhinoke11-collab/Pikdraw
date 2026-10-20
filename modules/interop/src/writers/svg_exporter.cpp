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
#include <unordered_set>
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

void report_unsupported_appearance(SvgStreamExportResult &result,
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
    const document::DocumentSnapshot &snapshot,
    const std::vector<document::ObjectId> &object_ids,
    SvgStreamExportResult &result) {
  for (const auto id : object_ids) {
    const auto object = snapshot.objects().get(id);
    if (!object) {
      result.report.add_issue(FidelitySeverity::Unsupported,
                              "object " + std::to_string(id), "Object scope",
                              "The selected object is not present on the active page");
      continue;
    }
    if (const auto *path = std::get_if<document::PathObject>(&*object)) {
      if (path->visible)
        report_unsupported_appearance(result, id, path->appearance);
    } else if (const auto *text = std::get_if<document::TextObject>(&*object)) {
      if (text->visible)
        report_unsupported_appearance(result, id, text->appearance);
    }
  }
}

void write_paint_opacity(std::ostream &stream, const char *attribute,
                         const color::ColorValue &color,
                         double component_opacity) {
  const double combined = color_alpha(color) * component_opacity;
  if (combined < 1.0)
    stream << ' ' << attribute << "=\"" << combined << '"';
}

void write_object_opacity(std::ostream &stream, double opacity) {
  if (opacity < 1.0)
    stream << " opacity=\"" << opacity << '"';
}

bool has_transform(const geometry::Transform2D &value) {
  return value.a != 1.0 || value.b != 0.0 || value.c != 0.0 ||
         value.d != 1.0 || value.tx != 0.0 || value.ty != 0.0;
}

void warn_preview_color(SvgStreamExportResult &result, document::ObjectId id,
                        const color::ColorValue &value,
                        const char *property) {
  if (value.color_space() != color::ColorSpace::Rgb ||
      value.overprint().fill || value.overprint().stroke) {
    result.report.add_issue(
        FidelitySeverity::Warning, std::to_string(id), property,
        "Source separation/overprint was converted to an RGB SVG preview color");
  }
}

void write_transform(std::ostream &stream,
                     const geometry::Transform2D &transform) {
  if (!has_transform(transform))
    return;
  stream << " transform=\"matrix(" << transform.a << ' ' << transform.b
         << ' ' << transform.c << ' ' << transform.d << ' ' << transform.tx
         << ' ' << transform.ty << ")\"";
}
void write_path_element(std::ostream &stream, document::ObjectId id,
                        const document::PathObject &path,
                        SvgStreamExportResult &result) {
  if (!path.visible) return;
  std::string issue;
  std::optional<document::EditablePath> migrated;
  const document::EditablePath *source = path.authoring_source.get();
  if (!source) {
    migrated = adapters::SvgPathAdapter::from_path2d(path.path, &issue);
    if (migrated)
      source = &*migrated;
  }
  const auto data = source
                        ? adapters::SvgPathAdapter::to_path_data(*source, &issue)
                        : std::optional<std::string>{};
  if (!data) {
    result.report.add_issue(FidelitySeverity::Unsupported,
                            "object " + std::to_string(id),
                            "Editable path", issue);
    return;
  }
  stream << "  <path d=\"" << *data << '"';
  if (path.has_fill) {
    stream << " fill=\"" << color_to_hex(path.fill) << '"';
    write_paint_opacity(stream, "fill-opacity", path.fill,
                        path.appearance.fill_opacity);
    warn_preview_color(result, id, path.fill, "fill color");
  } else {
    stream << " fill=\"none\"";
  }
  if (path.has_stroke) {
    stream << " stroke=\"" << color_to_hex(path.stroke)
           << "\" stroke-width=\"" << path.stroke_width_mm << '"';
    write_paint_opacity(stream, "stroke-opacity", path.stroke,
                        path.appearance.stroke_opacity);
    warn_preview_color(result, id, path.stroke, "stroke color");
  }
  write_object_opacity(stream, path.appearance.object_opacity);
  write_transform(stream, path.transform);
  stream << "/>\n";
}

void write_text_element(std::ostream &stream, document::ObjectId id,
                        const document::TextObject &text,
                        SvgStreamExportResult &result) {
  if (!text.visible) return;
  stream << "  <text x=\"" << text.position.x << "\" y=\""
         << text.position.y << "\" font-family=\""
         << xml_escape(text.font.family_name) << "\" font-size=\""
         << text.font.size_pt * 25.4 / 72.0 << "\" fill=\"" << color_to_hex(text.fill)
         << '"';
  write_paint_opacity(stream, "fill-opacity", text.fill,
                      text.appearance.fill_opacity);
  write_object_opacity(stream, text.appearance.object_opacity);
  write_transform(stream, text.transform);
  stream << '>' << xml_escape(text.text) << "</text>\n";
  warn_preview_color(result, id, text.fill, "text color");
}
} // namespace

SvgStreamExportResult SvgExporter::write_to_stream(
    const document::DocumentSnapshot &snapshot,
    std::ostream &stream,
    const SvgExportOptions &options,
    const std::vector<document::ObjectId> *included_object_ids) {
  SvgStreamExportResult result;
  std::vector<document::ObjectId> object_ids;
  object_ids.reserve(included_object_ids ? included_object_ids->size()
                                         : snapshot.objects().size());
  if (included_object_ids) {
    object_ids = *included_object_ids;
  } else {
    for (const auto &[id, object] : snapshot.objects().all()) {
      (void)object;
      object_ids.push_back(id);
    }
  }

  std::unordered_set<document::ObjectId> root_ids;
  for (const auto &layer : snapshot.active_page().layout.layers)
    root_ids.insert(layer.object_ids.begin(), layer.object_ids.end());

  std::unordered_set<document::ObjectId> nested_ids;
  for (const auto &[id, object] : snapshot.objects().all()) {
    if (const auto *grp = std::get_if<document::GroupObject>(&object)) {
      for (const auto child_id : grp->children_ids) {
        nested_ids.insert(child_id);
      }
    }
  }

  std::unordered_set<document::ObjectId> unique_ids;
  for (const auto id : object_ids) {
    if (!unique_ids.insert(id).second) {
      result.report.add_issue(FidelitySeverity::Unsupported,
                              "object " + std::to_string(id), "Object scope",
                              "An object ID appears more than once in the export scope");
    } else if (!snapshot.objects().contains(id)) {
      result.report.add_issue(FidelitySeverity::Unsupported,
                              "object " + std::to_string(id), "Object scope",
                              "The selected object is not present on the active page");
    } else if (!root_ids.contains(id) && (!included_object_ids ? !nested_ids.contains(id) : true)) {
      result.report.add_issue(FidelitySeverity::Unsupported,
                              "object " + std::to_string(id), "Group transforms",
                              "A nested object cannot be exported independently until its group transforms are supported");
    }
  }
  if (!result.report.has_unsupported())
    preflight_appearance_export(snapshot, object_ids, result);
  if (result.report.has_unsupported()) {
    result.error_message =
        "SVG export stopped before output because the selected scope or an appearance value has no verified mapping";
    return result;
  }

  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  if (options.include_xml_header)
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

  const auto &page = snapshot.page();
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
         << page.width_mm << "mm\" height=\"" << page.height_mm
         << "mm\" viewBox=\"0 0 " << page.width_mm << ' ' << page.height_mm
         << "\">\n";

  bool has_defs = false;
  for (const auto &[id, object] : snapshot.objects().all()) {
    if (!unique_ids.contains(id)) continue;
    if (const auto *grp = std::get_if<document::GroupObject>(&object)) {
      if (grp->is_clipping_container()) {
        const auto *mask = snapshot.objects().lookup_ref(grp->clip->mask_object_id);
        if (mask) {
          if (const auto *mask_path = std::get_if<document::PathObject>(mask)) {
            std::string issue;
            std::optional<document::EditablePath> migrated;
            const document::EditablePath *source = mask_path->authoring_source.get();
            if (!source) {
              migrated = adapters::SvgPathAdapter::from_path2d(mask_path->path, &issue);
              if (migrated) source = &*migrated;
            }
            const auto data = source ? adapters::SvgPathAdapter::to_path_data(*source, &issue) : std::optional<std::string>{};
            if (data) {
              if (!has_defs) {
                stream << "  <defs>\n";
                has_defs = true;
              }
              stream << "    <clipPath id=\"clip_" << grp->id << "\">\n";
              stream << "      <path d=\"" << *data << '"';
              write_transform(stream, mask_path->transform);
              stream << "/>\n    </clipPath>\n";
            }
          }
        }
      }
    }
  }
  if (has_defs) {
    stream << "  </defs>\n";
  }

  for (const auto &[id, object] : snapshot.objects().all()) {
    if (!unique_ids.contains(id) || nested_ids.contains(id))
      continue;
    if (const auto *path = std::get_if<document::PathObject>(&object)) {
      write_path_element(stream, id, *path, result);
      continue;
    }
    if (const auto *text = std::get_if<document::TextObject>(&object)) {
      write_text_element(stream, id, *text, result);
      continue;
    }
    if (const auto *group = std::get_if<document::GroupObject>(&object)) {
      if (group->is_clipping_container()) {
        const auto *mask = snapshot.objects().lookup_ref(group->clip->mask_object_id);
        const auto *mask_path = mask ? std::get_if<document::PathObject>(mask) : nullptr;
        if (group->clip->preserve_frame_appearance && mask_path && mask_path->has_fill) {
          auto fill_mask = *mask_path;
          fill_mask.has_stroke = false;
          write_path_element(stream, fill_mask.id, fill_mask, result);
        }
        stream << "  <g clip-path=\"url(#clip_" << group->id << ")\"";
        write_transform(stream, group->transform);
        stream << ">\n";
        for (const auto child_id : group->children_ids) {
          if (child_id == group->clip->mask_object_id) continue;
          const auto *child = snapshot.objects().lookup_ref(child_id);
          if (!child) continue;
          if (const auto *cp = std::get_if<document::PathObject>(child)) {
            write_path_element(stream, child_id, *cp, result);
          } else if (const auto *ct = std::get_if<document::TextObject>(child)) {
            write_text_element(stream, child_id, *ct, result);
          }
        }
        stream << "  </g>\n";
        if (group->clip->preserve_frame_appearance && mask_path && mask_path->has_stroke && mask_path->stroke_width_mm > 0.0) {
          auto stroke_mask = *mask_path;
          stroke_mask.has_fill = false;
          write_path_element(stream, stroke_mask.id, stroke_mask, result);
        }
        continue;
      }
      result.report.add_issue(FidelitySeverity::Unsupported,
                              "object " + std::to_string(id), "Group object",
                              "SVG group hierarchy and inherited transforms are not exported by this subset");
      continue;
    }
  }
  if (result.report.has_unsupported()) {
    result.error_message =
        "SVG export stopped because one or more objects cannot be represented without loss";
    return result;
  }
  stream << "</svg>\n";
  if (!stream.good()) {
    result.error_message = "SVG output stream failed while writing";
    return result;
  }
  result.success = true;
  return result;
}

SvgExportResult SvgExporter::export_with_report(
    const document::DocumentSnapshot &snapshot,
    const SvgExportOptions &options) {
  SvgExportResult result;
  std::ostringstream stream;
  const auto streamed = write_to_stream(snapshot, stream, options);
  result.report = streamed.report;
  result.error_message = streamed.error_message;
  if (!streamed.success)
    return result;
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
