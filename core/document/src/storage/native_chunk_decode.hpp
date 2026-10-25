#pragma once

#include <cstdint>
#include <functional>
#include <span>

namespace pikdraw::document::storage_detail {

using CancelCheck = std::function<bool()>;

enum class NativeDecodeStatus : uint8_t { Success, Cancelled, InvalidData };

/// Inflate one native v24 chunk while checking cancellation between bounded
/// output windows. The caller owns both input and output storage.
[[nodiscard]] NativeDecodeStatus inflate_native_chunk(
    std::span<const uint8_t> compressed, std::span<uint8_t> output,
    const CancelCheck& cancelled);

/// Calculate a chunk CRC while checking cancellation between 64 KiB windows.
[[nodiscard]] NativeDecodeStatus calculate_chunk_crc32(
    std::span<const uint8_t> bytes, const CancelCheck& cancelled,
    uint32_t& out_crc);

}  // namespace pikdraw::document::storage_detail
