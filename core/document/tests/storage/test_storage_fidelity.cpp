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


void appearance_codec_matches_independent_byte_fixtures();
void appearance_survives_native_v4_path_and_text_roundtrip();
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
  image.source.media_type = "image/png";
  image.source.original_bytes = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  image.source.dpi_x = 300.0;
  image.source.dpi_y = 150.0;
  image.source.dpi_from_source = true;
  image.source.frame_index = 1;
  image.source.frame_count = 2;
  image.source.source_bits_per_pixel = 32;
  image.source.exif_orientation = 6;
  image.source.orientation_applied = true;
  image.source.icc_profile = {0, 1, 2, 3, 4, 5};
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
            restored.pixels_rgba == image.pixels_rgba &&
            restored.source.media_type == image.source.media_type &&
            restored.source.original_bytes == image.source.original_bytes &&
            restored.source.dpi_x == image.source.dpi_x &&
            restored.source.dpi_y == image.source.dpi_y &&
            restored.source.dpi_from_source &&
            restored.source.frame_index == 1 &&
            restored.source.frame_count == 2 &&
            restored.source.source_bits_per_pixel == 32 &&
            restored.source.exif_orientation == 6 &&
            restored.source.orientation_applied &&
            restored.source.icc_profile == image.source.icc_profile,
        "native image source properties or RGBA pixels changed");
  check(restored.clip_path &&
            restored.clip_path->winding_rule() == geometry::WindingRule::EvenOdd &&
            restored.clip_path->size() == clip.size() &&
            restored.clip_path->elements().front().pt ==
                clip.elements().front().pt,
        "native image clipping path changed");

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
