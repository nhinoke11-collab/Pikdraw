#include "pikdraw/interop/adapters/raster_asset_byte_source.hpp"
#include "pikdraw/document/model/validation.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/raster/asset_store.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

namespace {

using pikdraw::interop::RasterAssetByteSource;
using pikdraw::interop::RasterImageData;
using pikdraw::interop::RasterImageDecoder;
using pikdraw::interop::RasterImageFormat;
using pikdraw::raster::RasterAssetStore;
using pikdraw::raster::RasterSourceDescriptor;

void check(bool condition, const char* expression) {
  if (!condition) throw std::runtime_error(expression);
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression)

class TestDirectory final {
 public:
  TestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
        ("pikdraw-raster-byte-source-" + std::to_string(stamp));
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

void append_be32(std::vector<uint8_t>& bytes, uint32_t value) {
  bytes.push_back(static_cast<uint8_t>(value >> 24));
  bytes.push_back(static_cast<uint8_t>(value >> 16));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
  bytes.push_back(static_cast<uint8_t>(value));
}

void append_le16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

uint32_t png_crc32(std::span<const uint8_t> bytes) {
  uint32_t crc = 0xffffffffu;
  for (const auto byte : bytes) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return ~crc;
}

void append_png_chunk(std::vector<uint8_t>& png, const char type[4],
                      std::span<const uint8_t> payload) {
  append_be32(png, static_cast<uint32_t>(payload.size()));
  const size_t crc_start = png.size();
  for (size_t i = 0; i < 4; ++i) png.push_back(static_cast<uint8_t>(type[i]));
  png.insert(png.end(), payload.begin(), payload.end());
  append_be32(png, png_crc32(std::span<const uint8_t>(png).subspan(crc_start)));
}

std::vector<uint8_t> make_png_fixture() {
  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::vector<uint8_t> header;
  append_be32(header, 2);
  append_be32(header, 1);
  header.insert(header.end(), {8, 6, 0, 0, 0});
  append_png_chunk(png, "IHDR", header);

  const std::array<uint8_t, 9> scanline{0, 20, 40, 60, 128, 80, 100, 120, 64};
  std::vector<uint8_t> zlib{0x78, 0x01, 0x01};
  append_le16(zlib, static_cast<uint16_t>(scanline.size()));
  append_le16(zlib, static_cast<uint16_t>(~scanline.size()));
  zlib.insert(zlib.end(), scanline.begin(), scanline.end());
  uint32_t a = 1, b = 0;
  for (const auto byte : scanline) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  append_be32(zlib, (b << 16) | a);
  append_png_chunk(png, "IDAT", zlib);
  append_png_chunk(png, "IEND", {});
  return png;
}

void file_backed_asset_decodes_as_rows() {
  TestDirectory directory;
  const auto source_path = directory.path() / "source.png";
  const auto store_path = directory.path() / "assets";
  const auto png = make_png_fixture();
  {
    std::ofstream output(source_path, std::ios::binary | std::ios::trunc);
    CHECK(output.good());
    output.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
    CHECK(output.good());
  }

  RasterAssetStore store(store_path);
  RasterSourceDescriptor descriptor;
  std::string error;
  CHECK(store.store_embedded(source_path, "image/png", descriptor,
                             {}, {}, &error));
  CHECK(descriptor.encoded_byte_length == png.size());
  CHECK(!descriptor.fingerprint_sha256.empty());
  CHECK(store.verify_source(descriptor, {}, {}, &error));
  const auto reader = store.open(descriptor, &error);
  CHECK(reader != nullptr);

  pikdraw::document::ImageObject image;
  image.id = pikdraw::document::generate_object_id();
  image.width_px = 2;
  image.height_px = 1;
  image.pixels_rgba = {0x14283c80u, 0x50647840u};
  image.source.media_type = "image/png";
  image.source.asset_provider = reader;
  image.asset_source = descriptor;
  pikdraw::document::validate_image_source(image);

  pikdraw::document::Document document("file-backed-raster-source");
  CHECK(document.execute_command(
      std::make_unique<pikdraw::document::AddObjectCommand>(image)).success);
  const auto saved = pikdraw::document::NativeFormat::serialize(
      *document.current_snapshot());
  const auto reopened = pikdraw::document::NativeFormat::deserialize(
      saved.data(), saved.size(), &error);
  CHECK(reopened != nullptr && error.empty());
  const auto reopened_object = reopened->get_object(image.id);
  CHECK(reopened_object.has_value());
  const auto* reopened_image =
      std::get_if<pikdraw::document::ImageObject>(&*reopened_object);
  CHECK(reopened_image != nullptr);
  CHECK(reopened_image->source.original_data() == png);
  CHECK(reopened_image->asset_source.has_value());
  CHECK(reopened_image->asset_source->asset_id == descriptor.asset_id);
  CHECK(reopened_image->asset_source->fingerprint_sha256 ==
        descriptor.fingerprint_sha256);
  CHECK(!reopened_image->source.asset_provider);

  RasterAssetByteSource source(image.source.asset_provider);
  CHECK(source.size_bytes() == png.size());
  std::array<uint8_t, 8> signature{};
  CHECK(RasterImageDecoder::read_exact(source, 0, signature));
  CHECK(std::equal(signature.begin(), signature.end(), png.begin()));
  std::array<uint8_t, 4> trailing{};
  CHECK(RasterImageDecoder::read_exact(
      source, png.size() - trailing.size(), trailing));
  CHECK(std::equal(trailing.begin(), trailing.end(), png.end() - 4));
  std::array<uint8_t, 1> out_of_range{};
  size_t bytes_read = 42;
  CHECK(!source.read_at(source.size_bytes() + 1, out_of_range, bytes_read));
  CHECK(bytes_read == 0);
  CHECK(source.read_at(source.size_bytes(), std::span<uint8_t>{}, bytes_read));
  CHECK(bytes_read == 0);

#ifdef _WIN32
  uint32_t delivered_rows = 0;
  std::vector<uint8_t> decoded_row;
  const auto decoded = RasterImageDecoder::decode_rows(
      source, {},
      [&](const RasterImageData& metadata, uint32_t row_y, uint32_t width,
          std::span<const uint8_t> row) {
        CHECK(metadata.format == RasterImageFormat::Png);
        CHECK(row_y == delivered_rows && width == 2 && row.size() == 8);
        decoded_row.assign(row.begin(), row.end());
        ++delivered_rows;
        return true;
      });
  CHECK(decoded.success && delivered_rows == 1 && decoded_row.size() == 8);
  const std::array<uint8_t, 8> expected{20, 40, 60, 128, 80, 100, 120, 64};
  CHECK(std::equal(decoded_row.begin(), decoded_row.end(), expected.begin()));
  CHECK(decoded.image.pixels_rgba.empty());
#endif
}

#ifdef _WIN32
void linked_asset_keeps_offsets_above_four_gibibytes(TestDirectory& directory) {
  constexpr uint64_t source_size = 0x100000000ull + 128;
  constexpr uint64_t marker_offset = 0x100000000ull + 32;
  const std::array<uint8_t, 8> marker{'R', 'A', 'S', 'T', 'E', 'R', '6', '4'};
  const auto path = directory.path() / "large-linked-source.bin";
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  bool valid = file != INVALID_HANDLE_VALUE;
  if (valid) {
    DWORD ignored = 0;
    valid = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0,
        nullptr, 0, &ignored, nullptr) != FALSE;
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(source_size);
    valid = valid && SetFilePointerEx(file, end, nullptr, FILE_BEGIN) != FALSE &&
        SetEndOfFile(file) != FALSE;
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(marker_offset);
    valid = valid && SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
    DWORD written = 0;
    valid = valid && WriteFile(file, marker.data(),
        static_cast<DWORD>(marker.size()), &written, nullptr) != FALSE &&
        written == marker.size();
    CloseHandle(file);
  }
  CHECK(valid);

  RasterAssetStore store(directory.path() / "linked-assets");
  RasterSourceDescriptor descriptor;
  std::string error;
  CHECK(store.register_linked(path, "image/png", descriptor, &error));
  const auto reader = store.open(descriptor, &error);
  CHECK(reader != nullptr);
  RasterAssetByteSource source(reader);
  CHECK(source.size_bytes() == source_size && source.size_bytes() > 0xffffffffull);
  std::array<uint8_t, marker.size()> actual{};
  CHECK(RasterImageDecoder::read_exact(source, marker_offset, actual));
  CHECK(std::equal(actual.begin(), actual.end(), marker.begin()));
}
#endif

}  // namespace

int main() {
  try {
    file_backed_asset_decodes_as_rows();
#ifdef _WIN32
    TestDirectory directory;
    linked_asset_keeps_offsets_above_four_gibibytes(directory);
#endif
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
  }
  return 0;
}

#undef CHECK
