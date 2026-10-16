#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "../../src/storage/byte_codec.hpp"
#include <filesystem>
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
  check(bytes.size() > 36 && bytes[4] == 3,
        "native writer did not emit container v3");
  std::string error;
  const auto reopened =
      NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  check(reopened && error.empty(), "v3 authoring path did not reopen");
  const auto restored = std::get<PathObject>(*reopened->get_object(path.id));
  check(restored.authoring_source &&
            restored.authoring_source->subpaths.front().id == SubpathId{9002} &&
            restored.authoring_source->subpaths.front().nodes[1].id == NodeId{9004} &&
            restored.authoring_source->subpaths.front().segments.front().id ==
                SegmentId{9005},
        "v3 changed stable path identities");
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
  const auto migrated_id =
      migrated_path.authoring_source->subpaths.front().nodes.front().id;
  const auto migrated_v3 = NativeFormat::serialize(*migrated);
  const auto migrated_again =
      NativeFormat::deserialize(migrated_v3.data(), migrated_v3.size());
  check(migrated_again &&
            std::get<PathObject>(*migrated_again->get_object(legacy_path.id))
                    .authoring_source->subpaths.front().nodes.front().id ==
                migrated_id,
        "v2 migration identities were not persisted by v3");

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
  future[4] = 4;
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
    versioned_recovery_draft_isolated_from_documents();
    document_path_ids_are_globally_unique();
    save_faults_and_paths();
    std::cout << "PASS source fidelity, malformed file quotas, checksum, save "
                 "faults, UTF8 paths and reopen\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
