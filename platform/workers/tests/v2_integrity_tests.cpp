#include "pikdraw/workers/process_launcher.hpp"
#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"
#include "../src/v2_proposal_validator.hpp"

#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace pikdraw::interop;
using namespace pikdraw::workers;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporarySource final {
    std::filesystem::path path;
    ~TemporarySource() {
        std::error_code ignored;
        if (!path.empty()) std::filesystem::remove(path, ignored);
    }

    bool Create() {
        path = std::filesystem::temp_directory_path() /
            ("pikdraw-v2-integrity-" + std::to_string(GetCurrentProcessId()) + ".bin");
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        for (uint32_t i = 0; i < 4096; ++i) {
            const char byte = static_cast<char>((i * 31u + 7u) & 0xffu);
            file.write(&byte, 1);
        }
        return static_cast<bool>(file);
    }
};

WorkerLaunchConfig ConfigFor(const SourceSnapshot& source, std::wstring mode) {
    auto executable = std::filesystem::current_path() / "spike_worker_test.exe";
    if (!std::filesystem::exists(executable)) {
        executable = std::filesystem::current_path() / "build" / "win-dev" /
            "platform" / "workers" / "spike_worker_test.exe";
    }
    WorkerLaunchConfig config;
    config.worker_executable_path = executable.wstring();
    config.working_directory = std::filesystem::current_path().wstring();
    config.use_appcontainer = false;
    config.trusted_test_worker = true;
    config.source_snapshot = &source;
    if (!mode.empty()) config.arguments = {std::move(mode)};
    config.default_timeout = std::chrono::seconds(3);
    return config;
}

V2ImportBegin BeginFor(const SourceSnapshot& source) {
    V2ImportBegin begin;
    begin.job.document_id = "v2-integrity-test-document";
    begin.job.epoch = 5;
    begin.job.base_revision = 12;
    begin.job.generation = 19;
    begin.job.source_token = source.Token();
    begin.source_bytes = source.SizeBytes();
    begin.source_sha256 = source.Sha256();
    begin.format_id = "TEST";
    begin.profile_id = "test-subset-v1";
    return begin;
}

V2ProposalStreamLimits Limits() {
    return V2ProposalStreamLimits{8192, 1024, 16, 16, 17};
}

bool StartNegotiated(
    WorkerProcessHost& host,
    const SourceSnapshot& source,
    std::wstring mode) {
    const auto config = ConfigFor(source, std::move(mode));
    return host.Launch(config) && host.NegotiateV2(IPC_V2_SUPPORTED_FEATURES, std::chrono::seconds(2));
}

bool Digest(std::span<const uint8_t> bytes, IpcV2Digest& output) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> object;
    const auto close = [&] {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    DWORD object_size = 0;
    DWORD written = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &written, 0)) ||
        object_size == 0 || written != sizeof(object_size)) {
        close();
        return false;
    }
    object.resize(object_size);
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0))) {
        close();
        return false;
    }
    const bool updated = BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), 0));
    const bool finished = updated && BCRYPT_SUCCESS(BCryptFinishHash(hash, output.data(),
        static_cast<ULONG>(output.size()), 0));
    close();
    return finished;
}

IpcV2Frame MakeFrame(std::vector<uint8_t> packet) {
    const auto frame = DeserializeV2Frame(packet);
    if (!frame) throw std::runtime_error("could not decode generated semantic test frame");
    return *frame;
}

void TestSvgHierarchyHostSemantics(const SourceSnapshot& source) {
    auto expected = BeginFor(source);
    expected.format_id = "SVG";
    expected.profile_id = "svg-hierarchy-v2";
    auto legacy_profile = expected;
    legacy_profile.profile_id = "svg-flat-v1";
    V2ProposalStreamValidator legacy_validator(legacy_profile, 64 * 1024, 128, 4);
    Require(!legacy_validator.IsReady(), "host enabled the incompatible legacy SVG profile");

    const auto run = [&](bool duplicate_owner, bool missing_child, bool wrong_order) {
        ImportPageRecord page{100, 50};
        ImportLayerRecord layer;
        layer.local_id = 1;
        layer.source_id = "layer";
        layer.name = "Layer";
        layer.object_local_ids = duplicate_owner ? std::vector<uint32_t>{1, 2} : std::vector<uint32_t>{1};
        ImportGroupRecord group;
        group.local_id = 1;
        group.source_id = "group";
        group.name = "Group";
        group.child_local_ids = missing_child ? std::vector<uint32_t>{99} : std::vector<uint32_t>{2};
        ImportPathRecord path;
        path.local_id = 2;
        path.source_id = "path";
        path.name = "Path";
        path.elements.push_back({ImportPathVerb::Move, 0, {}});
        ImportFidelityReportRecord report;
        report.coverage_profile = "svg-hierarchy-v2";
        report.issue_count = 0;

        std::vector<ImportRecordValue> values;
        values.emplace_back(page);
        if (wrong_order) {
            values.emplace_back(group);
            values.emplace_back(layer);
        } else {
            values.emplace_back(layer);
            values.emplace_back(group);
        }
        values.emplace_back(path);
        values.emplace_back(report);

        V2ProposalChunk chunk;
        chunk.job = expected.job;
        chunk.chunk_ordinal = 0;
        chunk.first_record_ordinal = 0;
        for (const auto& value : values) {
            const auto record = SerializeImportRecord(value);
            if (!record || !AppendV2ProposalRecord(chunk.canonical_record_bytes,
                    static_cast<uint16_t>(record->type), record->payload)) {
                throw std::runtime_error("could not encode semantic test record");
            }
            ++chunk.record_count;
        }
        chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
        V2ProposalStreamValidator validator(expected, 64 * 1024, 128, 4);
        if (!validator.IsReady()) throw std::runtime_error("SVG hierarchy validator failed to initialize");
        const bool accepted = validator.Accept(MakeFrame(SerializeV2ProposalChunk(chunk, 1)));
        return accepted;
    };

    {
        ImportPageRecord page{100, 50};
        ImportLayerRecord layer{1, "layer", "Layer", true, false, {1}};
        ImportGroupRecord group{1, "group", "Group", {2}};
        ImportPathRecord path;
        path.local_id = 2;
        path.source_id = "path";
        path.name = "Path";
        path.elements.push_back({ImportPathVerb::Move, 0, {}});
        ImportFidelityReportRecord report;
        report.coverage_profile = "svg-hierarchy-v2";

        std::vector<ImportRecordValue> values{page, layer, group, path, report};
        V2ProposalChunk chunk;
        chunk.job = expected.job;
        chunk.chunk_ordinal = 0;
        chunk.first_record_ordinal = 0;
        for (const auto& value : values) {
            const auto record = SerializeImportRecord(value);
            Require(record && AppendV2ProposalRecord(chunk.canonical_record_bytes,
                static_cast<uint16_t>(record->type), record->payload), "could not encode valid SVG hierarchy records");
            ++chunk.record_count;
        }
        chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
        V2ProposalStreamValidator validator(expected, 64 * 1024, 128, 4);
        Require(validator.IsReady(), "SVG hierarchy validator did not initialize");
        Require(validator.Accept(MakeFrame(SerializeV2ProposalChunk(chunk, 1))),
                "host rejected valid SVG layer/group references");

        V2ImportEnd end;
        end.job = expected.job;
        end.chunk_count = 1;
        end.record_count = chunk.record_count;
        end.canonical_bytes = chunk.canonical_record_bytes.size();
        end.fidelity_report_complete = true;
        Require(Digest(chunk.canonical_record_bytes, end.canonical_sha256), "could not hash semantic test stream");
        Require(validator.Accept(MakeFrame(SerializeV2ImportEnd(end, 2))) && validator.IsComplete(),
                "host did not complete a valid SVG hierarchy proposal");
    }
    Require(!run(true, false, false), "host accepted duplicate group/layer ownership");
    Require(!run(false, true, false), "host accepted a missing group child reference");
    Require(!run(false, false, true), "host accepted a layer after object records began");
}

void TestDxfProfileHostSemantics(const SourceSnapshot& source) {
    auto expected = BeginFor(source);
    expected.format_id = "DXF";
    expected.profile_id = "dxf-entities-v1";
    auto unsupported = expected;
    unsupported.profile_id = "dxf-full-v1";
    V2ProposalStreamValidator unsupported_validator(unsupported, 64 * 1024, 128, 4);
    Require(!unsupported_validator.IsReady(), "host enabled an unregistered DXF profile");

    ImportPageRecord page{100, 50};
    ImportLayerRecord layer{1, "layer", "CUT", true, false, {1}};
    ImportPathRecord path;
    path.local_id = 1;
    path.source_id = "entity";
    path.name = "LINE";
    path.elements.push_back({ImportPathVerb::Move, 0, {}});
    ImportFidelityReportRecord report;
    report.coverage_profile = "dxf-entities-v1";
    const std::vector<ImportRecordValue> values{page, layer, path, report};
    V2ProposalChunk chunk;
    chunk.job = expected.job;
    for (const auto& value : values) {
        const auto record = SerializeImportRecord(value);
        Require(record && AppendV2ProposalRecord(chunk.canonical_record_bytes,
            static_cast<uint16_t>(record->type), record->payload),
            "could not encode valid DXF semantic records");
        ++chunk.record_count;
    }
    chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
    V2ProposalStreamValidator validator(expected, 64 * 1024, 128, 4);
    Require(validator.IsReady(), "DXF host semantic validator did not initialize");
    Require(validator.Accept(MakeFrame(SerializeV2ProposalChunk(chunk, 1))),
            "host rejected a valid DXF layer/path proposal");

    V2ImportEnd end;
    end.job = expected.job;
    end.chunk_count = 1;
    end.record_count = chunk.record_count;
    end.canonical_bytes = chunk.canonical_record_bytes.size();
    end.fidelity_report_complete = true;
    Require(Digest(chunk.canonical_record_bytes, end.canonical_sha256),
            "could not hash valid DXF semantic records");
    Require(validator.Accept(MakeFrame(SerializeV2ImportEnd(end, 2))) &&
                validator.IsComplete(),
            "host did not complete a valid DXF proposal");

    auto wrong_report = values;
    std::get<ImportFidelityReportRecord>(wrong_report.back()).coverage_profile =
        "svg-hierarchy-v2";
    V2ProposalChunk wrong_chunk;
    wrong_chunk.job = expected.job;
    for (const auto& value : wrong_report) {
        const auto record = SerializeImportRecord(value);
        Require(record && AppendV2ProposalRecord(wrong_chunk.canonical_record_bytes,
            static_cast<uint16_t>(record->type), record->payload),
            "could not encode wrong-profile DXF records");
        ++wrong_chunk.record_count;
    }
    wrong_chunk.checksum_crc32 = V2ProposalRecordsCrc32(wrong_chunk.canonical_record_bytes);
    V2ProposalStreamValidator wrong_profile(expected, 64 * 1024, 128, 4);
    Require(wrong_profile.IsReady(), "DXF validator did not initialize for wrong-report test");
    Require(!wrong_profile.Accept(MakeFrame(SerializeV2ProposalChunk(wrong_chunk, 1))),
            "host accepted a DXF stream carrying the SVG coverage profile");
}

void TestRasterChunkHostSemantics(const SourceSnapshot& source) {
    auto expected = BeginFor(source);
    expected.format_id = "PNG";
    expected.profile_id = "raster-rgba8-v1";

    ImportPageRecord page{0.5291666667, 0.2645833333};
    ImportLayerRecord layer{1, "raster-layer", "Raster", true, false, {1}};
    ImportRasterImageRecord image;
    image.local_id = 1;
    image.source_id = "raster-image";
    image.name = "Image";
    image.media_type = "image/png";
    image.width_px = 2;
    image.height_px = 1;
    image.decoded_pixel_bytes = 8;
    image.source_bits_per_pixel = 32;
    image.frame_count = 1;
    image.dpi_x = 96;
    image.dpi_y = 96;
    ImportRasterImageChunkRecord pixels;
    pixels.local_id = 1;
    pixels.byte_offset = 0;
    pixels.total_bytes = 8;
    pixels.bytes = {255, 0, 0, 255, 0, 255, 0, 128};
    ImportFidelityIssueRecord issue;
    issue.source_locator = "raster-image";
    issue.feature_code = "raster_rgba8_conversion";
    issue.diagnostic_code = "appearance_and_editability_limited";
    ImportFidelityReportRecord report;
    report.coverage_profile = "raster-rgba8-v1";
    report.parse_outcome = ImportParseOutcome::CompleteWithDiagnostics;
    report.coverage = ImportCoverage::Partial;
    report.appearance = ImportAppearance::Partial;
    report.editability = ImportEditability::ExplicitConversion;
    report.production = ImportProduction::NotAssessed;
    report.issue_count = 1;

    const auto stream_for = [&](bool omit_pixels, bool wrong_offset) {
        std::vector<ImportRecordValue> values{page, layer, image};
        if (!omit_pixels) {
            auto chunk = pixels;
            if (wrong_offset) {
                chunk.byte_offset = 4;
                chunk.bytes.resize(4);
            }
            values.emplace_back(std::move(chunk));
        }
        values.emplace_back(issue);
        values.emplace_back(report);
        V2ProposalChunk chunk;
        chunk.job = expected.job;
        for (const auto& value : values) {
            const auto record = SerializeImportRecord(value);
            Require(record && AppendV2ProposalRecord(chunk.canonical_record_bytes,
                static_cast<uint16_t>(record->type), record->payload),
                "could not encode raster semantic test record");
            ++chunk.record_count;
        }
        chunk.checksum_crc32 = V2ProposalRecordsCrc32(chunk.canonical_record_bytes);
        return chunk;
    };

    auto valid_chunk = stream_for(false, false);
    V2ProposalStreamValidator valid(expected, 64 * 1024, 128, 4);
    Require(valid.IsReady(), "raster host validator did not initialize");
    Require(valid.Accept(MakeFrame(SerializeV2ProposalChunk(valid_chunk, 1))),
            "host rejected a complete RGBA8 raster proposal");
    V2ImportEnd end;
    end.job = expected.job;
    end.chunk_count = 1;
    end.record_count = valid_chunk.record_count;
    end.canonical_bytes = valid_chunk.canonical_record_bytes.size();
    end.fidelity_report_complete = true;
    Require(Digest(valid_chunk.canonical_record_bytes, end.canonical_sha256),
            "could not hash raster semantic stream");
    Require(valid.Accept(MakeFrame(SerializeV2ImportEnd(end, 2))) && valid.IsComplete(),
            "host did not complete a full raster fidelity stream");

    for (const auto [omit_pixels, wrong_offset] : {std::pair{true, false},
                                                   std::pair{false, true}}) {
        const auto malformed = stream_for(omit_pixels, wrong_offset);
        V2ProposalStreamValidator validator(expected, 64 * 1024, 128, 4);
        Require(validator.IsReady(), "negative raster validator did not initialize");
        Require(!validator.Accept(MakeFrame(SerializeV2ProposalChunk(malformed, 1))),
                "host accepted an incomplete or out-of-order raster pixel stream");
    }
}

void TestPositiveStream(const SourceSnapshot& source) {
    WorkerProcessHost host;
    Require(StartNegotiated(host, source, L"--test-v2-stream-good"), "v2 fixture negotiation failed");
    const auto request = SerializeV2ImportBegin(BeginFor(source), 1);
    size_t callbacks = 0;
    const bool complete = host.TransactV2ProposalStream(
        request, std::chrono::seconds(2), Limits(), [&](const IpcV2Frame& frame) {
            ++callbacks;
            return frame.header.sequence_id == callbacks &&
                   (frame.header.message_type == static_cast<uint16_t>(MessageType::V2ProposalChunk) ||
                    frame.header.message_type == static_cast<uint16_t>(MessageType::V2ImportEnd));
        });
    Require(complete && callbacks == 2, "valid v2 stream did not complete atomically");
    host.Terminate();
}

void TestRejectedStream(
    const SourceSnapshot& source,
    const wchar_t* mode,
    const char* expected_reason = nullptr,
    bool reject_consumer = false) {
    WorkerProcessHost host;
    Require(StartNegotiated(host, source, mode), "negative fixture negotiation failed");
    const auto request = SerializeV2ImportBegin(BeginFor(source), 1);
    auto limits = Limits();
    if (std::wstring_view(mode) == L"--test-v2-stream-quota") limits.max_total_response_bytes = 1;
    const auto consumer = reject_consumer
        ? std::function<bool(const IpcV2Frame&)>([](const IpcV2Frame&) { return false; })
        : std::function<bool(const IpcV2Frame&)>{};
    const bool complete = host.TransactV2ProposalStream(
        request, std::chrono::milliseconds(500), limits, consumer);
    Require(!complete, "invalid v2 stream was accepted");
    Require(std::string_view(host.LastTransportFailure()) != "none", "rejected stream lacks a diagnostic");
    if (expected_reason) Require(std::string_view(host.LastTransportFailure()) == expected_reason,
                                 "v2 rejection diagnostic changed");
}

void TestNegotiationFailures(const SourceSnapshot& source) {
    for (const auto* mode : {L"--test-v2-stream-negotiation-reject", L"--test-v2-stream-v1-downgrade"}) {
        WorkerProcessHost host;
        const auto config = ConfigFor(source, mode);
        Require(host.Launch(config), "negotiation failure fixture did not launch");
        Require(!host.NegotiateV2(IPC_V2_SUPPORTED_FEATURES, std::chrono::seconds(2)),
                "v2 mismatch was downgraded or accepted");
    }
}

void TestRequestSnapshotBinding(const SourceSnapshot& source) {
    WorkerProcessHost host;
    Require(StartNegotiated(host, source, L"--test-v2-stream-good"), "snapshot binding fixture failed to launch");
    auto begin = BeginFor(source);
    begin.source_sha256[0] ^= 1;
    const bool complete = host.TransactV2ProposalStream(
        SerializeV2ImportBegin(begin, 1), std::chrono::seconds(2), Limits(), {});
    Require(!complete, "host accepted a source hash different from the staged snapshot");
    Require(std::string_view(host.LastTransportFailure()).find("matching snapshot") != std::string_view::npos,
            "snapshot mismatch was not reported");
}

void TestNoAdapterExplicitError(const SourceSnapshot& source) {
    WorkerProcessHost host;
    Require(StartNegotiated(host, source, L""), "production worker v2 negotiation failed");
    const auto request = SerializeV2ImportBegin(BeginFor(source), 1);
    Require(!host.TransactV2ProposalStream(request, std::chrono::seconds(2), Limits(), {}),
            "worker without a registered parser claimed import success");
    Require(std::string_view(host.LastTransportFailure()) == "worker returned v2 error response",
            "worker did not report unsupported parser as a structured failure");
}

} // namespace

int main() {
    try {
        TemporarySource source_file;
        Require(source_file.Create(), "could not create synthetic source bytes");
        SourceSnapshotFailure failure = SourceSnapshotFailure::None;
        auto source = SourceSnapshot::Create(source_file.path.wstring(), 4096, &failure);
        Require(source && source->IsValid(), "could not stage synthetic source snapshot");
        const std::array<uint8_t, 9> crc_probe{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        Require(V2ProposalRecordsCrc32(crc_probe) == 0xCBF43926u, "CRC32 known vector mismatch");

        TestPositiveStream(*source);
        TestSvgHierarchyHostSemantics(*source);
        TestDxfProfileHostSemantics(*source);
        TestRasterChunkHostSemantics(*source);
        for (const auto* mode : {
                 L"--test-v2-stream-gap", L"--test-v2-stream-oversize", L"--test-v2-stream-quota",
                 L"--test-v2-stream-no-end", L"--test-v2-stream-bad-crc", L"--test-v2-stream-bad-hash",
                 L"--test-v2-stream-bad-count", L"--test-v2-stream-bad-report",
                 L"--test-v2-stream-bad-ordinal", L"--test-v2-stream-wrong-token",
                 L"--test-v2-stream-wrong-job"}) {
            TestRejectedStream(*source, mode);
        }
        TestRejectedStream(*source, L"--test-v2-stream-good", "v2 frame consumer rejected proposal", true);
        TestNegotiationFailures(*source);
        TestRequestSnapshotBinding(*source);
        TestNoAdapterExplicitError(*source);
        std::cout << "V2 integrity semantic suite passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2 integrity semantic tests failed: " << error.what() << '\n';
        return 1;
    }
}
