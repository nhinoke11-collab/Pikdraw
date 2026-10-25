#pragma once

#include "pikdraw/document/model/raster_edit_graph.hpp"
#include "pikdraw/document/model/raster_source_provider.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#ifndef _WIN32
#include <fstream>
#endif

namespace pikdraw::raster {

using RasterAssetId = document::RasterAssetId;
using RasterSourceDescriptor = document::RasterSourceDescriptor;

struct RasterAssetProgress {
  uint64_t bytes_copied{0};
  uint64_t total_bytes{0};
};

class RasterAssetReader final : public document::RasterSourceProvider {
 public:
  ~RasterAssetReader();
  RasterAssetReader(const RasterAssetReader&) = delete;
  RasterAssetReader& operator=(const RasterAssetReader&) = delete;

  [[nodiscard]] const RasterSourceDescriptor& descriptor() const noexcept {
    return descriptor_;
  }

  [[nodiscard]] uint64_t size_bytes() const noexcept override {
    return file_bytes_;
  }

  /// Reads from a 64-bit byte offset. A short read is valid only at EOF.
  [[nodiscard]] bool read_at(uint64_t offset, std::span<uint8_t> destination,
                             size_t& bytes_read,
                             std::string* error = nullptr) const override;

 private:
  friend class RasterAssetStore;
  RasterAssetReader(RasterSourceDescriptor descriptor,
                    std::filesystem::path path, uint64_t file_bytes);

  RasterSourceDescriptor descriptor_;
  std::filesystem::path path_;
  uint64_t file_bytes_{0};
#ifdef _WIN32
  void* handle_{nullptr};
#else
  mutable std::ifstream stream_;
#endif
  mutable std::mutex mutex_;
};

/// Disk-backed immutable source store. Imports and reads use bounded buffers;
/// callers must keep the descriptor reachable while a document/undo snapshot
/// references it. Derived preview/tile caches belong in separate storage.
class RasterAssetStore final {
 public:
  using CancelCheck = std::function<bool()>;
  using ProgressCallback = std::function<void(const RasterAssetProgress&)>;

  explicit RasterAssetStore(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }

  [[nodiscard]] bool store_embedded(
      const std::filesystem::path& source, std::string media_type,
      RasterSourceDescriptor& out_descriptor, const CancelCheck& cancelled = {},
      const ProgressCallback& progress = {}, std::string* error = nullptr) const;

  /// Stores an immutable in-memory source using bounded writes and hashing.
  [[nodiscard]] bool store_embedded_bytes(
      std::span<const uint8_t> source, std::string media_type,
      RasterSourceDescriptor& out_descriptor, const CancelCheck& cancelled = {},
      const ProgressCallback& progress = {}, std::string* error = nullptr) const;

  /// Rebuilds an Embedded cache entry using its persisted descriptor. The .pr
  /// source bytes remain authoritative; the cache is atomically replaced only
  /// after its digest matches the descriptor.
  [[nodiscard]] bool restore_embedded_bytes(
      std::span<const uint8_t> source,
      const RasterSourceDescriptor& descriptor,
      const CancelCheck& cancelled = {},
      const ProgressCallback& progress = {},
      std::string* error = nullptr) const;

  [[nodiscard]] bool register_linked(
      const std::filesystem::path& source, std::string media_type,
      RasterSourceDescriptor& out_descriptor,
      std::string* error = nullptr) const;

  /// Re-hashes an immutable source in bounded reads. Linked files are pinned
  /// for the duration of the operation; cancellation never reports success.
  [[nodiscard]] bool verify_source(
      const RasterSourceDescriptor& descriptor,
      const CancelCheck& cancelled = {},
      const ProgressCallback& progress = {},
      std::string* error = nullptr) const;

  [[nodiscard]] std::shared_ptr<RasterAssetReader> open(
      const RasterSourceDescriptor& descriptor,
      std::string* error = nullptr) const;

  [[nodiscard]] std::filesystem::path embedded_path(
      const RasterSourceDescriptor& descriptor) const;

 private:
  std::filesystem::path root_;
};

class TemporaryRasterAssetProvider;

/// Owns a unique per-import temporary source store. Providers keep this session
/// alive while snapshots or undo history reference an imported image.
class TemporaryRasterAssetSession final
    : public std::enable_shared_from_this<TemporaryRasterAssetSession> {
 public:
  using CancelCheck = RasterAssetStore::CancelCheck;
  using ProgressCallback = RasterAssetStore::ProgressCallback;

  [[nodiscard]] static std::shared_ptr<TemporaryRasterAssetSession> create(
      std::string* error = nullptr);
  ~TemporaryRasterAssetSession();
  TemporaryRasterAssetSession(const TemporaryRasterAssetSession&) = delete;
  TemporaryRasterAssetSession& operator=(const TemporaryRasterAssetSession&) = delete;

  [[nodiscard]] std::shared_ptr<TemporaryRasterAssetProvider> store_embedded(
      std::span<const uint8_t> source, std::string media_type,
      const CancelCheck& cancelled = {}, const ProgressCallback& progress = {},
      std::string* error = nullptr);
  [[nodiscard]] std::shared_ptr<TemporaryRasterAssetProvider> restore_embedded(
      std::span<const uint8_t> source,
      const RasterSourceDescriptor& descriptor,
      const CancelCheck& cancelled = {},
      const ProgressCallback& progress = {},
      std::string* error = nullptr);

 private:
  explicit TemporaryRasterAssetSession(std::filesystem::path root);
  std::filesystem::path root_;
  std::unique_ptr<RasterAssetStore> store_;
};

/// Disk-backed provider scoped to a TemporaryRasterAssetSession.
class TemporaryRasterAssetProvider final : public document::RasterSourceProvider {
 public:
  [[nodiscard]] const RasterSourceDescriptor& descriptor() const noexcept {
    return descriptor_;
  }
  [[nodiscard]] uint64_t size_bytes() const noexcept override;
  [[nodiscard]] bool read_at(uint64_t offset,
                             std::span<uint8_t> destination,
                             size_t& bytes_read,
                             std::string* error = nullptr) const override;

 private:
  friend class TemporaryRasterAssetSession;
  TemporaryRasterAssetProvider(
      std::shared_ptr<TemporaryRasterAssetSession> session,
      RasterSourceDescriptor descriptor,
      std::shared_ptr<RasterAssetReader> reader) noexcept;

  // Member order ensures the reader closes before the session removes its root.
  std::shared_ptr<TemporaryRasterAssetSession> session_;
  RasterSourceDescriptor descriptor_;
  std::shared_ptr<RasterAssetReader> reader_;
};

}  // namespace pikdraw::raster
