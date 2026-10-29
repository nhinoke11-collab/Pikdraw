#include "pikdraw/document/hierarchy/frame_geometry.hpp"
#include <functional>
#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "../path_adapter/svg_path_adapter.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
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

struct SvgColorConversion {
  color::ColorValue value;
  bool profile_needs_report{false};
  bool converted_to_srgb{false};
  std::string diagnostic;
};

class SvgColorContext {
public:
  explicit SvgColorContext(
      std::shared_ptr<const color::ProfileResource> page_profile, double page_height)
      : basis_{1,0,0,-1,0,page_height}, page_profile_(std::move(page_profile)) {
    const auto srgb_path = color::IccPreview::system_srgb_profile();
    if (srgb_path.empty()) return;
    const auto loaded = color::ProfileResource::load_file(
        srgb_path, color::ColorSpace::Rgb);
    if (loaded) destination_srgb_ = loaded.resource;
  }

  [[nodiscard]] const geometry::Transform2D& basis() const noexcept { return basis_; }
  [[nodiscard]] SvgColorConversion convert_to_srgb(
      const color::ColorValue &source) {
    auto source_profile = source.profile_resource();
    if (!source_profile && page_profile_ &&
        page_profile_->identity().color_space == source.color_space())
      source_profile = page_profile_;
    if (!source_profile)
      return {source, false, false, {}};
    if (source_profile->identity().color_space != color::ColorSpace::Rgb &&
        source_profile->identity().color_space != color::ColorSpace::Cmyk)
      return {source, true, false,
              "Only RGB and CMYK ICC source profiles can be converted to SVG sRGB"};
    if (!destination_srgb_)
      return {source, true, false,
              "The system sRGB output profile is unavailable; source ICC conversion was not applied"};
    if (source_profile->identity() == destination_srgb_->identity())
      return {source, false, false, {}};

    const auto &digest = source_profile->identity().checksum_sha256;
    auto found = transforms_.find(digest);
    if (found == transforms_.end()) {
      if (transforms_.size() >= kMaxCachedProfiles)
        return {source, true, false,
                "The SVG export ICC transform cache limit was reached"};
      auto created = color::ProfileColorTransform::create(
          source_profile, destination_srgb_);
      found = transforms_.emplace(
          digest, std::move(created.transform)).first;
      if (!found->second)
        return {source, true, false,
                "Windows CMM could not convert this source ICC profile to sRGB"};
    }
    if (!found->second)
      return {source, true, false,
              "Windows CMM could not convert this source ICC profile to sRGB"};

    auto profile_bound_source = source;
    if (!profile_bound_source.profile_resource() &&
        !profile_bound_source.set_profile_resource(source_profile))
      return {source, true, false,
              "The page ICC profile does not match this paint's color space"};
    color::ColorValue converted;
    if (!found->second->convert(profile_bound_source, converted))
      return {source, true, false,
              "Windows CMM failed while converting this paint to sRGB"};
    return {std::move(converted), true, true, {}};
  }

private:
  geometry::Transform2D basis_;
  static constexpr size_t kMaxCachedProfiles = 64;
  std::shared_ptr<const color::ProfileResource> page_profile_;
  std::shared_ptr<const color::ProfileResource> destination_srgb_;
  std::unordered_map<std::string,
      std::shared_ptr<const color::ProfileColorTransform>> transforms_;
};

std::string color_to_hex(const color::ColorValue &value,
                         SvgColorContext &color_context,
                         SvgStreamExportResult &result,
                         document::ObjectId id,
                         const char *property) {
  const auto converted = color_context.convert_to_srgb(value);
  if (converted.profile_needs_report) {
    std::string description;
    if (!converted.diagnostic.empty()) {
      description = converted.diagnostic + "; SVG hex uses the existing preview fallback";
    } else if (converted.converted_to_srgb) {
      description = "Source ICC profile was converted to sRGB; SVG hex does not preserve the source profile binding";
    } else {
      description = "The source profile matches sRGB; SVG hex does not preserve the ICC profile resource";
    }
    result.report.add_issue(FidelitySeverity::Warning,
                            "object " + std::to_string(id), property,
                            std::move(description));
  }
  const auto device = color::to_preview_rgba(converted.value).to_device();
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
      if (path->visible) {
        if (path->contour_spec.starts_with("annotation:"))
          result.report.add_issue(
              FidelitySeverity::Unsupported, "object " + std::to_string(id),
              "Annotation object",
              "This SVG export subset cannot preserve editable annotation text, leader geometry, or anchor semantics");
        else if (path->contour_spec.starts_with("detail_view:"))
          result.report.add_issue(
              FidelitySeverity::Unsupported, "object " + std::to_string(id),
              "Detail view object",
              "This SVG export subset cannot preserve the linked source rendering, label, or detail-view scale semantics");
        report_unsupported_appearance(result, id, path->appearance);
      }
    } else if(const auto* group=std::get_if<document::GroupObject>(&*object);group&&group->clip) {
      const auto* mask=snapshot.objects().lookup_ref(group->clip->mask_object_id);
      if(!mask||!document::frame_geometry(*mask))result.report.add_issue(FidelitySeverity::Unsupported,
          "object "+std::to_string(id),"Clip geometry","Unsupported frame geometry");
    } else if (std::holds_alternative<document::ImageObject>(*object)) {
      result.report.add_issue(FidelitySeverity::Unsupported,"object "+std::to_string(id),"Raster content",
          "Embedded SVG raster output is unsupported; use native storage or raster export");
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
        FidelitySeverity::Warning, "object " + std::to_string(id), property,
        "Source CMYK/spot/gray paint or overprint metadata was projected to an RGB SVG color; separation/overprint semantics are not preserved");
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
                        SvgStreamExportResult &result,
                        SvgColorContext &color_context) {
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
    stream << " fill=\"" << color_to_hex(path.fill, color_context, result,
                                                   id, "fill color") << '"';
    write_paint_opacity(stream, "fill-opacity", path.fill,
                        path.appearance.fill_opacity);
    warn_preview_color(result, id, path.fill, "fill color");
  } else {
    stream << " fill=\"none\"";
  }
  if (path.has_stroke) {
    stream << " stroke=\"" << color_to_hex(path.stroke, color_context, result,
                                                   id, "stroke color")
           << "\" stroke-width=\"" << path.stroke_width_mm << '"';
    write_paint_opacity(stream, "stroke-opacity", path.stroke,
                        path.appearance.stroke_opacity);
    warn_preview_color(result, id, path.stroke, "stroke color");
  }
  if(path.path.winding_rule()==geometry::WindingRule::EvenOdd) stream << " fill-rule=\"evenodd\"";
  write_object_opacity(stream, path.appearance.object_opacity);
  write_transform(stream, color_context.basis()*path.transform);
  stream << "/>\n";
}

void write_text_element(std::ostream &stream, document::ObjectId id,
                        const document::TextObject &text,
                        SvgStreamExportResult &result,
                        SvgColorContext &color_context) {
  if (!text.visible) return;
  stream << "  <text x=\"0\" y=\"0\" font-family=\""
         << xml_escape(text.font.family_name) << "\" font-size=\""
         << text.font.size_pt * 25.4 / 72.0 << "\" fill=\""
         << color_to_hex(text.fill, color_context, result, id, "text color")
         << '"';
  write_paint_opacity(stream, "fill-opacity", text.fill,
                      text.appearance.fill_opacity);
  write_object_opacity(stream, text.appearance.object_opacity);
  write_transform(stream, color_context.basis()*text.transform*
      geometry::Transform2D::translation(text.position.x,text.position.y)*geometry::Transform2D::scale(1,-1));
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
  std::function<void(document::ObjectId)> include_children=[&](document::ObjectId id) {
    const auto* object=snapshot.objects().lookup_ref(id);
    if(const auto* group=object?std::get_if<document::GroupObject>(object):nullptr)
      for(auto child:group->children_ids)if(unique_ids.insert(child).second){object_ids.push_back(child);include_children(child);}
  };
  const auto requested=object_ids;
  for(auto id:requested)include_children(id);
  if (!result.report.has_unsupported())
    preflight_appearance_export(snapshot, object_ids, result);
  if (result.report.has_unsupported()) {
    result.error_message =
        "SVG export stopped before output because the selected scope or an appearance value has no verified mapping";
    return result;
  }

  const auto& page = snapshot.page();
  const geometry::Rect2D bounds = options.bounds.value_or(
      geometry::Rect2D{0.0, 0.0, page.width_mm, page.height_mm});
  if (!bounds.is_finite() || bounds.is_empty()) {
    result.report.add_issue(FidelitySeverity::Unsupported, "SVG viewport", "Export bounds",
                            "The export rectangle must be finite and have positive dimensions");
    result.error_message = "SVG export stopped because the requested viewport is invalid";
    return result;
  }

  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  if (options.include_xml_header)
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

  SvgColorContext color_context(page.color_profile_resource,page.height_mm);
  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
         << bounds.width << "mm\" height=\"" << bounds.height
         << "mm\" viewBox=\"" << bounds.x << ' ' << bounds.y << ' '
         << bounds.width << ' ' << bounds.height
         << "\">\n";

  bool has_defs = false;
  for (const auto &[id, object] : snapshot.objects().all()) {
    if (!unique_ids.contains(id)) continue;
    if (const auto *grp = std::get_if<document::GroupObject>(&object)) {
      if (grp->is_clipping_container()) {
        const auto *mask = snapshot.objects().lookup_ref(grp->clip->mask_object_id);
        if (mask) {
          const auto geometry=document::frame_geometry(*mask);
          if (const auto *mask_path = geometry?&*geometry:nullptr) {
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
              stream << "    <clipPath id=\"clip_" << grp->id << "\" clipPathUnits=\"userSpaceOnUse\">\n";
              stream << "      <path d=\"" << *data << '"';
              stream << " clip-rule=\"" << (mask_path->path.winding_rule()==geometry::WindingRule::EvenOdd?"evenodd":"nonzero") << "\"";
              write_transform(stream, color_context.basis()*mask_path->transform);
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

  std::function<void(document::ObjectId)> write_object=[&](document::ObjectId id) {
    const auto* object=snapshot.objects().lookup_ref(id);
    if(!object||!unique_ids.contains(id)||!std::visit([](const auto& value){return value.visible;},*object))return;
    if(const auto* path=std::get_if<document::PathObject>(object)) {write_path_element(stream,id,*path,result,color_context);return;}
    if(const auto* text=std::get_if<document::TextObject>(object)) {write_text_element(stream,id,*text,result,color_context);return;}
    const auto* group=std::get_if<document::GroupObject>(object);if(!group)return;
    stream << "  <g";write_transform(stream,color_context.basis()*group->transform*color_context.basis());stream << ">\n";
    std::optional<document::PathObject> mask;
    if(group->clip) {
      const auto* source=snapshot.objects().lookup_ref(group->clip->mask_object_id);
      mask=source?document::frame_geometry(*source):std::nullopt;
      if(!mask) {result.report.add_issue(FidelitySeverity::Unsupported,"object "+std::to_string(id),"Clip geometry","Unsupported frame geometry");return;}
      if(group->clip->preserve_frame_appearance&&mask->has_fill){auto fill=*mask;fill.has_stroke=false;write_path_element(stream,fill.id,fill,result,color_context);}
      stream << "  <g clip-path=\"url(#clip_" << id << ")\">\n";
    }
    for(auto child:group->children_ids)if(!group->clip||child!=group->clip->mask_object_id)write_object(child);
    if(group->clip) {
      stream << "  </g>\n";
      if(group->clip->preserve_frame_appearance&&mask->has_stroke){auto stroke=*mask;stroke.has_fill=false;write_path_element(stream,stroke.id,stroke,result,color_context);}
    }
    stream << "  </g>\n";
  };
  for(const auto& layer:snapshot.page().layers) {
    if(!layer.visible || !layer.printable)continue;
    for(auto id:layer.object_ids)write_object(id);
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
