#pragma once

#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "pikdraw/document/model/raster_source_provider.hpp"

#include <memory>
#include <utility>

namespace pikdraw::interop {

/// Adapts a document-owned raster source provider to the decoder's 64-bit,
/// random-access byte-source interface without materializing the source.
class RasterAssetByteSource final : public RasterByteSource {
 public:
  explicit RasterAssetByteSource(
      std::shared_ptr<const document::RasterSourceProvider> provider) noexcept
      : provider_(std::move(provider)) {}

  [[nodiscard]] uint64_t size_bytes() const noexcept override {
    return provider_ ? provider_->size_bytes() : 0;
  }

  [[nodiscard]] bool read_at(uint64_t offset,
                             std::span<uint8_t> destination,
                             size_t& bytes_read) const noexcept override {
    bytes_read = 0;
    if (!provider_) return false;
    try {
      return provider_->read_at(offset, destination, bytes_read);
    } catch (...) {
      bytes_read = 0;
      return false;
    }
  }

 private:
  std::shared_ptr<const document::RasterSourceProvider> provider_;
};

}  // namespace pikdraw::interop
