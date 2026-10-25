#include "pikdraw/document/commands/clip_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/contour_commands.hpp"
#include "pikdraw/document/commands/dimension_commands.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "../../src/storage/byte_codec.hpp"
#include <algorithm>
#include <filesystem>
#include <array>
#include <atomic>
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

void write_u32_be(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value >> 24);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 3] = static_cast<uint8_t>(value);
}

std::shared_ptr<const color::ProfileResource> make_test_icc_profile(
    std::string name, uint8_t marker,
    color::ColorSpace space = color::ColorSpace::Rgb) {
  std::vector<uint8_t> bytes(148, 0);
  write_u32_be(bytes, 0, static_cast<uint32_t>(bytes.size()));
  const auto* signature = space == color::ColorSpace::Grayscale ? "GRAY" :
      space == color::ColorSpace::Cmyk ? "CMYK" : "RGB ";
  std::copy_n(signature, 4, bytes.begin() + 16);
  std::copy_n("XYZ ", 4, bytes.begin() + 20);
  std::copy_n("acsp", 4, bytes.begin() + 36);
  write_u32_be(bytes, 128, 1);
  std::copy_n("test", 4, bytes.begin() + 132);
  write_u32_be(bytes, 136, 144);
  write_u32_be(bytes, 140, 4);
  bytes[144] = marker;
  auto created = color::ProfileResource::create(
      std::move(name), space, std::move(bytes));
  check(static_cast<bool>(created), "synthetic ICC resource failed validation");
  return std::move(created.resource);
}


void appearance_codec_matches_independent_byte_fixtures();
void appearance_survives_native_v4_path_and_text_roundtrip();
void extended_effects_reject_legacy_node_schema();
void authoring_source_roundtrip_and_legacy_migration();

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
  text.strikethrough = true;
  text.letter_spacing_mm = 0.375;
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
            restored_text.font.size_pt == 17.125 && restored_text.strikethrough &&
            restored_text.letter_spacing_mm == 0.375,
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
  image.shared_pixels_rgba = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0x10203040, 0x50607080, 0x90a0b0c0, 0xd0e0f001});
  std::vector<uint8_t> encoded_samples16(32);
  for (size_t index = 0; index < encoded_samples16.size(); ++index)
    encoded_samples16[index] = static_cast<uint8_t>(index * 7);
  image.shared_pixels_rgba16le =
      std::make_shared<const std::vector<uint8_t>>(encoded_samples16);
  image.source.media_type = "image/png";
  std::vector<uint8_t> encoded_source(256u * 1024u);
  uint32_t source_seed = 0xC001D00Du;
  for (auto& byte : encoded_source) {
    source_seed ^= source_seed << 13;
    source_seed ^= source_seed >> 17;
    source_seed ^= source_seed << 5;
    byte = static_cast<uint8_t>(source_seed);
  }
  const uint8_t png_signature[]{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::copy(std::begin(png_signature), std::end(png_signature),
            encoded_source.begin());
  image.source.shared_original_bytes =
      std::make_shared<const std::vector<uint8_t>>(encoded_source);
  image.source.dpi_x = 300.0;
  image.source.dpi_y = 150.0;
  image.source.dpi_from_source = true;
  image.source.frame_index = 299;
  image.source.frame_count = 300;
  image.source.source_bits_per_pixel = 32;
  image.source.exif_orientation = 6;
  image.source.orientation_applied = true;
  image.source.icc_profile = {0, 1, 2, 3, 4, 5};
  image.source.preview_pixel_color_basis = RasterPixelColorBasis::ConvertedToSRgb;
  auto clip = geometry::Path2D::make_rect(0.25, 0.5, 1.5, 1.25);
  clip.set_winding_rule(geometry::WindingRule::EvenOdd);
  image.clip_path = clip;
  check(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success,
        "add image source");
  ImageObject duplicate = image;
  duplicate.id = generate_object_id();
  duplicate.name = "Equal encoded source with independent allocation";
  duplicate.source.shared_original_bytes =
      std::make_shared<const std::vector<uint8_t>>(image.source.original_data());
  duplicate.shared_pixels_rgba =
      std::make_shared<const std::vector<uint32_t>>(
          image.rgba_pixels().begin(), image.rgba_pixels().end());
  duplicate.shared_pixels_rgba16le =
      std::make_shared<const std::vector<uint8_t>>(
          *image.shared_pixels_rgba16le);
  check(duplicate.source.shared_original_bytes != image.source.shared_original_bytes &&
            duplicate.shared_pixels_rgba != image.shared_pixels_rgba &&
            duplicate.shared_pixels_rgba16le != image.shared_pixels_rgba16le &&
            doc.execute_command(std::make_unique<AddObjectCommand>(duplicate)).success,
        "add equal raster source and samples with separate allocations");
  ImageObject varied_preview = image;
  varied_preview.id = generate_object_id();
  varied_preview.name = "Same source with a distinct decoded preview";
  varied_preview.source.shared_original_bytes =
      std::make_shared<const std::vector<uint8_t>>(image.source.original_data());
  auto varied_pixels = std::vector<uint32_t>(image.rgba_pixels().begin(),
                                             image.rgba_pixels().end());
  varied_pixels.front() ^= 0x000000ffu;
  varied_preview.shared_pixels_rgba =
      std::make_shared<const std::vector<uint32_t>>(std::move(varied_pixels));
  check(doc.execute_command(
            std::make_unique<AddObjectCommand>(varied_preview)).success,
        "add same source with distinct decoded preview");
  ImageObject distinct_source = image;
  distinct_source.id = generate_object_id();
  distinct_source.name = "Different encoded source";
  auto distinct_bytes =
      std::make_shared<std::vector<uint8_t>>(encoded_source);
  (*distinct_bytes)[1024] ^= 0x5a;
  distinct_source.source.shared_original_bytes = distinct_bytes;
  check(doc.execute_command(std::make_unique<AddObjectCommand>(distinct_source)).success,
        "add distinct encoded raster source");

  const auto bytes = NativeFormat::serialize(*doc.current_snapshot());
  check(bytes.size() > kChunkedContainerHeaderBytes &&
            bytes[4] == kChunkedContainerFormatVersion,
        "embedded raster assets did not select the deduplicated format schema");
  std::string error;
  const auto reopened =
      NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(reopened && error.empty(), "native image did not reopen");
  const auto restored = std::get<ImageObject>(*reopened->get_object(image.id));
  check(restored.shared_pixels_rgba && restored.pixels_rgba.empty() &&
            restored.source.shared_original_bytes && restored.source.original_bytes.empty() &&
            restored.shared_pixels_rgba16le &&
            *restored.shared_pixels_rgba16le == encoded_samples16,
        "reopened image assets must remain shared for transform and Undo");
  check(restored.name == image.name &&
            restored.bounds_rect.approx_equal(image.bounds_rect) &&
            restored.transform.tx == image.transform.tx &&
            restored.transform.ty == image.transform.ty &&
            restored.width_px == image.width_px &&
            restored.height_px == image.height_px &&
            std::equal(restored.rgba_pixels().begin(), restored.rgba_pixels().end(),
                       image.rgba_pixels().begin(), image.rgba_pixels().end()) &&
            restored.source.media_type == image.source.media_type &&
            restored.source.original_data() == image.source.original_data() &&
            restored.source.dpi_x == image.source.dpi_x &&
            restored.source.dpi_y == image.source.dpi_y &&
            restored.source.dpi_from_source &&
            restored.source.frame_index == 299 &&
            restored.source.frame_count == 300 &&
            restored.source.source_bits_per_pixel == 32 &&
            restored.source.exif_orientation == 6 &&
            restored.source.orientation_applied &&
            restored.source.icc_profile == image.source.icc_profile &&
            restored.source.preview_pixel_color_basis ==
                image.source.preview_pixel_color_basis,
        "native image source properties or RGBA pixels changed");
  check(restored.clip_path &&
            restored.clip_path->winding_rule() == geometry::WindingRule::EvenOdd &&
            restored.clip_path->size() == clip.size() &&
            restored.clip_path->elements().front().pt ==
                clip.elements().front().pt,
        "native image clipping path changed");
  const auto restored_duplicate =
      std::get<ImageObject>(*reopened->get_object(duplicate.id));
  const auto restored_varied =
      std::get<ImageObject>(*reopened->get_object(varied_preview.id));
  const auto restored_distinct =
      std::get<ImageObject>(*reopened->get_object(distinct_source.id));
  check(restored.source.shared_original_bytes &&
            restored.source.shared_original_bytes ==
                restored_duplicate.source.shared_original_bytes &&
            restored_duplicate.source.original_data() == image.source.original_data() &&
            restored_duplicate.name == duplicate.name &&
            restored_duplicate.transform.tx == duplicate.transform.tx &&
            restored.shared_pixels_rgba ==
                restored_duplicate.shared_pixels_rgba &&
            restored.shared_pixels_rgba16le ==
                restored_duplicate.shared_pixels_rgba16le &&
            restored.source.shared_original_bytes ==
                restored_varied.source.shared_original_bytes &&
            restored.shared_pixels_rgba != restored_varied.shared_pixels_rgba &&
            restored.shared_pixels_rgba16le ==
                restored_varied.shared_pixels_rgba16le &&
            restored_varied.rgba_pixels().front() !=
                restored.rgba_pixels().front() &&
            restored_distinct.source.shared_original_bytes &&
            restored_distinct.source.shared_original_bytes !=
                restored.source.shared_original_bytes &&
            restored_distinct.shared_pixels_rgba ==
                restored.shared_pixels_rgba &&
            restored_distinct.shared_pixels_rgba16le ==
                restored.shared_pixels_rgba16le &&
            restored_distinct.source.original_data() ==
                distinct_source.source.original_data(),
        "native raster source table did not deduplicate equal encoded assets");
  Document unique_sources_doc("raster-source-nondeduplicated-size");
  for (uint32_t index = 0; index < 4; ++index) {
    ImageObject unique = image;
    unique.id = generate_object_id();
    unique.name = "Unique source " + std::to_string(index);
    auto unique_bytes = std::make_shared<std::vector<uint8_t>>(encoded_source);
    (*unique_bytes)[2048 + index] ^= static_cast<uint8_t>(index + 1);
    unique.source.shared_original_bytes = unique_bytes;
    check(unique_sources_doc.execute_command(
              std::make_unique<AddObjectCommand>(std::move(unique))).success,
          "add unique source size baseline");
  }
  const auto unique_source_bytes =
      NativeFormat::serialize(*unique_sources_doc.current_snapshot());
  check(unique_source_bytes.size() > bytes.size() + encoded_source.size() / 2,
        "raster asset table did not reduce repeated encoded source storage");

  ImageObject invalid = image;
  invalid.id = generate_object_id();
  invalid.source.dpi_x = 0.0;
  Document invalid_doc("invalid-raster-metadata");
  check(!invalid_doc.execute_command(
            std::make_unique<AddObjectCommand>(invalid)).success,
        "invalid raster resolution metadata was accepted");

  ImageObject oversized_icc = image;
  oversized_icc.id = generate_object_id();
  oversized_icc.source.icc_profile.resize(
      RasterSourceMetadata::kMaxIccProfileBytes + 1);
  Document oversized_doc("oversized-raster-profile");
  check(!oversized_doc.execute_command(
            std::make_unique<AddObjectCommand>(oversized_icc)).success,
        "oversized embedded raster ICC profile was accepted");
}

void locked_pdf_source_roundtrip() {
  auto asset = std::make_shared<PdfSourceAsset>();
  asset->sha256_hex = std::string(64, 'b');
  asset->page_count = 2;
  const std::vector<uint8_t> source_bytes{'%', 'P', 'D', 'F', '-', '1', '.', '7', '\n'};
  asset->bytes = std::make_shared<const std::vector<uint8_t>>(source_bytes);

  Page first_layout = Page::make_default();
  first_layout.width_mm = 215.9;
  first_layout.height_mm = 279.4;
  first_layout.display_length_unit = geometry::units::LengthUnit::Inch;
  first_layout.display_length_unit_explicit = true;
  first_layout.color_mode = color::DocumentColorMode::Cmyk;
  first_layout.color_profile_name = "Source CMYK profile";
  first_layout.raster_dpi = 150;
  first_layout.color_mode_explicit = true;
  Page second_layout = first_layout;
  second_layout.width_mm = 279.4;
  second_layout.height_mm = 215.9;
  second_layout.display_length_unit = geometry::units::LengthUnit::Meter;
  second_layout.color_mode = color::DocumentColorMode::Rgb;
  second_layout.color_profile_name = "Source RGB profile";
  second_layout.raster_dpi = 300;
  second_layout.raster_dpi_explicit = true;
  auto first = make_document_page("PDF Page 1", first_layout);
  auto second = make_document_page("PDF Page 2", second_layout);
  const auto first_preview = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0xff0000ffu, 0x00ff00ffu, 0x0000ffffu, 0xffffffffu});
  const auto second_preview = std::make_shared<const std::vector<uint32_t>>(
      std::vector<uint32_t>{0x112233ffu, 0x445566ffu, 0x778899ffu, 0xaabbccffu});
  first.pdf_source = PdfSourcePageReference{asset, 0, 0, 2, 2, first_preview};
  second.pdf_source = PdfSourcePageReference{asset, 1, 3, 2, 2, second_preview};
  DocumentSnapshot snapshot{9, 12, {first, second}, second.id,
                            "locked-pdf-roundtrip", DocumentContentMode::PdfPrintLocked};

  const auto bytes = NativeFormat::serialize(snapshot);
  check(bytes.size() > 36 && bytes[4] == kArtboardsFormatVersion,
        "locked PDF source preview did not use the current native format");
  std::string error;
  const auto restored = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(restored && error.empty(), "locked PDF source did not reopen");
  check(restored->is_content_locked() && restored->page_count() == 2 &&
            restored->active_page_id() == second.id,
        "locked PDF document mode or active page changed on reopen");
  check(restored->pages()[0].layout.display_length_unit == geometry::units::LengthUnit::Inch &&
            restored->pages()[0].layout.display_length_unit_explicit &&
            restored->pages()[1].layout.display_length_unit == geometry::units::LengthUnit::Meter &&
            restored->pages()[1].layout.display_length_unit_explicit,
        "per-page display unit metadata changed on native roundtrip");
  check(restored->pages()[0].layout.color_mode == color::DocumentColorMode::Cmyk &&
            restored->pages()[0].layout.color_profile_name == "Source CMYK profile" &&
            restored->pages()[0].layout.raster_dpi == 150 &&
            restored->pages()[0].layout.color_mode_explicit &&
            !restored->pages()[0].layout.raster_dpi_explicit &&
            restored->pages()[1].layout.color_mode == color::DocumentColorMode::Rgb &&
            restored->pages()[1].layout.color_profile_name == "Source RGB profile" &&
            restored->pages()[1].layout.raster_dpi == 300 &&
            restored->pages()[1].layout.color_mode_explicit &&
            restored->pages()[1].layout.raster_dpi_explicit,
        "per-page color mode, profile label, DPI, or explicit state changed on native roundtrip");
  const auto& restored_first = restored->pages()[0].pdf_source;
  const auto& restored_second = restored->pages()[1].pdf_source;
  check(restored_first && restored_second &&
            restored_first->asset == restored_second->asset &&
            restored_first->asset->sha256_hex == asset->sha256_hex &&
            *restored_first->asset->bytes == source_bytes &&
            restored_first->source_page_index == 0 &&
            restored_first->preview_width_px == 2 &&
            restored_first->preview_height_px == 2 &&
            *restored_first->preview_rgba == *first_preview &&
            restored_second->source_page_index == 1 &&
            restored_second->rotation_quarters_clockwise == 3 &&
            restored_second->preview_width_px == 2 &&
            restored_second->preview_height_px == 2 &&
            *restored_second->preview_rgba == *second_preview,
        "PDF source bytes, per-page locator, or screen preview changed on reopen");
  Document reopened(*restored);
  check(reopened.is_content_locked() && !reopened.execute_command(
            std::make_unique<AddObjectCommand>(PathObject{})).success,
        "native reopen made a locked PDF editable");
}

void page_icc_resources_roundtrip_and_deduplicate() {
  const auto profile = make_test_icc_profile("Synthetic page RGB", 0x52);
  Page first_layout = Page::make_default();
  first_layout.color_profile_name = profile->identity().name;
  first_layout.color_profile_resource = profile;
  first_layout.color_mode_explicit = true;
  Page second_layout = first_layout;
  second_layout.width_mm = 400.0;
  auto first = make_document_page("RGB page 1", first_layout);
  auto second = make_document_page("RGB page 2", second_layout);
  DocumentSnapshot snapshot{3, 7, {first, second}, first.id, "icc-page-roundtrip"};

  const auto bytes = NativeFormat::serialize(snapshot);
  check(bytes.size() > 36 && bytes[4] == kArtboardsFormatVersion,
        "page ICC data did not use the current native schema");
  std::string error;
  const auto restored = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(restored && error.empty(), "page ICC document failed native reopen");
  const auto& first_resource = restored->pages()[0].layout.color_profile_resource;
  const auto& second_resource = restored->pages()[1].layout.color_profile_resource;
  check(first_resource && first_resource == second_resource &&
            first_resource->identity().checksum_sha256 == profile->identity().checksum_sha256 &&
            first_resource->identity().name == profile->identity().name &&
            first_resource->bytes() == profile->bytes(),
        "native roundtrip lost the ICC bytes, digest, or shared page resource identity");
  check(restored->pages()[0].layout.color_profile_name == "Synthetic page RGB" &&
            restored->pages()[1].layout.color_profile_name == "Synthetic page RGB",
        "native roundtrip changed page profile labels");

  auto corrupt_digest = bytes;
  const auto& digest = profile->identity().checksum_sha256;
  const auto digest_position = std::search(corrupt_digest.begin() + 36,
      corrupt_digest.end(), digest.begin(), digest.end());
  check(digest_position != corrupt_digest.end(),
        "native ICC digest field was not serialized");
  *digest_position = *digest_position == '0' ? '1' : '0';
  repair_checksum(corrupt_digest);
  const auto rejected = NativeFormat::deserialize(
      corrupt_digest.data(), corrupt_digest.size(), &error);
  check(!rejected && error.find("digest mismatch") != std::string::npos,
        "native ICC dictionary accepted a digest that did not match its bytes");
}

void path_feature_specs_survive_native_roundtrip() {
  clear_all_attached_contours();
  DimensionRegistry::instance().clear();
  Document doc("native-path-feature-roundtrip");

  PathObject source;
  source.id = generate_object_id();
  source.name = "Measured contour source";
  source.path = geometry::Path2D::make_rect(0.0, 0.0, 120.0, 60.0);
  check(doc.execute_command(std::make_unique<AddObjectCommand>(source)).success,
        "path feature source could not be added");

  DimensionObject dimension;
  dimension.id = generate_object_id();
  dimension.kind = DimensionKind::Linear;
  dimension.linear_orientation = geometry::LinearOrientation::Horizontal;
  dimension.placement_point = {60.0, 80.0};
  dimension.style.units = UnitType::Foot;
  dimension.style.dual_enabled = true;
  dimension.style.dual_units = UnitType::Pica;
  SourceReference first;
  first.source_object_id = source.id;
  first.ref_kind = SourceRefKind::ExplicitPoint;
  first.explicit_point = {0.0, 60.0};
  SourceReference second;
  second.source_object_id = source.id;
  second.ref_kind = SourceRefKind::ExplicitPoint;
  second.explicit_point = {120.0, 60.0};
  dimension.references = {first, second};
  check(doc.execute_command(std::make_unique<AddDimensionCommand>(dimension)).success,
        "Dimension could not be added before native save");

  ContourFeature contour;
  contour.spacing_mm = 2.5;
  contour.steps = 3;
  contour.direction = geometry::ContourDirection::Outside;
  const auto contour_cmyk_profile = make_test_icc_profile(
      "Contour CMYK", 0x63, color::ColorSpace::Cmyk);
  contour.outline_color = color::ColorValue::make_cmyk(0.12, 0.28, 0.46, 0.17, 0.58);
  check(contour.outline_color.set_profile_resource(contour_cmyk_profile),
        "Contour CMYK profile could not be bound");
  contour.outline_color.set_overprint({true, false});
  contour.fill_color = color::SpotColor{
      "CutContour", 0.72, {0.16, 0.61, 0.37, 0.43}, true};
  contour.fill_color.set_overprint({false, true});
  check(doc.execute_command(std::make_unique<ApplyContourCommand>(source.id, contour)).success,
        "Contour could not be applied before native save");

  check(doc.undo() && doc.undo(), "path-feature Undo did not reach the source state");
  check(doc.redo() && doc.redo(), "path-feature Redo did not restore both features");
  const auto bytes = NativeFormat::serialize(*doc.current_snapshot());
  check(bytes.size() > 36 && bytes[4] == kArtboardsFormatVersion,
        "path feature metadata did not use the current native schema");
  std::string error;
  const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(reopened && error.empty(), "Dimension/Contour document failed native reopen");

  Document restored(*reopened);
  const auto restored_dimension = get_dimension(restored, dimension.id);
  check(restored_dimension &&
            restored_dimension->style.units == UnitType::Foot &&
            restored_dimension->style.dual_enabled &&
            restored_dimension->style.dual_units == UnitType::Pica &&
            restored_dimension->references.size() == 2 &&
            restored_dimension->references[0].source_object_id == source.id &&
            restored_dimension->references[1].explicit_point.x == 120.0,
        "native reopen lost Dimension units or source association");
  const auto restored_contour = get_object_contour(restored, source.id);
  check(restored_contour && restored_contour->steps == 3 &&
            restored_contour->spacing_mm == 2.5 &&
            restored_contour->direction == geometry::ContourDirection::Outside &&
            !restored_contour->derived_loops.empty(),
        "native reopen lost Contour parameters or derived geometry");
  const auto& restored_outline = restored_contour->outline_color;
  const auto& restored_fill = restored_contour->fill_color;
  const auto* restored_cmyk = std::get_if<color::CMYKColor>(&restored_outline.payload());
  const auto* restored_spot = std::get_if<color::SpotColor>(&restored_fill.payload());
  check(restored_cmyk && restored_cmyk->c == 0.12 && restored_cmyk->m == 0.28 &&
            restored_cmyk->y == 0.46 && restored_cmyk->k == 0.17 &&
            restored_cmyk->a == 0.58 && restored_outline.overprint().fill &&
            !restored_outline.overprint().stroke && restored_outline.profile_resource() &&
            restored_outline.profile_resource()->identity().checksum_sha256 ==
                contour_cmyk_profile->identity().checksum_sha256 &&
            restored_outline.profile_resource()->bytes() == contour_cmyk_profile->bytes(),
        "native reopen lost Contour CMYK channels, alpha, overprint, or ICC binding");
  check(restored_spot && restored_spot->spot_name == "CutContour" &&
            restored_spot->tint == 0.72 && restored_spot->alternate_rgb.r == 0.16 &&
            restored_spot->alternate_rgb.g == 0.61 &&
            restored_spot->alternate_rgb.b == 0.37 &&
            restored_spot->alternate_rgb.a == 0.43 && restored_spot->overprint &&
            !restored_fill.overprint().fill && restored_fill.overprint().stroke,
        "native reopen lost Contour Spot identity, tint, alternate alpha, or overprint");

  auto invalid_units = bytes;
  const std::string valid_unit = ";unit=4";
  const auto unit_position = std::search(invalid_units.begin() + 36,
      invalid_units.end(), valid_unit.begin(), valid_unit.end());
  check(unit_position != invalid_units.end(), "Dimension units were not serialized");
  *(unit_position + 6) = '8';
  repair_checksum(invalid_units);
  const auto rejected = NativeFormat::deserialize(
      invalid_units.data(), invalid_units.size(), &error);
  check(!rejected && error.find("Invalid or unsupported primary dimension unit") != std::string::npos,
        "native reader accepted an unsupported Dimension unit");
}

void paint_icc_bindings_roundtrip_across_native_color_sites() {
  const auto rgb_profile = make_test_icc_profile("Paint RGB", 0x61);
  const auto gray_profile = make_test_icc_profile(
      "Paint Gray", 0x62, color::ColorSpace::Grayscale);
  color::ColorValue rgb{color::RGBColor{0.2, 0.4, 0.6, 0.75}};
  color::ColorValue gray{color::GrayscaleColor{0.42, 0.8}};
  check(rgb.set_profile_resource(rgb_profile) &&
            gray.set_profile_resource(gray_profile),
        "matching paint ICC profiles could not be bound");
  auto mismatched = color::ColorValue{color::RGBColor{0.1, 0.2, 0.3, 1.0}};
  check(!mismatched.set_profile_resource(gray_profile),
        "paint accepted an ICC resource with a different color model");

  Page page = Page::make_default();
  page.color_profile_name = rgb_profile->identity().name;
  page.color_profile_resource = rgb_profile;
  PathObject path;
  path.id = generate_object_id();
  path.name = "ICC paint path";
  path.path = geometry::Path2D::make_rect(0, 0, 12, 8);
  path.fill = rgb;
  path.stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0};
  path.has_stroke = false;

  GradientPaint gradient;
  gradient.id = generate_object_id();
  gradient.stops = {
      GradientStop{generate_object_id(), 0.0, rgb, 1.0, 0.5},
      GradientStop{generate_object_id(), 1.0, gray, 1.0, 0.5}};
  check(gradient.is_valid(), "ICC gradient fixture is invalid");
  path.fill_gradient = std::make_shared<const GradientPaint>(gradient);

  DropShadowEffect shadow;
  shadow.color = rgb;
  path.appearance.effects.push_back(
      EffectNode{generate_effect_instance_id(), 1, 1, true, shadow});

  TextObject text;
  text.id = generate_object_id();
  text.name = "ICC paint text";
  text.text = "ICC";
  text.fill = rgb;
  page.layers.front().object_ids = {path.id, text.id};
  PersistentObjectMap objects;
  objects = objects.set(path.id, path);
  objects = objects.set(text.id, text);
  DocumentSnapshot snapshot{5, 9, page, objects, "paint-icc-roundtrip"};

  const auto bytes = NativeFormat::serialize(snapshot);
  std::string error;
  const auto restored = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(restored && error.empty(), "paint ICC document failed native reopen");
  const auto restored_path_value = restored->get_object(path.id);
  const auto restored_text_value = restored->get_object(text.id);
  check(restored_path_value && std::holds_alternative<PathObject>(*restored_path_value) &&
            restored_text_value && std::holds_alternative<TextObject>(*restored_text_value),
        "native reopen lost an ICC-bound path or text object");
  const auto& restored_path = std::get<PathObject>(*restored_path_value);
  const auto& restored_text = std::get<TextObject>(*restored_text_value);
  const auto& restored_shadow = std::get<DropShadowEffect>(
      restored_path.appearance.effects.front().parameters);
  check(restored_path.fill.profile_resource() ==
                restored->pages().front().layout.color_profile_resource &&
            restored_text.fill.profile_resource() == restored_path.fill.profile_resource() &&
            restored_shadow.color.profile_resource() == restored_path.fill.profile_resource(),
        "solid, text, effect, and page paints did not share the canonical ICC resource");
  check(restored_path.fill_gradient &&
            restored_path.fill_gradient->stops[0].color.profile_resource() ==
                restored_path.fill.profile_resource() &&
            restored_path.fill_gradient->stops[1].color.profile_resource() &&
            restored_path.fill_gradient->stops[1].color.profile_resource()->bytes() ==
                gray_profile->bytes(),
        "gradient stop ICC bindings or Gray profile bytes changed on reopen");
  check(!restored_path.stroke.profile_resource(),
        "unbound paint became explicitly profile-bound on native reopen");

  const auto saved_again = NativeFormat::serialize(*restored);
  const auto reopened_again = NativeFormat::deserialize(
      saved_again.data(), saved_again.size(), &error);
  check(reopened_again && error.empty(),
        "resolved paint ICC bindings did not survive a second save/reopen");
}

void save_faults_and_paths() {
  const auto directory =
      std::filesystem::absolute("build/storage-fidelity-owned-test");
  std::filesystem::create_directories(directory);
  const auto target = directory / std::filesystem::path(u8"biển hiệu.pr");
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
  std::atomic_bool cancel_load{true};
  std::string cancellation_error;
  const auto cancelled_memory_load = NativeFormat::deserialize(
      original.data(), original.size(), &cancellation_error, &cancel_load);
  check(!cancelled_memory_load &&
            cancellation_error.find("cancelled") != std::string::npos,
        "cancelled in-memory native decode returned a snapshot");
  cancellation_error.clear();
  const auto cancelled_file_load = SaveRecoveryService::load(
      utf8(target), &cancellation_error, &cancel_load);
  check(!cancelled_file_load &&
            cancellation_error.find("cancelled") != std::string::npos,
        "cancelled file native load returned a snapshot");
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

void powerclip_source_and_flags_roundtrip() {
    Document doc("clip-storage");
    PathObject frame;frame.id=generate_object_id();frame.path=geometry::Path2D::make_rect(3,4,20,30);frame.name="Khung";
    ImageObject image;image.id=generate_object_id();image.width_px=1;image.height_px=1;image.bounds_rect={0,0,40,20};
    image.shared_pixels_rgba=std::make_shared<const std::vector<uint32_t>>(std::vector<uint32_t>{0xff0088ff});
    check(doc.execute_command(std::make_unique<AddObjectCommand>(frame)).success,"add frame");
    check(doc.execute_command(std::make_unique<AddObjectCommand>(image)).success,"add image");
    auto command=std::make_unique<ClipCommand>(ClipOperation::Place,frame.id,std::vector<ObjectId>{image.id});
    const auto clip=command->result_id();check(doc.execute_command(std::move(command)).success,"place storage image");
    check(doc.execute_command(std::make_unique<TransformObjectCommand>(clip,geometry::Transform2D::translation(15,7)*geometry::Transform2D::rotation_degrees(23))).success,"clip transform");
    const auto source=doc.current_snapshot();
    const auto bytes=NativeFormat::serialize(*source);std::string error;
    const auto reopened=NativeFormat::deserialize(bytes.data(),bytes.size(),&error);
    check(bool(reopened),"clip reopen failed");
    const auto group=std::get<GroupObject>(*reopened->objects().get(clip));
    check(group.clip&&group.clip->mask_object_id==frame.id&&group.clip->lock_contents_to_frame,"clip flags lost");
    check(reopened->hierarchy().find(image.id)->world.map_point({2,3}).distance_to(source->hierarchy().find(image.id)->world.map_point({2,3}))<1e-8,"clip world transform lost");
    const auto restored=std::get<ImageObject>(*reopened->objects().get(image.id));
    check(restored.rgba_pixels().size()==1&&restored.rgba_pixels()[0]==image.rgba_pixels()[0],"clip raster source lost");
}

void layer_properties_survive_native_roundtrip() {
  Document doc("layer-properties-storage");
  check(doc.execute_command(std::make_unique<AddLayerCommand>("Print and color")).success,
        "add layer for property roundtrip");
  check(doc.execute_command(std::make_unique<SetLayerPrintableCommand>(1, false)).success,
        "disable layer printing");
  constexpr uint32_t layer_color = 0xEF4444FF;
  check(doc.execute_command(std::make_unique<SetLayerColorCommand>(1, layer_color)).success,
        "set layer color");

  const auto bytes = NativeFormat::serialize(*doc.current_snapshot());
  check(bytes.size() > 36 && bytes[4] == kArtboardsFormatVersion,
        "layer properties did not use the current extended native schema");
  std::string error;
  const auto reopened = NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(reopened && error.empty(), "layer properties failed native reopen");
  const auto& restored = reopened->page().layers.at(1);
  check(!restored.printable && restored.color_rgba == layer_color,
        "native reopen lost layer printable or color state");
}

int main() {
  powerclip_source_and_flags_roundtrip();
  try {
    source_roundtrip();
    authoring_source_roundtrip_and_legacy_migration();
    appearance_codec_matches_independent_byte_fixtures();
    appearance_survives_native_v4_path_and_text_roundtrip();
    extended_effects_reject_legacy_node_schema();
    versioned_recovery_draft_isolated_from_documents();
    document_path_ids_are_globally_unique();
    image_source_roundtrip();
    locked_pdf_source_roundtrip();
    page_icc_resources_roundtrip_and_deduplicate();
    path_feature_specs_survive_native_roundtrip();
    paint_icc_bindings_roundtrip_across_native_color_sites();
    layer_properties_survive_native_roundtrip();
    save_faults_and_paths();
    std::cout << "PASS source fidelity, malformed file quotas, checksum, save "
                 "faults, UTF8 paths and reopen\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
