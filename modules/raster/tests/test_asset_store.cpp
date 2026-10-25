#include "pikdraw/raster/asset_store.hpp"
#include "pikdraw/raster/raster_source_restore.hpp"
#include "pikdraw/document/storage/native_format.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

void check(bool condition, const char* expression) {
  if (condition) return;
  std::cerr << "[FAIL] " << expression << '\n';
  std::exit(1);
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression)

class TestDirectory final {
 public:
  TestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
        ("pikdraw-raster-assets-" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TestDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

uint8_t expected_byte(uint64_t offset) {
  return static_cast<uint8_t>((offset * 131u + 17u) & 0xffu);
}

void write_source(const std::filesystem::path& path, uint64_t bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  CHECK(output);
  std::array<uint8_t, 64 * 1024> buffer{};
  uint64_t written = 0;
  while (written < bytes) {
    const auto count = static_cast<size_t>((std::min)(
        static_cast<uint64_t>(buffer.size()), bytes - written));
    for (size_t i = 0; i < count; ++i)
      buffer[i] = expected_byte(written + i);
    output.write(reinterpret_cast<const char*>(buffer.data()),
                 static_cast<std::streamsize>(count));
    CHECK(output);
    written += count;
  }
}

bool create_sparse_source(const std::filesystem::path& path, uint64_t bytes,
                          uint64_t marker_offset,
                          std::span<const uint8_t> marker) {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD ignored = 0;
  const bool sparse = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0,
      nullptr, 0, &ignored, nullptr) != FALSE;
  LARGE_INTEGER end{};
  end.QuadPart = static_cast<LONGLONG>(bytes);
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(marker_offset);
  DWORD written = 0;
  const bool ok = sparse && SetFilePointerEx(file, end, nullptr, FILE_BEGIN) &&
      SetEndOfFile(file) && SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
      WriteFile(file, marker.data(), static_cast<DWORD>(marker.size()), &written,
                nullptr) && written == marker.size() && FlushFileBuffers(file);
  CloseHandle(file);
  return ok;
#else
  const int file = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
  if (file < 0) return false;
  const bool sized = ::ftruncate(file, static_cast<off_t>(bytes)) == 0;
  const auto written = sized
      ? ::pwrite(file, marker.data(), marker.size(), static_cast<off_t>(marker_offset))
      : -1;
  const bool ok = written == static_cast<ssize_t>(marker.size());
  ::close(file);
  return ok;
#endif
}

void check_read(pikdraw::raster::RasterAssetReader& reader, uint64_t offset,
                size_t size) {
  std::vector<uint8_t> actual(size);
  size_t bytes_read = 0;
  std::string error;
  CHECK(reader.read_at(offset, actual, bytes_read, &error));
  CHECK(bytes_read == size);
  for (size_t i = 0; i < size; ++i)
    CHECK(actual[i] == expected_byte(offset + i));
}

void embedded_source_is_copied_and_read_by_range() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  const auto source = test_directory.path() / "source.rgba";
  const auto managed = test_directory.path() / "managed";
  constexpr uint64_t source_bytes = 4ull * 1024ull * 1024ull + 137ull;
  write_source(source, source_bytes);

  RasterAssetStore store(managed);
  RasterSourceDescriptor descriptor;
  uint64_t latest_progress = 0;
  std::string error;
  CHECK(store.store_embedded(source, "application/x-test-raster", descriptor,
      {}, [&](const RasterAssetProgress& progress) {
        CHECK(progress.total_bytes == source_bytes);
        CHECK(progress.bytes_copied > latest_progress);
        latest_progress = progress.bytes_copied;
      }, &error));
  CHECK(descriptor.mode == pikdraw::document::RasterStorageMode::Embedded);
  CHECK(descriptor.encoded_byte_length == source_bytes);
  CHECK(descriptor.asset_id.valid() && descriptor.revision != 0);
  CHECK(latest_progress == source_bytes);
  CHECK(std::filesystem::file_size(store.embedded_path(descriptor)) == source_bytes);

  auto reader = store.open(descriptor, &error);
  CHECK(reader);
  check_read(*reader, 2ull * 1024ull * 1024ull + 11ull, 257);
  check_read(*reader, source_bytes - 43, 43);
  std::array<uint8_t, 8> at_eof{};
  size_t bytes_read = 0;
  CHECK(reader->read_at(source_bytes, at_eof, bytes_read, &error));
  CHECK(bytes_read == 0);
  CHECK(!reader->read_at(source_bytes + 1, at_eof, bytes_read, &error));
}

void embedded_memory_source_is_copied_and_read_by_range() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  constexpr size_t source_size = 2u * 1024u * 1024u + 29u;
  std::vector<uint8_t> source(source_size);
  for (size_t i = 0; i < source.size(); ++i)
    source[i] = expected_byte(i);

  RasterAssetStore store(test_directory.path() / "managed");
  RasterSourceDescriptor descriptor;
  std::string error;
  uint64_t copied = 0;
  CHECK(store.store_embedded_bytes(source, "image/png", descriptor, {},
      [&](const RasterAssetProgress& progress) {
        CHECK(progress.total_bytes == source_size);
        CHECK(progress.bytes_copied > copied);
        copied = progress.bytes_copied;
      }, &error));
  CHECK(descriptor.mode == pikdraw::document::RasterStorageMode::Embedded);
  CHECK(descriptor.encoded_byte_length == source_size);
  CHECK(descriptor.fingerprint_sha256.size() == 64);
  CHECK(copied == source_size);

  auto reader = store.open(descriptor, &error);
  CHECK(reader);
  std::array<uint8_t, 257> actual{};
  size_t bytes_read = 0;
  constexpr uint64_t offset = 1024ull * 1024ull + 13;
  CHECK(reader->read_at(offset, actual, bytes_read, &error));
  CHECK(bytes_read == actual.size());
  for (size_t i = 0; i < actual.size(); ++i)
    CHECK(actual[i] == source[static_cast<size_t>(offset) + i]);
  CHECK(store.verify_source(descriptor, {}, {}, &error));
}

void cancelled_memory_source_copy_leaves_no_partial_asset() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  std::vector<uint8_t> source(3u * 1024u * 1024u, 0x5a);
  const auto managed = test_directory.path() / "managed";
  RasterAssetStore store(managed);
  RasterSourceDescriptor descriptor;
  std::string error;
  bool cancelled = false;
  const bool stored = store.store_embedded_bytes(source, "image/png", descriptor,
      [&] { return cancelled; },
      [&](const RasterAssetProgress&) { cancelled = true; }, &error);
  CHECK(!stored);
  CHECK(error == "Raster asset copy was cancelled.");
  CHECK(std::filesystem::is_empty(managed));

  const std::array<uint8_t, 8> one_block{1, 2, 3, 4, 5, 6, 7, 8};
  bool cancelled_after_final_progress = false;
  error.clear();
  CHECK(!store.store_embedded_bytes(one_block, "image/png", descriptor,
      [&] { return cancelled_after_final_progress; },
      [&](const RasterAssetProgress&) { cancelled_after_final_progress = true; },
      &error));
  CHECK(error == "Raster asset copy was cancelled.");
  CHECK(std::filesystem::is_empty(managed));
}

void cancelled_embedded_copy_leaves_no_partial_asset() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  const auto source = test_directory.path() / "source.rgba";
  const auto managed = test_directory.path() / "managed";
  write_source(source, 3ull * 1024ull * 1024ull);
  RasterAssetStore store(managed);
  RasterSourceDescriptor descriptor;
  std::string error;
  bool cancelled = false;
  const bool stored = store.store_embedded(source, "application/x-test-raster",
      descriptor, [&] { return cancelled; },
      [&](const RasterAssetProgress&) { cancelled = true; }, &error);
  CHECK(!stored);
  size_t files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(managed)) {
    (void)entry;
    ++files;
  }
  CHECK(files == 0);
}

void linked_sources_detect_changed_size() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  const auto source = test_directory.path() / "linked.rgba";
  write_source(source, 1024);
  RasterAssetStore store(test_directory.path() / "managed");
  RasterSourceDescriptor descriptor;
  std::string error;
  CHECK(store.register_linked(source, "application/x-test-raster", descriptor,
                               &error));
  CHECK(descriptor.mode == pikdraw::document::RasterStorageMode::Linked);
  {
    auto reader = store.open(descriptor, &error);
    CHECK(reader);
    check_read(*reader, 100, 64);
  }
  {
    std::ofstream output(source, std::ios::binary | std::ios::app);
    CHECK(output);
    output.put('\0');
  }
  CHECK(!store.open(descriptor, &error));
  CHECK(!error.empty());
}

void linked_asset_store_reads_offsets_past_four_gib() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  const auto source = test_directory.path() / "sparse-over-4gib.tif";
  constexpr uint64_t marker_offset = (uint64_t{1} << 32) + 12345;
  constexpr uint64_t source_bytes = marker_offset + 4096;
  constexpr std::array<uint8_t, 9> marker{
      0x50, 0x49, 0x4b, 0x44, 0x52, 0x2d, 0x36, 0x34, 0x21};
  CHECK(create_sparse_source(source, source_bytes, marker_offset, marker));

  RasterAssetStore store(test_directory.path() / "managed");
  RasterSourceDescriptor descriptor;
  std::string error;
  CHECK(store.register_linked(source, "image/tiff", descriptor, &error));
  CHECK(descriptor.encoded_byte_length == source_bytes);
  auto reader = store.open(descriptor, &error);
  CHECK(reader);
  std::array<uint8_t, marker.size()> actual{};
  size_t bytes_read = 0;
  CHECK(reader->read_at(marker_offset, actual, bytes_read, &error));
  CHECK(bytes_read == marker.size());
  CHECK(actual == marker);
}

void embedded_sources_are_hashed_and_verification_detects_corruption() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  const auto source = test_directory.path() / "sha256-input.bin";
  const auto managed = test_directory.path() / "managed";
  {
    std::ofstream output(source, std::ios::binary | std::ios::trunc);
    CHECK(output);
    output.write("abc", 3);
    CHECK(output);
  }

  RasterAssetStore store(managed);
  RasterSourceDescriptor descriptor;
  std::string error;
  CHECK(store.store_embedded(source, "application/octet-stream", descriptor,
                             {}, {}, &error));
  CHECK(descriptor.fingerprint_sha256 ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  uint64_t verified_bytes = 0;
  CHECK(store.verify_source(descriptor, {}, [&](const RasterAssetProgress& p) {
    verified_bytes = p.bytes_copied;
  }, &error));
  CHECK(verified_bytes == 3);

  constexpr uint64_t kMultiBufferBytes = 3ull * 1024ull * 1024ull;
  const auto multi_source = test_directory.path() / "multi-buffer.bin";
  {
    std::ofstream output(multi_source, std::ios::binary | std::ios::trunc);
    CHECK(output);
    std::array<uint8_t, 64 * 1024> block{};
    block.fill(static_cast<uint8_t>('a'));
    for (uint64_t written = 0; written < kMultiBufferBytes;
         written += block.size()) {
      output.write(reinterpret_cast<const char*>(block.data()),
                   static_cast<std::streamsize>(block.size()));
      CHECK(output);
    }
  }
  RasterSourceDescriptor multi_descriptor;
  CHECK(store.store_embedded(multi_source, "application/octet-stream",
      multi_descriptor, {}, {}, &error));
  CHECK(multi_descriptor.fingerprint_sha256 ==
      "6f850bc94ae6f7de14297c01616c36d712d22864497b28a63b81d776b035e656");
  uint64_t multi_verified_bytes = 0;
  CHECK(store.verify_source(multi_descriptor, {},
      [&](const RasterAssetProgress& p) {
        multi_verified_bytes = p.bytes_copied;
      }, &error));
  CHECK(multi_verified_bytes == kMultiBufferBytes);

  error.clear();
  CHECK(!store.verify_source(descriptor, [] { return true; }, {}, &error));
  CHECK(error == "Raster source verification was cancelled.");

  {
    std::fstream corrupt(store.embedded_path(descriptor),
        std::ios::binary | std::ios::in | std::ios::out);
    CHECK(corrupt);
    corrupt.seekp(0);
    corrupt.put('x');
    CHECK(corrupt);
  }
  error.clear();
  CHECK(!store.verify_source(descriptor, {}, {}, &error));
  CHECK(error == "Raster source content does not match its SHA-256 fingerprint.");
}

void embedded_cache_restore_checks_authoritative_source_hash() {
  using namespace pikdraw::raster;
  TestDirectory test_directory;
  std::vector<uint8_t> source(2u * 1024u * 1024u + 19u);
  for (size_t i = 0; i < source.size(); ++i)
    source[i] = expected_byte(i);

  RasterAssetStore source_store(test_directory.path() / "source-cache");
  RasterSourceDescriptor descriptor;
  std::string error;
  CHECK(source_store.store_embedded_bytes(source, "image/png", descriptor,
                                           {}, {}, &error));

  RasterAssetStore reopened_store(test_directory.path() / "reopened-cache");
  uint64_t restored_bytes = 0;
  CHECK(reopened_store.restore_embedded_bytes(source, descriptor, {},
      [&](const RasterAssetProgress& progress) {
        CHECK(progress.total_bytes == source.size());
        CHECK(progress.bytes_copied > restored_bytes);
        restored_bytes = progress.bytes_copied;
      }, &error));
  CHECK(restored_bytes == source.size());
  auto restored = reopened_store.open(descriptor, &error);
  CHECK(restored);
  std::array<uint8_t, 64> readback{};
  size_t bytes_read = 0;
  CHECK(restored->read_at(source.size() - readback.size(), readback,
                           bytes_read, &error));
  CHECK(bytes_read == readback.size());
  CHECK(std::equal(readback.begin(), readback.end(),
      source.end() - static_cast<std::ptrdiff_t>(readback.size())));

  // A bad embedded cache is replaced only after the original .pr bytes pass
  // the persisted SHA-256 check.
  {
    std::fstream corrupt(reopened_store.embedded_path(descriptor),
        std::ios::binary | std::ios::in | std::ios::out);
    CHECK(corrupt);
    corrupt.seekp(0);
    corrupt.put('x');
    CHECK(corrupt);
  }
  CHECK(reopened_store.restore_embedded_bytes(source, descriptor, {}, {}, &error));
  CHECK(reopened_store.verify_source(descriptor, {}, {}, &error));
  source[0] ^= 0xff;
  CHECK(!reopened_store.restore_embedded_bytes(source, descriptor, {}, {}, &error));
  CHECK(error == "Embedded raster source bytes do not match their persisted SHA-256.");
  CHECK(reopened_store.verify_source(descriptor, {}, {}, &error));
}

void native_reopen_reattaches_runtime_source_provider() {
  using namespace pikdraw;
  TestDirectory test_directory;
  std::vector<uint8_t> source{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
                              0x10, 0x20, 0x30, 0x40};
  raster::RasterAssetStore source_store(test_directory.path() / "original-cache");
  raster::RasterSourceDescriptor descriptor;
  std::string error;
  CHECK(source_store.store_embedded_bytes(source, "image/png", descriptor,
                                           {}, {}, &error));

  document::ImageObject image;
  image.id = document::generate_object_id();
  image.name = "reopened raster source";
  image.width_px = 1;
  image.height_px = 1;
  image.shared_pixels_rgba =
      std::make_shared<const std::vector<uint32_t>>(1, 0x102030ff);
  image.source.media_type = "image/png";
  auto source_provider = source_store.open(descriptor, &error);
  CHECK(source_provider && error.empty());
  image.source.asset_provider = source_provider;
  image.asset_source = descriptor;
  document::PersistentObjectMap objects;
  objects = objects.set(image.id, image);
  auto snapshot = std::make_shared<document::DocumentSnapshot>(
      11, 29, document::Page::make_default(), std::move(objects), "restore-test");

  const auto bytes = document::NativeFormat::serialize(*snapshot);
  auto reopened = document::NativeFormat::deserialize(bytes.data(), bytes.size(), &error);
  CHECK(reopened && error.empty());
  const auto* persisted = reopened->objects().lookup_ref(image.id);
  CHECK(persisted && std::holds_alternative<document::ImageObject>(*persisted));
  CHECK(!std::get<document::ImageObject>(*persisted).source.asset_provider);

  const auto restored = raster::restore_embedded_source_providers(reopened);
  CHECK(restored);
  CHECK(restored.snapshot->epoch() == reopened->epoch());
  CHECK(restored.snapshot->revision() == reopened->revision());
  const auto* restored_object = restored.snapshot->objects().lookup_ref(image.id);
  CHECK(restored_object && std::holds_alternative<document::ImageObject>(*restored_object));
  const auto& restored_image = std::get<document::ImageObject>(*restored_object);
  CHECK(restored_image.source.original_data().empty());
  CHECK(restored_image.asset_source->asset_id == descriptor.asset_id);
  CHECK(restored_image.source.asset_provider);
  CHECK(restored_image.source.asset_provider->size_bytes() == source.size());
  std::array<uint8_t, 12> readback{};
  size_t bytes_read = 0;
  CHECK(restored_image.source.asset_provider->read_at(0, readback, bytes_read, &error));
  CHECK(bytes_read == source.size());
  CHECK(std::equal(readback.begin(), readback.end(), source.begin()));

  const auto cancelled = raster::restore_embedded_source_providers(
      reopened, [] { return true; });
  CHECK(!cancelled && cancelled.snapshot == nullptr);
}

}  // namespace

int main() {
  embedded_source_is_copied_and_read_by_range();
  embedded_memory_source_is_copied_and_read_by_range();
  cancelled_memory_source_copy_leaves_no_partial_asset();
  cancelled_embedded_copy_leaves_no_partial_asset();
  linked_sources_detect_changed_size();
  linked_asset_store_reads_offsets_past_four_gib();
  embedded_sources_are_hashed_and_verification_detects_corruption();
  embedded_cache_restore_checks_authoritative_source_hash();
  native_reopen_reattaches_runtime_source_provider();
  return 0;
}

#undef CHECK
