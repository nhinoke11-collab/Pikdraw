#include "native_chunk_decode.hpp"

#include <algorithm>
#include <limits>
#include <zlib.h>

namespace pikdraw::document::storage_detail {

NativeDecodeStatus inflate_native_chunk(
    std::span<const uint8_t> compressed, std::span<uint8_t> output,
    const CancelCheck& cancelled) {
  if (compressed.empty() || output.empty() ||
      compressed.size() > std::numeric_limits<uInt>::max())
    return NativeDecodeStatus::InvalidData;

  z_stream stream{};
  if (inflateInit(&stream) != Z_OK)
    return NativeDecodeStatus::InvalidData;
  struct InflateEnd {
    z_stream& stream;
    ~InflateEnd() { inflateEnd(&stream); }
  } inflate_end{stream};

  stream.next_in = const_cast<Bytef*>(compressed.data());
  stream.avail_in = static_cast<uInt>(compressed.size());
  size_t output_written = 0;
  int status = Z_OK;
  while (status == Z_OK) {
    if (cancelled && cancelled())
      return NativeDecodeStatus::Cancelled;

    const size_t remaining = output.size() - output_written;
    uint8_t overflow_byte = 0;
    const bool checking_overflow = remaining == 0;
    const size_t output_window = checking_overflow
        ? 1
        : std::min<size_t>(remaining, 64u * 1024u);
    stream.next_out = checking_overflow
        ? &overflow_byte
        : output.data() + output_written;
    stream.avail_out = static_cast<uInt>(output_window);
    const uInt input_before = stream.avail_in;
    status = inflate(&stream, Z_NO_FLUSH);
    const size_t produced = output_window - stream.avail_out;

    if (checking_overflow) {
      if (produced != 0)
        return NativeDecodeStatus::InvalidData;
    } else {
      output_written += produced;
    }
    if (status == Z_STREAM_END)
      break;
    if (status != Z_OK ||
        (input_before == stream.avail_in && produced == 0))
      return NativeDecodeStatus::InvalidData;
  }

  if (cancelled && cancelled())
    return NativeDecodeStatus::Cancelled;
  return output_written == output.size() && stream.avail_in == 0
      ? NativeDecodeStatus::Success
      : NativeDecodeStatus::InvalidData;
}

NativeDecodeStatus calculate_chunk_crc32(
    std::span<const uint8_t> bytes, const CancelCheck& cancelled,
    uint32_t& out_crc) {
  if (bytes.empty())
    return NativeDecodeStatus::InvalidData;
  uLong crc = crc32(0L, Z_NULL, 0);
  constexpr size_t kWindowBytes = 64u * 1024u;
  for (size_t offset = 0; offset < bytes.size();) {
    if (cancelled && cancelled())
      return NativeDecodeStatus::Cancelled;
    const size_t count = std::min(kWindowBytes, bytes.size() - offset);
    crc = crc32(crc, bytes.data() + offset, static_cast<uInt>(count));
    offset += count;
  }
  if (cancelled && cancelled())
    return NativeDecodeStatus::Cancelled;
  out_crc = static_cast<uint32_t>(crc);
  return NativeDecodeStatus::Success;
}

}  // namespace pikdraw::document::storage_detail
