#include "pikdraw/desktop/workflows/document_workflow.hpp"

#include "pikdraw/document/commands/import_content_command.hpp"
#include "pikdraw/interop/adapters/dxf_importer.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "pikdraw/workers/process_launcher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <type_traits>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
namespace {

constexpr uint64_t kMaxImportResponseBytes = 20ull * 1024 * 1024;
constexpr uint64_t kMaxImportCanonicalBytes = 16ull * 1024 * 1024;
constexpr uint64_t kMaxImportRecords = 40002;
constexpr uint32_t kMaxImportChunks = 64;
constexpr uint32_t kMaxImportFrames = 65;

struct SourceSignature {
    ImportFormat format{ImportFormat::Svg};
    bool recognized{false};
};

bool ReadPrefix(HANDLE source, std::array<uint8_t, 4096>& prefix, size_t& count) {
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN)) return false;
    DWORD read = 0;
    if (!ReadFile(source, prefix.data(), static_cast<DWORD>(prefix.size()), &read, nullptr)) return false;
    count = read;
    return SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN) != FALSE;
}

SourceSignature DetectSignature(HANDLE source) {
    std::array<uint8_t, 4096> bytes{};
    size_t count = 0;
    if (!ReadPrefix(source, bytes, count)) return {};
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), count);
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    const auto svg = text.find("<svg");
    if (svg != std::string_view::npos && svg < 2048) return {ImportFormat::Svg, true};
    const auto section = text.find("SECTION");
    const auto entities = text.find("ENTITIES");
    if (section != std::string_view::npos && entities != std::string_view::npos &&
        section < 2048 && entities < 4096 && text.find('\0') == std::string_view::npos)
        return {ImportFormat::Dxf, true};
    return {};
}

std::wstring WorkerExecutablePath() {
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) return {};
    module.resize(length);
    return (std::filesystem::path(module).parent_path() / L"workers" / L"spike_worker.exe").wstring();
}

bool AppendValidatedRecords(const pikdraw::interop::IpcV2Frame& frame,
                            std::vector<uint8_t>& canonical,
                            size_t& record_count) {
    using namespace pikdraw::interop;
    const auto packet = SerializeV2Frame(
        static_cast<MessageType>(frame.header.message_type), frame.header.sequence_id, frame.payload);
    if (packet.empty()) return false;
    const auto chunk = DeserializeV2ProposalChunk(packet);
    if (!chunk || chunk->canonical_record_bytes.size() > kMaxImportCanonicalBytes - canonical.size())
        return false;
    canonical.insert(canonical.end(), chunk->canonical_record_bytes.begin(), chunk->canonical_record_bytes.end());
    record_count += chunk->record_count;
    return record_count <= kMaxImportRecords;
}

std::optional<color::ColorValue> DecodeColor(const pikdraw::interop::ImportColorRecord& source) {
    using namespace pikdraw::interop;
    color::ColorValue result;
    switch (source.model) {
    case ImportColorModel::Rgb:
        result = color::RGBColor{source.components[0], source.components[1],
                                 source.components[2], source.components[3]};
        break;
    case ImportColorModel::Cmyk:
        result = color::CMYKColor{source.components[0], source.components[1],
                                  source.components[2], source.components[3], source.components[4]};
        break;
    case ImportColorModel::Spot:
        result = color::SpotColor{source.spot_name, source.components[0],
            color::RGBColor{source.alternate_rgb[0], source.alternate_rgb[1],
                            source.alternate_rgb[2], source.alternate_rgb[3]},
            (source.overprint_flags & 1) != 0};
        break;
    case ImportColorModel::Gray:
        result = color::GrayscaleColor{source.components[0], source.components[1]};
        break;
    default:
        return std::nullopt;
    }
    result.set_overprint({(source.overprint_flags & 1) != 0,
                          (source.overprint_flags & 2) != 0});
    return result.is_valid() ? std::optional<color::ColorValue>{std::move(result)} : std::nullopt;
}

bool DecodeCanonicalProposal(std::span<const uint8_t> bytes,
                             ImportedContentProposal& output) {
    using namespace pikdraw::interop;
    std::vector<ImportRecordValue> records;
    size_t offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8) return false;
        const uint16_t raw_type = static_cast<uint16_t>(bytes[offset]) |
            static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
        const uint16_t flags = static_cast<uint16_t>(bytes[offset + 2]) |
            static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 3]) << 8);
        const uint32_t payload_size = static_cast<uint32_t>(bytes[offset + 4]) |
            (static_cast<uint32_t>(bytes[offset + 5]) << 8) |
            (static_cast<uint32_t>(bytes[offset + 6]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 7]) << 24);
        offset += 8;
        if (raw_type < 1 || raw_type > 7 || flags != 0 || payload_size > bytes.size() - offset)
            return false;
        const auto value = DeserializeImportRecord(static_cast<ImportRecordType>(raw_type),
                                                    bytes.subspan(offset, payload_size));
        if (!value) return false;
        records.push_back(*value);
        offset += payload_size;
    }

    std::unordered_map<uint32_t, document::ObjectId> ids;
    for (const auto& value : records) {
        const auto local_id = std::visit([](const auto& record) -> uint32_t {
            using T = std::decay_t<decltype(record)>;
            if constexpr (std::is_same_v<T, ImportPathRecord> ||
                          std::is_same_v<T, ImportTextRecord> ||
                          std::is_same_v<T, ImportGroupRecord>) return record.local_id;
            return 0;
        }, value);
        if (!local_id) continue;
        const document::ObjectId id = document::generate_object_id();
        if (id == document::kInvalidObjectId || !ids.emplace(local_id, id).second) return false;
    }
    if (ids.empty()) return false;

    for (const auto& value : records) {
        if (const auto* page = std::get_if<ImportPageRecord>(&value)) {
            output.source_page_width_mm = page->width_mm;
            output.source_page_height_mm = page->height_mm;
        } else if (const auto* layer = std::get_if<ImportLayerRecord>(&value)) {
            document::Layer converted{layer->name, layer->visible, layer->locked, {}};
            converted.object_ids.reserve(layer->object_local_ids.size());
            for (const uint32_t id : layer->object_local_ids) {
                const auto found = ids.find(id);
                if (found == ids.end()) return false;
                converted.object_ids.push_back(found->second);
            }
            output.layers.push_back(std::move(converted));
        } else if (const auto* group = std::get_if<ImportGroupRecord>(&value)) {
            document::GroupObject converted;
            converted.id = ids.at(group->local_id);
            converted.name = group->name;
            converted.children_ids.reserve(group->child_local_ids.size());
            for (const uint32_t id : group->child_local_ids) converted.children_ids.push_back(ids.at(id));
            output.objects.emplace_back(std::move(converted));
        } else if (const auto* path = std::get_if<ImportPathRecord>(&value)) {
            auto fill = DecodeColor(path->fill);
            auto stroke = DecodeColor(path->stroke);
            if (!fill || !stroke || path->winding_rule > 1) return false;
            document::PathObject converted;
            converted.id = ids.at(path->local_id);
            converted.name = path->name;
            converted.has_fill = path->has_fill;
            converted.has_stroke = path->has_stroke;
            converted.fill = std::move(*fill);
            converted.stroke = std::move(*stroke);
            converted.stroke_width_mm = path->stroke_width_mm;
            converted.path.set_winding_rule(static_cast<geometry::WindingRule>(path->winding_rule));
            for (const auto& element : path->elements) {
                const auto& v = element.values;
                switch (element.verb) {
                case ImportPathVerb::Move: converted.path.move_to(v[0], v[1]); break;
                case ImportPathVerb::Line: converted.path.line_to(v[0], v[1]); break;
                case ImportPathVerb::Quad: converted.path.quad_to(v[2], v[3], v[0], v[1]); break;
                case ImportPathVerb::Cubic: converted.path.cubic_to(v[2], v[3], v[4], v[5], v[0], v[1]); break;
                case ImportPathVerb::Arc: converted.path.arc_to(v[6], v[7], v[8],
                    (element.flags & 1) != 0, (element.flags & 2) != 0, v[0], v[1]); break;
                case ImportPathVerb::Close: converted.path.close(); break;
                default: return false;
                }
            }
            if (converted.path.empty()) return false;
            output.objects.emplace_back(std::move(converted));
        } else if (const auto* text = std::get_if<ImportTextRecord>(&value)) {
            auto fill = DecodeColor(text->fill);
            if (!fill) return false;
            document::TextObject converted;
            converted.id = ids.at(text->local_id);
            converted.name = text->name;
            converted.text = text->utf8_text;
            converted.font.family_name = text->font_family;
            converted.font.postscript_name.clear();
            converted.font.weight = text->font_weight;
            converted.font.italic = text->italic;
            converted.font.size_pt = text->font_size_pt;
            converted.position = {text->x_mm, text->y_mm};
            converted.fill = std::move(*fill);
            output.objects.emplace_back(std::move(converted));
        } else if (const auto* report = std::get_if<ImportFidelityReportRecord>(&value)) {
            output.profile_id = report->coverage_profile;
            output.coverage_status = report->coverage == ImportCoverage::Partial ? "partial" : "unknown";
            output.appearance_status = report->appearance == ImportAppearance::Partial ? "partial" : "unknown";
            output.editability_status = report->editability == ImportEditability::NativeEditable
                ? "native_editable" : "explicit_conversion";
            output.production_status = report->production == ImportProduction::Blocked
                ? "blocked" : "not_assessed";
        } else if (const auto* issue = std::get_if<ImportFidelityIssueRecord>(&value)) {
            output.issues.push_back({issue->feature_code, issue->diagnostic_code});
        }
    }
    return !output.layers.empty() && output.objects.size() == ids.size() &&
           output.source_page_width_mm > 0 && output.source_page_height_mm > 0 &&
           !output.profile_id.empty();
}

} // namespace

document::AsyncProposalMetadata DocumentWorkflow::begin_import_job(uint64_t job_id) {
    return root_.document().begin_job(job_id);
}

void DocumentWorkflow::cancel_import_job(uint64_t job_id) {
    root_.document().cancel_job(job_id);
}

document::ProposalStatus DocumentWorkflow::commit_import_proposal(
    document::AsyncProposalMetadata metadata, ImportedContentProposal proposal) {
    document::AsyncProposal staged;
    staged.metadata = std::move(metadata);
    staged.description = "Import " + proposal.format_name;
    staged.commands.push_back(std::make_unique<document::ImportContentCommand>(
        std::move(proposal.layers), std::move(proposal.objects)));
    const auto status = root_.document().validate_and_commit_proposal(std::move(staged));
    if (status == document::ProposalStatus::Accepted) sync_status_bar();
    return status;
}

ImportWorkerResult run_import_worker_job(
    const std::wstring& source_path, ImportFormat format,
    const document::AsyncProposalMetadata& job, std::stop_token cancellation,
    const std::function<void(ImportProgress)>& on_progress) {
    ImportWorkerResult result;
    const auto progress = [&](ImportProgress value) { if (on_progress) on_progress(value); };
#ifndef _WIN32
    (void)source_path; (void)format; (void)job; (void)cancellation; (void)progress;
    result.error_message = "The isolated vector import worker is available only in the Windows desktop build.";
    return result;
#else
    using namespace pikdraw::interop;
    using namespace pikdraw::workers;
    try {
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }
        progress({ImportProgressStage::Snapshotting, 0});
        SourceSnapshotFailure snapshot_failure{};
        auto source = SourceSnapshot::Create(source_path,
            format == ImportFormat::Svg ? SvgImporter::kMaxSourceBytes : DxfImporter::kMaxSourceBytes,
            &snapshot_failure);
        if (!source || !source->IsValid()) {
            result.error_message = "The selected file could not be staged safely or exceeds the import size limit.";
            return result;
        }
        const auto signature = DetectSignature(source->ReadHandle());
        if (!signature.recognized || signature.format != format) {
            result.error_message = "The file extension and source signature do not match a supported SVG or DXF file.";
            return result;
        }
        const auto extension = std::filesystem::path(source_path).extension().wstring();
        std::wstring normalized_extension = extension;
        std::transform(normalized_extension.begin(), normalized_extension.end(), normalized_extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if ((format == ImportFormat::Svg && normalized_extension != L".svg") ||
            (format == ImportFormat::Dxf && normalized_extension != L".dxf")) {
            result.error_message = "Use the matching .svg or .dxf file extension for the detected source format.";
            return result;
        }
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }

        const std::wstring worker_path = WorkerExecutablePath();
        if (worker_path.empty() || !std::filesystem::exists(worker_path)) {
            result.error_message = "The isolated import worker is missing from the application package.";
            return result;
        }
        progress({ImportProgressStage::StartingWorker, 0});
        WorkerLaunchConfig config;
        config.worker_executable_path = worker_path;
        config.working_directory = std::filesystem::path(worker_path).parent_path().wstring();
        config.appcontainer_profile_name = L"PikDrawVectorImport";
        config.use_appcontainer = true;
        config.source_snapshot = &*source;
        WorkerProcessHost host;
        if (!host.Launch(config)) {
            result.error_message = "Windows could not start the isolated import worker.";
            return result;
        }
        constexpr uint32_t required_features = IPC_V2_FEATURE_SOURCE_HANDLE |
                                               IPC_V2_FEATURE_CHECKED_PROPOSAL_STREAM;
        if (!host.NegotiateV2(required_features, std::chrono::seconds(3))) {
            result.error_message = "The isolated import worker did not negotiate the required protocol.";
            return result;
        }
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }

        V2ImportBegin begin;
        begin.job.document_id = job.document_id;
        begin.job.epoch = job.epoch;
        begin.job.base_revision = job.base_revision;
        begin.job.generation = job.job_generation;
        begin.job.source_token = source->Token();
        begin.source_bytes = source->SizeBytes();
        begin.source_sha256 = source->Sha256();
        begin.format_id = format == ImportFormat::Svg ? "SVG" : "DXF";
        begin.profile_id = format == ImportFormat::Svg ? "svg-hierarchy-v2" : "dxf-entities-v1";
        const auto request = SerializeV2ImportBegin(begin, 1);
        const V2ProposalStreamLimits limits{kMaxImportResponseBytes, kMaxImportCanonicalBytes,
            kMaxImportRecords, kMaxImportChunks, kMaxImportFrames};
        std::vector<uint8_t> canonical;
        canonical.reserve(1024 * 1024);
        size_t record_count = 0;
        progress({ImportProgressStage::Reading, 0});
        const bool complete = host.TransactV2ProposalStream(request, std::chrono::seconds(30), limits,
            [&](const IpcV2Frame& frame) {
                if (cancellation.stop_requested()) return false;
                if (static_cast<MessageType>(frame.header.message_type) == MessageType::V2ImportEnd)
                    return true;
                if (!AppendValidatedRecords(frame, canonical, record_count)) return false;
                progress({ImportProgressStage::Reading, record_count});
                return true;
            }, cancellation);
        if (!complete) {
            if (cancellation.stop_requested() || std::string_view(host.LastTransportFailure()) == "v2 import cancelled") {
                result.cancelled = true;
            } else if (std::string_view(host.LastTransportFailure()) == "worker returned v2 error response") {
                result.error_message = format == ImportFormat::Svg
                    ? "This file is malformed or uses features outside the supported static SVG subset."
                    : "This file is malformed or uses entities outside the supported 2D DXF profile; source units are required.";
            } else {
                result.error_message = "The isolated import worker rejected an incomplete or invalid proposal.";
            }
            return result;
        }
        if (cancellation.stop_requested()) { result.cancelled = true; return result; }
        progress({ImportProgressStage::Validating, record_count});
        result.proposal.format_name = format == ImportFormat::Svg ? "SVG" : "DXF";
        result.proposal.source_bytes = source->SizeBytes();
        if (!DecodeCanonicalProposal(canonical, result.proposal)) {
            result.error_message = "The verified import proposal could not be mapped into native document objects.";
            return result;
        }
        result.success = true;
        progress({ImportProgressStage::Complete, record_count});
        return result;
    } catch (...) {
        if (cancellation.stop_requested()) result.cancelled = true;
        else result.error_message = "The import job failed while staging or validating the source.";
        return result;
    }
#endif
}

} // namespace pikdraw::desktop
