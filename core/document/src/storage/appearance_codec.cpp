#include "byte_codec.hpp"
#include "pikdraw/document/model/validation.hpp"

namespace pikdraw::document::storage_detail {
namespace {
constexpr uint32_t max_blend_id_bytes = 32;
constexpr uint32_t max_effect_type_id_bytes = 128;

std::string_view blend_id(BlendMode mode) {
  switch (mode) {
  case BlendMode::Normal: return "normal";
  case BlendMode::Multiply: return "multiply";
  case BlendMode::Screen: return "screen";
  case BlendMode::Overlay: return "overlay";
  case BlendMode::Darken: return "darken";
  case BlendMode::Lighten: return "lighten";
  }
  throw std::invalid_argument("Unknown appearance blend mode");
}

BlendMode parse_blend_id(const std::string &id) {
  if (id == "normal") return BlendMode::Normal;
  if (id == "multiply") return BlendMode::Multiply;
  if (id == "screen") return BlendMode::Screen;
  if (id == "overlay") return BlendMode::Overlay;
  if (id == "darken") return BlendMode::Darken;
  if (id == "lighten") return BlendMode::Lighten;
  throw std::invalid_argument("Unknown required appearance blend ID");
}

void require_empty(const Reader &reader, const char *message) {
  if (reader.remaining() != 0)
    throw std::invalid_argument(message);
}

void write_effect_node(Writer &appearance_writer, const EffectNode &node,
                       const ProfileResourceIndexMap *profile_indices) {
  Writer parameters(true);
  std::visit(
      [&](const auto &effect) {
        using T = std::decay_t<decltype(effect)>;
        if constexpr (std::is_same_v<T, GaussianBlurEffect>) {
          parameters.f64(effect.sigma_x_mm);
          parameters.f64(effect.sigma_y_mm);
        } else if constexpr (std::is_same_v<T, DropShadowEffect>) {
          parameters.f64(effect.dx_mm);
          parameters.f64(effect.dy_mm);
          parameters.f64(effect.sigma_x_mm);
          parameters.f64(effect.sigma_y_mm);
          write_color(parameters, effect.color, profile_indices);
          parameters.f64(effect.opacity);
          if (node.schema_minor >= kDropShadowBlendSchemaMinor)
            parameters.str(std::string(blend_id(effect.blend_mode)));
        } else if constexpr (std::is_same_v<T, FeatherEffect>) {
          parameters.f64(effect.radius_mm);
        } else if constexpr (std::is_same_v<T, OuterGlowEffect>) {
          parameters.f64(effect.radius_mm);
          write_color(parameters, effect.color, profile_indices);
          parameters.f64(effect.opacity);
        } else if constexpr (std::is_same_v<T, InnerGlowEffect>) {
          parameters.f64(effect.radius_mm);
          write_color(parameters, effect.color, profile_indices);
          parameters.f64(effect.opacity);
        } else if constexpr (std::is_same_v<T, InnerShadowEffect>) {
          parameters.f64(effect.dx_mm);
          parameters.f64(effect.dy_mm);
          parameters.f64(effect.sigma_x_mm);
          parameters.f64(effect.sigma_y_mm);
          write_color(parameters, effect.color, profile_indices);
          parameters.f64(effect.opacity);
          parameters.str(std::string(blend_id(effect.blend_mode)));
        } else if constexpr (std::is_same_v<T, ColorOverlayEffect>) {
          write_color(parameters, effect.color, profile_indices);
          parameters.f64(effect.opacity);
          parameters.str(std::string(blend_id(effect.blend_mode)));
        }
      },
      node.parameters);

  Writer body(true);
  body.u64(node.id);
  body.str(std::string(effect_type_id(node.parameters)));
  body.u32(node.schema_version);
  body.u32(node.schema_minor);
  body.flag(node.enabled);
  body.count(parameters.bytes.size(), max_blob_bytes);
  body.append(parameters.bytes);
  appearance_writer.count(body.bytes.size(),
                          max_blob_bytes);
  appearance_writer.append(body.bytes);
}

EffectNode read_effect_node(Reader &appearance_reader,
                            bool include_profile_bindings) {
  const auto node_length = appearance_reader.u32();
  auto body = appearance_reader.subreader(node_length);
  EffectNode node;
  node.id = body.u64();
  if (node.id == kInvalidEffectInstanceId)
    throw std::invalid_argument("Zero appearance effect ID");
  const auto type_id = body.str(max_effect_type_id_bytes);
  node.schema_version = body.u32();
  node.schema_minor = body.u32();
  if (node.schema_version != kBasicEffectNodeSchemaVersion ||
      node.schema_minor > kExtendedEffectNodeSchemaMinor)
    throw std::invalid_argument("Unsupported required effect node schema");
  node.enabled = body.flag();
  const auto parameter_length = body.u32();
  auto parameters = body.subreader(parameter_length);
  if (type_id == "pikdraw.effect.gaussian-blur") {
    if (node.schema_minor != kBasicEffectNodeSchemaMinor)
      throw std::invalid_argument("Unsupported Gaussian blur node schema");
    GaussianBlurEffect blur{parameters.f64(), parameters.f64()};
    require_empty(parameters, "Gaussian blur payload has trailing bytes");
    node.parameters = blur;
  } else if (type_id == "pikdraw.effect.drop-shadow") {
    DropShadowEffect shadow;
    shadow.dx_mm = parameters.f64();
    shadow.dy_mm = parameters.f64();
    shadow.sigma_x_mm = parameters.f64();
    shadow.sigma_y_mm = parameters.f64();
    shadow.color = read_color(parameters, include_profile_bindings);
    shadow.opacity = parameters.f64();
    if (node.schema_minor >= kDropShadowBlendSchemaMinor)
      shadow.blend_mode = parse_blend_id(parameters.str(max_blend_id_bytes));
    require_empty(parameters, "Drop shadow payload has trailing bytes");
    node.parameters = std::move(shadow);
  } else if (type_id == "pikdraw.effect.feather") {
    if (node.schema_minor < kExtendedEffectNodeSchemaMinor)
      throw std::invalid_argument("Unsupported feather node schema");
    FeatherEffect feather;
    feather.radius_mm = parameters.f64();
    require_empty(parameters, "Feather payload has trailing bytes");
    node.parameters = feather;
  } else if (type_id == "pikdraw.effect.outer-glow") {
    if (node.schema_minor < kExtendedEffectNodeSchemaMinor)
      throw std::invalid_argument("Unsupported outer glow node schema");
    OuterGlowEffect glow;
    glow.radius_mm = parameters.f64();
    glow.color = read_color(parameters, include_profile_bindings);
    glow.opacity = parameters.f64();
    require_empty(parameters, "Outer glow payload has trailing bytes");
    node.parameters = std::move(glow);
  } else if (type_id == "pikdraw.effect.inner-glow") {
    if (node.schema_minor < kExtendedEffectNodeSchemaMinor)
      throw std::invalid_argument("Unsupported inner glow node schema");
    InnerGlowEffect glow;
    glow.radius_mm = parameters.f64();
    glow.color = read_color(parameters, include_profile_bindings);
    glow.opacity = parameters.f64();
    require_empty(parameters, "Inner glow payload has trailing bytes");
    node.parameters = std::move(glow);
  } else if (type_id == "pikdraw.effect.inner-shadow") {
    if (node.schema_minor < kExtendedEffectNodeSchemaMinor)
      throw std::invalid_argument("Unsupported inner shadow node schema");
    InnerShadowEffect shadow;
    shadow.dx_mm = parameters.f64();
    shadow.dy_mm = parameters.f64();
    shadow.sigma_x_mm = parameters.f64();
    shadow.sigma_y_mm = parameters.f64();
    shadow.color = read_color(parameters, include_profile_bindings);
    shadow.opacity = parameters.f64();
    shadow.blend_mode = parse_blend_id(parameters.str(max_blend_id_bytes));
    require_empty(parameters, "Inner shadow payload has trailing bytes");
    node.parameters = std::move(shadow);
  } else if (type_id == "pikdraw.effect.color-overlay") {
    if (node.schema_minor < kExtendedEffectNodeSchemaMinor)
      throw std::invalid_argument("Unsupported color overlay node schema");
    ColorOverlayEffect overlay;
    overlay.color = read_color(parameters, include_profile_bindings);
    overlay.opacity = parameters.f64();
    overlay.blend_mode = parse_blend_id(parameters.str(max_blend_id_bytes));
    require_empty(parameters, "Color overlay payload has trailing bytes");
    node.parameters = std::move(overlay);
  } else {
    throw std::invalid_argument("Unknown required appearance effect type ID");
  }
  require_empty(body, "Effect node record has trailing bytes");
  return node;
}
} // namespace

void write_appearance(Writer &writer, const Appearance &appearance,
                      const ProfileResourceIndexMap *profile_indices) {
  validate_appearance(appearance);
  Writer record(true);
  record.f64(appearance.object_opacity);
  record.f64(appearance.fill_opacity);
  record.f64(appearance.stroke_opacity);
  record.str(std::string(blend_id(appearance.blend_mode)));
  record.count(appearance.effects.size(),
               static_cast<uint32_t>(kMaxEffectsPerObject));
  for (const auto &node : appearance.effects)
    write_effect_node(record, node, profile_indices);
  writer.count(record.bytes.size(), max_blob_bytes);
  writer.append(record.bytes);
}

Appearance read_appearance(Reader &reader, size_t &remaining_effects,
                           bool include_profile_bindings) {
  const auto record_length = reader.u32();
  auto record = reader.subreader(record_length);
  Appearance appearance;
  appearance.object_opacity = record.f64();
  appearance.fill_opacity = record.f64();
  appearance.stroke_opacity = record.f64();
  appearance.blend_mode = parse_blend_id(record.str(max_blend_id_bytes));
  const auto effect_count =
      record.count(static_cast<uint32_t>(kMaxEffectsPerObject), 4);
  if (effect_count > remaining_effects)
    throw std::length_error("Document exceeds maximum appearance effect count");
  remaining_effects -= effect_count;
  appearance.effects.reserve(effect_count);
  for (uint32_t i = 0; i < effect_count; ++i)
    appearance.effects.push_back(read_effect_node(record, include_profile_bindings));
  require_empty(record, "Appearance record has trailing bytes");
  validate_appearance(appearance);
  return appearance;
}

} // namespace pikdraw::document::storage_detail
