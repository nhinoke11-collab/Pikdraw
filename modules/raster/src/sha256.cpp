#include "sha256.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace pikdraw::raster::detail {
namespace {

constexpr std::array<uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

uint32_t read_be32(const uint8_t* bytes) noexcept {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
      (static_cast<uint32_t>(bytes[1]) << 16) |
      (static_cast<uint32_t>(bytes[2]) << 8) |
      static_cast<uint32_t>(bytes[3]);
}

void write_be64(uint8_t* bytes, uint64_t value) noexcept {
  for (size_t i = 0; i < 8; ++i)
    bytes[i] = static_cast<uint8_t>(value >> ((7u - i) * 8u));
}

}  // namespace

void Sha256::process_block(const uint8_t* block) noexcept {
  std::array<uint32_t, 64> words{};
  for (size_t i = 0; i < 16; ++i) words[i] = read_be32(block + i * 4);
  for (size_t i = 16; i < words.size(); ++i) {
    const auto x = words[i - 15];
    const auto y = words[i - 2];
    const uint32_t sigma0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
    const uint32_t sigma1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
    words[i] = words[i - 16] + sigma0 + words[i - 7] + sigma1;
  }

  auto a = state_[0];
  auto b = state_[1];
  auto c = state_[2];
  auto d = state_[3];
  auto e = state_[4];
  auto f = state_[5];
  auto g = state_[6];
  auto h = state_[7];
  for (size_t i = 0; i < words.size(); ++i) {
    const uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t first = h + sum1 + choice + kRoundConstants[i] + words[i];
    const uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t second = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + first;
    d = c;
    c = b;
    b = a;
    a = first + second;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

bool Sha256::update(std::span<const uint8_t> bytes) noexcept {
  constexpr uint64_t kMaximumBytes = std::numeric_limits<uint64_t>::max() / 8u;
  if (finished_ || bytes.size() > kMaximumBytes - total_bytes_) return false;
  total_bytes_ += static_cast<uint64_t>(bytes.size());
  size_t consumed = 0;
  if (buffered_bytes_ != 0) {
    const auto count = (std::min)(buffer_.size() - buffered_bytes_, bytes.size());
    std::copy_n(bytes.begin(), count, buffer_.begin() + buffered_bytes_);
    buffered_bytes_ += count;
    consumed += count;
    if (buffered_bytes_ == buffer_.size()) {
      process_block(buffer_.data());
      buffered_bytes_ = 0;
    }
  }
  while (bytes.size() - consumed >= buffer_.size()) {
    process_block(bytes.data() + consumed);
    consumed += buffer_.size();
  }
  if (consumed < bytes.size()) {
    buffered_bytes_ = bytes.size() - consumed;
    std::copy(bytes.begin() + consumed, bytes.end(), buffer_.begin());
  }
  return true;
}

bool Sha256::finish_hex(std::string& output) noexcept {
  if (finished_) return false;
  const uint64_t bit_length = total_bytes_ * 8u;
  buffer_[buffered_bytes_++] = 0x80u;
  if (buffered_bytes_ > 56) {
    std::fill(buffer_.begin() + buffered_bytes_, buffer_.end(), 0);
    process_block(buffer_.data());
    buffered_bytes_ = 0;
  }
  std::fill(buffer_.begin() + buffered_bytes_, buffer_.begin() + 56, 0);
  write_be64(buffer_.data() + 56, bit_length);
  process_block(buffer_.data());

  static constexpr char kHex[] = "0123456789abcdef";
  try {
    output.clear();
    output.reserve(state_.size() * 8);
    for (const auto word : state_) {
      for (int shift = 28; shift >= 0; shift -= 4)
        output.push_back(kHex[(word >> shift) & 0x0fu]);
    }
  } catch (...) {
    output.clear();
    return false;
  }
  finished_ = true;
  return true;
}

}  // namespace pikdraw::raster::detail
