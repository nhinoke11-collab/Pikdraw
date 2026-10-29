#pragma once

#include "byte_codec.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pikdraw::document::storage_detail {

struct RasterSourceTable {
  struct Asset {
    const std::vector<uint8_t>* inline_bytes{nullptr};
    std::shared_ptr<const RasterSourceProvider> provider;
    uint64_t byte_count{0};
  };
  std::vector<Asset> assets;
  std::unordered_map<const std::vector<uint8_t>*, uint32_t> inline_indices;
  std::unordered_map<const RasterSourceProvider*, uint32_t> provider_indices;
};

template <class T>
struct RasterValueTable {
  std::vector<const std::vector<T>*> assets;
  std::unordered_map<const std::vector<T>*, uint32_t> indices;

  void add(const std::vector<T>* value) {
    if (!value || value->empty() || indices.contains(value)) return;
    if (value->size() > std::numeric_limits<size_t>::max() / sizeof(T))
      throw std::length_error("Raster sample asset exceeds addressable memory");
    const auto byte_count = value->size() * sizeof(T);
    const std::string_view view(
        reinterpret_cast<const char*>(value->data()), byte_count);
    const auto hash = std::hash<std::string_view>{}(view);
    auto& candidates = content_buckets[hash];
    uint32_t index = 0;
    for (const auto candidate : candidates) {
      if (*assets[candidate - 1] == *value) {
        index = candidate;
        break;
      }
    }
    if (index == 0) {
      if (assets.size() >= std::numeric_limits<uint32_t>::max())
        throw std::length_error("Too many raster sample assets");
      index = static_cast<uint32_t>(assets.size() + 1);
      assets.push_back(value);
      candidates.push_back(index);
    }
    indices.emplace(value, index);
  }

  [[nodiscard]] uint32_t index(const std::vector<T>* value) const {
    if (!value || value->empty()) return 0;
    return indices.at(value);
  }

 private:
  std::unordered_map<size_t, std::vector<uint32_t>> content_buckets;
};

struct RasterPixelTables {
  RasterValueTable<uint32_t> rgba8;
  RasterValueTable<uint8_t> rgba16;
};

RasterPixelTables collect_raster_pixel_tables(const DocumentSnapshot& snapshot);
RasterSourceTable collect_raster_sources(const DocumentSnapshot& snapshot);

void write_raster_source_asset(Writer& writer,
                               const RasterSourceTable::Asset& asset);

void write_raster_asset_reference(Writer& writer, const ImageObject& image,
                                  bool use_shared_pixel_table,
                                  uint32_t rgba16_index);

void read_raster_asset_reference(
    Reader& reader, ImageObject& image, bool use_shared_pixel_table,
    const std::vector<std::shared_ptr<const std::vector<uint8_t>>>& rgba16_assets);

} // namespace pikdraw::document::storage_detail
