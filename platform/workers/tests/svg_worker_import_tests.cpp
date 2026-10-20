#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "pikdraw/workers/process_launcher.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cwctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

using namespace pikdraw::interop;
using namespace pikdraw::workers;
namespace fs = std::filesystem;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporarySource final {
    fs::path path;
    ~TemporarySource() {
        std::error_code ignored;
        if (!path.empty()) fs::remove(path, ignored);
    }

    bool Write(std::string_view contents, std::string_view extension = ".svg") {
        path = fs::temp_directory_path() /
            ("pikdraw-vector-worker-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(GetTickCount64()) + std::string(extension));
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return static_cast<bool>(file);
    }
};

struct ImportCapture final {
    size_t callback_count{0};
    size_t chunk_count{0};
    bool saw_end{false};
    bool semantic_records_valid{true};
    std::vector<ImportRecordValue> records;
};

uint16_t ReadU16(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t ReadU32(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

bool CaptureFrame(const IpcV2Frame& frame, ImportCapture& capture) {
    ++capture.callback_count;
    const auto type = static_cast<MessageType>(frame.header.message_type);
    const auto packet = SerializeV2Frame(type, frame.header.sequence_id, frame.payload);
    if (type == MessageType::V2ProposalChunk) {
        const auto chunk = DeserializeV2ProposalChunk(packet);
        if (!chunk) return capture.semantic_records_valid = false;
        ++capture.chunk_count;
        const auto bytes = std::span<const uint8_t>(chunk->canonical_record_bytes);
        size_t offset = 0;
        for (uint32_t record_index = 0; record_index < chunk->record_count; ++record_index) {
            if (bytes.size() - offset < 8) return capture.semantic_records_valid = false;
            const uint16_t raw_type = ReadU16(bytes, offset);
            const uint16_t reserved = ReadU16(bytes, offset + 2);
            const uint32_t payload_size = ReadU32(bytes, offset + 4);
            offset += 8;
            if (reserved != 0 || payload_size > bytes.size() - offset ||
                record_index >= chunk->record_types.size() ||
                chunk->record_types[record_index] != raw_type ||
                raw_type < static_cast<uint16_t>(ImportRecordType::Page) ||
                raw_type > static_cast<uint16_t>(ImportRecordType::Group)) {
                return capture.semantic_records_valid = false;
            }
            const auto record = DeserializeImportRecord(
                static_cast<ImportRecordType>(raw_type), bytes.subspan(offset, payload_size));
            if (!record) return capture.semantic_records_valid = false;
            capture.records.push_back(*record);
            offset += payload_size;
        }
        return offset == bytes.size();
    }
    if (type == MessageType::V2ImportEnd) {
        const auto end = DeserializeV2ImportEnd(packet);
        if (!end || !end->fidelity_report_complete) return capture.semantic_records_valid = false;
        capture.saw_end = true;
        return true;
    }
    return capture.semantic_records_valid = false;
}

WorkerLaunchConfig ConfigFor(const SourceSnapshot& source) {
    fs::path executable = fs::current_path() / "spike_worker_test.exe";
    if (!fs::exists(executable)) {
        executable = fs::current_path() / "build" / "win-dev" / "platform" / "workers" / "spike_worker_test.exe";
    }
    WorkerLaunchConfig config;
    config.worker_executable_path = executable.wstring();
    config.working_directory = fs::current_path().wstring();
    // This is a repository-owned test worker. Runtime AppContainer denial is covered by the separate live test.
    config.use_appcontainer = false;
    config.trusted_test_worker = true;
    config.source_snapshot = &source;
    config.default_timeout = std::chrono::seconds(5);
    return config;
}

V2ImportBegin BeginFor(const SourceSnapshot& source, std::string format = "SVG", std::string profile = "svg-hierarchy-v3") {
    V2ImportBegin begin;
    begin.job.document_id = "svg-worker-import-test-document";
    begin.job.epoch = 3;
    begin.job.base_revision = 7;
    begin.job.generation = 19;
    begin.job.source_token = source.Token();
    begin.source_bytes = source.SizeBytes();
    begin.source_sha256 = source.Sha256();
    begin.format_id = std::move(format);
    begin.profile_id = std::move(profile);
    return begin;
}

V2ProposalStreamLimits Limits() {
    return V2ProposalStreamLimits{4 * 1024 * 1024, 2 * 1024 * 1024, 128, 16, 17};
}

struct RunResult final {
    bool complete{false};
    std::string failure;
    ImportCapture capture;
};

RunResult RunImport(const SourceSnapshot& source, std::string format = "SVG",
                    std::string profile = "svg-hierarchy-v3",
                    std::optional<uint32_t> source_page_index = std::nullopt) {
    WorkerProcessHost host;
    const auto config = ConfigFor(source);
    Require(fs::exists(config.worker_executable_path), "spike_worker executable was not found beside this test");
    Require(host.Launch(config), "could not launch isolated SVG test worker");
    Require(host.NegotiateV2(IPC_V2_SUPPORTED_FEATURES, std::chrono::seconds(3)),
            "SVG test worker did not negotiate C6 v2");
    auto begin = BeginFor(source, std::move(format), std::move(profile));
    begin.source_page_index = source_page_index;
    const auto request = SerializeV2ImportBegin(begin, 1);
    RunResult result;
    result.complete = host.TransactV2ProposalStream(
        request, std::chrono::seconds(5), Limits(), [&](const IpcV2Frame& frame) {
            return CaptureFrame(frame, result.capture);
        });
    result.failure = host.LastTransportFailure();
    return result;
}

void TestCdrWorkerImportFromEnvironment() {
#ifdef _WIN32
    const wchar_t* fixture = _wgetenv(L"PIKDRAW_CDR_WORKER_FIXTURE");
    if (!fixture || !*fixture) return;
    const auto source = SourceSnapshot::Create(fixture, 64ull * 1024 * 1024);
    Require(source && source->IsValid(), "could not snapshot the CDR worker fixture");

    auto config = ConfigFor(*source);
    config.job_limits.max_memory_bytes = 0; // Shared system-wide 90% RAM guard is the only memory limit.
    WorkerProcessHost host;
    Require(host.Launch(config), "could not launch the repository-owned CDR worker");
    Require(host.NegotiateV2(IPC_V2_SUPPORTED_FEATURES, std::chrono::seconds(3)),
            "CDR worker did not negotiate protocol v2");

    V2ImportBegin begin;
    begin.job.document_id = "cdr-worker-import-test-document";
    begin.job.epoch = 3;
    begin.job.base_revision = 7;
    begin.job.generation = 19;
    begin.job.source_token = source->Token();
    begin.source_bytes = source->SizeBytes();
    begin.source_sha256 = source->Sha256();
    begin.format_id = "CorelDRAW";
    begin.profile_id = "cdr-libcdr-svg-v2";
    const auto request = SerializeV2ImportBegin(begin, 1);

    size_t page_count = 0;
    size_t layer_count = 0;
    size_t object_count = 0;
    size_t image_count = 0;
    size_t clipped_group_count = 0;
    size_t image_chunk_count = 0;
    uint32_t max_image_width = 0;
    uint32_t max_image_height = 0;
    uint64_t max_image_pixel_count = 0;
    size_t fidelity_reports = 0;
    size_t frames_received = 0;
    bool records_valid = true;
    const V2ProposalStreamLimits limits{
        164ull * 1024 * 1024, 160ull * 1024 * 1024, 40002, 192, 193};
    const bool complete = host.TransactV2ProposalStream(request, std::chrono::seconds(60), limits,
        [&](const IpcV2Frame& frame) {
            ++frames_received;
            const auto type = static_cast<MessageType>(frame.header.message_type);
            if (type == MessageType::V2ImportEnd) return true;
            const auto packet = SerializeV2Frame(type, frame.header.sequence_id, frame.payload);
            const auto chunk = DeserializeV2ProposalChunk(packet);
            if (!chunk) return records_valid = false;
            const auto bytes = std::span<const uint8_t>(chunk->canonical_record_bytes);
            size_t offset = 0;
            for (uint32_t index = 0; index < chunk->record_count; ++index) {
                if (bytes.size() - offset < 8) return records_valid = false;
                const uint16_t raw_type = ReadU16(bytes, offset);
                const uint16_t flags = ReadU16(bytes, offset + 2);
                const uint32_t payload_bytes = ReadU32(bytes, offset + 4);
                offset += 8;
                if (flags != 0 || payload_bytes > bytes.size() - offset ||
                    raw_type < static_cast<uint16_t>(ImportRecordType::Page) ||
                    raw_type > static_cast<uint16_t>(ImportRecordType::RasterSourceChunk))
                    return records_valid = false;
                const auto record = DeserializeImportRecord(
                    static_cast<ImportRecordType>(raw_type), bytes.subspan(offset, payload_bytes));
                if (!record) return records_valid = false;
                if (std::holds_alternative<ImportPageRecord>(*record)) ++page_count;
                else if (std::holds_alternative<ImportLayerRecord>(*record)) ++layer_count;
                else if (std::holds_alternative<ImportRasterImageChunkRecord>(*record)) ++image_chunk_count;
                else if (std::holds_alternative<ImportFidelityReportRecord>(*record)) ++fidelity_reports;
                else if (const auto* group = std::get_if<ImportGroupRecord>(&*record)) {
                    ++object_count;
                    if (group->clip_mask_local_id != 0) ++clipped_group_count;
                }
                else if (std::holds_alternative<ImportPathRecord>(*record) ||
                         std::holds_alternative<ImportTextRecord>(*record) ||
                         std::holds_alternative<ImportRasterImageRecord>(*record)) {
                    ++object_count;
                    if (const auto* image = std::get_if<ImportRasterImageRecord>(&*record)) {
                        ++image_count;
                        const uint64_t pixels = static_cast<uint64_t>(image->width_px) * image->height_px;
                        if (pixels > max_image_pixel_count) {
                            max_image_pixel_count = pixels;
                            max_image_width = image->width_px;
                            max_image_height = image->height_px;
                        }
                    }
                }
                offset += payload_bytes;
            }
            return offset == bytes.size();
        });
    if (!complete) {
        throw std::runtime_error("CDR worker stream failed: " +
            std::string(host.LastTransportFailure()) + ", exit=0x" +
            std::to_string(host.LastWorkerExitCode()) + ", frames=" +
            std::to_string(frames_received));
    }
    if (!records_valid || page_count != 1 || layer_count == 0 || object_count == 0 ||
        fidelity_reports != 1 ||
        (image_count != 0 && image_chunk_count == 0)) {
        std::cerr << "CDR proposal counts: records_valid=" << records_valid
                  << ", pages=" << page_count << ", layers=" << layer_count
                  << ", objects=" << object_count << ", clips=" << clipped_group_count
                  << ", reports=" << fidelity_reports << ", images=" << image_count
                  << ", pixel_chunks=" << image_chunk_count << ", largest_image="
                  << max_image_width << 'x' << max_image_height << '\n';
        throw std::runtime_error("CDR worker returned an incomplete editable proposal");
    }
    if (std::filesystem::path(fixture).filename().wstring() ==
        L"Bóc tách sân khấu Minh Châu Event (1).cdr") {
        Require(max_image_width == 1672 && max_image_height == 941,
                "CDR image import changed the largest embedded source raster dimensions");
    }
    std::cout << "CDR worker imported " << object_count << " editable objects, "
              << layer_count << " layers, " << clipped_group_count << " clipping groups, and "
              << image_count << " embedded images (largest " << max_image_width << 'x'
              << max_image_height << " pixels).\n";
#endif
}

std::optional<SourceSnapshot> SnapshotFor(const fs::path& path);

void TestPdfWorkerPageSelectionFromEnvironment() {
#ifdef _WIN32
    const wchar_t* fixture = _wgetenv(L"PIKDRAW_PDF_WORKER_FIXTURE");
    if (!fixture || !*fixture) return;
    const auto source = SnapshotFor(fs::path(fixture));
    Require(source && source->IsValid(), "could not snapshot the PDF worker fixture");
    const auto result = RunImport(*source, "PDF", "pdf-page-vector-v2", 1);
    if (!result.complete) {
        throw std::runtime_error("PDF selected-page worker stream failed: " + result.failure);
    }
    Require(result.capture.semantic_records_valid && result.capture.saw_end,
            "PDF selected-page worker returned an invalid proposal stream");
    bool saw_page = false;
    bool saw_selected_layer = false;
    for (const auto& record : result.capture.records) {
        if (const auto* layer = std::get_if<ImportLayerRecord>(&record))
            saw_selected_layer |= layer->source_id.rfind("pdf-page-2", 0) == 0;
        if (std::holds_alternative<ImportPageRecord>(record)) saw_page = true;
    }
    Require(saw_page && saw_selected_layer,
            "PDF worker proposal did not reflect the selected source page");
    std::cout << "PDF worker materialized selected source page 2 with "
              << result.capture.records.size() << " validated records.\n";
#endif
}

std::optional<SourceSnapshot> SnapshotFor(const fs::path& path) {
    return SourceSnapshot::Create(path.wstring(), 64ULL * 1024ULL * 1024ULL);
}

void TestWorkerImportAndTypedProposal() {
    constexpr std::string_view source = R"(<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="100mm" height="50mm" viewBox="0 0 400 200">
        <g id="layer-main" inkscape:groupmode="layer" inkscape:label="Main">
            <g id="group-main">
                <path id="outline" d="M0 0 C20 0 20 20 40 20 Q60 40 80 20 Z" fill="#2480c0" stroke="#123" stroke-width="2"/>
                <text id="label" x="20" y="100" font-family="Noto Sans" font-size="24">Tiếng Việt &amp; vectors</text>
            </g>
        </g>
    </svg>)";
    TemporarySource file;
    Require(file.Write(source), "could not write synthetic SVG test source");
    auto snapshot = SnapshotFor(file.path);
    Require(snapshot && snapshot->IsValid(), "could not create read-only source snapshot");
    const auto result = RunImport(*snapshot);
    Require(result.complete, "worker did not return a complete SVG proposal stream");
    Require(result.capture.semantic_records_valid && result.capture.saw_end,
            "worker stream contains invalid typed import records or no terminal report");
    Require(result.capture.chunk_count == 1 && result.capture.records.size() == 7,
            "worker proposal record count or chunking changed");
    Require(std::holds_alternative<ImportPageRecord>(result.capture.records[0]), "proposal did not begin with page record");
    Require(std::holds_alternative<ImportLayerRecord>(result.capture.records[1]), "proposal layer record missing");
    Require(std::holds_alternative<ImportGroupRecord>(result.capture.records[2]), "proposal group record missing");
    Require(std::holds_alternative<ImportPathRecord>(result.capture.records[3]), "proposal path record missing");
    Require(std::holds_alternative<ImportTextRecord>(result.capture.records[4]), "proposal text record missing");
    Require(std::holds_alternative<ImportFidelityIssueRecord>(result.capture.records[5]), "font fidelity issue missing");
    Require(std::holds_alternative<ImportFidelityReportRecord>(result.capture.records[6]), "terminal fidelity report missing");
    const auto& page = std::get<ImportPageRecord>(result.capture.records[0]);
    const auto& layer = std::get<ImportLayerRecord>(result.capture.records[1]);
    const auto& group = std::get<ImportGroupRecord>(result.capture.records[2]);
    const auto& path = std::get<ImportPathRecord>(result.capture.records[3]);
    const auto& text = std::get<ImportTextRecord>(result.capture.records[4]);
    const auto& report = std::get<ImportFidelityReportRecord>(result.capture.records[6]);
    Require(page.width_mm == 100 && page.height_mm == 50, "page dimensions were not converted to millimeters");
    Require(layer.source_id == "layer-main" && layer.name == "Main" && layer.object_local_ids == std::vector<uint32_t>{1},
            "layer metadata or root group reference was lost");
    Require(group.source_id == "group-main" && group.local_id == 1 && group.child_local_ids == std::vector<uint32_t>({2, 3}),
            "nested group structure was lost");
    Require(path.source_id == "outline" && path.local_id == 2 && path.elements.size() == 4 && path.has_stroke,
            "editable path geometry or source identity was lost");
    Require(text.source_id == "label" && text.local_id == 3 && text.utf8_text == "Tiếng Việt & vectors" && text.font_family == "Noto Sans",
            "editable Unicode text or font metadata was lost");
    Require(report.coverage_profile == "svg-hierarchy-v3" && report.issue_count == 1 &&
            report.production == ImportProduction::NotAssessed,
            "proposal did not report its bounded coverage and production status");
}

void TestWorkerRejectsUnsupportedMarkupAndFormats() {
    for (const auto source_text : {
             std::string_view(R"(<!DOCTYPE svg [<!ENTITY x SYSTEM "file:///private">]><svg width="10mm" height="10mm"/>)"),
             std::string_view(R"(<svg width="10mm" height="10mm"><filter id="f"/><rect width="2" height="2"/></svg>)"),
             std::string_view(R"svg(<svg width="10mm" height="10mm"><circle cx="2" cy="2" r="1"/></svg>)svg")}) {
        TemporarySource file;
        Require(file.Write(source_text), "could not write hostile SVG test source");
        auto snapshot = SnapshotFor(file.path);
        Require(snapshot && snapshot->IsValid(), "could not create hostile SVG source snapshot");
        const auto result = RunImport(*snapshot);
        Require(!result.complete && result.capture.callback_count == 0,
                "unsupported SVG produced a partial proposal stream");
        Require(result.failure == "worker returned v2 error response", "unsupported SVG lacked structured worker error");
    }

    TemporarySource file;
    Require(file.Write(R"(<svg width="10mm" height="10mm"/>)"), "could not write unsupported format source");
    auto snapshot = SnapshotFor(file.path);
    Require(snapshot && snapshot->IsValid(), "could not create unsupported format source snapshot");
    const auto result = RunImport(*snapshot, "PDF", "pdf-static-v1");
    Require(!result.complete && result.capture.callback_count == 0,
            "worker accepted a format without a registered parser");
    Require(result.failure == "worker returned v2 error response" ||
            result.failure == "unsupported format import profile",
            "unsupported format/profile lacked a structured rejection");
}

void TestWorkerImportsEpsAsEditablePathsAndRejectsExecutableOperators() {
    constexpr std::string_view source =
        "%!PS-Adobe-3.0 EPSF-3.0\n"
        "%%BoundingBox: 10 20 110 70\n"
        "%%EndComments\n"
        "/m {moveto} bind def /l {lineto} bind def /f {fill} bind def\n"
        "0 1 1 0 setcmykcolor 10 20 m 110 20 l 60 70 l closepath f showpage\n";
    TemporarySource file;
    Require(file.Write(source, ".eps"), "could not write EPS vector source");
    const auto snapshot = SnapshotFor(file.path);
    Require(snapshot && snapshot->IsValid(), "could not create EPS source snapshot");
    const auto result = RunImport(*snapshot, "EPS", "eps-path-v1");
    Require(result.complete && result.capture.saw_end && result.capture.semantic_records_valid,
            "EPS worker did not return a complete typed proposal");
    Require(result.capture.records.size() == 6 &&
            std::holds_alternative<ImportPageRecord>(result.capture.records[0]) &&
            std::holds_alternative<ImportLayerRecord>(result.capture.records[1]) &&
            std::holds_alternative<ImportPathRecord>(result.capture.records[2]) &&
            std::holds_alternative<ImportFidelityIssueRecord>(result.capture.records[3]) &&
            std::holds_alternative<ImportFidelityIssueRecord>(result.capture.records[4]) &&
            std::holds_alternative<ImportFidelityReportRecord>(result.capture.records[5]),
            "EPS worker changed the editable path proposal record order");
    const auto& path = std::get<ImportPathRecord>(result.capture.records[2]);
    Require(path.has_fill && !path.has_stroke && path.fill.model == ImportColorModel::Cmyk &&
            path.elements.size() == 4,
            "EPS vector path or source CMYK color was not mapped to native editable geometry");
    const auto& layer = std::get<ImportLayerRecord>(result.capture.records[1]);
    Require(layer.name == "EPS Artwork" && layer.object_local_ids == std::vector<uint32_t>{1},
            "EPS native layer proposal is invalid");
    bool saw_report = false;
    for (const auto& value : result.capture.records) {
        if (const auto* report = std::get_if<ImportFidelityReportRecord>(&value)) {
            saw_report = report->coverage_profile == "eps-path-v1" &&
                report->issue_count == 2 && report->production == ImportProduction::NotAssessed;
        }
    }
    Require(saw_report, "EPS worker omitted its partial coverage/fidelity report");

    constexpr std::string_view hostile =
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n%%EndComments\n"
        "deletefile\n";
    TemporarySource hostile_file;
    Require(hostile_file.Write(hostile, ".eps"), "could not write hostile EPS source");
    const auto hostile_snapshot = SnapshotFor(hostile_file.path);
    Require(hostile_snapshot && hostile_snapshot->IsValid(), "could not snapshot hostile EPS source");
    const auto rejected = RunImport(*hostile_snapshot, "EPS", "eps-path-v1");
    Require(!rejected.complete && rejected.capture.callback_count == 0 &&
            rejected.failure == "worker returned v2 error response",
            "EPS worker executed or partially accepted an unsupported PostScript operator");
}

struct CorpusResult final {
    uint64_t source_bytes{0};
    bool parser_executed{false};
    bool accepted{false};
    bool semantic_records_valid{false};
    std::string result;
};

void WriteCorpusReport(const fs::path& path, const std::vector<CorpusResult>& rows) {
    fs::create_directories(path.parent_path());
    std::ofstream report(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(report), "could not create sanitized local SVG corpus report");
    size_t accepted = 0;
    size_t executed = 0;
    for (const auto& row : rows) {
        if (row.accepted) ++accepted;
        if (row.parser_executed) ++executed;
    }
    report << "{\"schema_version\":1,\"profile\":\"svg-hierarchy-v3\",\"parser_executed\":true,"
           << "\"svg_files\":" << rows.size() << ",\"parser_runs\":" << executed
           << ",\"accepted\":" << accepted << ",\"results\":[";
    for (size_t index = 0; index < rows.size(); ++index) {
        if (index != 0) report << ',';
        const auto& row = rows[index];
        report << "{\"ordinal\":" << (index + 1) << ",\"source_bytes\":" << row.source_bytes
               << ",\"parser_executed\":" << (row.parser_executed ? "true" : "false")
               << ",\"accepted\":" << (row.accepted ? "true" : "false")
               << ",\"semantic_records_valid\":" << (row.semantic_records_valid ? "true" : "false")
               << ",\"result\":\"" << row.result << "\"}";
    }
    report << "]}\n";
    Require(static_cast<bool>(report), "could not finish sanitized local SVG corpus report");
}

int ProbeCorpus(const fs::path& corpus) {
    Require(fs::exists(corpus) && fs::is_directory(corpus), "local corpus directory does not exist");
    std::vector<fs::path> svg_files;
    for (fs::recursive_directory_iterator it(corpus, fs::directory_options::skip_permission_denied), end;
         it != end; ++it) {
        std::error_code error;
        if (it->is_symlink(error) || error || !it->is_regular_file(error) || error) continue;
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (extension == ".svg") svg_files.push_back(it->path());
    }
    Require(!svg_files.empty(), "local corpus contains no regular SVG fixture");

    std::vector<CorpusResult> rows;
    for (const auto& path : svg_files) {
        CorpusResult row;
        std::error_code error;
        row.source_bytes = fs::file_size(path, error);
        auto snapshot = error ? std::optional<SourceSnapshot>{} : SnapshotFor(path);
        if (!snapshot || !snapshot->IsValid()) {
            row.result = "source_snapshot_rejected";
            rows.push_back(std::move(row));
            continue;
        }
        const auto result = RunImport(*snapshot);
        row.parser_executed = result.capture.callback_count > 0 ||
            result.failure == "worker returned v2 error response";
        row.accepted = result.complete;
        row.semantic_records_valid = result.capture.semantic_records_valid && result.capture.saw_end;
        row.result = "accepted";
        if (!result.complete) {
            std::ifstream source_file(path, std::ios::binary | std::ios::ate);
            const auto source_end = source_file ? source_file.tellg() : std::streampos(-1);
            if (source_end > 0 && static_cast<uint64_t>(source_end) <= SvgImporter::kMaxSourceBytes) {
                std::string source_bytes(static_cast<size_t>(source_end), '\0');
                source_file.seekg(0, std::ios::beg);
                if (source_file.read(source_bytes.data(), static_cast<std::streamsize>(source_bytes.size()))) {
                    const auto direct = SvgImporter::import_from_string(source_bytes);
                    row.result = direct.success ? "worker_contract_rejected" : "rejected_subset";
                    if (!direct.report.issues().empty()) {
                        row.result += "_" + direct.report.issues().back().feature;
                    }
                } else {
                    row.result = "diagnostic_read_failed";
                }
            } else {
                row.result = "diagnostic_source_limit";
            }
        }
        if (result.complete) Require(row.semantic_records_valid, "actual SVG proposal failed typed record validation");
        if (!result.complete) Require(result.capture.callback_count == 0, "actual SVG rejection leaked partial proposal records");
        rows.push_back(std::move(row));
    }
    WriteCorpusReport(corpus / "test-reports" / "svg-worker-import.json", rows);
    std::cout << "SVG corpus probe: " << rows.size() << " files; parser runs="
              << std::count_if(rows.begin(), rows.end(), [](const CorpusResult& row) { return row.parser_executed; })
              << "; accepted=" << std::count_if(rows.begin(), rows.end(), [](const CorpusResult& row) { return row.accepted; })
              << "; report is sanitized and contains no source names or hashes\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--probe-corpus") return ProbeCorpus(fs::path(argv[2]));
        Require(argc == 1, "usage: test_pikdraw_worker_svg_import [--probe-corpus <cdr-folder>]");
        TestWorkerImportAndTypedProposal();
        TestWorkerImportsEpsAsEditablePathsAndRejectsExecutableOperators();
        TestWorkerRejectsUnsupportedMarkupAndFormats();
        TestCdrWorkerImportFromEnvironment();
        TestPdfWorkerPageSelectionFromEnvironment();
        std::cout << "SVG/EPS isolated vector worker import tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SVG worker import tests failed: " << error.what() << '\n';
        return 1;
    }
}
