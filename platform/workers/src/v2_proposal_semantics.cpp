#include "v2_proposal_semantics.hpp"

#include <utility>

namespace pikdraw::workers {
using namespace pikdraw::interop;

bool V2ProposalStreamValidator::SemanticState::AddObject(
    uint32_t id, std::string_view source_id,
    V2ProposalStreamValidator::SemanticState::ObjectInfo object) {
  if (phase == Phase::Issues || phase == Phase::Report ||
      phase == Phase::Complete || id != next_object_id || source_id.empty() ||
      !source_ids.emplace(source_id).second)
    return false;
  if (phase == Phase::Layers)
    phase = Phase::Objects;
  objects.emplace(id, std::move(object));
  ++next_object_id;
  return true;
}

bool V2ProposalStreamValidator::SemanticState::Consume(
    ImportRecordType type, const ImportRecordValue &value) {
  if (phase == Phase::Complete)
    return false;
  if (type == ImportRecordType::Page) {
    if (phase != Phase::NeedPage || records_seen != 0 ||
        !std::holds_alternative<ImportPageRecord>(value))
      return false;
    phase = Phase::Layers;
  } else if (type == ImportRecordType::Layer) {
    const auto *layer = std::get_if<ImportLayerRecord>(&value);
    if (!layer || (phase != Phase::Layers && phase != Phase::Objects) ||
        phase == Phase::Objects || layer->local_id != next_layer_id ||
        layer->source_id.empty() ||
        !source_ids.emplace(layer->source_id).second)
      return false;
    layer_roots.push_back(layer->object_local_ids);
    ++next_layer_id;
  } else if (type == ImportRecordType::Group) {
    const auto *group = std::get_if<ImportGroupRecord>(&value);
    if (!group ||
        !AddObject(group->local_id, group->source_id,
                   ObjectInfo{ObjectKind::Group, group->child_local_ids}))
      return false;
  } else if (type == ImportRecordType::Path) {
    const auto *path = std::get_if<ImportPathRecord>(&value);
    if (!path || !AddObject(path->local_id, path->source_id,
                            ObjectInfo{ObjectKind::Path, {}}))
      return false;
  } else if (type == ImportRecordType::Text) {
    const auto *text = std::get_if<ImportTextRecord>(&value);
    if (!text || !AddObject(text->local_id, text->source_id,
                            ObjectInfo{ObjectKind::Text, {}}))
      return false;
  } else if (type == ImportRecordType::FidelityIssue) {
    const auto *issue = std::get_if<ImportFidelityIssueRecord>(&value);
    if (!issue || issue->severity == ImportFidelitySeverity::Unsupported ||
        (!issue->source_locator.empty() &&
         !source_ids.contains(issue->source_locator)) ||
        phase == Phase::NeedPage || phase == Phase::Report ||
        phase == Phase::Complete)
      return false;
    phase = Phase::Issues;
    ++issue_count;
  } else if (type == ImportRecordType::FidelityReport) {
    const auto *report = std::get_if<ImportFidelityReportRecord>(&value);
    if (!report ||
        (phase != Phase::Layers && phase != Phase::Objects &&
         phase != Phase::Issues) ||
        layer_roots.empty() ||
        report->coverage_profile != expected_coverage_profile ||
        report->issue_count != issue_count ||
        report->coverage != ImportCoverage::Partial ||
        report->parse_outcome !=
            (issue_count == 0 ? ImportParseOutcome::Complete
                              : ImportParseOutcome::CompleteWithDiagnostics) ||
        report->appearance != (issue_count == 0 ? ImportAppearance::Unknown
                                                : ImportAppearance::Partial) ||
        report->editability != ImportEditability::NativeEditable ||
        report->production != ImportProduction::NotAssessed || !ValidateGraph())
      return false;
    phase = Phase::Complete;
  } else {
    return false;
  }
  ++records_seen;
  return true;
}

bool V2ProposalStreamValidator::SemanticState::ConsumeChunk(
    std::span<const uint8_t> bytes, uint32_t expected_count) {
  size_t offset = 0;
  uint32_t count = 0;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < 8)
      return false;
    const uint16_t raw_type =
        static_cast<uint16_t>(bytes[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
    const uint16_t flags =
        static_cast<uint16_t>(bytes[offset + 2]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 3]) << 8);
    const uint32_t payload_size =
        static_cast<uint32_t>(bytes[offset + 4]) |
        (static_cast<uint32_t>(bytes[offset + 5]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 6]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 7]) << 24);
    offset += 8;
    if (flags != 0 || payload_size > bytes.size() - offset || raw_type < 1 ||
        raw_type > 7)
      return false;
    const auto type = static_cast<ImportRecordType>(raw_type);
    const auto value =
        DeserializeImportRecord(type, bytes.subspan(offset, payload_size));
    if (!value || !Consume(type, *value))
      return false;
    offset += payload_size;
    ++count;
  }
  return count == expected_count;
}

bool V2ProposalStreamValidator::SemanticState::ValidateGraph() const {
  if (layer_roots.empty())
    return false;
  std::unordered_map<uint32_t, uint32_t> owners;
  owners.reserve(objects.size());
  for (const auto &[id, object] : objects) {
    if (object.kind != ObjectKind::Group) {
      if (!object.children.empty())
        return false;
      continue;
    }
    uint32_t previous_child = id;
    for (const uint32_t child_id : object.children) {
      const auto found = objects.find(child_id);
      if (found == objects.end() || child_id <= previous_child ||
          ++owners[child_id] != 1)
        return false;
      previous_child = child_id;
    }
  }
  for (const auto &roots : layer_roots) {
    uint32_t previous_root = 0;
    for (const uint32_t root_id : roots) {
      if (!objects.contains(root_id) || root_id <= previous_root ||
          ++owners[root_id] != 1)
        return false;
      previous_root = root_id;
    }
  }
  for (const auto &[id, object] : objects) {
    (void)object;
    const auto owner = owners.find(id);
    if (owner == owners.end() || owner->second != 1)
      return false;
  }
  return true;
}

bool V2ProposalStreamValidator::SemanticState::IsComplete() const noexcept {
  return phase == Phase::Complete;
}

} // namespace pikdraw::workers
