#include "v2_proposal_semantics.hpp"

#include <algorithm>
#include <utility>

namespace pikdraw::workers {
using namespace pikdraw::interop;

bool V2ProposalStreamValidator::SemanticState::AddObject(
    uint32_t id, std::string_view source_id,
    V2ProposalStreamValidator::SemanticState::ObjectInfo object) {
  if (phase == Phase::Issues || phase == Phase::Report ||
      phase == Phase::Complete || phase == Phase::NeedPage ||
      phase == Phase::Pages || id != next_object_id || source_id.empty() ||
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
    const auto *page = std::get_if<ImportPageRecord>(&value);
    const bool page_set = expected_coverage_profile == "pdf-page-set-v3" ||
        expected_coverage_profile == "ai-pdf-compatible-page-set-v3";
    if (!page || (phase != Phase::NeedPage && phase != Phase::Pages) ||
        (pages.empty() ? records_seen != 0 : !page_set) ||
        page->local_id != pages.size() + 1 ||
        (page_set && (!page->has_manifest_metadata ||
                      page->source_page_index >= 1000000 ||
                      (!pages.empty() && page->source_page_index <=
                          pages.back().source_page_index))) ||
        (!page_set && (page->local_id != 1 || page->has_manifest_metadata)))
      return false;
    pages.push_back(*page);
    phase = Phase::Pages;
  } else if (type == ImportRecordType::Layer) {
    const auto *layer = std::get_if<ImportLayerRecord>(&value);
    const bool page_set = expected_coverage_profile == "pdf-page-set-v3" ||
        expected_coverage_profile == "ai-pdf-compatible-page-set-v3";
    if (!layer || (phase != Phase::Pages && phase != Phase::Layers &&
                   phase != Phase::Objects) ||
        phase == Phase::Objects || layer->local_id != next_layer_id ||
        (page_set && !layer->has_page_association) ||
        (!page_set && (layer->has_page_association || layer->page_local_id != 1)) ||
        layer->page_local_id == 0 || layer->page_local_id > pages.size() ||
        layer->source_id.empty() ||
        !source_ids.emplace(layer->source_id).second)
      return false;
    phase = Phase::Layers;
    layer_roots.push_back(layer->object_local_ids);
    layer_page_ids.push_back(layer->page_local_id);
    ++next_layer_id;
  } else if (type == ImportRecordType::Group) {
    const auto *group = std::get_if<ImportGroupRecord>(&value);
    if (!group ||
        !AddObject(group->local_id, group->source_id,
                   ObjectInfo{ObjectKind::Group, group->child_local_ids,
                              group->clip_mask_local_id}))
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
  } else if (type == ImportRecordType::RasterImage) {
    const auto *image = std::get_if<ImportRasterImageRecord>(&value);
    const bool raster_document = expected_coverage_profile == "raster-rgba8-v1";
    const bool cdr_document = expected_coverage_profile == "cdr-libcdr-svg-v2";
    const bool pdf_document = expected_coverage_profile == "pdf-page-vector-v2" ||
        expected_coverage_profile == "ai-pdf-compatible-vector-v2" ||
        expected_coverage_profile == "pdf-page-set-v3" ||
        expected_coverage_profile == "ai-pdf-compatible-page-set-v3";
    const bool eps_document = expected_coverage_profile == "eps-path-v1";
    if (!image || phase == Phase::Issues || phase == Phase::Report ||
        phase == Phase::Complete || (!raster_document && !cdr_document &&
                                    !pdf_document && !eps_document) ||
        (raster_document && has_raster) ||
        !AddObject(image->local_id, image->source_id,
                   ObjectInfo{ObjectKind::Raster, {}}))
      return false;
    const uint32_t data_local_id = image->pixel_data_local_id == 0
        ? image->local_id : image->pixel_data_local_id;
    if (data_local_id == image->local_id) {
      if (!raster_chunks.emplace(data_local_id, RasterChunkState{
              image->decoded_pixel_bytes, 0, image->width_px, image->height_px}).second)
        return false;
    } else {
      const auto data = raster_chunks.find(data_local_id);
      if (data == raster_chunks.end() ||
          data->second.total_bytes != image->decoded_pixel_bytes ||
          data->second.width_px != image->width_px ||
          data->second.height_px != image->height_px ||
          data->second.next_byte_offset != data->second.total_bytes)
        return false;
    }
    if (image->source_bytes_size == 0) {
      if (image->source_data_local_id != 0) return false;
    } else if (image->source_data_local_id == image->local_id) {
      if (!raster_source_next_offsets.emplace(image->local_id, 0).second ||
          !raster_source_total_bytes.emplace(image->local_id,
                                             image->source_bytes_size).second)
        return false;
    } else {
      const auto source_total = raster_source_total_bytes.find(image->source_data_local_id);
      const auto source_next = raster_source_next_offsets.find(image->source_data_local_id);
      if (source_total == raster_source_total_bytes.end() ||
          source_next == raster_source_next_offsets.end() ||
          source_total->second != image->source_bytes_size ||
          source_next->second != source_total->second)
        return false;
    }
    has_raster = true;
  } else if (type == ImportRecordType::RasterImageChunk) {
    const auto *chunk = std::get_if<ImportRasterImageChunkRecord>(&value);
    if (!chunk || phase != Phase::Objects)
      return false;
    const auto found = raster_chunks.find(chunk->local_id);
    if (found == raster_chunks.end() ||
        chunk->total_bytes != found->second.total_bytes ||
        chunk->byte_offset != found->second.next_byte_offset ||
        chunk->bytes.size() > found->second.total_bytes - found->second.next_byte_offset)
      return false;
    found->second.next_byte_offset += static_cast<uint32_t>(chunk->bytes.size());
  } else if (type == ImportRecordType::RasterSourceChunk) {
    const auto *chunk = std::get_if<ImportRasterSourceChunkRecord>(&value);
    if (!chunk || phase != Phase::Objects)
      return false;
    const auto total = raster_source_total_bytes.find(chunk->local_id);
    const auto next = raster_source_next_offsets.find(chunk->local_id);
    if (total == raster_source_total_bytes.end() ||
        next == raster_source_next_offsets.end() ||
        chunk->total_bytes != total->second ||
        chunk->byte_offset != next->second ||
        chunk->bytes.size() > total->second - next->second)
      return false;
    next->second += static_cast<uint32_t>(chunk->bytes.size());
  } else if (type == ImportRecordType::FidelityIssue) {
    const auto *issue = std::get_if<ImportFidelityIssueRecord>(&value);
    if (!issue || issue->severity == ImportFidelitySeverity::Unsupported ||
        (!issue->source_locator.empty() &&
         !source_ids.contains(issue->source_locator)) ||
        phase == Phase::NeedPage || phase == Phase::Pages || phase == Phase::Report ||
        phase == Phase::Complete)
      return false;
    phase = Phase::Issues;
    ++issue_count;
  } else if (type == ImportRecordType::FidelityReport) {
    const auto *report = std::get_if<ImportFidelityReportRecord>(&value);
    if (!report ||
        (phase != Phase::Layers && phase != Phase::Objects &&
         phase != Phase::Issues) ||
        pages.empty() || layer_roots.empty() ||
        report->coverage_profile != expected_coverage_profile ||
        report->issue_count != issue_count ||
        report->coverage != ImportCoverage::Partial ||
        report->parse_outcome !=
            (issue_count == 0 ? ImportParseOutcome::Complete
                              : ImportParseOutcome::CompleteWithDiagnostics) ||
        report->appearance != (issue_count == 0 ? ImportAppearance::Unknown
                                                : ImportAppearance::Partial) ||
        report->editability != (expected_coverage_profile == "raster-rgba8-v1"
            ? ImportEditability::ExplicitConversion : ImportEditability::NativeEditable) ||
        report->production != ImportProduction::NotAssessed || !ValidateGraph())
      return false;
    for (const auto &[id, chunks] : raster_chunks) {
      (void)id;
      if (chunks.next_byte_offset != chunks.total_bytes) return false;
    }
    for (const auto &[id, total] : raster_source_total_bytes) {
      const auto next = raster_source_next_offsets.find(id);
      if (next == raster_source_next_offsets.end() || next->second != total)
        return false;
    }
    if (expected_coverage_profile == "raster-rgba8-v1" &&
        (!has_raster || objects.size() != 1 || raster_chunks.size() != 1 ||
         pages.size() != 1 || layer_roots.size() != 1 || layer_roots.front().size() != 1 ||
         layer_roots.front().front() != raster_chunks.begin()->first))
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
        raw_type > static_cast<uint16_t>(ImportRecordType::RasterSourceChunk))
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
  if (pages.empty() || layer_roots.empty() || layer_roots.size() != layer_page_ids.size())
    return false;
  std::unordered_map<uint32_t, size_t> layer_count_by_page;
  for (const uint32_t page_id : layer_page_ids) {
    if (page_id == 0 || page_id > pages.size()) return false;
    ++layer_count_by_page[page_id];
  }
  for (uint32_t page_id = 1; page_id <= pages.size(); ++page_id)
    if (!layer_count_by_page.contains(page_id)) return false;
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
    if (object.clip_mask_id != 0) {
      const auto mask = objects.find(object.clip_mask_id);
      if (mask == objects.end() || mask->second.kind != ObjectKind::Path ||
          std::find(object.children.begin(), object.children.end(), object.clip_mask_id) ==
              object.children.end())
        return false;
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
  std::unordered_map<uint32_t, uint32_t> object_pages;
  object_pages.reserve(objects.size());
  for (size_t layer_index = 0; layer_index < layer_roots.size(); ++layer_index) {
    const uint32_t page_id = layer_page_ids[layer_index];
    std::vector<uint32_t> pending(layer_roots[layer_index].begin(),
                                  layer_roots[layer_index].end());
    while (!pending.empty()) {
      const uint32_t id = pending.back();
      pending.pop_back();
      const auto [page, inserted] = object_pages.emplace(id, page_id);
      if (!inserted && page->second != page_id) return false;
      const auto object = objects.find(id);
      if (object == objects.end()) return false;
      pending.insert(pending.end(), object->second.children.begin(),
                     object->second.children.end());
    }
  }
  if (object_pages.size() != objects.size()) return false;
  return true;
}

bool V2ProposalStreamValidator::SemanticState::IsComplete() const noexcept {
  return phase == Phase::Complete;
}

} // namespace pikdraw::workers
