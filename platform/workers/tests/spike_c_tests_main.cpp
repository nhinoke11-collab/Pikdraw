#include "pikdraw/interop/interop_types.hpp"
#include "pikdraw/interop/ipc_dto.hpp"
#include "pikdraw/interop/payload_validator.hpp"
#include "pikdraw/interop/format_capability_matrix.hpp"
#include "pikdraw/interop/fixture_manifest.hpp"
#include "pikdraw/workers/win32_common.hpp"
#include "pikdraw/workers/job_object.hpp"
#include "pikdraw/workers/appcontainer.hpp"
#include "pikdraw/workers/process_launcher.hpp"
#include "pikdraw/workers/canary_definitions.hpp"
#include "pikdraw/workers/source_snapshot.hpp"
#include <algorithm>
#include <array>

#include <iostream>
#include <cassert>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <cstring>

using namespace pikdraw::interop;
using namespace pikdraw::workers;
int RunLiveContainment();
int RunLocalSourceCorpusProbe(const std::filesystem::path& corpus_root);

namespace {

int g_tests_passed = 0;
int g_tests_failed = 0;

struct TemporarySourceFile {
    std::wstring path;
    ~TemporarySourceFile() {
        if (!path.empty()) DeleteFileW(path.c_str());
    }
};

bool CreateDeterministicSource(TemporarySourceFile& file, uint64_t bytes) {
    std::array<wchar_t, MAX_PATH + 1> directory{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    if (length == 0 || length >= directory.size()) return false;

    std::array<wchar_t, MAX_PATH + 1> path{};
    if (GetTempFileNameW(directory.data(), L"pds", 0, path.data()) == 0) return false;
    file.path = path.data();

    ScopedHandle output(CreateFileW(
        file.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!output.IsValid()) return false;

    std::vector<uint8_t> buffer(256 * 1024);
    uint64_t written_total = 0;
    while (written_total < bytes) {
        const size_t count = static_cast<size_t>(
            std::min<uint64_t>(buffer.size(), bytes - written_total));
        for (size_t i = 0; i < count; ++i) {
            buffer[i] = static_cast<uint8_t>((written_total + i) * 31 + 7);
        }
        DWORD offset = 0;
        while (offset < count) {
            DWORD written = 0;
            if (!WriteFile(output.Get(), buffer.data() + offset,
                           static_cast<DWORD>(count - offset), &written, nullptr) ||
                written == 0) return false;
            offset += written;
        }
        written_total += count;
    }
    return FlushFileBuffers(output.Get()) != FALSE;
}

std::wstring DigestArgument(const SourceDigest& digest) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve(digest.size() * 2);
    for (const uint8_t byte : digest) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

// --------------------------------------------------------------------
// Test 1: Transform Matrix Semantics and Vector Verification (Shared Contract)
// --------------------------------------------------------------------
void TestTransformVectors() {
    // Vector 1: large-sign-relative
    Point2D p1{20000.005, 500.0};
    Transform2D m1{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    Point2D origin1{20000.0, 0.0};
    Point2D world1 = m1.Apply(p1);
    Point2D relative1{world1.x - origin1.x, world1.y - origin1.y};
    TEST_ASSERT(std::abs(relative1.x - 0.005) < 1e-9, "large-sign-relative x failed");
    TEST_ASSERT(std::abs(relative1.y - 500.0) < 1e-9, "large-sign-relative y failed");

    // Vector 2: reflection
    Point2D p2{2.0, 3.0};
    Transform2D m2{-1.0, 0.0, 0.0, 1.0, 10.0, 0.0};
    Point2D world2 = m2.Apply(p2);
    TEST_ASSERT(std::abs(world2.x - 8.0) < 1e-9, "reflection x failed");
    TEST_ASSERT(std::abs(world2.y - 3.0) < 1e-9, "reflection y failed");

    // Vector 3: nonuniform-scale-translate
    Point2D p3{2.0, 3.0};
    Transform2D m3{2.0, 0.0, 0.0, 0.5, 10.0, -4.0};
    Point2D world3 = m3.Apply(p3);
    TEST_ASSERT(std::abs(world3.x - 14.0) < 1e-9, "nonuniform-scale-translate x failed");
    TEST_ASSERT(std::abs(world3.y - (-2.5)) < 1e-9, "nonuniform-scale-translate y failed");

    // Transform Composition: compose(parent, local) applies local first
    Transform2D parent{2.0, 0.0, 0.0, 2.0, 5.0, 5.0};
    Transform2D local{1.0, 0.0, 0.0, 1.0, 10.0, 20.0};
    Transform2D comp = Transform2D::Compose(parent, local);
    Point2D origin{0.0, 0.0};
    Point2D res = comp.Apply(origin);
    // local(0,0) -> (10, 20); parent(10,20) -> (2*10+5, 2*20+5) = (25, 45)
    TEST_ASSERT(std::abs(res.x - 25.0) < 1e-9 && std::abs(res.y - 45.0) < 1e-9, "Compose order failed");

    g_tests_passed++;
    std::cout << "[PASS] TestTransformVectors\n";
}

// --------------------------------------------------------------------
// Test 2: Bounded IPC Framing and Validator Limits
// --------------------------------------------------------------------
void TestIpcBoundaryValidation() {
    // 1. Invalid Magic
    IpcHeader bad_magic{};
    bad_magic.magic = 0xDEADBEEF;
    bad_magic.wire_version = 1;
    auto d1 = PayloadValidator::ValidateHeader(bad_magic);
    TEST_ASSERT(!d1.IsOk() && d1.code == ValidationResult::MalformedData, "Invalid magic not caught");

    // 2. Wire version mismatch
    IpcHeader bad_ver{};
    bad_ver.magic = IPC_MAGIC;
    bad_ver.wire_version = 2;
    auto d2 = PayloadValidator::ValidateHeader(bad_ver);
    TEST_ASSERT(!d2.IsOk() && d2.code == ValidationResult::VersionMismatch, "Version mismatch not caught");

    // 3. Payload size limit (16 MiB)
    IpcHeader big_hdr{};
    big_hdr.magic = IPC_MAGIC;
    big_hdr.wire_version = 1;
    big_hdr.payload_bytes = 17 * 1024 * 1024;
    auto d3 = PayloadValidator::ValidateHeader(big_hdr);
    TEST_ASSERT(!d3.IsOk() && d3.code == ValidationResult::PayloadOversized, "Payload oversized not caught");

    // 4. Tree depth limit (64)
    std::vector<VectorGraphicObject> deep_objs;
    VectorGraphicObject deep_obj{};
    deep_obj.depth = 65;
    deep_objs.push_back(deep_obj);
    auto d4 = PayloadValidator::ValidateGraphicPayload(deep_objs);
    TEST_ASSERT(!d4.IsOk() && d4.code == ValidationResult::DepthLimitExceeded, "Depth > 64 not caught");

    // 5. Non-finite values (NaN / Inf)
    std::vector<VectorGraphicObject> nan_objs;
    VectorGraphicObject nan_obj{};
    nan_obj.depth = 1;
    nan_obj.transform.tx = std::numeric_limits<double>::quiet_NaN();
    nan_objs.push_back(nan_obj);
    auto d5 = PayloadValidator::ValidateGraphicPayload(nan_objs);
    TEST_ASSERT(!d5.IsOk() && d5.code == ValidationResult::NonFiniteCoordinate, "NaN coordinate not caught");

    g_tests_passed++;
    std::cout << "[PASS] TestIpcBoundaryValidation\n";
}

// --------------------------------------------------------------------
// Test 3: Stale Proposal Rejection Schedule (Undo, Reopen, Cancel)
// --------------------------------------------------------------------
void TestStaleProposalRejection() {
    JobEnvelope host_state{"doc-1", 1, 10, 1};

    // Schedule 1: Undo stale proposal
    // snapshot r=10 epoch=1 gen=1; edit ->11; undo ->12; worker returns base=10 epoch=1 gen=1
    JobEnvelope host_after_undo{"doc-1", 1, 12, 1};
    JobEnvelope worker_return_10{"doc-1", 1, 10, 1};
    auto d_undo = PayloadValidator::ValidateJobEnvelope(worker_return_10, host_after_undo, false);
    TEST_ASSERT(!d_undo.IsOk() && d_undo.code == ValidationResult::StaleRevision, "Undo stale proposal must reject");

    // Schedule 2: Close/Reopen stale proposal
    // snapshot r=10 epoch=1 gen=1; close/reopen epoch=2; worker returns epoch=1
    JobEnvelope host_reopened{"doc-1", 2, 10, 1};
    auto d_reopen = PayloadValidator::ValidateJobEnvelope(worker_return_10, host_reopened, false);
    TEST_ASSERT(!d_reopen.IsOk() && d_reopen.code == ValidationResult::StaleEpoch, "Close/reopen stale proposal must reject");

    // Schedule 3: Superseded generation
    JobEnvelope host_newer_gen{"doc-1", 1, 10, 2};
    auto d_gen = PayloadValidator::ValidateJobEnvelope(worker_return_10, host_newer_gen, false);
    TEST_ASSERT(!d_gen.IsOk() && d_gen.code == ValidationResult::StaleGeneration, "Stale generation must reject");

    // Schedule 4: Cancelled task
    auto d_cancel = PayloadValidator::ValidateJobEnvelope(worker_return_10, host_state, true);
    TEST_ASSERT(!d_cancel.IsOk() && d_cancel.code == ValidationResult::CancelledJob, "Cancelled job must reject");

    g_tests_passed++;
    std::cout << "[PASS] TestStaleProposalRejection\n";
}

// --------------------------------------------------------------------
// Test 4: Job Object Lifecycle and Quotas
// --------------------------------------------------------------------
void TestJobObjectConfiguration() {
    JobLimitsConfig config{};
    config.max_memory_bytes = 128 * 1024 * 1024; // 128 MiB
    config.kill_on_job_close = true;
    config.active_process_limit = 1;

    JobObjectGuard job;
    TEST_ASSERT(job.Create(config), "JobObject creation failed");
    TEST_ASSERT(job.IsValid(), "JobObject handle invalid");
    config.active_process_limit=8;
    config.prevent_child_processes=true;
    JobObjectGuard strict;
    TEST_ASSERT(strict.Create(config), "Strict JobObject creation failed");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION actual{};
    TEST_ASSERT(QueryInformationJobObject(strict.Get(),JobObjectExtendedLimitInformation,&actual,sizeof(actual),nullptr), "Cannot query actual Job limits");
    TEST_ASSERT(actual.BasicLimitInformation.ActiveProcessLimit==1, "Prevent-child policy must override a larger process limit");

    g_tests_passed++;
    std::cout << "[PASS] TestJobObjectConfiguration\n";
}

// --------------------------------------------------------------------
// Test 5: Command Line Quoting with UTF-16 and Spaces
// --------------------------------------------------------------------
void TestCommandLineQuoting() {
    std::wstring path_with_spaces = L"C:\\Program Files\\PikDraw Testing\\worker process.exe";
    std::wstring quoted = QuoteCommandLineArgument(path_with_spaces);
    TEST_ASSERT(quoted == L"\"C:\\Program Files\\PikDraw Testing\\worker process.exe\"", "Path quoting failed");

    std::wstring non_ascii = L"C:\\測試目錄\\test worker.exe";
    std::wstring quoted_non_ascii = QuoteCommandLineArgument(non_ascii);
    TEST_ASSERT(quoted_non_ascii == L"\"C:\\測試目錄\\test worker.exe\"", "Non-ASCII quoting failed");

    g_tests_passed++;
    std::cout << "[PASS] TestCommandLineQuoting\n";
}

// --------------------------------------------------------------------
// Test 6: Format Capability Matrix & Synthetic Fixture Integrity
// --------------------------------------------------------------------
void TestFormatMatrixAndFixtures() {
    auto matrix = FormatCapabilityRegistry::GetMatrix();
    TEST_ASSERT(!matrix.empty(), "Format capability matrix is empty");

    bool has_svg = false;
    bool has_cdr = false;
    bool has_pdf = false;
    for (const auto& entry : matrix) {
        if (entry.format_name == "SVG Native Subset") has_svg = true;
        if (entry.format_name == "CorelDRAW Vector Graphics") has_cdr = true;
        if (entry.format_name == "PDF Vector Representation") has_pdf = true;
    }
    TEST_ASSERT(has_svg && has_cdr && has_pdf, "Key formats missing from capability matrix");

    TEST_ASSERT(FixtureManifest::Count() == 0, "Unprovisioned corpus cannot contain invented evidence");
    for (const auto& entry : matrix) {
        TEST_ASSERT(entry.import_status == FormatCapabilityStatus::UnsupportedReject &&
            entry.export_status == FormatCapabilityStatus::UnsupportedReject, "Unimplemented adapter must reject");
    }
    TEST_ASSERT(!FormatCapabilityRegistry::IsFormatSupported("SVG"), "No SVG parser exists");
    TEST_ASSERT(!FormatCapabilityRegistry::IsFormatSupported("PIKDRAW_JSON"), "IPC is not a source format");

    g_tests_passed++;
    std::cout << "[PASS] TestFormatMatrixAndFixtures\n";
}

// --------------------------------------------------------------------
// Test 7: AppContainer Canaries Execution or Truthful Gate Report
// --------------------------------------------------------------------
void TestAppContainerSecurityCanaries() {
    if (RunLiveContainment()==0) ++g_tests_passed;
    else ++g_tests_failed; // No skip/profile/transport failure is a containment pass.

}

void TestCodecRejectsMalformedFrames() {
    ImportPayloadRequest request{{"doc",1,2,3},"SVG",{1,2,3}};
    const auto bytes=SerializeImportRequest(request,7);
    TEST_ASSERT(DeserializeImportRequest(bytes).has_value(), "Valid request roundtrip");
    for (size_t size=0;size<bytes.size();++size)
        TEST_ASSERT(!DeserializeImportRequest(std::span(bytes).first(size)), "Every truncated request must reject");
    auto trailing=bytes; trailing.push_back(0);
    TEST_ASSERT(!DeserializeImportRequest(trailing), "Trailing frame bytes reject");
    IpcHeader huge{}; huge.message_type=static_cast<uint16_t>(MessageType::ImportRequest);
    huge.payload_bytes=UINT32_MAX;
    std::vector<uint8_t> malformed(sizeof(huge)); std::memcpy(malformed.data(),&huge,sizeof(huge));
    TEST_ASSERT(!DeserializeImportRequest(malformed), "Oversize rejects before allocation");
    VectorGraphicObject object; object.id="shape"; object.depth=1; object.fill_color={0.1,0.2,0.3,0.4};
    ImportPayloadResponse response{{"doc",1,2,3},true,"",{object},{}};
    auto decoded=DeserializeImportResponse(SerializeImportResponse(response,8));
    TEST_ASSERT(decoded && decoded->objects[0].fill_color.a==0.4, "Wire must retain represented RGBA metadata");
    response.objects.push_back(object);
    bool rejected=false; try { (void)SerializeImportResponse(response,8); } catch (const std::invalid_argument&) { rejected=true; }
    TEST_ASSERT(rejected, "Duplicate IDs reject before serialization");
    const std::array<uint8_t, 3> v2_payload{0x10, 0x20, 0x30};
    const auto v2_packet = SerializeV2Frame(MessageType::V2ProposalChunk, 44, v2_payload);
    const auto v2_decoded = DeserializeV2Frame(v2_packet);
    TEST_ASSERT(v2_decoded && v2_decoded->header.sequence_id == 44 &&
                v2_decoded->payload == std::vector<uint8_t>(v2_payload.begin(), v2_payload.end()),
                "V2 bounded frame roundtrip");
    TEST_ASSERT(!PayloadValidator::ValidateHeader(v2_decoded->header).IsOk(),
                "V1 validator rejects v2 frame without downgrade");
    for (size_t size = 0; size < v2_packet.size(); ++size) {
        TEST_ASSERT(!DeserializeV2Frame(std::span(v2_packet).first(size)),
                    "Every truncated v2 frame rejects");
    }
    IpcHeader oversized_v2{};
    oversized_v2.wire_version = IPC_WIRE_VERSION_V2;
    oversized_v2.message_type = static_cast<uint16_t>(MessageType::V2ProposalChunk);
    oversized_v2.payload_bytes = static_cast<uint32_t>(IPC_V2_MAX_FRAME_BYTES + 1);
    TEST_ASSERT(!ValidateV2FrameHeader(oversized_v2), "V2 oversized frame rejected before allocation");
    bool v2_oversize_threw = false;
    try {
        std::vector<uint8_t> too_large(IPC_V2_MAX_FRAME_BYTES + 1);
        (void)SerializeV2Frame(MessageType::V2ProposalChunk, 45, too_large);
    } catch (const std::invalid_argument&) {
        v2_oversize_threw = true;
    }
    TEST_ASSERT(v2_oversize_threw, "V2 serializer refuses payload above frame limit");
    ++g_tests_passed; std::cout << "[PASS] TestCodecRejectsMalformedFrames\n";
}

void TestV2ProposalStreams() {
    auto worker_path = std::filesystem::current_path() / "spike_worker.exe";
    if (!std::filesystem::exists(worker_path)) {
        worker_path = std::filesystem::current_path() / "build" / "win-dev" /
            "platform" / "workers" / "spike_worker.exe";
    }
    WorkerLaunchConfig config;
    config.worker_executable_path = worker_path.wstring();
    config.use_appcontainer = false;
    config.trusted_test_worker = true;
    WorkerProcessHost host;
    const std::array<uint8_t, 0> empty_payload{};
    const auto request = SerializeV2Frame(
        MessageType::V2ImportBegin, 50, empty_payload);

    config.arguments = {L"--test-v2-stream-good"};
    TEST_ASSERT(host.Launch(config), "Launch valid v2 stream worker");
    uint32_t frames_seen = 0;
    uint64_t bytes_seen = 0;
    const bool complete = host.TransactV2ProposalStream(
        request,
        std::chrono::seconds(2),
        5,
        2,
        [&](const IpcV2Frame& frame) {
            ++frames_seen;
            bytes_seen += frame.payload.size();
            return frame.header.sequence_id == 49 + frames_seen;
        });
    TEST_ASSERT(complete && frames_seen == 2 && bytes_seen == 5,
                "V2 stream accepts ordered chunks and terminal frame");
    host.Terminate();

    config.arguments = {L"--test-v2-stream-gap"};
    TEST_ASSERT(host.Launch(config), "Launch out-of-order v2 fixture");
    TEST_ASSERT(!host.TransactV2ProposalStream(
                    request, std::chrono::seconds(2), 16, 4, {}),
                "V2 stream rejects a sequence gap");
    TEST_ASSERT(std::string_view(host.LastTransportFailure()) == "invalid v2 response header/order",
                "Sequence failure is surfaced explicitly");

    config.arguments = {L"--test-v2-stream-oversize"};
    TEST_ASSERT(host.Launch(config), "Launch oversized v2 fixture");
    TEST_ASSERT(!host.TransactV2ProposalStream(
                    request, std::chrono::seconds(2), 16, 4, {}),
                "V2 stream rejects oversized header before body allocation");

    config.arguments = {L"--test-v2-stream-quota"};
    TEST_ASSERT(host.Launch(config), "Launch v2 quota fixture");
    TEST_ASSERT(!host.TransactV2ProposalStream(
                    request, std::chrono::seconds(2), 2, 4, {}),
                "V2 stream rejects response above total-byte quota");

    config.arguments = {L"--test-v2-stream-no-end"};
    TEST_ASSERT(host.Launch(config), "Launch incomplete v2 fixture");
    TEST_ASSERT(!host.TransactV2ProposalStream(
                    request, std::chrono::seconds(2), 16, 4, {}),
                "V2 stream rejects EOF before terminal frame");
    TEST_ASSERT(std::string_view(host.LastTransportFailure()) == "v2 response header failed/timeout",
                "Incomplete stream failure is surfaced explicitly");

    config.arguments = {L"--test-v2-stream-good"};
    TEST_ASSERT(host.Launch(config), "Launch v2 consumer-failure fixture");
    TEST_ASSERT(!host.TransactV2ProposalStream(
                    request, std::chrono::seconds(2), 16, 4,
                    [](const IpcV2Frame&) { return false; }),
                "V2 stream stops when the bounded staging consumer rejects a frame");

    ++g_tests_passed;
    std::cout << "[PASS] TestV2ProposalStreams\n";
}
void TestSourceSnapshotBroker() {
    TemporarySourceFile source;
    constexpr uint64_t source_bytes = 3 * 1024 * 1024 + 37;
    TEST_ASSERT(CreateDeterministicSource(source, source_bytes), "Create synthetic source file");

    SourceSnapshotFailure failure = SourceSnapshotFailure::None;
    DWORD system_error = ERROR_SUCCESS;
    auto snapshot = SourceSnapshot::Create(source.path, source_bytes, &failure, &system_error);
    TEST_ASSERT(snapshot && snapshot->IsValid(), "Create bounded source snapshot");
    TEST_ASSERT(snapshot->SizeBytes() == source_bytes, "Snapshot exact byte count");
    TEST_ASSERT(std::any_of(snapshot->Token().begin(), snapshot->Token().end(),
                            [](uint8_t value) { return value != 0; }),
                "Snapshot token is nonzero");

    uint64_t bytes_read = 0;
    SourceDigest digest{};
    TEST_ASSERT(HashReadOnlyHandle(
                    snapshot->ReadHandle(), source_bytes, bytes_read, digest, &system_error),
                "Hash staged source handle");
    TEST_ASSERT(bytes_read == source_bytes && digest == snapshot->Sha256(),
                "Snapshot hash matches staged bytes");

    auto oversized = SourceSnapshot::Create(source.path, source_bytes - 1, &failure, &system_error);
    TEST_ASSERT(!oversized && failure == SourceSnapshotFailure::SourceTooLarge,
                "Reject over-profile source before worker launch");

    ++g_tests_passed;
    std::cout << "[PASS] TestSourceSnapshotBroker\n";
}
void TestTrustedWorkerLifecycleAndDeadlines() {
    // Explicitly no AppContainer: this proves transport/lifecycle only.
    WorkerLaunchConfig config;
    auto worker_path = std::filesystem::current_path() / "spike_worker.exe";
    if (!std::filesystem::exists(worker_path)) {
        worker_path = std::filesystem::current_path() / "build" / "win-dev" / "platform" / "workers" / "spike_worker.exe";
    }
    config.worker_executable_path = worker_path.wstring();
    config.use_appcontainer = false;
    WorkerProcessHost host;
    TEST_ASSERT(!host.Launch(config), "Unsandboxed launch requires trusted test opt-in");
    config.trusted_test_worker=true;
    TEST_ASSERT(host.Launch(config), "Trusted fixture worker launch");
    IpcHeader ping{}; ping.message_type=static_cast<uint16_t>(MessageType::Ping); ping.sequence_id=15;
    auto packet=std::span(reinterpret_cast<const uint8_t*>(&ping),sizeof(ping));
    TEST_ASSERT(host.TransactBinaryPayload(packet,std::chrono::seconds(2)), "Actual child ping/pong");
    ImportPayloadRequest import{{"doc",1,1,1},"SVG",{1,2,3}};
    auto result=host.TransactBinaryPayload(SerializeImportRequest(import,16),std::chrono::seconds(2));
    TEST_ASSERT(result, "Actual child import diagnostic response");
    auto decoded=DeserializeImportResponse(*result);
    TEST_ASSERT(decoded && !decoded->success && decoded->objects.empty(), "Unsupported input cannot fabricate geometry");

    TemporarySourceFile synthetic_source;
    constexpr uint64_t synthetic_source_bytes = 3 * 1024 * 1024 + 37;
    TEST_ASSERT(CreateDeterministicSource(synthetic_source, synthetic_source_bytes),
                "Create inherited-handle probe source");
    SourceSnapshotFailure snapshot_failure = SourceSnapshotFailure::None;
    auto source_snapshot = SourceSnapshot::Create(
        synthetic_source.path, synthetic_source_bytes, &snapshot_failure);
    TEST_ASSERT(source_snapshot && source_snapshot->IsValid(), "Stage source for child process");
    host.Terminate();
    config.source_snapshot = &*source_snapshot;
    config.arguments = {
        L"--test-source-probe",
        L"--test-source-size",
        std::to_wstring(source_snapshot->SizeBytes()),
        L"--test-source-sha256",
        DigestArgument(source_snapshot->Sha256())
    };
    TEST_ASSERT(host.Launch(config), "Launch child with exact inherited source handle");
    TEST_ASSERT(host.WaitForExit(std::chrono::seconds(3)), "Wait for bounded source probe exit");
    TEST_ASSERT(host.LastWorkerExitCode() == 0, "Child read exact bytes and write was denied");
    host.Terminate();
    config.source_snapshot = nullptr;
    config.arguments.clear();
    host.Terminate(); TEST_ASSERT(!host.IsAlive(), "Child terminated");
    config.arguments={L"--test-stall-before-read"};
    TEST_ASSERT(host.Launch(config), "Stalling worker launch");
    import.raw_input_bytes.resize(1024*1024);
    const auto start=std::chrono::steady_clock::now();
    TEST_ASSERT(!host.TransactBinaryPayload(SerializeImportRequest(import,17),std::chrono::milliseconds(100)), "Blocked write times out");
    TEST_ASSERT(!host.IsAlive(), "Timeout terminates worker");
    TEST_ASSERT(std::chrono::steady_clock::now()-start<std::chrono::seconds(3), "Write deadline is bounded");
    config.arguments={L"--test-truncated-response"};
    TEST_ASSERT(host.Launch(config), "Truncated worker launch");
    TEST_ASSERT(!host.TransactBinaryPayload(packet,std::chrono::seconds(1)), "Partial frame/child exit rejects");
    TEST_ASSERT(!host.IsAlive(), "Partial response terminates worker scope");
    ++g_tests_passed; std::cout << "[PASS] TestTrustedWorkerLifecycleAndDeadlines (no containment claim)\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--probe-corpus") {
        return RunLocalSourceCorpusProbe(std::filesystem::path(argv[2]));
    }
    std::cout << "=== Starting Workers Tests ===\n" << std::flush;
    bool run_live = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--live-containment") run_live = true;
    }

    std::cout << "===================================================\n" << std::flush;
    std::cout << "PikDraw Spike C Tests: Native Windows Worker & Interop\n" << std::flush;
    std::cout << "===================================================\n" << std::flush;

    TestTransformVectors();
    TestIpcBoundaryValidation();
    TestStaleProposalRejection();
    TestJobObjectConfiguration();
    TestCommandLineQuoting();
    TestFormatMatrixAndFixtures();
    TestCodecRejectsMalformedFrames();
    TestV2ProposalStreams();
    TestSourceSnapshotBroker();
    TestTrustedWorkerLifecycleAndDeadlines();

    if (run_live) {
        TestAppContainerSecurityCanaries();
    } else {
        std::cout << "[GATE] containment=NOT_MEASURED; local protocol and lifecycle verified.\n";
    }

    std::cout << "---------------------------------------------------\n";
    std::cout << "Summary: " << g_tests_passed << " passed, " << g_tests_failed << " failed.\n";
    std::cout << "===================================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
