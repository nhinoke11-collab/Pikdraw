#pragma once

#include "pikdraw/document/storage/native_format.hpp"

#include <cstdint>
#include <span>

namespace pikdraw::document::storage_detail {

void append_native_chunk_record(const NativeFormat::StreamWrite& write,
                                std::span<const uint8_t> uncompressed);
uint32_t combine_crc32(uint32_t first, uint32_t second,
                       uint64_t second_length) noexcept;

} // namespace pikdraw::document::storage_detail
