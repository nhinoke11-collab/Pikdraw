#include "pikdraw/raster/asset_store.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace pikdraw::raster {
namespace {

constexpr size_t kCopyBufferBytes = 1024u * 1024u;
std::atomic<uint64_t> next_asset_sequence{1};

void set_error(std::string* error, std::string message) {
  if (error) *error = std::move(message);
}

std::string utf8_path(const std::filesystem::path& path) {
  const auto value = path.generic_u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path path_from_utf8(const std::string& value) {
  const auto* bytes = reinterpret_cast<const char8_t*>(value.data());
  return std::filesystem::path(std::u8string(bytes, bytes + value.size()));
}

int64_t modified_ticks(const std::filesystem::path& path,
                       std::error_code& error) {
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error) return 0;
  const auto ticks = modified.time_since_epoch().count();
  if constexpr (sizeof(ticks) > sizeof(int64_t)) {
    if (ticks > static_cast<decltype(ticks)>(std::numeric_limits<int64_t>::max()) ||
        ticks < static_cast<decltype(ticks)>(std::numeric_limits<int64_t>::min()))
      return 0;
  }
  return static_cast<int64_t>(ticks);
}

RasterAssetId new_asset_id() {
  std::random_device random;
  const auto now = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto sequence = next_asset_sequence.fetch_add(1, std::memory_order_relaxed);
  RasterAssetId id{
      (static_cast<uint64_t>(random()) << 32) ^ random() ^ now,
      (static_cast<uint64_t>(random()) << 32) ^ random() ^ sequence};
  if (!id.valid()) id.low = sequence == 0 ? 1 : sequence;
  return id;
}

std::string id_component(RasterAssetId id) {
  std::ostringstream value;
  value << std::hex << std::setfill('0') << std::setw(16) << id.high
        << std::setw(16) << id.low;
  return value.str();
}

#ifdef _WIN32
class PinnedInput final {
 public:
  explicit PinnedInput(const std::filesystem::path& path) {
    handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  }
  ~PinnedInput() {
    if (handle_ != INVALID_HANDLE_VALUE && handle_) CloseHandle(handle_);
  }
  PinnedInput(const PinnedInput&) = delete;
  PinnedInput& operator=(const PinnedInput&) = delete;
  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] bool valid() const noexcept {
    return handle_ && handle_ != INVALID_HANDLE_VALUE;
  }

 private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
};

bool read_sequential(HANDLE handle, uint8_t* data, DWORD capacity,
                     DWORD& read_bytes) {
  read_bytes = 0;
  return ReadFile(handle, data, capacity, &read_bytes, nullptr) != FALSE;
}
#endif

}  // namespace

RasterAssetReader::RasterAssetReader(RasterSourceDescriptor descriptor,
                                     std::filesystem::path path,
                                     uint64_t file_bytes)
    : descriptor_(std::move(descriptor)),
      path_(std::move(path)),
      file_bytes_(file_bytes) {
#ifdef _WIN32
  const auto handle = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING,
                                  FILE_FLAG_RANDOM_ACCESS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
      static_cast<uint64_t>(size.QuadPart) != file_bytes_) {
    CloseHandle(handle);
    return;
  }
  handle_ = handle;
#else
  stream_.open(path_, std::ios::binary);
  if (!stream_) return;
  std::error_code error;
  const auto actual_size = std::filesystem::file_size(path_, error);
  if (error || actual_size != file_bytes_) stream_.close();
#endif
}

RasterAssetReader::~RasterAssetReader() {
#ifdef _WIN32
  if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
#endif
}

bool RasterAssetReader::read_at(uint64_t offset,
                                std::span<uint8_t> destination,
                                size_t& bytes_read, std::string* error) const {
  bytes_read = 0;
  if (offset > file_bytes_) {
    set_error(error, "Raster source offset is past end of file.");
    return false;
  }
  if (offset == file_bytes_ || destination.empty()) return true;
  const uint64_t available = file_bytes_ - offset;
  const size_t requested = static_cast<size_t>(
      (std::min)(available, static_cast<uint64_t>(destination.size())));

  std::lock_guard lock(mutex_);
#ifdef _WIN32
  const auto handle = static_cast<HANDLE>(handle_);
  if (!handle) {
    set_error(error, "Raster source handle is not available.");
    return false;
  }
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) {
    set_error(error, "Raster source seek failed.");
    return false;
  }
  while (bytes_read < requested) {
    const auto remaining = requested - bytes_read;
    const DWORD chunk = static_cast<DWORD>((std::min)(
        remaining, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
    DWORD actual = 0;
    if (!ReadFile(handle, destination.data() + bytes_read, chunk, &actual,
                  nullptr)) {
      set_error(error, "Raster source read failed.");
      return false;
    }
    bytes_read += actual;
    if (actual != chunk) break;
  }
#else
  if (!stream_.is_open() ||
      offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    set_error(error, "Raster source handle or offset is not available.");
    return false;
  }
  stream_.clear();
  stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!stream_) {
    set_error(error, "Raster source seek failed.");
    return false;
  }
  while (bytes_read < requested) {
    const auto remaining = requested - bytes_read;
    const auto chunk = static_cast<std::streamsize>((std::min)(
        remaining, static_cast<size_t>(std::numeric_limits<std::streamsize>::max())));
    stream_.read(reinterpret_cast<char*>(destination.data() + bytes_read), chunk);
    const auto actual = stream_.gcount();
    if (actual > 0) bytes_read += static_cast<size_t>(actual);
    if (actual != chunk) {
      if (!stream_.eof() && stream_.fail()) {
        set_error(error, "Raster source read failed.");
        return false;
      }
      break;
    }
  }
#endif
  return true;
}

RasterAssetStore::RasterAssetStore(std::filesystem::path root)
    : root_(std::filesystem::absolute(std::move(root))) {
  std::error_code error;
  std::filesystem::create_directories(root_, error);
  if (error) throw std::filesystem::filesystem_error(
      "Could not create raster asset store", root_, error);
}

std::filesystem::path RasterAssetStore::embedded_path(
    const RasterSourceDescriptor& descriptor) const {
  return root_ / (id_component(descriptor.asset_id) + "-" +
                  std::to_string(descriptor.revision) + ".blob");
}

bool RasterAssetStore::store_embedded(
    const std::filesystem::path& source, std::string media_type,
    RasterSourceDescriptor& out_descriptor, const CancelCheck& cancelled,
    const ProgressCallback& progress, std::string* error) const {
  if (media_type.empty()) {
    set_error(error, "Raster media type is required.");
    return false;
  }
  std::error_code fs_error;
  const auto expected_size = std::filesystem::file_size(source, fs_error);
  if (fs_error || expected_size == 0) {
    set_error(error, fs_error ? "Raster source size could not be read."
                              : "Raster source is empty.");
    return false;
  }

#ifdef _WIN32
  PinnedInput input(source);
  if (!input.valid()) {
    set_error(error, "Raster source could not be opened read-only.");
    return false;
  }
  LARGE_INTEGER pinned_size{};
  if (!GetFileSizeEx(input.get(), &pinned_size) || pinned_size.QuadPart < 0 ||
      static_cast<uint64_t>(pinned_size.QuadPart) != expected_size) {
    set_error(error, "Raster source changed while it was being opened.");
    return false;
  }
#else
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    set_error(error, "Raster source could not be opened read-only.");
    return false;
  }
#endif

  RasterSourceDescriptor descriptor;
  descriptor.asset_id = new_asset_id();
  descriptor.revision = 1;
  descriptor.mode = document::RasterStorageMode::Embedded;
  descriptor.encoded_byte_length = static_cast<uint64_t>(expected_size);
  descriptor.media_type = std::move(media_type);
  const auto destination = embedded_path(descriptor);
  auto staging = destination;
  staging += ".partial";
  std::filesystem::remove(staging, fs_error);

  std::ofstream output(staging, std::ios::binary | std::ios::trunc);
  if (!output) {
    std::filesystem::remove(staging, fs_error);
    set_error(error, "Raster asset staging file could not be created.");
    return false;
  }
  std::vector<uint8_t> buffer(kCopyBufferBytes);
  detail::Sha256 digest;
  uint64_t copied = 0;
  bool ok = true;
  while (copied < descriptor.encoded_byte_length) {
    if (cancelled && cancelled()) {
      set_error(error, "Raster asset copy was cancelled.");
      ok = false;
      break;
    }
    const auto remaining = descriptor.encoded_byte_length - copied;
    const size_t requested = static_cast<size_t>((std::min)(
        static_cast<uint64_t>(buffer.size()), remaining));
#ifdef _WIN32
    DWORD actual = 0;
    if (!read_sequential(input.get(), buffer.data(),
                         static_cast<DWORD>(requested), actual) ||
        actual == 0) {
      set_error(error, "Raster source ended or failed during managed copy.");
      ok = false;
      break;
    }
    const auto count = static_cast<size_t>(actual);
#else
    const auto request_size = static_cast<std::streamsize>(requested);
    input.read(reinterpret_cast<char*>(buffer.data()), request_size);
    const auto actual = input.gcount();
    if (actual <= 0) {
      set_error(error, "Raster source ended or failed during managed copy.");
      ok = false;
      break;
    }
    const auto count = static_cast<size_t>(actual);
#endif
    if (!digest.update(std::span<const uint8_t>(buffer.data(), count))) {
      set_error(error, "Raster source exceeds the SHA-256 length domain.");
      ok = false;
      break;
    }
    output.write(reinterpret_cast<const char*>(buffer.data()),
                 static_cast<std::streamsize>(count));
    if (!output) {
      set_error(error, "Raster asset staging write failed, possibly due to disk space.");
      ok = false;
      break;
    }
    copied += count;
    if (progress) progress({copied, descriptor.encoded_byte_length});
  }
  output.flush();
  if (!output) {
    set_error(error, "Raster asset staging flush failed.");
    ok = false;
  }
  output.close();

  if (ok && cancelled && cancelled()) {
    set_error(error, "Raster asset copy was cancelled.");
    ok = false;
  }

  if (!ok || copied != descriptor.encoded_byte_length) {
    std::filesystem::remove(staging, fs_error);
    return false;
  }
  if (!digest.finish_hex(descriptor.fingerprint_sha256)) {
    std::filesystem::remove(staging, fs_error);
    set_error(error, "Raster source SHA-256 could not be finalized.");
    return false;
  }
  std::filesystem::rename(staging, destination, fs_error);
  if (fs_error) {
    std::filesystem::remove(staging, fs_error);
    set_error(error, "Raster asset could not be committed atomically.");
    return false;
  }
  out_descriptor = std::move(descriptor);
  return true;
}

bool RasterAssetStore::store_embedded_bytes(
    std::span<const uint8_t> source, std::string media_type,
    RasterSourceDescriptor& out_descriptor, const CancelCheck& cancelled,
    const ProgressCallback& progress, std::string* error) const {
  if (source.empty()) {
    set_error(error, "Raster source is empty.");
    return false;
  }
  if (media_type.empty()) {
    set_error(error, "Raster media type is required.");
    return false;
  }

  RasterSourceDescriptor descriptor;
  descriptor.asset_id = new_asset_id();
  descriptor.revision = 1;
  descriptor.mode = document::RasterStorageMode::Embedded;
  descriptor.encoded_byte_length = static_cast<uint64_t>(source.size());
  descriptor.media_type = std::move(media_type);
  const auto destination = embedded_path(descriptor);
  auto staging = destination;
  staging += ".partial";
  std::error_code fs_error;
  std::filesystem::remove(staging, fs_error);

  std::ofstream output(staging, std::ios::binary | std::ios::trunc);
  if (!output) {
    std::filesystem::remove(staging, fs_error);
    set_error(error, "Raster asset staging file could not be created.");
    return false;
  }
  detail::Sha256 digest;
  uint64_t copied = 0;
  bool ok = true;
  while (copied < descriptor.encoded_byte_length) {
    if (cancelled && cancelled()) {
      set_error(error, "Raster asset copy was cancelled.");
      ok = false;
      break;
    }
    const size_t count = static_cast<size_t>((std::min)(
        static_cast<uint64_t>(kCopyBufferBytes),
        descriptor.encoded_byte_length - copied));
    const auto block = source.subspan(static_cast<size_t>(copied), count);
    if (!digest.update(block)) {
      set_error(error, "Raster source exceeds the SHA-256 length domain.");
      ok = false;
      break;
    }
    output.write(reinterpret_cast<const char*>(block.data()),
                 static_cast<std::streamsize>(block.size()));
    if (!output) {
      set_error(error, "Raster asset staging write failed, possibly due to disk space.");
      ok = false;
      break;
    }
    copied += count;
    if (progress) progress({copied, descriptor.encoded_byte_length});
  }
  output.flush();
  if (!output) {
    set_error(error, "Raster asset staging flush failed.");
    ok = false;
  }
  output.close();

  if (ok && cancelled && cancelled()) {
    set_error(error, "Raster asset copy was cancelled.");
    ok = false;
  }

  if (!ok || copied != descriptor.encoded_byte_length) {
    std::filesystem::remove(staging, fs_error);
    return false;
  }
  if (!digest.finish_hex(descriptor.fingerprint_sha256)) {
    std::filesystem::remove(staging, fs_error);
    set_error(error, "Raster source SHA-256 could not be finalized.");
    return false;
  }
  std::filesystem::rename(staging, destination, fs_error);
  if (fs_error) {
    std::filesystem::remove(staging, fs_error);
    set_error(error, "Raster asset could not be committed atomically.");
    return false;
  }
  out_descriptor = std::move(descriptor);
  return true;
}

bool RasterAssetStore::restore_embedded_bytes(
    std::span<const uint8_t> source,
    const RasterSourceDescriptor& descriptor,
    const CancelCheck& cancelled, const ProgressCallback& progress,
    std::string* error) const {
  if (!descriptor.asset_id.valid() || descriptor.revision == 0 ||
      descriptor.mode != document::RasterStorageMode::Embedded ||
      descriptor.encoded_byte_length == 0 ||
      descriptor.encoded_byte_length != source.size() ||
      descriptor.media_type.empty() || descriptor.media_type.size() > 64 ||
      descriptor.fingerprint_sha256.size() != 64) {
    set_error(error, "Embedded raster restore descriptor is invalid.");
    return false;
  }
  for (const char value : descriptor.fingerprint_sha256) {
    if (!((value >= '0' && value <= '9') ||
          (value >= 'a' && value <= 'f'))) {
      set_error(error, "Embedded raster restore fingerprint is malformed.");
      return false;
    }
  }
  if (cancelled && cancelled()) {
    set_error(error, "Embedded raster restore was cancelled.");
    return false;
  }

  const auto destination = embedded_path(descriptor);
  std::error_code fs_error;
  if (std::filesystem::exists(destination, fs_error) && !fs_error) {
    std::string verify_error;
    if (verify_source(descriptor, cancelled, progress, &verify_error)) {
      detail::Sha256 source_digest;
      std::string source_fingerprint;
      uint64_t verified_source_bytes = 0;
      while (verified_source_bytes < descriptor.encoded_byte_length) {
        if (cancelled && cancelled()) {
          set_error(error, "Embedded raster restore was cancelled.");
          return false;
        }
        const size_t count = static_cast<size_t>((std::min)(
            static_cast<uint64_t>(kCopyBufferBytes),
            descriptor.encoded_byte_length - verified_source_bytes));
        if (!source_digest.update(source.subspan(
                static_cast<size_t>(verified_source_bytes), count))) {
          set_error(error,
                    "Embedded raster restore exceeds the SHA-256 length domain.");
          return false;
        }
        verified_source_bytes += count;
      }
      if (!source_digest.finish_hex(source_fingerprint) ||
          source_fingerprint != descriptor.fingerprint_sha256) {
        set_error(error,
                  "Embedded raster source bytes do not match their persisted SHA-256.");
        return false;
      }
      if (error) error->clear();
      return true;
    }
    if (cancelled && cancelled()) {
      set_error(error, "Embedded raster restore was cancelled.");
      return false;
    }
  }
  fs_error.clear();

  auto staging = destination;
  staging += ".restore-" +
      std::to_string(next_asset_sequence.fetch_add(1, std::memory_order_relaxed));
  std::filesystem::remove(staging, fs_error);
  fs_error.clear();
  std::ofstream output(staging, std::ios::binary | std::ios::trunc);
  if (!output) {
    set_error(error, "Embedded raster restore staging file could not be created.");
    return false;
  }

  detail::Sha256 digest;
  uint64_t copied = 0;
  while (copied < descriptor.encoded_byte_length) {
    if (cancelled && cancelled()) {
      set_error(error, "Embedded raster restore was cancelled.");
      output.close();
      std::filesystem::remove(staging, fs_error);
      return false;
    }
    const size_t count = static_cast<size_t>((std::min)(
        static_cast<uint64_t>(kCopyBufferBytes),
        descriptor.encoded_byte_length - copied));
    const auto block = source.subspan(static_cast<size_t>(copied), count);
    if (!digest.update(block)) {
      set_error(error, "Embedded raster restore exceeds the SHA-256 length domain.");
      output.close();
      std::filesystem::remove(staging, fs_error);
      return false;
    }
    output.write(reinterpret_cast<const char*>(block.data()),
                 static_cast<std::streamsize>(block.size()));
    if (!output) {
      set_error(error, "Embedded raster restore staging write failed.");
      output.close();
      std::filesystem::remove(staging, fs_error);
      return false;
    }
    copied += count;
    if (progress) progress({copied, descriptor.encoded_byte_length});
  }
  output.flush();
  if (!output) {
    set_error(error, "Embedded raster restore staging flush failed.");
    output.close();
    std::filesystem::remove(staging, fs_error);
    return false;
  }
  output.close();
  if (cancelled && cancelled()) {
    set_error(error, "Embedded raster restore was cancelled.");
    std::filesystem::remove(staging, fs_error);
    return false;
  }

  std::string actual_fingerprint;
  if (!digest.finish_hex(actual_fingerprint) ||
      actual_fingerprint != descriptor.fingerprint_sha256) {
    set_error(error, "Embedded raster source bytes do not match their persisted SHA-256.");
    std::filesystem::remove(staging, fs_error);
    return false;
  }
#ifdef _WIN32
  if (!MoveFileExW(staging.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    set_error(error, "Embedded raster cache could not be atomically restored.");
    std::filesystem::remove(staging, fs_error);
    return false;
  }
#else
  std::filesystem::rename(staging, destination, fs_error);
  if (fs_error) {
    set_error(error, "Embedded raster cache could not be atomically restored.");
    std::filesystem::remove(staging, fs_error);
    return false;
  }
#endif
  if (error) error->clear();
  return true;
}

bool RasterAssetStore::verify_source(
    const RasterSourceDescriptor& descriptor, const CancelCheck& cancelled,
    const ProgressCallback& progress, std::string* error) const {
  if (descriptor.fingerprint_sha256.size() != 64) {
    set_error(error, "Raster source has no complete SHA-256 fingerprint.");
    return false;
  }
  for (const auto value : descriptor.fingerprint_sha256) {
    if (!((value >= '0' && value <= '9') ||
          (value >= 'a' && value <= 'f'))) {
      set_error(error, "Raster source SHA-256 fingerprint is malformed.");
      return false;
    }
  }
  auto reader = open(descriptor, error);
  if (!reader) return false;

  detail::Sha256 digest;
  std::vector<uint8_t> buffer(kCopyBufferBytes);
  uint64_t offset = 0;
  while (offset < descriptor.encoded_byte_length) {
    if (cancelled && cancelled()) {
      set_error(error, "Raster source verification was cancelled.");
      return false;
    }
    const auto remaining = descriptor.encoded_byte_length - offset;
    const auto requested = static_cast<size_t>((std::min)(
        static_cast<uint64_t>(buffer.size()), remaining));
    size_t actual = 0;
    if (!reader->read_at(offset,
          std::span<uint8_t>(buffer.data(), requested), actual, error) ||
        actual != requested) {
      if (error && error->empty())
        set_error(error, "Raster source changed or ended during verification.");
      return false;
    }
    if (!digest.update(std::span<const uint8_t>(buffer.data(), actual))) {
      set_error(error, "Raster source exceeds the SHA-256 length domain.");
      return false;
    }
    offset += actual;
    if (progress) progress({offset, descriptor.encoded_byte_length});
  }

  std::string actual_fingerprint;
  if (!digest.finish_hex(actual_fingerprint)) {
    set_error(error, "Raster source SHA-256 could not be finalized.");
    return false;
  }
  if (actual_fingerprint != descriptor.fingerprint_sha256) {
    set_error(error, "Raster source content does not match its SHA-256 fingerprint.");
    return false;
  }
  return true;
}

bool RasterAssetStore::register_linked(
    const std::filesystem::path& source, std::string media_type,
    RasterSourceDescriptor& out_descriptor, std::string* error) const {
  if (media_type.empty()) {
    set_error(error, "Raster media type is required.");
    return false;
  }
  std::error_code fs_error;
  const auto size = std::filesystem::file_size(source, fs_error);
  if (fs_error || size == 0) {
    set_error(error, fs_error ? "Linked raster source size could not be read."
                              : "Linked raster source is empty.");
    return false;
  }
  const auto ticks = modified_ticks(source, fs_error);
  if (fs_error) {
    set_error(error, "Linked raster source timestamp could not be read.");
    return false;
  }
  RasterSourceDescriptor descriptor;
  descriptor.asset_id = new_asset_id();
  descriptor.revision = 1;
  descriptor.mode = document::RasterStorageMode::Linked;
  descriptor.encoded_byte_length = static_cast<uint64_t>(size);
  descriptor.media_type = std::move(media_type);
  descriptor.locator = utf8_path(std::filesystem::absolute(source));
  descriptor.source_modified_ticks = ticks;
  out_descriptor = std::move(descriptor);
  return true;
}

std::shared_ptr<RasterAssetReader> RasterAssetStore::open(
    const RasterSourceDescriptor& descriptor, std::string* error) const {
  if (!descriptor.asset_id.valid() || descriptor.revision == 0 ||
      descriptor.encoded_byte_length == 0 || descriptor.media_type.empty()) {
    set_error(error, "Raster asset descriptor is incomplete.");
    return {};
  }
  std::error_code fs_error;
  auto source = descriptor.mode == document::RasterStorageMode::Embedded
      ? embedded_path(descriptor) : path_from_utf8(descriptor.locator);
  const auto actual_size = std::filesystem::file_size(source, fs_error);
  if (fs_error || actual_size != descriptor.encoded_byte_length) {
    set_error(error, descriptor.mode == document::RasterStorageMode::Linked
        ? "Linked raster source is missing or has changed size."
        : "Embedded raster asset is missing or has changed size.");
    return {};
  }
  if (descriptor.mode == document::RasterStorageMode::Linked &&
      modified_ticks(source, fs_error) != descriptor.source_modified_ticks) {
    set_error(error, "Linked raster source has changed since it was registered.");
    return {};
  }
  auto reader = std::shared_ptr<RasterAssetReader>(
      new RasterAssetReader(descriptor, std::move(source), actual_size));
#ifdef _WIN32
  if (!reader->handle_) {
#else
  if (!reader->stream_.is_open()) {
#endif
    set_error(error, "Raster asset could not be opened for random-access reads.");
    return {};
  }
  return reader;
}

TemporaryRasterAssetSession::TemporaryRasterAssetSession(
    std::filesystem::path root)
    : root_(std::move(root)),
      store_(std::make_unique<RasterAssetStore>(root_)) {}

std::shared_ptr<TemporaryRasterAssetSession> TemporaryRasterAssetSession::create(
    std::string* error) {
  std::error_code fs_error;
  const auto temporary_root = std::filesystem::temp_directory_path(fs_error);
  if (fs_error) {
    set_error(error, "Temporary raster asset directory is unavailable.");
    return {};
  }
  for (size_t attempt = 0; attempt < 8; ++attempt) {
    fs_error.clear();
    const auto root = temporary_root /
        ("pikdraw-raster-session-" + id_component(new_asset_id()));
    if (!std::filesystem::create_directory(root, fs_error)) {
      if (!fs_error) continue;
      set_error(error, "Temporary raster asset directory could not be created.");
      return {};
    }
    try {
      return std::shared_ptr<TemporaryRasterAssetSession>(
          new TemporaryRasterAssetSession(root));
    } catch (...) {
      std::filesystem::remove_all(root, fs_error);
      set_error(error, "Temporary raster asset store could not be initialized.");
      return {};
    }
  }
  set_error(error, "A unique temporary raster asset directory could not be reserved.");
  return {};
}

TemporaryRasterAssetSession::~TemporaryRasterAssetSession() {
  store_.reset();
  std::error_code ignored;
  std::filesystem::remove_all(root_, ignored);
}

std::shared_ptr<TemporaryRasterAssetProvider>
TemporaryRasterAssetSession::store_embedded(
    std::span<const uint8_t> source, std::string media_type,
    const CancelCheck& cancelled, const ProgressCallback& progress,
    std::string* error) {
  RasterSourceDescriptor descriptor;
  if (!store_->store_embedded_bytes(source, std::move(media_type), descriptor,
                                    cancelled, progress, error))
    return {};
  auto reader = store_->open(descriptor, error);
  if (!reader) return {};
  try {
    return std::shared_ptr<TemporaryRasterAssetProvider>(
        new TemporaryRasterAssetProvider(shared_from_this(),
                                         std::move(descriptor),
                                         std::move(reader)));
  } catch (...) {
    set_error(error, "Temporary raster source provider could not be allocated.");
    return {};
  }
}

std::shared_ptr<TemporaryRasterAssetProvider>
TemporaryRasterAssetSession::restore_embedded(
    std::span<const uint8_t> source,
    const RasterSourceDescriptor& descriptor,
    const CancelCheck& cancelled, const ProgressCallback& progress,
    std::string* error) {
  if (!store_->restore_embedded_bytes(source, descriptor, cancelled, progress,
                                      error))
    return {};
  auto reader = store_->open(descriptor, error);
  if (!reader) return {};
  try {
    return std::shared_ptr<TemporaryRasterAssetProvider>(
        new TemporaryRasterAssetProvider(shared_from_this(), descriptor,
                                         std::move(reader)));
  } catch (...) {
    set_error(error, "Restored raster source provider could not be allocated.");
    return {};
  }
}

TemporaryRasterAssetProvider::TemporaryRasterAssetProvider(
    std::shared_ptr<TemporaryRasterAssetSession> session,
    RasterSourceDescriptor descriptor,
    std::shared_ptr<RasterAssetReader> reader) noexcept
    : session_(std::move(session)),
      descriptor_(std::move(descriptor)),
      reader_(std::move(reader)) {}

uint64_t TemporaryRasterAssetProvider::size_bytes() const noexcept {
  return reader_ ? reader_->size_bytes() : 0;
}

bool TemporaryRasterAssetProvider::read_at(
    uint64_t offset, std::span<uint8_t> destination, size_t& bytes_read,
    std::string* error) const {
  if (!reader_) {
    bytes_read = 0;
    set_error(error, "Temporary raster source handle is unavailable.");
    return false;
  }
  return reader_->read_at(offset, destination, bytes_read, error);
}

}  // namespace pikdraw::raster
