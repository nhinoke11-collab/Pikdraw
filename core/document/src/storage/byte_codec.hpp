#pragma once
#include "pikdraw/document/storage/native_format.hpp"
#include <algorithm>
#include <bit>
#include <atomic>
#include <cstring>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace pikdraw::document::storage_detail {
using ProfileResourceIndexMap =
    std::unordered_map<const color::ProfileResource *, uint32_t>;
// Legacy record counts and blob lengths are u32 on disk. Keep those wire-width
// bounds, but do not impose the old 256 MiB whole-container ceiling. Container
// allocation is checked against available memory by the native reader.
inline constexpr uint32_t max_blob_bytes = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t max_objects = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t max_elements = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t max_image_pixels = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t max_string_bytes = 16u * 1024u * 1024u;

inline bool is_valid_utf8(std::string_view value) noexcept {
  size_t i = 0;
  while (i < value.size()) {
    const auto lead = static_cast<uint8_t>(value[i]);
    if (lead <= 0x7f) {
      ++i;
      continue;
    }
    size_t continuation_count = 0;
    uint8_t second_min = 0x80;
    uint8_t second_max = 0xbf;
    if (lead >= 0xc2 && lead <= 0xdf) {
      continuation_count = 1;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      continuation_count = 2;
      if (lead == 0xe0) second_min = 0xa0;
      if (lead == 0xed) second_max = 0x9f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      continuation_count = 3;
      if (lead == 0xf0) second_min = 0x90;
      if (lead == 0xf4) second_max = 0x8f;
    } else {
      return false;
    }
    if (continuation_count > value.size() - i - 1)
      return false;
    const auto second = static_cast<uint8_t>(value[i + 1]);
    if (second < second_min || second > second_max)
      return false;
    for (size_t j = 2; j <= continuation_count; ++j) {
      const auto byte = static_cast<uint8_t>(value[i + j]);
      if (byte < 0x80 || byte > 0xbf)
        return false;
    }
    i += continuation_count + 1;
  }
  return true;
}

class Writer {
public:
  explicit Writer(bool validate_utf8 = false) : validate_utf8_(validate_utf8) {}
  Writer(bool validate_utf8,
         std::function<void(std::span<const uint8_t>)> sink,
         size_t buffer_limit)
      : validate_utf8_(validate_utf8), sink_(std::move(sink)),
        buffer_limit_(buffer_limit) {
    if (!sink_ || buffer_limit_ == 0)
      throw std::invalid_argument("Invalid streaming native writer");
    bytes.reserve(std::min<size_t>(buffer_limit_, 64u * 1024u));
  }

  void u8(uint8_t value) {
    append(std::span<const uint8_t>(&value, 1));
  }
  void u32(uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
      u8(static_cast<uint8_t>(value >> shift));
  }
  void u64(uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
      u8(static_cast<uint8_t>(value >> shift));
  }
  void f64(double value) {
    if (!std::isfinite(value))
      throw std::invalid_argument("Non-finite native number");
    u64(std::bit_cast<uint64_t>(value));
  }
  void flag(bool value) { u8(value ? 1 : 0); }
  void count(size_t value, uint32_t limit) {
    if (value > limit)
      throw std::length_error("Native collection exceeds budget");
    u32(static_cast<uint32_t>(value));
  }
  void str(const std::string &value) {
    if (validate_utf8_ && !is_valid_utf8(value))
      throw std::invalid_argument("Native string is not valid UTF-8");
    count(value.size(), max_string_bytes);
    append(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(value.data()), value.size()));
  }
  void blob(std::span<const uint8_t> value, uint32_t limit) {
    count(value.size(), limit);
    append(value);
  }
  void transform(const geometry::Transform2D &value) {
    f64(value.a);
    f64(value.b);
    f64(value.c);
    f64(value.d);
    f64(value.tx);
    f64(value.ty);
  }
  void append(std::span<const uint8_t> data) {
    if (data.size() > std::numeric_limits<uint64_t>::max() - total_size_)
      throw std::length_error("Native data size overflows u64");
    total_size_ += data.size();
    if (!sink_) {
      ensure(data.size());
      bytes.insert(bytes.end(), data.begin(), data.end());
      return;
    }
    while (!data.empty()) {
      const auto available = buffer_limit_ - bytes.size();
      const auto amount = std::min(available, data.size());
      bytes.insert(bytes.end(), data.begin(), data.begin() + amount);
      data = data.subspan(amount);
      if (bytes.size() == buffer_limit_) {
        sink_(bytes);
        bytes.clear();
      }
    }
  }

  [[nodiscard]] uint64_t size() const noexcept {
    return sink_ ? total_size_ : static_cast<uint64_t>(bytes.size());
  }

  void flush() {
    if (sink_ && !bytes.empty()) {
      sink_(bytes);
      bytes.clear();
    }
  }
  std::vector<uint8_t> bytes;

private:
  void ensure(size_t length) const {
    if (length > bytes.max_size() - bytes.size())
      throw std::length_error("Native data exceeds addressable memory");
  }
  bool validate_utf8_{false};
  std::function<void(std::span<const uint8_t>)> sink_;
  size_t buffer_limit_{0};
  uint64_t total_size_{0};
};

class Reader {
public:
  explicit Reader(std::span<const uint8_t> data, bool validate_utf8 = false,
                  const std::atomic_bool *cancel_requested = nullptr)
      : data_(data), validate_utf8_(validate_utf8),
        cancel_requested_(cancel_requested) {}
  uint8_t u8() {
    require(1);
    return data_[position_++];
  }
  uint32_t u32() {
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
      result |= static_cast<uint32_t>(u8()) << shift;
    return result;
  }
  uint64_t u64() {
    uint64_t result = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
      result |= static_cast<uint64_t>(u8()) << shift;
    return result;
  }
  double f64() {
    const auto result = std::bit_cast<double>(u64());
    if (!std::isfinite(result))
      throw std::invalid_argument("Non-finite native number");
    return result;
  }
  bool flag() {
    const auto value = u8();
    if (value > 1)
      throw std::invalid_argument("Invalid native boolean tag");
    return value != 0;
  }
  uint32_t count(uint32_t limit, size_t minimum_bytes = 1) {
    const auto value = u32();
    if (value > limit || value > remaining() / minimum_bytes)
      throw std::length_error("Native collection count exceeds payload/budget at offset " +
          std::to_string(position_ - 4) + " (count=" + std::to_string(value) +
          ", remaining=" + std::to_string(remaining()) + ")");
    return value;
  }
  std::string str(uint32_t limit = max_string_bytes) {
    const auto length = count(limit);
    require(length);
    std::string result(length, '\0');
    for (size_t copied = 0; copied < length;) {
      check_cancel();
      const auto part = std::min<size_t>(64u * 1024u, length - copied);
      std::memcpy(result.data() + copied, data_.data() + position_ + copied, part);
      copied += part;
    }
    position_ += length;
    if (validate_utf8_ && !is_valid_utf8(result))
      throw std::invalid_argument("Native string is not valid UTF-8");
    return result;
  }
  std::vector<uint8_t> blob(uint32_t limit) {
    const auto length = count(limit);
    require(length);
    std::vector<uint8_t> result(length);
    for (size_t copied = 0; copied < length;) {
      check_cancel();
      const auto part = std::min<size_t>(64u * 1024u, length - copied);
      std::memcpy(result.data() + copied, data_.data() + position_ + copied, part);
      copied += part;
    }
    position_ += length;
    return result;
  }
  std::span<const uint8_t> take(size_t length) {
    require(length);
    const auto result = data_.subspan(position_, length);
    position_ += length;
    return result;
  }
  Reader subreader(size_t length) {
    require(length);
    Reader result(data_.subspan(position_, length), validate_utf8_,
                  cancel_requested_);
    position_ += length;
    return result;
  }
  geometry::Transform2D transform() {
    return {f64(), f64(), f64(), f64(), f64(), f64()};
  }
  size_t remaining() const noexcept { return data_.size() - position_; }

private:
  void check_cancel() const {
    if (cancel_requested_ &&
        cancel_requested_->load(std::memory_order_acquire))
      throw std::runtime_error("Native load cancelled");
  }
  void require(size_t length) const {
    if (length > remaining())
      throw std::invalid_argument("Truncated native payload");
    if (cancel_requested_ && position_ >= next_cancel_check_) {
      check_cancel();
      next_cancel_check_ = position_ + 64u * 1024u;
    }
  }
  std::span<const uint8_t> data_;
  size_t position_{0};
  bool validate_utf8_{false};
  const std::atomic_bool *cancel_requested_{nullptr};
  mutable size_t next_cancel_check_{0};
};

void write_color(Writer &writer, const color::ColorValue &value,
                 const ProfileResourceIndexMap *profile_indices = nullptr);
color::ColorValue read_color(Reader &reader, bool include_profile_binding = false);
void write_object(Writer &writer, const ObjectVariant &object,
                  bool include_authoring_source,
                  const EditablePath *source_override = nullptr,
                  bool include_appearance = false,
                  bool include_gradient = false,
                  const ProfileResourceIndexMap *profile_indices = nullptr,
                  bool include_image_pixels = true);
ObjectVariant read_object(Reader &reader, uint32_t &remaining_elements,
                          bool include_authoring_source,
                          bool include_profile_bindings = false,
                          bool include_image_pixels = true);
void write_appearance(Writer &writer, const Appearance &appearance,
                      const ProfileResourceIndexMap *profile_indices = nullptr);
Appearance read_appearance(Reader &reader, size_t &remaining_effects,
                           bool include_profile_bindings = false);
void write_gradient(Writer &writer, const std::shared_ptr<const GradientPaint> &gradient,
                    const ProfileResourceIndexMap *profile_indices = nullptr);
std::shared_ptr<const GradientPaint> read_gradient(Reader &reader, size_t &remaining_stops,
                                                   bool include_profile_bindings = false);
void write_editable_path(Writer &writer, const EditablePath &source);
EditablePath read_editable_path(Reader &reader, uint32_t &remaining_elements);
uint32_t container_crc(const uint8_t *data, size_t size) noexcept;
uint32_t crc32_update(uint32_t state, const uint8_t *data,
                      size_t size) noexcept;
uint32_t container_crc_cancellable(const uint8_t *data, size_t size,
                                   const std::atomic_bool *cancel_requested);
} // namespace pikdraw::document::storage_detail
