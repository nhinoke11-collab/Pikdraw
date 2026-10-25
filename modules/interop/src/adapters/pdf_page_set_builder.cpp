#include "pikdraw/interop/adapters/pdf_worker_adapter.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace pikdraw::interop {
namespace {

constexpr uint32_t kPdfSourcePreviewBaseDpi = 144;
constexpr uint64_t kPdfSourcePreviewPagePixels = 4'000'000;
constexpr uint64_t kPdfSourcePreviewTotalPixels = 16'000'000;

uint32_t ChoosePdfSourcePreviewDpi(const PdfSourceManifest& manifest,
                                   size_t first, size_t last) {
    double total_pixels_at_base = 0.0;
    double largest_page_pixels_at_base = 0.0;
    for (size_t index = first; index < last; ++index) {
        const auto& page = manifest.pages[index];
        const double width_pt = page.right_pt - page.left_pt;
        const double height_pt = page.top_pt - page.bottom_pt;
        const double pixels = std::ceil(width_pt * kPdfSourcePreviewBaseDpi / 72.0) *
                              std::ceil(height_pt * kPdfSourcePreviewBaseDpi / 72.0);
        total_pixels_at_base += pixels;
        largest_page_pixels_at_base = (std::max)(largest_page_pixels_at_base, pixels);
    }
    if (total_pixels_at_base <= 0.0 || largest_page_pixels_at_base <= 0.0)
        return 1;
    const double total_scale = std::sqrt(
        static_cast<double>(kPdfSourcePreviewTotalPixels) / total_pixels_at_base);
    const double page_scale = std::sqrt(
        static_cast<double>(kPdfSourcePreviewPagePixels) / largest_page_pixels_at_base);
    const double scale = (std::min)({1.0, total_scale, page_scale}) * 0.97;
    const auto dpi = static_cast<uint32_t>(
        std::floor(kPdfSourcePreviewBaseDpi * scale));
    return (std::max)(1u, (std::min)(dpi, kPdfSourcePreviewBaseDpi));
}

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

bool AddRasterChunk(std::vector<CanonicalImportRecord>& output,
                    uint32_t local_id, uint32_t byte_offset,
                    uint32_t total_bytes, std::span<const uint8_t> bytes,
                    std::string& diagnostic) {
    if (output.size() >= IPC_MAX_OBJECTS) {
        diagnostic = "PDF page preview chunks exceed the worker record limit";
        return false;
    }
    auto encoded = SerializeRasterImageChunk(local_id, byte_offset, total_bytes, bytes);
    if (!encoded) {
        diagnostic = "PDF page preview chunk exceeded the canonical import limits";
        return false;
    }
    output.push_back(std::move(*encoded));
    return true;
}

} // namespace

bool BuildPdfSourceManifestImportRecords(
    std::span<const uint8_t> source,
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
        const size_t first = source_page_index ? *source_page_index : 0;
        const size_t last = source_page_index ? first + 1 : manifest.pages.size();
        if (last - first > (IPC_MAX_OBJECTS - 1) / 4) {
            diagnostic = "PDF page manifest exceeds the worker record limit";
            return false;
        }
        uint32_t local_page_id = 1;
        uint32_t local_layer_id = 1;
        for (size_t index = first; index < last; ++index) {
            const auto& descriptor = manifest.pages[index];
            if (descriptor.source_index != index || descriptor.page_object_count < 0 ||
                descriptor.rotation_quarters_clockwise > 3) {
                diagnostic = "PDF page manifest contains invalid source metadata";
                records.clear();
                return false;
            }
            ImportPageRecord page;
            page.width_mm = descriptor.width_mm;
            page.height_mm = descriptor.height_mm;
            if ((descriptor.rotation_quarters_clockwise & 1u) != 0)
                std::swap(page.width_mm, page.height_mm);
            page.local_id = local_page_id++;
            page.source_page_index = descriptor.source_index;
            page.name = "PDF Page " + std::to_string(descriptor.source_index + 1);
            page.rotation_degrees = static_cast<uint16_t>(
                descriptor.rotation_quarters_clockwise * 90u);
            page.blank = descriptor.page_object_count == 0;
            page.has_manifest_metadata = true;
            if (!AddSerialized(records, ImportRecordValue{page}, diagnostic)) {
                records.clear();
                return false;
            }
        }

        std::vector<uint32_t> source_page_indices;
        source_page_indices.reserve(last - first);
        for (size_t index = first; index < last; ++index)
            source_page_indices.push_back(static_cast<uint32_t>(index));

        local_layer_id = 1;
        local_page_id = 1;
        for (size_t index = first; index < last; ++index) {
            ImportLayerRecord layer;
            layer.local_id = local_layer_id++;
            layer.source_id = "pdf-source-page-" + std::to_string(index + 1);
            layer.name = "PDF Source";
            layer.visible = true;
            layer.locked = true;
            // The worker protocol needs preview raster records to have one
            // owning layer. The desktop extracts these pixels into the locked
            // page cache and removes the temporary import objects at commit.
            layer.object_local_ids.push_back(local_page_id);
            layer.page_local_id = local_page_id++;
            layer.has_page_association = true;
            if (!AddSerialized(records, ImportRecordValue{layer}, diagnostic)) {
                records.clear();
                return false;
            }
        }

        const uint32_t preview_dpi =
            ChoosePdfSourcePreviewDpi(manifest, first, last);
        const bool previews_ok = RenderPdfSourcePagesRgba8(
            source, source_page_indices, preview_dpi,
            kPdfSourcePreviewPagePixels, kPdfSourcePreviewTotalPixels,
            [&](uint32_t source_index, PdfRenderedPageRgba8&& rendered) {
                if (source_index < first || source_index >= last) {
                    diagnostic = "PDF preview renderer returned an unrequested page";
                    return false;
                }
                const auto& descriptor = manifest.pages[source_index];
                double page_width_mm = descriptor.width_mm;
                double page_height_mm = descriptor.height_mm;
                if ((descriptor.rotation_quarters_clockwise & 1u) != 0)
                    std::swap(page_width_mm, page_height_mm);
                const uint32_t local_id =
                    static_cast<uint32_t>(source_index - first + 1);
                const uint64_t byte_count = static_cast<uint64_t>(rendered.rgba.size());
                if (byte_count == 0 || byte_count > UINT32_MAX) {
                    diagnostic = "PDF page preview exceeds the C6 raster byte range";
                    return false;
                }
                ImportRasterImageRecord image;
                image.local_id = local_id;
                image.source_id = "pdf-source-page-" +
                                  std::to_string(source_index + 1) + "/preview";
                image.name = "PDF source screen preview";
                image.media_type = "application/x-pikdraw-rgba8";
                image.width_px = rendered.width_px;
                image.height_px = rendered.height_px;
                image.decoded_pixel_bytes = static_cast<uint32_t>(byte_count);
                image.source_bits_per_pixel = 32;
                image.dpi_x = preview_dpi;
                image.dpi_y = preview_dpi;
                image.width_mm = page_width_mm;
                image.height_mm = page_height_mm;
                if (!AddSerialized(records, ImportRecordValue{image}, diagnostic))
                    return false;

                for (size_t offset = 0; offset < rendered.rgba.size();
                     offset += kMaxRasterChunkPayloadBytes) {
                    const size_t length = (std::min)(kMaxRasterChunkPayloadBytes,
                        rendered.rgba.size() - offset);
                    if (!AddRasterChunk(records, local_id,
                                        static_cast<uint32_t>(offset),
                                        image.decoded_pixel_bytes,
                                        std::span<const uint8_t>(rendered.rgba)
                                            .subspan(offset, length),
                                        diagnostic))
                        return false;
                }
                return true;
            }, diagnostic);
        if (!previews_ok) {
            records.clear();
            if (!diagnostic.starts_with("PDF preview"))
                diagnostic = "PDF screen preview failed: " + diagnostic;
            return false;
        }

        ImportFidelityReportRecord report;
        report.coverage_profile = "pdf-print-source-v1";
        if (!AddSerialized(records, ImportRecordValue{report}, diagnostic)) {
            records.clear();
            return false;
        }
        return true;
    } catch (...) {
        records.clear();
        diagnostic = "PDF page manifest allocation failed";
        return false;
    }
}

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
            uint32_t local_id = 0;
            switch (encoded.type) {
            case ImportRecordType::Group:
            case ImportRecordType::Path:
            case ImportRecordType::Text:
            case ImportRecordType::RasterImage:
                if (encoded.payload.size() < sizeof(uint32_t)) {
                    diagnostic = "PDF page adapter returned a truncated object record";
                    records.clear();
                    return false;
                }
                local_id = static_cast<uint32_t>(encoded.payload[0]) |
                    (static_cast<uint32_t>(encoded.payload[1]) << 8) |
                    (static_cast<uint32_t>(encoded.payload[2]) << 16) |
                    (static_cast<uint32_t>(encoded.payload[3]) << 24);
                break;
            default:
                break;
            }
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
