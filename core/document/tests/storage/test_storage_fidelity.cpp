#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "../../src/storage/byte_codec.hpp"
#include <filesystem>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace pikdraw;
using namespace pikdraw::document;

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string utf8(const std::filesystem::path &path) {
  const auto bytes = path.u8string();
  return {bytes.begin(), bytes.end()};
}

std::vector<uint8_t> read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

void repair_checksum(std::vector<uint8_t> &bytes) {
  for (size_t i = 32; i < 36; ++i)
    bytes[i] = 0;
  const auto checksum =
      NativeFormat::calculate_crc32(bytes.data(), bytes.size());
  for (unsigned i = 0; i < 4; ++i)
    bytes[32 + i] = static_cast<uint8_t>(checksum >> (8 * i));
}

void fixture_u8(std::vector<uint8_t> &bytes, uint8_t value) {
  bytes.push_back(value);
}

void fixture_u32(std::vector<uint8_t> &bytes, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    fixture_u8(bytes, static_cast<uint8_t>(value >> shift));
}

void fixture_u64(std::vector<uint8_t> &bytes, uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8)
    fixture_u8(bytes, static_cast<uint8_t>(value >> shift));
}

void fixture_f64(std::vector<uint8_t> &bytes, double value) {
  fixture_u64(bytes, std::bit_cast<uint64_t>(value));
}

void fixture_string(std::vector<uint8_t> &bytes, std::string_view value) {
  fixture_u32(bytes, static_cast<uint32_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<uint8_t> fixture_node(uint64_t id, std::string_view type,
                                  bool enabled,
                                  const std::vector<uint8_t> &parameters,
                                  uint32_t schema_minor = 0) {
  std::vector<uint8_t> body;
  fixture_u64(body, id);
  fixture_string(body, type);
  fixture_u32(body, 1);
  fixture_u32(body, schema_minor);
  fixture_u8(body, enabled ? 1 : 0);
  fixture_u32(body, static_cast<uint32_t>(parameters.size()));
  body.insert(body.end(), parameters.begin(), parameters.end());
  std::vector<uint8_t> record;
  fixture_u32(record, static_cast<uint32_t>(body.size()));
  record.insert(record.end(), body.begin(), body.end());
  return record;
}

std::vector<uint8_t> fixture_appearance(
    double object_opacity, double fill_opacity, double stroke_opacity,
    std::string_view blend, const std::vector<std::vector<uint8_t>> &nodes) {
  std::vector<uint8_t> record;
  fixture_f64(record, object_opacity);
  fixture_f64(record, fill_opacity);
  fixture_f64(record, stroke_opacity);
  fixture_string(record, blend);
  fixture_u32(record, static_cast<uint32_t>(nodes.size()));
  for (const auto &node : nodes)
    record.insert(record.end(), node.begin(), node.end());
  std::vector<uint8_t> result;
  fixture_u32(result, static_cast<uint32_t>(record.size()));
  result.insert(result.end(), record.begin(), record.end());
  return result;
}

std::vector<uint8_t> make_legacy_v2_fixture(const PathObject &path) {
  using namespace storage_detail;
  Writer payload;
  payload.str("legacy-v2-fixture");
  const auto page = Page::make_default();
  payload.f64(page.width_mm);
  payload.f64(page.height_mm);
  payload.count(page.layers.size(), 10000);
  for (const auto &layer : page.layers) {
    payload.str(layer.name);
    payload.flag(layer.visible);
    payload.flag(layer.locked);
    payload.count(1, max_objects);
    payload.u64(path.id);
  }
  payload.count(1, max_objects);
  write_object(payload, ObjectVariant{path}, false);
  Writer container;
  for (const auto byte : kPikDrawMagic)
    container.u8(byte);
  container.u32(kLegacyFormatVersion);
  container.u32(0);
  container.u64(6);
  container.u64(9);
  container.u32(static_cast<uint32_t>(payload.bytes.size()));
  container.u32(0);
  container.append(payload.bytes);
  const auto crc = NativeFormat::calculate_crc32(container.bytes.data(),
                                                  container.bytes.size());
  for (unsigned i = 0; i < 4; ++i)
    container.bytes[32 + i] = static_cast<uint8_t>(crc >> (8 * i));
  return std::move(container.bytes);
}

std::vector<uint8_t> make_legacy_v3_fixture(const PathObject &path) {
  using namespace storage_detail;
  Writer payload;
  payload.str("legacy-v3-fixture");
  payload.u32(kAuthoringSourceFormatVersion);
  payload.u32(kAuthoringPathSchemaMajor);
  payload.u32(kAuthoringPathSchemaMinor);
  const auto page = Page::make_default();
  payload.f64(page.width_mm);
  payload.f64(page.height_mm);
  payload.count(page.layers.size(), 10000);
  for (const auto &layer : page.layers) {
    payload.str(layer.name);
    payload.flag(layer.visible);
    payload.flag(layer.locked);
    payload.count(1, max_objects);
    payload.u64(path.id);
  }
  payload.count(1, max_objects);
  write_object(payload, ObjectVariant{path}, true);
  Writer container;
  for (const auto byte : kPikDrawMagic)
    container.u8(byte);
  container.u32(kAuthoringSourceFormatVersion);
  container.u32(0);
  container.u64(7);
  container.u64(11);
  container.u32(static_cast<uint32_t>(payload.bytes.size()));
  container.u32(0);
  container.append(payload.bytes);
  repair_checksum(container.bytes);
  return std::move(container.bytes);
}

void appearance_codec_matches_independent_byte_fixtures() {
  using namespace storage_detail;
  Appearance appearance;
  appearance.object_opacity = 0.5;
  appearance.fill_opacity = 0.25;
  appearance.stroke_opacity = 0.75;
  appearance.blend_mode = BlendMode::Multiply;
  EffectNode blur;
  blur.id = 0x0102030405060708ull;
  blur.enabled = false;
  blur.parameters = GaussianBlurEffect{1.25, 2.5};
  appearance.effects.push_back(blur);

  std::vector<uint8_t> blur_parameters;
  fixture_f64(blur_parameters, 1.25);
  fixture_f64(blur_parameters, 2.5);
  const auto blur_fixture = fixture_appearance(
      0.5, 0.25, 0.75, "multiply",
      {fixture_node(0x0102030405060708ull,
                    "pikdraw.effect.gaussian-blur", false, blur_parameters)});
  Writer encoded_blur(true);
  write_appearance(encoded_blur, appearance);
  check(encoded_blur.bytes == blur_fixture,
        "Gaussian appearance bytes differ from independent fixture");
  Reader blur_reader(blur_fixture, true);
  size_t effect_budget = kMaxEffectsPerDocument;
  const auto decoded_blur = read_appearance(blur_reader, effect_budget);
  check(blur_reader.remaining() == 0 && effect_budget == kMaxEffectsPerDocument - 1 &&
            decoded_blur.object_opacity == 0.5 &&
            decoded_blur.fill_opacity == 0.25 &&
            decoded_blur.stroke_opacity == 0.75 &&
            decoded_blur.blend_mode == BlendMode::Multiply &&
            decoded_blur.effects.size() == 1 &&
            decoded_blur.effects.front().id == blur.id &&
            !decoded_blur.effects.front().enabled &&
            std::get<GaussianBlurEffect>(decoded_blur.effects.front().parameters)
                    .sigma_y_mm == 2.5,
        "Gaussian appearance fixture did not decode exactly");

  DropShadowEffect shadow;
  shadow.dx_mm = 1.5;
  shadow.dy_mm = -2.25;
  shadow.sigma_x_mm = 0.5;
  shadow.sigma_y_mm = 0.75;
  shadow.color = color::RGBColor{1.0, 0.25, 0.0, 0.8};
  shadow.opacity = 0.4;
  EffectNode shadow_node;
  shadow_node.id = 0x1112131415161718ull;
  shadow_node.parameters = shadow;
  appearance.object_opacity = 1.0;
  appearance.fill_opacity = 0.9;
  appearance.stroke_opacity = 0.8;
  appearance.blend_mode = BlendMode::Screen;
  appearance.effects.push_back(shadow_node);

  std::vector<uint8_t> shadow_parameters;
  fixture_f64(shadow_parameters, 1.5);
  fixture_f64(shadow_parameters, -2.25);
  fixture_f64(shadow_parameters, 0.5);
  fixture_f64(shadow_parameters, 0.75);
  fixture_u8(shadow_parameters, 0); // RGB tag
  fixture_u8(shadow_parameters, 0); // fill overprint
  fixture_u8(shadow_parameters, 0); // stroke overprint
  fixture_f64(shadow_parameters, 1.0);
  fixture_f64(shadow_parameters, 0.25);
  fixture_f64(shadow_parameters, 0.0);
  fixture_f64(shadow_parameters, 0.8);
  fixture_f64(shadow_parameters, 0.4);
  const auto ordered_fixture = fixture_appearance(
      1.0, 0.9, 0.8, "screen",
      {fixture_node(0x0102030405060708ull,
                    "pikdraw.effect.gaussian-blur", false, blur_parameters),
       fixture_node(0x1112131415161718ull,
                    "pikdraw.effect.drop-shadow", true, shadow_parameters)});
  Writer encoded_ordered(true);
  write_appearance(encoded_ordered, appearance);
  check(encoded_ordered.bytes == ordered_fixture,
        "ordered blur/shadow bytes differ from independent fixture");
  Reader ordered_reader(ordered_fixture, true);
  effect_budget = kMaxEffectsPerDocument;
  const auto decoded_ordered = read_appearance(ordered_reader, effect_budget);
  const auto &decoded_shadow = std::get<DropShadowEffect>(
      decoded_ordered.effects.at(1).parameters);
  check(ordered_reader.remaining() == 0 &&
            decoded_ordered.effects.size() == 2 &&
            decoded_ordered.effects[0].id == blur.id &&
            decoded_ordered.effects[1].id == shadow_node.id &&
            decoded_shadow.dx_mm == 1.5 && decoded_shadow.dy_mm == -2.25 &&
            decoded_shadow.sigma_y_mm == 0.75 &&
            decoded_shadow.opacity == 0.4 &&
            std::get<color::RGBColor>(decoded_shadow.color.payload()).a == 0.8,
        "ordered blur/shadow source parameters changed on decode");

  auto blended_shadow = shadow;
  blended_shadow.blend_mode = BlendMode::Multiply;
  EffectNode blended_shadow_node = shadow_node;
  blended_shadow_node.schema_minor = kDropShadowBlendSchemaMinor;
  blended_shadow_node.parameters = blended_shadow;
  Appearance shadow_blend_appearance;
  shadow_blend_appearance.effects.push_back(blended_shadow_node);
  auto blended_shadow_parameters = shadow_parameters;
  fixture_string(blended_shadow_parameters, "multiply");
  const auto shadow_blend_fixture = fixture_appearance(
      1.0, 1.0, 1.0, "normal",
      {fixture_node(0x1112131415161718ull,
                    "pikdraw.effect.drop-shadow", true,
                    blended_shadow_parameters, kDropShadowBlendSchemaMinor)});
  Writer encoded_shadow_blend(true);
  write_appearance(encoded_shadow_blend, shadow_blend_appearance);
  check(encoded_shadow_blend.bytes == shadow_blend_fixture,
        "Drop Shadow schema 1.1 bytes differ from independent fixture");
  Reader shadow_blend_reader(shadow_blend_fixture, true);
  effect_budget = kMaxEffectsPerDocument;
  const auto decoded_shadow_blend =
      read_appearance(shadow_blend_reader, effect_budget);
  check(shadow_blend_reader.remaining() == 0 &&
            decoded_shadow_blend.effects.front().schema_minor ==
                kDropShadowBlendSchemaMinor &&
            std::get<DropShadowEffect>(
                decoded_shadow_blend.effects.front().parameters).blend_mode ==
                BlendMode::Multiply,
        "Drop Shadow schema 1.1 blend ID did not roundtrip");

  const std::array<std::pair<BlendMode, std::string_view>, 6> blend_ids{{
      {BlendMode::Normal, "normal"}, {BlendMode::Multiply, "multiply"},
      {BlendMode::Screen, "screen"}, {BlendMode::Overlay, "overlay"},
      {BlendMode::Darken, "darken"}, {BlendMode::Lighten, "lighten"}}};
  for (const auto &[mode, stable_id] : blend_ids) {
    Appearance blend;
    blend.blend_mode = mode;
    const auto fixture = fixture_appearance(1.0, 1.0, 1.0, stable_id, {});
    Writer writer(true);
    write_appearance(writer, blend);
    check(writer.bytes == fixture, "blend ID byte fixture mismatch");
    Reader reader(fixture, true);
    effect_budget = kMaxEffectsPerDocument;
    check(read_appearance(reader, effect_budget).blend_mode == mode &&
              reader.remaining() == 0,
          "stable blend ID did not decode");
  }

  auto rejected = [](std::vector<uint8_t> malformed) {
    try {
      Reader reader(malformed, true);
      size_t budget = kMaxEffectsPerDocument;
      (void)read_appearance(reader, budget);
    } catch (const std::exception &) {
      return true;
    }
    return false;
  };
  auto bad_node_length = blur_fixture;
  std::fill(bad_node_length.begin() + 44, bad_node_length.begin() + 48, 0xff);
  check(rejected(std::move(bad_node_length)),
        "oversized node record length was accepted");
  auto unknown_type = blur_fixture;
  unknown_type[60] = static_cast<uint8_t>('x');
  check(rejected(std::move(unknown_type)), "unknown required node type was accepted");
  auto unknown_schema = blur_fixture;
  unknown_schema[88] = 2;
  check(rejected(std::move(unknown_schema)),
        "unknown required effect schema was accepted");
  auto invalid_utf8 = blur_fixture;
  invalid_utf8[32] = 0xc0;
  check(rejected(std::move(invalid_utf8)), "invalid v4 UTF-8 was accepted");
  auto short_parameters = blur_fixture;
  short_parameters[97] = 15;
  check(rejected(std::move(short_parameters)),
        "truncated Gaussian payload was accepted");
  auto trailing_record_byte = blur_fixture;
  ++trailing_record_byte[0];
  trailing_record_byte.push_back(0);
  check(rejected(std::move(trailing_record_byte)),
        "unconsumed appearance record byte was accepted");
}

void appearance_survives_native_v4_path_and_text_roundtrip() {
  Document document("appearance-native-roundtrip");
  PathObject path;
  path.id = 71000;
  path.path = geometry::Path2D::make_rect(1.25, 2.5, 30.0, 15.0);
  path.appearance.object_opacity = 0.6;
  path.appearance.fill_opacity = 0.4;
  path.appearance.stroke_opacity = 0.8;
  path.appearance.blend_mode = BlendMode::Overlay;
  EffectNode blur;
  blur.id = 71001;
  blur.parameters = GaussianBlurEffect{0.125, 1.75};
  path.appearance.effects.push_back(blur);
  check(document.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add appearance path");

  TextObject text;
  text.id = 72000;
  text.text = "hiệu ứng";
  text.appearance.object_opacity = 0.9;
  text.appearance.fill_opacity = 0.7;
  text.appearance.blend_mode = BlendMode::Darken;
  DropShadowEffect shadow;
  shadow.dx_mm = -1.25;
  shadow.dy_mm = 2.5;
  shadow.sigma_x_mm = 0.75;
  shadow.sigma_y_mm = 1.25;
  shadow.color = color::SpotColor{"PANTONE 青", 0.35,
                                  color::RGBColor{0.2, 0.4, 0.6, 0.85}, true};
  shadow.color.set_overprint({true, false});
  shadow.opacity = 0.55;
  shadow.blend_mode = BlendMode::Screen;
  EffectNode shadow_node;
  shadow_node.id = 72001;
  shadow_node.schema_minor = kDropShadowBlendSchemaMinor;
  shadow_node.parameters = shadow;
  text.appearance.effects.push_back(shadow_node);
  check(document.execute_command(std::make_unique<AddObjectCommand>(text)).success,
        "add appearance text");

  const auto bytes = NativeFormat::serialize(*document.current_snapshot());
  check(bytes.size() > 36 && (bytes[4] == 4 || bytes[4] == 5),
        "appearance document did not save as native v4/v5");
  std::string error;
  const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(reopened && error.empty(), "appearance document did not reopen");
  const auto restored_path = std::get<PathObject>(*reopened->get_object(path.id));
  const auto restored_text = std::get<TextObject>(*reopened->get_object(text.id));
  check(restored_path.appearance.object_opacity == 0.6 &&
            restored_path.appearance.fill_opacity == 0.4 &&
            restored_path.appearance.stroke_opacity == 0.8 &&
            restored_path.appearance.blend_mode == BlendMode::Overlay &&
            restored_path.appearance.effects.size() == 1 &&
            restored_path.appearance.effects.front().id == blur.id &&
            std::get<GaussianBlurEffect>(
                restored_path.appearance.effects.front().parameters)
                    .sigma_x_mm == 0.125,
        "Path appearance changed through v4 container roundtrip");
  const auto &restored_shadow = std::get<DropShadowEffect>(
      restored_text.appearance.effects.front().parameters);
  const auto &restored_spot =
      std::get<color::SpotColor>(restored_shadow.color.payload());
  check(restored_text.text == text.text &&
            restored_text.appearance.object_opacity == 0.9 &&
            restored_text.appearance.fill_opacity == 0.7 &&
            restored_text.appearance.blend_mode == BlendMode::Darken &&
            restored_text.appearance.effects.front().id == shadow_node.id &&
            restored_text.appearance.effects.front().schema_minor ==
                kDropShadowBlendSchemaMinor &&
            restored_shadow.dx_mm == -1.25 &&
            restored_shadow.sigma_y_mm == 1.25 &&
            restored_shadow.opacity == 0.55 &&
            restored_shadow.blend_mode == BlendMode::Screen &&
            restored_spot.spot_name == "PANTONE 青" &&
            restored_spot.tint == 0.35 && restored_spot.overprint &&
            restored_shadow.color.overprint().fill,
        "Text appearance or spot shadow source changed through v4");
}

void authoring_source_roundtrip_and_legacy_migration() {
  Document doc("authoring-source-roundtrip");
  PathObject path;
  path.id = 9001;
  EditablePath source;
  EditableSubpath subpath;
  subpath.id = SubpathId{9002};
  subpath.nodes = {
      PathNode{NodeId{9003}, {100.0, 0.0}, PathNodeMode::Cusp, false, {},
               true, {110.0, 5.0}},
      PathNode{NodeId{9004}, {0.0, 100.0}, PathNodeMode::Cusp, true,
               {-5.0, 110.0}, false, {}}};
  EditableSegment arc;
  arc.id = SegmentId{9005};
  arc.kind = PathSegmentKind::EllipticArc;
  arc.arc = {{0, 0}, {100, 0}, {0, 100}, 0, 1.57079632679489661923};
  subpath.segments.push_back(arc);
  source.subpaths.push_back(subpath);
  path.set_authoring_source(source);
  check(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add analytic source path");

  const auto bytes = NativeFormat::serialize(*doc.current_snapshot());
  check(bytes.size() > 36 && (bytes[4] == 4 || bytes[4] == 5),
        "native writer did not emit container v4/v5");
  std::string error;
  const auto reopened =
      NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(reopened && error.empty(), "v4 authoring path did not reopen");
  const auto restored = std::get<PathObject>(*reopened->get_object(path.id));
  check(restored.authoring_source &&
            restored.authoring_source->subpaths.front().id == SubpathId{9002} &&
            restored.authoring_source->subpaths.front().nodes[1].id == NodeId{9004} &&
            restored.authoring_source->subpaths.front().segments.front().id ==
                SegmentId{9005},
        "v4 changed stable path identities");
  const auto &restored_arc =
      restored.authoring_source->subpaths.front().segments.front().arc;
  check(restored_arc.center == geometry::Point2D{0, 0} &&
            restored_arc.axis_u == geometry::Point2D{100, 0} &&
            restored_arc.axis_v == geometry::Point2D{0, 100} &&
            restored_arc.start_angle_rad == 0 &&
            restored_arc.sweep_angle_rad == arc.arc.sweep_angle_rad,
        "analytic arc source was approximated or altered");
  const auto saved_again = NativeFormat::serialize(*reopened);
  const auto reopened_again =
      NativeFormat::deserialize(saved_again.data(), saved_again.size());
  check(reopened_again &&
            std::get<PathObject>(*reopened_again->get_object(path.id))
                    .authoring_source->subpaths.front().nodes[0].id ==
                NodeId{9003},
        "stable path identity changed on repeated save/reopen");

  auto legacy_path = PathObject{};
  legacy_path.id = 12000;
  legacy_path.path.move_to({1, 2});
  legacy_path.path.line_to({30, 2});
  legacy_path.path.quad_to(35, 10, 30, 20);
  legacy_path.path.cubic_to(24, 28, 8, 32, 1, 20);
  legacy_path.path.close();
  const auto legacy = make_legacy_v2_fixture(legacy_path);
  const auto migrated =
      NativeFormat::deserialize(legacy.data(), legacy.size(), &error);
  check(migrated && error.empty(), "valid v2 migration failed");
  const auto migrated_path =
      std::get<PathObject>(*migrated->get_object(legacy_path.id));
  check(migrated_path.authoring_source &&
            migrated_path.authoring_source->subpaths.front().segments.size() ==
                4 &&
            migrated_path.authoring_source->subpaths.front().segments[1].kind ==
                PathSegmentKind::QuadraticBezier &&
            migrated_path.authoring_source->subpaths.front().segments[2].kind ==
                PathSegmentKind::CubicBezier,
        "v2 migration did not allocate stable authoring identities");
  check(migrated_path.appearance.object_opacity == 1.0 &&
            migrated_path.appearance.fill_opacity == 1.0 &&
            migrated_path.appearance.stroke_opacity == 1.0 &&
            migrated_path.appearance.blend_mode == BlendMode::Normal &&
            migrated_path.appearance.effects.empty(),
        "v2 migration did not create default appearance");
  const auto migrated_id =
      migrated_path.authoring_source->subpaths.front().nodes.front().id;
  const auto migrated_v4 = NativeFormat::serialize(*migrated);
  const auto migrated_again =
      NativeFormat::deserialize(migrated_v4.data(), migrated_v4.size());
  check(migrated_again &&
            std::get<PathObject>(*migrated_again->get_object(legacy_path.id))
                    .authoring_source->subpaths.front().nodes.front().id ==
                migrated_id,
        "v2 migration identities were not persisted by v4");

  auto v3_path = PathObject{};
  v3_path.id = 12002;
  v3_path.path = geometry::Path2D::make_rect(2, 3, 20, 10);
  std::string conversion_error;
  auto v3_source = editable_path_from_path2d(v3_path.path, &conversion_error);
  check(v3_source.has_value(), "v3 fixture path source conversion failed");
  v3_path.set_authoring_source(std::move(*v3_source));
  const auto v3_fixture = make_legacy_v3_fixture(v3_path);
  const auto from_v3 = NativeFormat::deserialize(
      v3_fixture.data(), v3_fixture.size(), &error);
  check(from_v3 && error.empty(), "valid v3 migration failed");
  const auto migrated_v3_path =
      std::get<PathObject>(*from_v3->get_object(v3_path.id));
  check(migrated_v3_path.appearance.object_opacity == 1.0 &&
            migrated_v3_path.appearance.fill_opacity == 1.0 &&
            migrated_v3_path.appearance.stroke_opacity == 1.0 &&
            migrated_v3_path.appearance.blend_mode == BlendMode::Normal &&
            migrated_v3_path.appearance.effects.empty(),
        "v3 migration did not create default appearance");
  const auto v3_saved_as_v4 = NativeFormat::serialize(*from_v3);
  check((v3_saved_as_v4[4] == 4 || v3_saved_as_v4[4] == 5) &&
            NativeFormat::deserialize(v3_saved_as_v4.data(),
                                      v3_saved_as_v4.size(), &error),
        "explicit save of v3 did not migrate to current format");

  auto unsupported_legacy = PathObject{};
  unsupported_legacy.id = 12001;
  unsupported_legacy.path.move_to({0, 0});
  unsupported_legacy.path.arc_to(0, 10, 0, false, true, 20, 0);
  const auto unsupported_bytes = make_legacy_v2_fixture(unsupported_legacy);
  check(!NativeFormat::deserialize(unsupported_bytes.data(),
                                   unsupported_bytes.size(), &error) &&
            error.find("12001") != std::string::npos &&
            error.find("losslessly") != std::string::npos,
        "ambiguous legacy arc did not report a blocking fidelity diagnostic");

  auto future = bytes;
  future[4] = 99;
  repair_checksum(future);
  check(!NativeFormat::deserialize(future.data(), future.size(), &error) &&
            error.find("Unsupported native version") != std::string::npos,
        "unknown container version was accepted");
  auto unknown_schema = bytes;
  const size_t schema_major = 36 + 4 + std::string("authoring-source-roundtrip").size() + 4;
  unknown_schema[schema_major] = 99;
  repair_checksum(unknown_schema);
  check(!NativeFormat::deserialize(unknown_schema.data(), unknown_schema.size(),
                                   &error) &&
            error.find("authoring-path schema") != std::string::npos,
        "unknown required authoring schema was accepted");
}

void versioned_recovery_draft_isolated_from_documents() {
  RecoveryDraftRecord draft;
  draft.document_id = "draft-parent-doc";
  draft.document_epoch = 77;
  draft.base_revision = 12;
  draft.base_document_crc32 = 0x1234abcd;
  draft.payload = {0, 1, 2, 0xfe, 0xff};
  auto bytes = RecoveryDraftFormat::serialize(draft);
  std::string error;
  const auto decoded =
      RecoveryDraftFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(decoded && error.empty() && decoded->document_id == draft.document_id &&
            decoded->document_epoch == draft.document_epoch &&
            decoded->base_revision == draft.base_revision &&
            decoded->base_document_crc32 == draft.base_document_crc32 &&
            decoded->payload == draft.payload,
        "versioned draft metadata or payload failed roundtrip");
  check(!NativeFormat::deserialize(bytes.data(), bytes.size(), &error),
        "recovery draft was accepted as a committed native document");
  bytes.back() ^= 1;
  check(!RecoveryDraftFormat::deserialize(bytes.data(), bytes.size(), &error) &&
            error.find("checksum") != std::string::npos,
        "corrupt recovery draft checksum was accepted");

  const auto directory =
      std::filesystem::absolute("build/recovery-draft-owned-test");
  std::filesystem::create_directories(directory);
  const auto target = directory / "session.pikdraft";
  auto saved = SaveRecoveryService::atomic_write_recovery_draft(
      utf8(target), draft);
  check(saved.success, "atomic recovery draft save failed");
  const auto original = read_bytes(target);
  const auto failed = SaveRecoveryService::atomic_write_recovery_draft(
      utf8(target), RecoveryDraftRecord{"draft-parent-doc", 77, 13,
                                        0x1234abcd, 1, {9, 8, 7}},
      {SaveFaultPoint::BeforeReplace});
  check(!failed.success && read_bytes(target) == original,
        "failed draft save damaged its previous checkpoint");
  const auto loaded = SaveRecoveryService::load_recovery_draft(utf8(target),
                                                               &error);
  check(loaded && error.empty() && loaded->base_revision == 12 &&
            loaded->payload == draft.payload,
        "atomic recovery draft did not reopen");
  std::filesystem::remove(target);
  std::filesystem::remove(directory);
}

void document_path_ids_are_globally_unique() {
  auto make_path = [] {
    PathObject path;
    EditablePath source;
    EditableSubpath subpath;
    subpath.id = SubpathId{30002};
    subpath.nodes = {PathNode{NodeId{30003}, {0, 0}},
                     PathNode{NodeId{30004}, {10, 0}}};
    subpath.segments = {EditableSegment{SegmentId{30005}}};
    source.subpaths.push_back(std::move(subpath));
    path.set_authoring_source(std::move(source));
    return path;
  };
  auto first = make_path();
  first.id = 30000;
  auto second = make_path();
  second.id = 30001;
  Page page = Page::make_default();
  page.layers.front().object_ids = {first.id, second.id};
  PersistentObjectMap objects;
  objects = objects.set(first.id, ObjectVariant{first});
  objects = objects.set(second.id, ObjectVariant{second});
  bool rejected = false;
  try {
    DocumentSnapshot invalid(99, 0, std::move(page), std::move(objects),
                            "duplicate-path-id-test");
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "duplicate authoring IDs across objects were accepted");
}

void source_roundtrip() {
  Document doc("source-properties");
  PathObject path;
  path.id = 100;
  path.name = "Biển hiệu";
  path.path = geometry::Path2D::make_rect(3.125, 4.25, 20000, 500);
  path.path.set_winding_rule(geometry::WindingRule::EvenOdd);
  path.fill =
      color::CMYKColor{0.123456789012345, 0.234567891234567, 0.3, 0.4, 0.7};
  path.fill.set_overprint({true, true});
  path.stroke =
      color::SpotColor{"PANTONE test", 0.65, {0.1, 0.2, 0.3, 0.45}, true};
  path.stroke.set_overprint({false, true});
  path.stroke_width_mm = 0.123456789012345;
  path.has_stroke = false;
  path.transform = geometry::Transform2D::translation(15.1234567890123, -7.25);
  check(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add source path");
  TextObject text;
  text.id = 101;
  text.text = "Tiếng Việt / Tie\xCC\x82\xCC\x81ng";
  text.font = {"Arial", "Arial-BoldItalicMT", 700, true, 17.125};
  text.fill = color::GrayscaleColor{0.234567891234567, 0.55};
  check(doc.execute_command(std::make_unique<AddObjectCommand>(text)).success,
        "add source text");
  auto bytes = NativeFormat::serialize(*doc.current_snapshot());
  std::string error;
  const auto loaded =
      NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(loaded && error.empty(), "native parse");
  const auto restored = std::get<PathObject>(*loaded->get_object(100));
  check(restored.path.winding_rule() == geometry::WindingRule::EvenOdd,
        "fill rule lost");
  check(restored.fill.color_space() == color::ColorSpace::Cmyk,
        "CMYK model lost");
  check(restored.appearance.object_opacity == 1.0 &&
            restored.appearance.blend_mode == BlendMode::Normal &&
            restored.appearance.effects.empty(),
        "default appearance changed in v4 roundtrip");
  check(std::get<color::CMYKColor>(restored.fill.payload()).c ==
            std::get<color::CMYKColor>(path.fill.payload()).c,
        "source double rounded through preview");
  check(restored.fill.overprint().fill && restored.fill.overprint().stroke,
        "overprint config lost");
  const auto spot = std::get<color::SpotColor>(restored.stroke.payload());
  check(spot.spot_name == "PANTONE test" && spot.tint == 0.65 &&
            spot.overprint && spot.alternate_rgb.a == 0.45 &&
            restored.stroke.overprint().stroke,
        "spot metadata lost");
  check(!restored.has_stroke &&
            restored.stroke_width_mm == path.stroke_width_mm &&
            restored.transform.tx == path.transform.tx,
        "path properties lost");
  const auto restored_text = std::get<TextObject>(*loaded->get_object(101));
  check(restored_text.text == text.text &&
            restored_text.font.postscript_name == text.font.postscript_name &&
            restored_text.font.weight == 700 && restored_text.font.italic &&
            restored_text.font.size_pt == 17.125,
        "source text/font lost");
  check(restored_text.fill.color_space() == color::ColorSpace::Grayscale,
        "gray source model lost");
  Document reopened(*loaded);
  check(reopened.id() == doc.id() && reopened.epoch() != doc.epoch() &&
            !reopened.is_dirty(),
        "reopen lifecycle wrong");
  check(reopened
                .execute_command(std::make_unique<TransformObjectCommand>(
                    100, geometry::Transform2D::translation(1, 2)))
                .success &&
            reopened.undo(),
        "reopened source is not editable");
  const auto repeated_bytes = NativeFormat::serialize(*doc.current_snapshot());
  const auto repeated =
      NativeFormat::deserialize(repeated_bytes.data(), repeated_bytes.size());
  check(repeated != nullptr, "source-less path repeat save failed to reopen");
  const auto first_source = std::get<PathObject>(*loaded->get_object(100));
  const auto repeated_source = std::get<PathObject>(*repeated->get_object(100));
  check(first_source.authoring_source && repeated_source.authoring_source &&
            first_source.authoring_source->subpaths.front().nodes.front().id ==
                repeated_source.authoring_source->subpaths.front().nodes.front().id,
        "source-less legacy path got different IDs on repeat save");
  Document epoch_source("epoch-source");
  DocumentSnapshot matching_epoch(epoch_source.epoch() + 1, 0,
                                  Page::make_default(), {},
                                  "saved-epoch-collision");
  Document reopened_collision(matching_epoch);
  check(reopened_collision.epoch() != matching_epoch.epoch() &&
            !reopened_collision.is_dirty(),
        "reopen reused the saved session epoch");
  auto corrupted = bytes;
  corrupted[20] ^= 1; // Revision header is protected too, not only payload.
  check(!NativeFormat::deserialize(corrupted.data(), corrupted.size()),
        "unprotected header mutation");
  for (size_t length = 0; length < bytes.size(); ++length)
    check(!NativeFormat::deserialize(bytes.data(), length),
          "truncated native file accepted");
  check(!NativeFormat::deserialize(nullptr, bytes.size()),
        "null input accepted");
  corrupted = bytes;
  corrupted[36] = 0xff;
  corrupted[37] = 0xff;
  corrupted[38] = 0xff;
  corrupted[39] = 0x7f;
  repair_checksum(corrupted);
  check(!NativeFormat::deserialize(corrupted.data(), corrupted.size()),
        "huge valid-checksum string allocation accepted");
  const auto marker = std::string_view("123456789");
  check(NativeFormat::calculate_crc32(
            reinterpret_cast<const uint8_t *>(marker.data()), marker.size()) ==
            0xcbf43926,
        "CRC32 known-answer vector failed");
}

void image_source_roundtrip() {
  Document doc("image-source-roundtrip");
  ImageObject image;
  image.id = generate_object_id();
  image.name = "RGBA source";
  image.bounds_rect = {2.25, 3.5, 4.75, 6.125};
  image.transform = geometry::Transform2D::translation(11.5, -7.25);
  image.width_px = 2;
  image.height_px = 2;
  image.pixels_rgba = {0x10203040, 0x50607080, 0x90a0b0c0, 0xd0e0f001};
  auto clip = geometry::Path2D::make_rect(0.25, 0.5, 1.5, 1.25);
  clip.set_winding_rule(geometry::WindingRule::EvenOdd);
  image.clip_path = clip;
  check(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success,
        "add image source");

  const auto bytes = NativeFormat::serialize(*doc.current_snapshot());
  std::string error;
  const auto reopened =
      NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(reopened && error.empty(), "native image did not reopen");
  const auto restored = std::get<ImageObject>(*reopened->get_object(image.id));
  check(restored.name == image.name &&
            restored.bounds_rect.approx_equal(image.bounds_rect) &&
            restored.transform.tx == image.transform.tx &&
            restored.transform.ty == image.transform.ty &&
            restored.width_px == image.width_px &&
            restored.height_px == image.height_px &&
            restored.pixels_rgba == image.pixels_rgba,
        "native image source properties or RGBA pixels changed");
  check(restored.clip_path &&
            restored.clip_path->winding_rule() == geometry::WindingRule::EvenOdd &&
            restored.clip_path->size() == clip.size() &&
            restored.clip_path->elements().front().pt ==
                clip.elements().front().pt,
        "native image clipping path changed");
}

void save_faults_and_paths() {
  const auto directory =
      std::filesystem::absolute("build/storage-fidelity-owned-test");
  std::filesystem::create_directories(directory);
  const auto target = directory / std::filesystem::path(u8"biển hiệu.pikdraw");
  Document doc("../document/with separators");
  PathObject path;
  path.id = generate_object_id();
  path.path = geometry::Path2D::make_rect(0, 0, 10, 10);
  check(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success,
        "add");
  check(SaveRecoveryService::atomic_save(utf8(target), *doc.current_snapshot())
            .success,
        "UTF8 save failed");
  const auto original = read_bytes(target);
  check(doc
            .execute_command(std::make_unique<TransformObjectCommand>(
                path.id, geometry::Transform2D::translation(2, 3)))
            .success,
        "transform");
  for (auto stage : {SaveFaultPoint::AfterWrite, SaveFaultPoint::BeforeFlush,
                     SaveFaultPoint::BeforeReplace}) {
    const auto result = SaveRecoveryService::atomic_save(
        utf8(target), *doc.current_snapshot(), {stage});
    check(!result.success && !result.error_message.empty(),
          "injected failure reports success");
    check(read_bytes(target) == original,
          "failed save damaged previous source file");
    for (const auto &entry : std::filesystem::directory_iterator(directory))
      check(utf8(entry.path().filename()).find(".tmp.") == std::string::npos,
            "save temp leaked");
  }
  check(SaveRecoveryService::atomic_save(utf8(target), *doc.current_snapshot())
            .success,
        "replace save failed");
  const auto loaded = SaveRecoveryService::load(utf8(target));
  check(loaded && loaded->revision() == 2 && loaded->document_id() == doc.id(),
        "UTF8 reopen failed");
  const auto recovery = directory / "recovery";
  std::string recovery_error;
  check(!SaveRecoveryService::write_recovery_checkpoint(
            utf8(recovery), "different-document", *loaded, &recovery_error) &&
            !recovery_error.empty(),
        "mismatched recovery identity was accepted or lost its diagnostic");
  const auto saved_bytes = read_bytes(target);
  check(!SaveRecoveryService::write_recovery_checkpoint(
            utf8(target), doc.id(), *loaded, &recovery_error) &&
            !recovery_error.empty() && read_bytes(target) == saved_bytes,
        "checkpoint directory failure was silent or damaged the source file");
  check(SaveRecoveryService::write_recovery_checkpoint(
            utf8(recovery), doc.id(), *loaded, &recovery_error),
        (std::string("checkpoint failed: ") + recovery_error).c_str());
  for (const auto &entry : std::filesystem::directory_iterator(recovery))
    check(entry.path().parent_path() == recovery,
          "document ID escaped recovery directory");
  check(SaveRecoveryService::find_latest_checkpoint(utf8(recovery), doc.id())
            .has_value(),
        "checkpoint not found");
  SaveRecoveryService::cleanup_checkpoints(utf8(recovery), doc.id());
  check(!SaveRecoveryService::find_latest_checkpoint(utf8(recovery), doc.id()),
        "checkpoint cleanup failed");
  Document long_id(std::string(512, 'x'));
  check(SaveRecoveryService::write_recovery_checkpoint(
            utf8(recovery), long_id.id(), *long_id.current_snapshot(),
            &recovery_error),
        (std::string("long ID checkpoint failed: ") + recovery_error).c_str());
  const auto long_id_checkpoint =
      SaveRecoveryService::find_latest_checkpoint(utf8(recovery), long_id.id());
  check(long_id_checkpoint.has_value(), "long ID recovery lookup failed");
  SaveRecoveryService::cleanup_checkpoints(utf8(recovery), long_id.id());
  // Remove only files/directories created by this test, without recursive
  // computed-path deletion.
  std::filesystem::remove(recovery);
  std::filesystem::remove(target);
  std::filesystem::remove(directory);
}

int main() {
  try {
    source_roundtrip();
    authoring_source_roundtrip_and_legacy_migration();
    appearance_codec_matches_independent_byte_fixtures();
    appearance_survives_native_v4_path_and_text_roundtrip();
    versioned_recovery_draft_isolated_from_documents();
    document_path_ids_are_globally_unique();
    image_source_roundtrip();
    save_faults_and_paths();
    std::cout << "PASS source fidelity, malformed file quotas, checksum, save "
                 "faults, UTF8 paths and reopen\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
