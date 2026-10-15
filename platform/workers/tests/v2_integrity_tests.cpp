#include "pikdraw/workers/process_launcher.hpp"

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
    auto executable = std::filesystem::current_path() / "spike_worker.exe";
    if (!std::filesystem::exists(executable)) {
        executable = std::filesystem::current_path() / "build" / "win-dev" /
            "platform" / "workers" / "spike_worker.exe";
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
    begin.format_id = "SVG";
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
