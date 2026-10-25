#include "pikdraw/raster/tile_cache.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace pikdraw::raster {
namespace {

size_t mix_hash(size_t seed, size_t value) noexcept {
  return seed ^ (value + static_cast<size_t>(0x9e3779b97f4a7c15ull) +
                  (seed << 6) + (seed >> 2));
}

uint64_t bytes_per_pixel(RasterSampleLayout layout) noexcept {
  switch (layout) {
    case RasterSampleLayout::RGBA8:
    case RasterSampleLayout::BGRA8: return 4;
    case RasterSampleLayout::RGBA16: return 8;
    case RasterSampleLayout::RGBA32Float: return 16;
  }
  return 0;
}

}  // namespace

bool RasterTile::valid() const noexcept {
  const auto bpp = bytes_per_pixel(key.sample_layout);
  if (!key.asset_id.valid() || key.revision == 0 ||
      key.evaluator_version == 0 || width == 0 || height == 0 || bpp == 0 ||
      !reservation)
    return false;
  const uint64_t minimum_stride = static_cast<uint64_t>(width) * bpp;
  if (row_stride < minimum_stride ||
      row_stride > std::numeric_limits<uint64_t>::max() / height)
    return false;
  const uint64_t required = row_stride * height;
  return required == pixels.size() && reservation->bytes() >= required;
}

size_t RasterTileKeyHash::operator()(const RasterTileKey& key) const noexcept {
  size_t seed = std::hash<uint64_t>{}(key.asset_id.high);
  seed = mix_hash(seed, std::hash<uint64_t>{}(key.asset_id.low));
  seed = mix_hash(seed, std::hash<uint64_t>{}(key.revision));
  seed = mix_hash(seed, std::hash<uint64_t>{}(key.edit_graph_fingerprint));
  seed = mix_hash(seed, std::hash<uint32_t>{}(key.frame_index));
  seed = mix_hash(seed, std::hash<uint32_t>{}(key.level_of_detail));
  seed = mix_hash(seed, std::hash<int64_t>{}(key.tile_x));
  seed = mix_hash(seed, std::hash<int64_t>{}(key.tile_y));
  seed = mix_hash(seed, std::hash<uint8_t>{}(
      static_cast<uint8_t>(key.sample_layout)));
  seed = mix_hash(seed, std::hash<std::string>{}(key.output_profile));
  return mix_hash(seed, std::hash<uint32_t>{}(key.evaluator_version));
}

RasterTileCache::RasterTileCache(uint64_t capacity_bytes)
    : capacity_bytes_(capacity_bytes) {}

std::shared_ptr<const RasterTile> RasterTileCache::find(
    const RasterTileKey& key) {
  std::lock_guard lock(mutex_);
  const auto found = entries_.find(key);
  if (found == entries_.end()) return {};
  if (clock_ == std::numeric_limits<uint64_t>::max()) {
    uint64_t stamp = 1;
    for (auto& [entry_key, entry] : entries_) {
      (void)entry_key;
      entry.last_use = stamp++;
    }
    clock_ = stamp;
  }
  found->second.last_use = ++clock_;
  return found->second.tile;
}

void RasterTileCache::evict_to_fit(uint64_t incoming_bytes,
                                   const RasterTileKey* preserve_key) {
  if (incoming_bytes > capacity_bytes_) incoming_bytes = capacity_bytes_;
  while (resident_bytes_ > capacity_bytes_ - incoming_bytes &&
         !entries_.empty()) {
    auto victim = entries_.end();
    for (auto current = entries_.begin(); current != entries_.end(); ++current) {
      if (preserve_key && current->first == *preserve_key) continue;
      if (victim == entries_.end() ||
          current->second.last_use < victim->second.last_use)
        victim = current;
    }
    if (victim == entries_.end()) break;
    resident_bytes_ -= victim->second.bytes;
    entries_.erase(victim);
  }
}

bool RasterTileCache::insert(std::shared_ptr<const RasterTile> tile) {
  if (!tile || !tile->valid()) return false;
  const uint64_t bytes = static_cast<uint64_t>(tile->pixels.size());
  std::lock_guard lock(mutex_);
  if (bytes > capacity_bytes_) return false;
  const auto existing = entries_.find(tile->key);
  if (existing != entries_.end()) {
    resident_bytes_ -= existing->second.bytes;
    entries_.erase(existing);
  }
  evict_to_fit(bytes);
  if (resident_bytes_ > capacity_bytes_ - bytes) return false;
  const auto stamp = clock_ == std::numeric_limits<uint64_t>::max()
      ? 1 : ++clock_;
  if (stamp == 1 && clock_ == std::numeric_limits<uint64_t>::max()) clock_ = 1;
  entries_.emplace(tile->key, Entry{tile, bytes, stamp});
  resident_bytes_ += bytes;
  return true;
}

void RasterTileCache::set_capacity(uint64_t capacity_bytes) {
  std::lock_guard lock(mutex_);
  capacity_bytes_ = capacity_bytes;
  evict_to_fit(0);
}

void RasterTileCache::clear() noexcept {
  std::lock_guard lock(mutex_);
  entries_.clear();
  resident_bytes_ = 0;
}

uint64_t RasterTileCache::capacity_bytes() const noexcept {
  std::lock_guard lock(mutex_);
  return capacity_bytes_;
}

uint64_t RasterTileCache::resident_bytes() const noexcept {
  std::lock_guard lock(mutex_);
  return resident_bytes_;
}

size_t RasterTileCache::entry_count() const noexcept {
  std::lock_guard lock(mutex_);
  return entries_.size();
}

}  // namespace pikdraw::raster
