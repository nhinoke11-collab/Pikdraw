#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "v2_proposal_validator.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pikdraw::workers {

struct V2ProposalStreamValidator::SemanticState {
  enum class Phase { NeedPage, Pages, Layers, Objects, Issues, Report, Complete };
  enum class ObjectKind { Path, Text, Group, Raster };
  struct ObjectInfo {
    ObjectKind kind{ObjectKind::Path};
    std::vector<uint32_t> children;
    uint32_t clip_mask_id{0};
  };

  Phase phase{Phase::NeedPage};
  uint32_t next_layer_id{1};
  uint32_t next_object_id{1};
  uint32_t issue_count{0};
  uint64_t records_seen{0};
  uint16_t failed_record_type{0};
  std::string_view failed_record_detail{};
  std::string expected_coverage_profile;
  std::unordered_set<std::string> source_ids;
  std::vector<pikdraw::interop::ImportPageRecord> pages;
  std::vector<std::vector<uint32_t>> layer_roots;
  std::vector<uint32_t> layer_page_ids;
  std::unordered_map<uint32_t, ObjectInfo> objects;
  struct RasterChunkState {
    uint32_t total_bytes{0};
    uint32_t next_byte_offset{0};
    uint32_t width_px{0};
    uint32_t height_px{0};
  };
  std::unordered_map<uint32_t, RasterChunkState> raster_chunks;
  std::unordered_map<uint32_t, uint64_t> raster_source_next_offsets;
  std::unordered_map<uint32_t, uint64_t> raster_source_total_bytes;
  bool has_raster{false};

  [[nodiscard]] bool AddObject(uint32_t id, std::string_view source_id,
                               ObjectInfo object);
  [[nodiscard]] bool Consume(pikdraw::interop::ImportRecordType type,
                             const pikdraw::interop::ImportRecordValue &value);
  [[nodiscard]] bool ConsumeChunk(std::span<const uint8_t> bytes,
                                  uint32_t expected_count);
  [[nodiscard]] bool ValidateGraph() const;
  [[nodiscard]] bool IsComplete() const noexcept;
};

} // namespace pikdraw::workers
