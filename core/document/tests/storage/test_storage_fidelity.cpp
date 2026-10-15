#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
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
    save_faults_and_paths();
    std::cout << "PASS source fidelity, malformed file quotas, checksum, save "
                 "faults, UTF8 paths and reopen\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
