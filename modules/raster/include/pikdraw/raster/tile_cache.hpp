#pragma once

#include "pikdraw/raster/asset_store.hpp"
#include "pikdraw/raster/memory_budget.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pikdraw::raster {

enum class RasterSampleLayout : uint8_t { RGBA8, BGRA8, RGBA16, RGBA32Float };

struct RasterTileKey {
  RasterAssetId asset_id;
  uint64_t revision{0};
  uint64_t edit_graph_fingerprint{0};
  uint32_t frame_index{0};
  uint32_t level_of_detail{0};
  int64_t tile_x{0};
  int64_t tile_y{0};
  RasterSampleLayout sample_layout{RasterSampleLayout::RGBA8};
  std::string output_profile;
  uint32_t evaluator_version{1};

  friend bool operator==(const RasterTileKey&, const RasterTileKey&) = default;
};

struct RasterTile {
  RasterTileKey key;
  uint32_t width{0};
  uint32_t height{0};
  uint64_t row_stride{0};
  std::vector<uint8_t> pixels;
  std::shared_ptr<RasterMemoryBudget::Reservation> reservation;

  [[nodiscard]] bool valid() const noexcept;
};

struct RasterTileKeyHash {
  [[nodiscard]] size_t operator()(const RasterTileKey& key) const noexcept;
};

/// Thread-safe CPU tile LRU. The shared reservation remains live while either
/// the cache or a renderer still holds a tile, including after LRU eviction.
class RasterTileCache final {
 public:
  explicit RasterTileCache(uint64_t capacity_bytes);

  [[nodiscard]] std::shared_ptr<const RasterTile> find(
      const RasterTileKey& key);
  [[nodiscard]] bool insert(std::shared_ptr<const RasterTile> tile);
  void set_capacity(uint64_t capacity_bytes);
  void clear() noexcept;

  [[nodiscard]] uint64_t capacity_bytes() const noexcept;
  [[nodiscard]] uint64_t resident_bytes() const noexcept;
  [[nodiscard]] size_t entry_count() const noexcept;

 private:
  struct Entry {
    std::shared_ptr<const RasterTile> tile;
    uint64_t bytes{0};
    uint64_t last_use{0};
  };

  void evict_to_fit(uint64_t incoming_bytes,
                    const RasterTileKey* preserve_key = nullptr);

  mutable std::mutex mutex_;
  std::unordered_map<RasterTileKey, Entry, RasterTileKeyHash> entries_;
  uint64_t capacity_bytes_{0};
  uint64_t resident_bytes_{0};
  uint64_t clock_{0};
};

}  // namespace pikdraw::raster
