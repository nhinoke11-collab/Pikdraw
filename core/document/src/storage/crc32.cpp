#include "byte_codec.hpp"
#include <array>

namespace pikdraw::document {
namespace {
constexpr auto crc_table = [] {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < table.size(); ++i) {
    auto value = i;
    for (int j = 0; j < 8; ++j)
      value = (value & 1) ? 0xedb88320u ^ (value >> 1) : value >> 1;
    table[i] = value;
  }
  return table;
}();

uint32_t checksum(const uint8_t *data, size_t size,
                  bool zero_header_checksum) noexcept {
  uint32_t result = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    const auto byte = zero_header_checksum && i >= 32 && i < 36 ? 0 : data[i];
    result = crc_table[(result ^ byte) & 0xff] ^ (result >> 8);
  }
  return result ^ 0xffffffffu;
}
} // namespace

uint32_t NativeFormat::calculate_crc32(const uint8_t *data,
                                       size_t length) noexcept {
  return checksum(data, length, false);
}

uint32_t storage_detail::container_crc(const uint8_t *data,
                                       size_t size) noexcept {
  return checksum(data, size, true);
}
} // namespace pikdraw::document
