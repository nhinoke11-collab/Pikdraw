#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace pikdraw::raster::detail {

class Sha256 final {
 public:
  [[nodiscard]] bool update(std::span<const uint8_t> bytes) noexcept;
  [[nodiscard]] bool finish_hex(std::string& output) noexcept;

 private:
  void process_block(const uint8_t* block) noexcept;

  std::array<uint32_t, 8> state_{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}};
  std::array<uint8_t, 64> buffer_{};
  uint64_t total_bytes_{0};
  size_t buffered_bytes_{0};
  bool finished_{false};
};

}  // namespace pikdraw::raster::detail
