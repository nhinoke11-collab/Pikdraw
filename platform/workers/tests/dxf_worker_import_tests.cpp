#include "pikdraw/interop/adapters/dxf_importer.hpp"
#include "pikdraw/interop/adapters/dxf_worker_adapter.hpp"
#include "pikdraw/workers/process_launcher.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace pikdraw::interop;
using namespace pikdraw::workers;
namespace fs = std::filesystem;

namespace {

void Require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TemporarySource final {
  fs::path path;
  ~TemporarySource() {
    std::error_code ignored;
    if (!path.empty())
      fs::remove(path, ignored);
  }

  bool Write(std::string_view text) {
    path = fs::temp_directory_path() /
           ("pikdraw-dxf-worker-" + std::to_string(GetCurrentProcessId()) +
            "-" + std::to_string(GetTickCount64()) + ".dxf");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
  }
};

struct ImportCapture final {
  size_t callbacks{0};
  bool saw_end{false};
  bool records_valid{true};
  std::vector<ImportRecordValue> records;
};

uint16_t U16(std::span<const uint8_t> bytes, size_t at) {
  return static_cast<uint16_t>(bytes[at]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[at + 1]) << 8);
}

uint32_t U32(std::span<const uint8_t> bytes, size_t at) {
  return static_cast<uint32_t>(bytes[at]) |
         (static_cast<uint32_t>(bytes[at + 1]) << 8) |
         (static_cast<uint32_t>(bytes[at + 2]) << 16) |
         (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

bool Capture(const IpcV2Frame &frame, ImportCapture &capture) {
  ++capture.callbacks;
  const auto type = static_cast<MessageType>(frame.header.message_type);
  const auto packet =
      SerializeV2Frame(type, frame.header.sequence_id, frame.payload);
  if (type == MessageType::V2ProposalChunk) {
    const auto chunk = DeserializeV2ProposalChunk(packet);
    if (!chunk)
      return capture.records_valid = false;
    const auto bytes = std::span<const uint8_t>(chunk->canonical_record_bytes);
    size_t offset = 0;
    for (uint32_t i = 0; i < chunk->record_count; ++i) {
      if (bytes.size() - offset < 8)
        return capture.records_valid = false;
      const uint16_t raw = U16(bytes, offset);
      const uint16_t reserved = U16(bytes, offset + 2);
      const uint32_t size = U32(bytes, offset + 4);
      offset += 8;
      if (reserved != 0 || size > bytes.size() - offset || raw < 1 || raw > 7 ||
          i >= chunk->record_types.size() || chunk->record_types[i] != raw)
        return capture.records_valid = false;
      const auto value = DeserializeImportRecord(
          static_cast<ImportRecordType>(raw), bytes.subspan(offset, size));
      if (!value)
        return capture.records_valid = false;
      capture.records.push_back(*value);
      offset += size;
    }
    return offset == bytes.size() && capture.records_valid;
  }
  if (type == MessageType::V2ImportEnd) {
    const auto end = DeserializeV2ImportEnd(packet);
    if (!end || !end->fidelity_report_complete)
      return capture.records_valid = false;
    capture.saw_end = true;
    return true;
  }
  return capture.records_valid = false;
}

WorkerLaunchConfig ConfigFor(const SourceSnapshot &source) {
  auto executable = fs::current_path() / "spike_worker.exe";
  if (!fs::exists(executable))
    executable = fs::current_path() / "build" / "win-dev" / "platform" /
                 "workers" / "spike_worker.exe";
  WorkerLaunchConfig config;
  config.worker_executable_path = executable.wstring();
  config.working_directory = fs::current_path().wstring();
  config.use_appcontainer = false;
  config.trusted_test_worker = true;
  config.source_snapshot = &source;
  return config;
}

V2ProposalStreamLimits Limits() {
  return {20 * 1024 * 1024, 16 * 1024 * 1024, 40002, 64, 65};
}

struct RunResult final {
  bool complete{false};
  std::string failure;
  ImportCapture capture;
};

RunResult RunImport(const SourceSnapshot &source, std::string format = "DXF",
                    std::string profile = "dxf-entities-v1") {
  V2ImportBegin begin;
  begin.job.document_id = "dxf-worker-import-test-document";
  begin.job.epoch = 3;
  begin.job.base_revision = 7;
  begin.job.generation = 19;
  begin.job.source_token = source.Token();
  begin.source_bytes = source.SizeBytes();
  begin.source_sha256 = source.Sha256();
  begin.format_id = std::move(format);
  begin.profile_id = std::move(profile);

  WorkerProcessHost host;
  const auto config = ConfigFor(source);
  Require(fs::exists(config.worker_executable_path),
          "spike_worker executable was not found beside this test");
  Require(host.Launch(config), "could not launch isolated DXF test worker");
  Require(host.NegotiateV2(IPC_V2_SUPPORTED_FEATURES, std::chrono::seconds(3)),
          "DXF test worker did not negotiate C6 v2");
  RunResult result;
  result.complete = host.TransactV2ProposalStream(
      SerializeV2ImportBegin(begin, 1), std::chrono::seconds(10), Limits(),
      [&](const IpcV2Frame &frame) { return Capture(frame, result.capture); });
  result.failure = host.LastTransportFailure();
  return result;
}

std::optional<SourceSnapshot> Snapshot(const fs::path &path) {
  return SourceSnapshot::Create(path.wstring(), DxfImporter::kMaxSourceBytes);
}

constexpr std::string_view kValidDxf = R"DXF(0
SECTION
2
HEADER
9
$INSUNITS
70
4
0
ENDSEC
0
SECTION
2
TABLES
0
TABLE
2
LAYER
70
2
0
LAYER
2
CUT
70
0
62
1
6
CONTINUOUS
0
LAYER
2
ENGRAVE
70
0
62
5
6
CONTINUOUS
0
ENDTAB
0
ENDSEC
0
SECTION
2
ENTITIES
0
LINE
5
10
8
CUT
10
0
20
0
30
0
11
20
21
0
31
0
0
LWPOLYLINE
5
11
8
CUT
90
3
70
1
10
20
20
0
42
0.414213562373095
10
30
20
10
10
20
20
20
0
ARC
5
12
8
ENGRAVE
10
50
20
50
30
0
40
10
50
0
51
90
0
CIRCLE
5
13
8
ENGRAVE
10
80
20
50
30
0
40
5
0
ENDSEC
0
EOF
)DXF";

void TestIsolatedWorkerProposal() {
  TemporarySource file;
  Require(file.Write(kValidDxf), "could not write synthetic DXF test source");
  const auto source = Snapshot(file.path);
  Require(source && source->IsValid(), "could not stage synthetic DXF source");
  const auto result = RunImport(*source);
  Require(result.complete, "isolated DXF worker proposal did not complete");
  Require(
      result.capture.records_valid && result.capture.saw_end,
      "worker stream contained invalid typed records or no terminal report");
  Require(result.capture.records.size() == 8,
          "DXF worker proposal record count changed");
  Require(std::holds_alternative<ImportPageRecord>(result.capture.records[0]),
          "DXF proposal did not begin with a page");
  Require(
      std::holds_alternative<ImportLayerRecord>(result.capture.records[1]) &&
          std::holds_alternative<ImportLayerRecord>(result.capture.records[2]),
      "DXF source layers were not retained");
  Require(std::holds_alternative<ImportFidelityReportRecord>(
              result.capture.records.back()),
          "DXF proposal omitted its terminal fidelity report");
  const auto &cut = std::get<ImportLayerRecord>(result.capture.records[1]);
  const auto &engrave = std::get<ImportLayerRecord>(result.capture.records[2]);
  Require(cut.name == "CUT" &&
              cut.object_local_ids == std::vector<uint32_t>{1, 2} &&
              engrave.name == "ENGRAVE" &&
              engrave.object_local_ids == std::vector<uint32_t>{3, 4},
          "DXF layer membership or object order was lost");
  const auto &polyline = std::get<ImportPathRecord>(result.capture.records[4]);
  Require(polyline.source_id == "dxf-object-11" &&
              polyline.elements.size() >= 4 &&
              polyline.elements[1].verb == ImportPathVerb::Arc,
          "DXF polyline bulge was not preserved as an editable arc");
  const auto &report =
      std::get<ImportFidelityReportRecord>(result.capture.records.back());
  Require(report.coverage_profile == "dxf-entities-v1" &&
              report.production == ImportProduction::NotAssessed,
          "DXF proposal reported an incorrect profile or production status");
}

void TestUnsupportedEntityRejectsAtomically() {
  constexpr std::string_view unsupported = R"DXF(0
SECTION
2
HEADER
9
$INSUNITS
70
4
0
ENDSEC
0
SECTION
2
TABLES
0
TABLE
2
LAYER
70
1
0
LAYER
2
0
70
0
62
7
6
CONTINUOUS
0
ENDTAB
0
ENDSEC
0
SECTION
2
ENTITIES
0
ELLIPSE
8
0
10
0
20
0
11
10
21
0
40
0.5
0
ENDSEC
0
EOF
)DXF";
  TemporarySource file;
  Require(file.Write(unsupported), "could not write unsupported DXF source");
  const auto source = Snapshot(file.path);
  Require(source && source->IsValid(),
          "could not stage unsupported DXF source");
  const auto result = RunImport(*source);
  Require(
      !result.complete && result.capture.callbacks == 0 &&
          result.failure == "worker returned v2 error response",
      "unsupported DXF emitted partial records or lacked a structured error");

  const auto wrong_profile = RunImport(*source, "DXF", "dxf-full-v1");
  Require(!wrong_profile.complete && wrong_profile.capture.callbacks == 0 &&
              wrong_profile.failure == "unsupported format import profile",
          "worker accepted an unregistered DXF profile");
}

struct CorpusRow final {
  uint64_t bytes{0};
  bool parser_executed{false};
  bool accepted{false};
  bool semantic_records_valid{false};
  std::string result;
};

void WriteCorpusReport(const fs::path &path,
                       const std::vector<CorpusRow> &rows) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(out),
          "could not create sanitized local DXF report");
  const auto count = [&](auto predicate) {
    return std::count_if(rows.begin(), rows.end(), predicate);
  };
  out << "{\"schema_version\":1,\"profile\":\"dxf-entities-v1\","
         "\"dxf_files\":"
      << rows.size() << ",\"parser_runs\":"
      << count([](const CorpusRow &r) { return r.parser_executed; })
      << ",\"accepted\":"
      << count([](const CorpusRow &r) { return r.accepted; })
      << ",\"results\":[";
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i)
      out << ',';
    const auto &row = rows[i];
    out << "{\"ordinal\":" << i + 1 << ",\"source_bytes\":" << row.bytes
        << ",\"parser_executed\":" << (row.parser_executed ? "true" : "false")
        << ",\"accepted\":" << (row.accepted ? "true" : "false")
        << ",\"semantic_records_valid\":"
        << (row.semantic_records_valid ? "true" : "false") << ",\"result\":\""
        << row.result << "\"}";
  }
  out << "]}\n";
  Require(static_cast<bool>(out),
          "could not finish sanitized local DXF report");
}

int ProbeCorpus(const fs::path &corpus) {
  Require(fs::exists(corpus) && fs::is_directory(corpus),
          "local corpus directory does not exist");
  std::vector<fs::path> files;
  for (fs::recursive_directory_iterator
           it(corpus, fs::directory_options::skip_permission_denied),
       end;
       it != end; ++it) {
    std::error_code error;
    if (it->is_symlink(error) || error || !it->is_regular_file(error) || error)
      continue;
    std::string extension = it->path().extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".dxf")
      files.push_back(it->path());
  }
  std::vector<CorpusRow> rows;
  for (const auto &path : files) {
    CorpusRow row;
    std::error_code error;
    row.bytes = fs::file_size(path, error);
    const auto source =
        error ? std::optional<SourceSnapshot>{} : Snapshot(path);
    if (!source || !source->IsValid()) {
      row.result = "source_snapshot_rejected";
      rows.push_back(std::move(row));
      continue;
    }
    const auto result = RunImport(*source);
    row.accepted = result.complete;
    row.parser_executed =
        result.complete ||
        (row.bytes <= DxfImporter::kMaxSourceBytes &&
         result.failure == "worker returned v2 error response");
    row.semantic_records_valid =
        result.capture.records_valid && result.capture.saw_end;
    row.result = result.complete
                     ? "accepted"
                     : (row.parser_executed ? "rejected_by_profile"
                                            : "worker_protocol_failure");
    if (result.complete)
      Require(row.semantic_records_valid,
              "actual DXF proposal failed typed host semantic validation");
    else
      Require(result.capture.callbacks == 0,
              "actual DXF rejection leaked partial proposal records");
    rows.push_back(std::move(row));
  }
  WriteCorpusReport(corpus / "test-reports" / "dxf-worker-import.json", rows);
  std::cout << "DXF corpus probe: " << rows.size() << " files; parser runs="
            << std::count_if(
                   rows.begin(), rows.end(),
                   [](const CorpusRow &r) { return r.parser_executed; })
            << "; accepted="
            << std::count_if(rows.begin(), rows.end(),
                             [](const CorpusRow &r) { return r.accepted; })
            << "; report is sanitized and contains no source names or hashes\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--probe-corpus")
      return ProbeCorpus(fs::path(argv[2]));
    Require(
        argc == 1,
        "usage: test_pikdraw_worker_dxf_import [--probe-corpus <cdr-folder>]");
    TestIsolatedWorkerProposal();
    TestUnsupportedEntityRejectsAtomically();
    std::cout << "DXF isolated worker import tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "DXF worker import tests failed: " << error.what() << '\n';
    return 1;
  }
}
