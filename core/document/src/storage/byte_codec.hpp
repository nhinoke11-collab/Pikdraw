#pragma once
#include "pikdraw/document/storage/native_format.hpp"
#include <bit>
#include <span>
#include <stdexcept>

namespace pikdraw::document::storage_detail {
inline constexpr size_t max_file_bytes = 256u * 1024u * 1024u;
inline constexpr uint32_t max_objects = 100000;
inline constexpr uint32_t max_elements = 1000000;
inline constexpr uint32_t max_string_bytes = 16u * 1024u * 1024u;

class Writer {
public:
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
};

class Reader {
public:
  explicit Reader(std::span<const uint8_t> data) : data_(data) {}
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
  std::string str() {
    const auto length = count(max_string_bytes);
    const auto *start =
        reinterpret_cast<const char *>(data_.data() + position_);
    std::string result(start, length);
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
};

void write_color(Writer &writer, const color::ColorValue &value);
color::ColorValue read_color(Reader &reader);
void write_object(Writer &writer, const ObjectVariant &object);
ObjectVariant read_object(Reader &reader, uint32_t &remaining_elements);
uint32_t container_crc(const uint8_t *data, size_t size) noexcept;
} // namespace pikdraw::document::storage_detail
