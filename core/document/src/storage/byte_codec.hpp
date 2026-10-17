#pragma once
#include "pikdraw/document/storage/native_format.hpp"
#include <bit>
#include <span>
#include <stdexcept>
#include <string_view>

namespace pikdraw::document::storage_detail {
inline constexpr size_t max_file_bytes = 256u * 1024u * 1024u;
inline constexpr uint32_t max_objects = 100000;
inline constexpr uint32_t max_elements = 1000000;
inline constexpr uint32_t max_image_pixels =
    static_cast<uint32_t>(max_file_bytes / sizeof(uint32_t));
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

  void u8(uint8_t value) {
    ensure(1);
    bytes.push_back(value);
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
    ensure(value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
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
    ensure(data.size());
    bytes.insert(bytes.end(), data.begin(), data.end());
  }
  std::vector<uint8_t> bytes;

private:
  void ensure(size_t length) const {
    if (length > max_file_bytes - bytes.size())
      throw std::length_error("Native file exceeds 256 MiB budget");
  }
  bool validate_utf8_{false};
};

class Reader {
public:
  explicit Reader(std::span<const uint8_t> data, bool validate_utf8 = false)
      : data_(data), validate_utf8_(validate_utf8) {}
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
      throw std::length_error("Native collection count exceeds payload/budget");
    return value;
  }
  std::string str(uint32_t limit = max_string_bytes) {
    const auto length = count(limit);
    const auto *start =
        reinterpret_cast<const char *>(data_.data() + position_);
    std::string result(start, length);
    position_ += length;
    if (validate_utf8_ && !is_valid_utf8(result))
      throw std::invalid_argument("Native string is not valid UTF-8");
    return result;
  }
  Reader subreader(size_t length) {
    require(length);
    Reader result(data_.subspan(position_, length), validate_utf8_);
    position_ += length;
    return result;
  }
  geometry::Transform2D transform() {
    return {f64(), f64(), f64(), f64(), f64(), f64()};
  }
  size_t remaining() const noexcept { return data_.size() - position_; }

private:
  void require(size_t length) const {
    if (length > remaining())
      throw std::invalid_argument("Truncated native payload");
  }
  std::span<const uint8_t> data_;
  size_t position_{0};
  bool validate_utf8_{false};
};

void write_color(Writer &writer, const color::ColorValue &value);
color::ColorValue read_color(Reader &reader);
void write_object(Writer &writer, const ObjectVariant &object,
                  bool include_authoring_source,
                  const EditablePath *source_override = nullptr,
                  bool include_appearance = false);
ObjectVariant read_object(Reader &reader, uint32_t &remaining_elements,
                          bool include_authoring_source);
void write_appearance(Writer &writer, const Appearance &appearance);
Appearance read_appearance(Reader &reader, size_t &remaining_effects);
void write_editable_path(Writer &writer, const EditablePath &source);
EditablePath read_editable_path(Reader &reader, uint32_t &remaining_elements);
uint32_t container_crc(const uint8_t *data, size_t size) noexcept;
} // namespace pikdraw::document::storage_detail
