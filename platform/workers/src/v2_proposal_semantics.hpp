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
  enum class Phase { NeedPage, Layers, Objects, Issues, Report, Complete };
  enum class ObjectKind { Path, Text, Group };
  struct ObjectInfo {
    ObjectKind kind{ObjectKind::Path};
    std::vector<uint32_t> children;
  };

  Phase phase{Phase::NeedPage};
  uint32_t next_layer_id{1};
  uint32_t next_object_id{1};
  uint32_t issue_count{0};
  uint64_t records_seen{0};
  std::string expected_coverage_profile;
  std::unordered_set<std::string> source_ids;
  std::vector<std::vector<uint32_t>> layer_roots;
  std::unordered_map<uint32_t, ObjectInfo> objects;

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
