#include "pikdraw/interop/adapters/pdf_worker_adapter.hpp"

#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace pikdraw::interop {
namespace {

struct PageRecords {
    ImportPageRecord page;
    std::vector<ImportLayerRecord> layers;
    std::vector<ImportRecordValue> objects;
    std::vector<ImportFidelityIssueRecord> issues;
};

bool AddSerialized(std::vector<CanonicalImportRecord>& output,
                   const ImportRecordValue& value,
                   std::string& diagnostic) {
    if (output.size() >= IPC_MAX_OBJECTS) {
        diagnostic = "PDF page-set record count exceeds the worker limit";
        return false;
    }
    auto encoded = SerializeImportRecord(value);
    if (!encoded) {
        diagnostic = "PDF page-set record exceeded the canonical import limits";
        return false;
    }
    output.push_back(std::move(*encoded));
    return true;
}

} // namespace

bool BuildPdfPageSetImportRecords(
    std::span<const uint8_t> source,
    bool is_ai_representation,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    std::optional<uint32_t> source_page_index) {
    records.clear();
    diagnostic.clear();
    try {
    PdfSourceManifest manifest;
    if (!InspectPdfSource(source, manifest, diagnostic)) return false;
    if (source_page_index && *source_page_index >= manifest.pages.size()) {
        diagnostic = "Selected PDF page index is outside the inspected page tree";
        return false;
    }

    std::vector<PageRecords> selected_pages;
    const size_t first = source_page_index ? *source_page_index : 0;
    const size_t last = source_page_index ? first + 1 : manifest.pages.size();
    selected_pages.reserve(last - first);
    uint32_t next_object_id = 1;
    uint32_t next_layer_id = 1;
    uint32_t total_issues = 0;
    const std::string single_profile = is_ai_representation
        ? "ai-pdf-compatible-vector-v2" : "pdf-page-vector-v2";
    const std::string page_set_profile = is_ai_representation
        ? "ai-pdf-compatible-page-set-v3" : "pdf-page-set-v3";

    for (size_t source_index = first; source_index < last; ++source_index) {
        const auto& descriptor = manifest.pages[source_index];
        if (descriptor.source_index != source_index || descriptor.page_object_count < 0) {
            diagnostic = "PDF page manifest contains an invalid source index or object count";
            records.clear();
            return false;
        }
        PageRecords page;
        page.page.width_mm = descriptor.width_mm;
        page.page.height_mm = descriptor.height_mm;
        if ((descriptor.rotation_quarters_clockwise & 1u) != 0)
            std::swap(page.page.width_mm, page.page.height_mm);
        page.page.local_id = static_cast<uint32_t>(selected_pages.size() + 1);
        page.page.source_page_index = descriptor.source_index;
        page.page.name = "PDF Page " + std::to_string(descriptor.source_index + 1);
        page.page.rotation_degrees = static_cast<uint16_t>(
            (descriptor.rotation_quarters_clockwise % 4u) * 90u);
        page.page.blank = descriptor.page_object_count == 0;
        page.page.has_manifest_metadata = true;

        std::vector<CanonicalImportRecord> single_records;
        if (descriptor.page_object_count != 0 &&
            !BuildPdfImportRecords(source, is_ai_representation, single_records,
                                   diagnostic, descriptor.source_index)) {
            diagnostic = "PDF page " + std::to_string(descriptor.source_index + 1) +
                " could not be imported: " + diagnostic;
            records.clear();
            return false;
        }

        std::unordered_map<uint32_t, uint32_t> object_ids;
        for (const auto& encoded : single_records) {
            auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
            if (!decoded) {
                diagnostic = "PDF page adapter returned an invalid canonical record";
                records.clear();
                return false;
            }
            uint32_t local_id = 0;
            if (const auto* group = std::get_if<ImportGroupRecord>(&*decoded)) local_id = group->local_id;
            else if (const auto* path = std::get_if<ImportPathRecord>(&*decoded)) local_id = path->local_id;
            else if (const auto* text = std::get_if<ImportTextRecord>(&*decoded)) local_id = text->local_id;
            else if (const auto* image = std::get_if<ImportRasterImageRecord>(&*decoded)) local_id = image->local_id;
            if (local_id != 0) {
                if (next_object_id == 0 || next_object_id > IPC_MAX_OBJECTS ||
                    !object_ids.emplace(local_id, next_object_id++).second) {
                    diagnostic = "PDF page-set exceeds the bounded global object-ID range";
                    records.clear();
                    return false;
                }
            }
        }
        const auto remap_id = [&](uint32_t local_id, uint32_t& mapped) {
            const auto found = object_ids.find(local_id);
            if (found == object_ids.end()) return false;
            mapped = found->second;
            return true;
        };
        const std::string source_prefix = "pdf-page-" +
            std::to_string(descriptor.source_index + 1) + "/";
        bool saw_report = descriptor.page_object_count == 0;
        for (const auto& encoded : single_records) {
            auto decoded = DeserializeImportRecord(encoded.type, encoded.payload);
            if (!decoded) {
                diagnostic = "PDF page adapter returned an invalid canonical record";
                records.clear();
                return false;
            }
            bool valid = true;
            std::visit([&](auto& record) {
                using T = std::decay_t<decltype(record)>;
                if constexpr (std::is_same_v<T, ImportPageRecord>) {
                    return;
                } else if constexpr (std::is_same_v<T, ImportLayerRecord>) {
                    record.local_id = next_layer_id++;
                    record.page_local_id = page.page.local_id;
                    record.has_page_association = true;
                    record.source_id = source_prefix + record.source_id;
                    for (auto& id : record.object_local_ids) {
                        uint32_t mapped = 0;
                        if (!remap_id(id, mapped)) { valid = false; return; }
                        id = mapped;
                    }
                    page.layers.push_back(std::move(record));
                } else if constexpr (std::is_same_v<T, ImportGroupRecord>) {
                    uint32_t mapped = 0;
                    if (!remap_id(record.local_id, mapped)) { valid = false; return; }
                    record.local_id = mapped;
                    record.source_id = source_prefix + record.source_id;
                    for (auto& id : record.child_local_ids) {
                        if (!remap_id(id, mapped)) { valid = false; return; }
                        id = mapped;
                    }
                    if (record.clip_mask_local_id != 0) {
                        if (!remap_id(record.clip_mask_local_id, mapped)) { valid = false; return; }
                        record.clip_mask_local_id = mapped;
                    }
                    page.objects.emplace_back(std::move(record));
                } else if constexpr (std::is_same_v<T, ImportPathRecord> ||
                                     std::is_same_v<T, ImportTextRecord>) {
                    uint32_t mapped = 0;
                    if (!remap_id(record.local_id, mapped)) { valid = false; return; }
                    record.local_id = mapped;
                    record.source_id = source_prefix + record.source_id;
                    page.objects.emplace_back(std::move(record));
                } else if constexpr (std::is_same_v<T, ImportRasterImageRecord>) {
                    uint32_t mapped = 0;
                    if (!remap_id(record.local_id, mapped)) { valid = false; return; }
                    record.local_id = mapped;
                    record.source_id = source_prefix + record.source_id;
                    if (record.pixel_data_local_id != 0 &&
                        !remap_id(record.pixel_data_local_id, record.pixel_data_local_id)) {
                        valid = false; return;
                    }
                    if (record.source_data_local_id != 0 &&
                        !remap_id(record.source_data_local_id, record.source_data_local_id)) {
                        valid = false; return;
                    }
                    page.objects.emplace_back(std::move(record));
                } else if constexpr (std::is_same_v<T, ImportRasterImageChunkRecord> ||
                                     std::is_same_v<T, ImportRasterSourceChunkRecord>) {
                    if (!remap_id(record.local_id, record.local_id)) { valid = false; return; }
                    page.objects.emplace_back(std::move(record));
                } else if constexpr (std::is_same_v<T, ImportFidelityIssueRecord>) {
                    if (!source_page_index &&
                        (record.feature_code == "pdf-pages-after-first-not-imported" ||
                         record.feature_code == "pdf-other-pages-not-selected"))
                        return;
                    if (!record.source_locator.empty())
                        record.source_locator = source_prefix + record.source_locator;
                    page.issues.push_back(std::move(record));
                } else if constexpr (std::is_same_v<T, ImportFidelityReportRecord>) {
                    if (record.coverage_profile != single_profile) { valid = false; return; }
                    saw_report = true;
                }
            }, *decoded);
            if (!valid) {
                diagnostic = "PDF page-set record references an unknown local object ID";
                records.clear();
                return false;
            }
        }
        if (!saw_report || (page.page.blank && !page.layers.empty()) ||
            (!page.page.blank && page.layers.empty())) {
            diagnostic = "PDF page did not yield a complete page, layer and fidelity manifest";
            records.clear();
            return false;
        }
        if (page.page.blank) {
            ImportLayerRecord empty_layer;
            empty_layer.local_id = next_layer_id++;
            empty_layer.page_local_id = page.page.local_id;
            empty_layer.has_page_association = true;
            empty_layer.source_id = source_prefix + "empty-layer";
            empty_layer.name = "Layer 1";
            page.layers.push_back(std::move(empty_layer));
        }
        if (source_page_index && manifest.pages.size() > 1 &&
            descriptor.page_object_count == 0) {
            ImportFidelityIssueRecord issue;
            issue.severity = ImportFidelitySeverity::Warning;
            issue.source_locator = source_prefix + "empty-layer";
            issue.feature_code = *source_page_index == 0
                ? "pdf-pages-after-first-not-imported" : "pdf-other-pages-not-selected";
            issue.diagnostic_code = "only_selected_source_page_imported";
            page.issues.push_back(std::move(issue));
        }
        if (page.issues.size() > UINT32_MAX - total_issues) {
            diagnostic = "PDF page-set fidelity issue count overflowed";
            records.clear();
            return false;
        }
        total_issues += static_cast<uint32_t>(page.issues.size());
        selected_pages.push_back(std::move(page));
    }

    for (const auto& page : selected_pages)
        if (!AddSerialized(records, ImportRecordValue{page.page}, diagnostic)) return false;
    for (const auto& page : selected_pages)
        for (const auto& layer : page.layers)
            if (!AddSerialized(records, ImportRecordValue{layer}, diagnostic)) return false;
    for (const auto& page : selected_pages)
        for (const auto& object : page.objects)
            if (!AddSerialized(records, object, diagnostic)) return false;
    for (const auto& page : selected_pages)
        for (const auto& issue : page.issues)
            if (!AddSerialized(records, ImportRecordValue{issue}, diagnostic)) return false;

    ImportFidelityReportRecord report;
    report.coverage_profile = page_set_profile;
    report.issue_count = total_issues;
    report.parse_outcome = total_issues == 0
        ? ImportParseOutcome::Complete : ImportParseOutcome::CompleteWithDiagnostics;
    report.appearance = total_issues == 0
        ? ImportAppearance::Unknown : ImportAppearance::Partial;
    if (!AddSerialized(records, ImportRecordValue{report}, diagnostic) ||
        records.size() > IPC_MAX_OBJECTS) {
        if (diagnostic.empty()) diagnostic = "PDF page-set record count exceeds the worker limit";
        records.clear();
        return false;
    }
    return true;
    } catch (...) {
        records.clear();
        diagnostic = "PDF page-set allocation failed while retaining the source hierarchy";
        return false;
    }
}

} // namespace pikdraw::interop
