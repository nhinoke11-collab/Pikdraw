#include "native_chunk_encode.hpp"

#include "byte_codec.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace pikdraw::document::storage_detail {
namespace {

std::vector<uint8_t> deflate_chunk(std::span<const uint8_t> input) {
  if (input.empty() || input.size() > kNativeChunkBytes)
    throw std::length_error("Invalid native compression chunk size");
  uLongf output_size = compressBound(static_cast<uLong>(input.size()));
  std::vector<uint8_t> output(static_cast<size_t>(output_size));
  const auto status = compress2(output.data(), &output_size, input.data(),
                                static_cast<uLong>(input.size()), Z_BEST_SPEED);
  if (status != Z_OK)
    throw std::runtime_error("Native DEFLATE compression failed");
  output.resize(static_cast<size_t>(output_size));
  return output;
}

uint32_t crc_matrix_times(const std::array<uint32_t, 32>& matrix,
                          uint32_t vector) noexcept {
  uint32_t result = 0;
  size_t index = 0;
  while (vector != 0) {
    if ((vector & 1u) != 0) result ^= matrix[index];
    vector >>= 1;
    ++index;
  }
  return result;
}

void crc_matrix_square(std::array<uint32_t, 32>& square,
                       const std::array<uint32_t, 32>& matrix) noexcept {
  for (size_t index = 0; index < square.size(); ++index)
    square[index] = crc_matrix_times(matrix, matrix[index]);
}

} // namespace

void append_native_chunk_record(const NativeFormat::StreamWrite& write,
                                std::span<const uint8_t> uncompressed) {
  auto compressed = deflate_chunk(uncompressed);
  const bool use_deflate = compressed.size() < uncompressed.size();
  const auto stored = use_deflate
      ? std::span<const uint8_t>(compressed)
      : uncompressed;
  Writer record_header;
  record_header.u32(static_cast<uint32_t>(uncompressed.size()));
  record_header.u32(static_cast<uint32_t>(stored.size()));
  record_header.u8(use_deflate ? 1 : 0);
  record_header.u8(0);
  record_header.u8(0);
  record_header.u8(0);
  record_header.u32(NativeFormat::calculate_crc32(uncompressed.data(),
                                                  uncompressed.size()));
  write(record_header.bytes);
  write(stored);
}

uint32_t combine_crc32(uint32_t first, uint32_t second,
                       uint64_t second_length) noexcept {
  if (second_length == 0) return first;
  std::array<uint32_t, 32> odd{};
  std::array<uint32_t, 32> even{};
  odd[0] = 0xedb88320u;
  uint32_t row = 1;
  for (size_t index = 1; index < odd.size(); ++index) {
    odd[index] = row;
    row <<= 1;
  }
  crc_matrix_square(even, odd);
  crc_matrix_square(odd, even);
  do {
    crc_matrix_square(even, odd);
    if ((second_length & 1u) != 0)
      first = crc_matrix_times(even, first);
    second_length >>= 1;
    if (second_length == 0) break;
    crc_matrix_square(odd, even);
    if ((second_length & 1u) != 0)
      first = crc_matrix_times(odd, first);
    second_length >>= 1;
  } while (second_length != 0);
  return first ^ second;
}

} // namespace pikdraw::document::storage_detail
