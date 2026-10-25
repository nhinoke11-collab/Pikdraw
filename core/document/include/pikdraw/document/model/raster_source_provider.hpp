#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace pikdraw::document {

/// Process-local, immutable access to encoded raster source bytes.
/// The persistent RasterSourceDescriptor carries identity and provenance;
/// this interface keeps file handles and storage implementation out of .pr.
class RasterSourceProvider {
 public:
  virtual ~RasterSourceProvider() = default;

  [[nodiscard]] virtual uint64_t size_bytes() const noexcept = 0;
  [[nodiscard]] virtual bool read_at(uint64_t offset,
                                     std::span<uint8_t> destination,
                                     size_t& bytes_read,
                                     std::string* error = nullptr) const = 0;
};

}  // namespace pikdraw::document
