#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <securityappcontainer.h>
#include "pikdraw/workers/process_launcher.hpp"
#include "pikdraw/workers/canary_definitions.hpp"
#include "pikdraw/workers/source_snapshot.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <vector>
#include <algorithm>
#include <vector>

using namespace pikdraw::workers;
using namespace pikdraw::interop;
namespace fs=std::filesystem;
namespace {
std::string escaped(const std::string& input) {
    std::string result;
    for (unsigned char c:input) {
        if (c=='"' || c=='\\') { result+='\\'; result+=static_cast<char>(c); }
        else if (c=='\n') result+="\\n";
        else if (c=='\r') result+="\\r";
        else if (c=='\t') result+="\\t";
        else if (c<32) result+='?';
        else result+=static_cast<char>(c);
    }
    return result;
}
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
struct SocketGuard {
    SOCKET socket{INVALID_SOCKET};
    ~SocketGuard() { if (socket!=INVALID_SOCKET) closesocket(socket); }
};
struct WinsockGuard {
    WinsockGuard() { WSADATA data{}; require(WSAStartup(MAKEWORD(2,2),&data)==0,"WSAStartup control failed"); }
    ~WinsockGuard() { WSACleanup(); }
};
void ping(WorkerProcessHost& host) {
    IpcHeader header{}; header.message_type=static_cast<uint16_t>(MessageType::Ping); header.sequence_id=987;
    const auto packet=std::span(reinterpret_cast<const uint8_t*>(&header),sizeof(header));
    require(host.TransactBinaryPayload(packet,std::chrono::seconds(5)).has_value(),"Live worker did not ping/pong");
}
CanaryPayloadResponse request(WorkerProcessHost& host,CanaryPayloadRequest input,uint32_t sequence) {
    auto frame=host.TransactBinaryPayload(SerializeCanaryRequest(input,sequence),std::chrono::seconds(5));
    if (!frame) throw std::runtime_error("Canary type="+std::to_string(input.canary_type)+" sequence="+std::to_string(sequence)+
        " transport="+host.LastTransportFailure()+" worker_exit="+std::to_string(host.LastWorkerExitCode())+"; no pass");
    auto response=DeserializeCanaryResponse(*frame);
    require(response && response->canary_type==input.canary_type && response->action_attempted,
        "Canary action was not executed");
    return *response;
}
struct LocalProbeRecord {
    std::string id;
    std::string extension;
    std::string result;
    uint64_t source_bytes{0};
    uint64_t elapsed_ms{0};
};

struct ScratchDirectoryGuard {
    fs::path path;
    ~ScratchDirectoryGuard() {
        std::error_code ignored;
        if (!path.empty()) fs::remove_all(path, ignored);
    }
};

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

const char* SnapshotFailureName(SourceSnapshotFailure failure) {
    switch (failure) {
        case SourceSnapshotFailure::InvalidLimit: return "invalid_profile_limit";
        case SourceSnapshotFailure::SourceOpenFailed: return "source_open_failed";
        case SourceSnapshotFailure::SourceIsReparsePoint: return "source_reparse_rejected";
        case SourceSnapshotFailure::SourceIsNotRegularFile: return "source_not_regular";
        case SourceSnapshotFailure::SourceTooLarge: return "source_limit_exceeded";
        case SourceSnapshotFailure::StagingCreateFailed: return "snapshot_stage_failed";
        case SourceSnapshotFailure::SourceReadFailed: return "source_read_failed";
        case SourceSnapshotFailure::StagingWriteFailed: return "snapshot_write_failed";
        case SourceSnapshotFailure::SnapshotVerificationFailed: return "snapshot_verify_failed";
        case SourceSnapshotFailure::CryptographicFailure: return "sha256_failed";
        case SourceSnapshotFailure::None: return "none";
    }
    return "unknown";
}

bool HasProbeExtension(const std::string& extension) {
    return extension == ".cdr" || extension == ".ai" || extension == ".pdf" ||
        extension == ".eps" || extension == ".dxf" || extension == ".svg" ||
        extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

} // namespace

int RunLiveContainment() {
    const auto evidence=fs::current_path()/"live-containment-report.json";
    std::ostringstream rows;
    int passed=0;
    bool source_handle_read_only=false;
    const auto write_report=[&](const char* status,const std::string& error) {
        std::ofstream out(evidence,std::ios::binary);
        require(static_cast<bool>(out),"Cannot write containment evidence");
        out << "{\"status\":\"" << status << "\",\"denied_canaries\":" << passed
            << ",\"required_canaries\":5,\"source_handle_read_only\":" << (source_handle_read_only?"true":"false")
            << ",\"full_spike_accepted\":false,\"error\":\"" << escaped(error)
            << "\",\"limits\":\"One Windows local run; no format fidelity, parser corpus, CPU/memory quota or full Spike C acceptance\",\"cases\":["
            << rows.str() << "]}\n";
    };
    try {
        WinsockGuard winsock;
        SocketGuard listener;
        listener.socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        require(listener.socket!=INVALID_SOCKET,"Cannot create positive-control socket");
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        require(bind(listener.socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0 &&
            listen(listener.socket,4)==0,"Cannot bind/listen positive-control loopback endpoint");
        int length=sizeof(address);
        require(getsockname(listener.socket,reinterpret_cast<sockaddr*>(&address),&length)==0,"Cannot obtain listener port");

        const auto suffix=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        const auto scratch=fs::current_path()/(L"live-canary-"+suffix);
        require(fs::create_directory(scratch),"Canary scratch must be newly created");
        ScratchDirectoryGuard scratch_guard{scratch};
        const auto bin=scratch/L"bin";
        require(fs::create_directory(bin),"Cannot create owned worker directory");
        const auto profile=L"PikDraw.SpikeC."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
        PSID derived=nullptr;
        const HRESULT derived_result=DeriveAppContainerSidFromAppContainerName(profile.c_str(),&derived);
        if (FAILED(derived_result) || !derived) {
            write_report("not_measured","Cannot derive unique test profile SID"); return 2;
        }
        ScopedSid sid(derived);
        // Only the newly created bin directory is granted. Never grant a parent/user tree.
        require(AppContainerProfileGuard::GrantDirectoryAccess(bin.wstring(),sid.Get()),"Cannot grant owned bin RX");
        const auto executable=bin/L"spike_worker_test.exe";
        auto worker_src = fs::current_path() / L"spike_worker_test.exe";
        if (!fs::exists(worker_src)) {
            worker_src = fs::current_path() / L"build/win-dev/platform/workers/spike_worker_test.exe";
        }
        require(fs::copy_file(worker_src, executable), "Cannot copy own static worker");
        const auto read_probe=scratch/L"ungranted-read.txt";
        { std::ofstream file(read_probe,std::ios::binary); file << "PIKDRAW_SYNTHETIC_CANARY\n"; require(static_cast<bool>(file),"Cannot create read control"); }
        const auto write_probe=scratch/L"ungranted-write.txt";

        // This handle is intentionally inheritable but excluded from HANDLE_LIST.
        // Padding puts its numeric value beyond this small worker's normal handle table.
        std::vector<ScopedHandle> padding;
        for (int i=0;i<2048;++i) {
            padding.emplace_back(CreateEventW(nullptr,FALSE,FALSE,nullptr));
            require(padding.back().IsValid(),"Cannot allocate bounded handle-control padding");
        }
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        ScopedHandle ungranted(CreateEventW(&attributes,FALSE,FALSE,nullptr));
        DWORD handle_flags=0;
        require(ungranted.IsValid() && GetHandleInformation(ungranted.Get(),&handle_flags) &&
            (handle_flags&HANDLE_FLAG_INHERIT),"Host handle control is not valid/inheritable");

        WorkerLaunchConfig control_config;
        control_config.worker_executable_path=executable.wstring(); control_config.working_directory=bin.wstring();
        control_config.use_appcontainer=false; control_config.trusted_test_worker=true;
        control_config.test_inherited_handle=ungranted.Get();
        control_config.job_limits.active_process_limit=2; control_config.job_limits.prevent_child_processes=false;
        WorkerProcessHost control;
        require(control.Launch(control_config),"Trusted canary control failed to launch"); ping(control);
        WorkerLaunchConfig sandbox_config=control_config;
        sandbox_config.use_appcontainer=true; sandbox_config.appcontainer_profile_name=profile;
        sandbox_config.test_inherited_handle=INVALID_HANDLE_VALUE;
        sandbox_config.job_limits.active_process_limit=1; sandbox_config.job_limits.prevent_child_processes=true;
        WorkerProcessHost sandbox;
        if (!sandbox.Launch(sandbox_config)) {
            write_report("not_measured","AppContainer worker failed to launch; no denied action evidence"); return 2;
        }
        require(sandbox.GetAppContainer().IsValid() && EqualSid(sid.Get(),sandbox.GetAppContainer().GetSid()),
            "Worker AppContainer SID differs from granted unique SID");
        ping(sandbox);

        const auto selected_source = scratch / L"selected-source.bin";
        constexpr uint64_t selected_source_bytes = 20ULL * 1024ULL * 1024ULL + 19;
        {
            std::ofstream source(selected_source, std::ios::binary);
            require(static_cast<bool>(source), "Cannot create synthetic large source");
            std::vector<uint8_t> bytes(256 * 1024);
            uint64_t offset = 0;
            while (offset < selected_source_bytes) {
                const size_t count = static_cast<size_t>(
                    std::min<uint64_t>(bytes.size(), selected_source_bytes - offset));
                for (size_t i = 0; i < count; ++i) {
                    bytes[i] = static_cast<uint8_t>((offset + i) * 17 + 3);
                }
                source.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(count));
                require(static_cast<bool>(source), "Cannot write synthetic large source");
                offset += count;
            }
        }
        SourceSnapshotFailure snapshot_failure = SourceSnapshotFailure::None;
        auto snapshot = SourceSnapshot::Create(
            selected_source.wstring(), 64ULL * 1024ULL * 1024ULL, &snapshot_failure);
        require(snapshot && snapshot->SizeBytes() == selected_source_bytes,
                "Cannot create bounded large-source snapshot");

        const auto source_profile = profile + L".Source";
        PSID source_derived = nullptr;
        const HRESULT source_derived_result = DeriveAppContainerSidFromAppContainerName(source_profile.c_str(), &source_derived);
        require(SUCCEEDED(source_derived_result) && source_derived != nullptr,
                "Cannot derive source-probe AppContainer SID");
        ScopedSid source_sid(source_derived);
        require(AppContainerProfileGuard::GrantDirectoryAccess(bin.wstring(), source_sid.Get()),
                "Cannot grant source-probe worker RX");

        WorkerLaunchConfig source_config = sandbox_config;
        source_config.appcontainer_profile_name = source_profile;
        source_config.source_snapshot = &*snapshot;
        source_config.arguments = {
            L"--test-source-probe",
            L"--test-source-size",
            std::to_wstring(snapshot->SizeBytes()),
            L"--test-source-sha256",
            [&] {
                constexpr wchar_t digits[] = L"0123456789abcdef";
                std::wstring text;
                text.reserve(snapshot->Sha256().size() * 2);
                for (const uint8_t byte : snapshot->Sha256()) {
                    text.push_back(digits[byte >> 4]);
                    text.push_back(digits[byte & 0x0f]);
                }
                return text;
            }()
        };
        WorkerProcessHost source_worker;
        require(source_worker.Launch(source_config),
                "AppContainer worker with source capability failed to launch");
        require(source_worker.WaitForExit(std::chrono::seconds(10)) &&
                    source_worker.LastWorkerExitCode() == 0,
                "AppContainer could not read exact large source or write was not denied");
        source_worker.Terminate();
        source_handle_read_only = true;        // A Job quota probe can terminate the offending process; run it last.
        for (uint32_t type:std::array<uint32_t,5>{1,2,3,5,4}) {
            CanaryPayloadRequest input{}; input.canary_type=type;
            if (type==CanaryFileRead) input.target_path=WideToUtf8(read_probe.wstring());
            if (type==CanaryFileWrite) input.target_path=WideToUtf8(write_probe.wstring());
            if (type==CanaryNetworkOutbound) { input.network_address="127.0.0.1"; input.network_port=ntohs(address.sin_port); }
            if (type==CanaryChildProcess) input.target_path=WideToUtf8(executable.wstring());
            if (type==CanaryUngrantedHandle) input.test_handle_value=reinterpret_cast<uintptr_t>(ungranted.Get());
            const auto control_result=request(control,input,100+type);
            const bool control_ok=!control_result.denied_by_os && control_result.os_error_code==0;
            require(control_ok,"Positive control failed; denial would not prove sandbox policy");
            const auto result=request(sandbox,input,200+type);
            const uint32_t expected=type==CanaryNetworkOutbound ? WSAEACCES : type==CanaryUngrantedHandle ? ERROR_INVALID_HANDLE : ERROR_ACCESS_DENIED;
            const bool denied=result.denied_by_os && (result.os_error_code==expected ||
                (type==CanaryChildProcess && result.os_error_code==ERROR_NOT_ENOUGH_QUOTA));
            if (type>1) rows << ',';
            rows << "{\"type\":" << type << ",\"control_ok\":true,\"attempted\":true,\"denied\":" << (denied?"true":"false")
                 << ",\"os_error\":" << result.os_error_code << ",\"diagnostic\":\"" << escaped(result.diagnostic_message) << "\"}";
            if (denied) ++passed;
        }
        require(!fs::exists(write_probe),"Sandbox left an unauthorized write artifact");
        std::ifstream probe(read_probe,std::ios::binary); std::string value((std::istreambuf_iterator<char>(probe)),{});
        require(value=="PIKDRAW_SYNTHETIC_CANARY\n","Read probe contents changed");
        sandbox.Terminate(); control.Terminate();
        require(passed==5,"One or more live canaries did not produce an explicit OS denial; see per-case evidence");
        write_report("pass","");
        std::cout << "[PASS] Five live denied AppContainer/HANDLE_LIST/Job canaries; controls succeeded\n";
        return 0;
    } catch (const std::exception& error) {
        write_report("fail",error.what()); std::cerr << "[FAIL] Live containment: " << error.what() << '\n'; return 1;
    }
}
int RunLocalSourceCorpusProbe(const fs::path& corpus_root) {
    constexpr uint64_t max_source_bytes = 1024ULL * 1024ULL * 1024ULL;
    constexpr uint64_t max_corpus_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr size_t max_files = 256;
    std::error_code filesystem_error;
    if (!fs::is_directory(corpus_root, filesystem_error) || filesystem_error) {
        std::cerr << "[FAIL] Local corpus directory unavailable\n";
        return 2;
    }

    auto worker_source = fs::absolute("spike_worker_test.exe", filesystem_error);
    if (filesystem_error || !fs::exists(worker_source)) {
        worker_source = fs::absolute("build/win-dev/platform/workers/spike_worker_test.exe", filesystem_error);
    }
    if (filesystem_error || !fs::is_regular_file(worker_source)) {
        std::cerr << "[FAIL] Worker test executable unavailable\n";
        return 2;
    }

    const auto run_id = std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64());
    ScratchDirectoryGuard scratch{fs::temp_directory_path() / (L"PikDraw-Probe-" + run_id)};
    if (!fs::create_directory(scratch.path, filesystem_error) || filesystem_error) {
        std::cerr << "[FAIL] Cannot create private worker directory\n";
        return 2;
    }
    const auto worker_bin = scratch.path / L"bin";
    if (!fs::create_directory(worker_bin, filesystem_error) || filesystem_error) {
        std::cerr << "[FAIL] Cannot create worker execution directory\n";
        return 2;
    }
    const auto worker_executable = worker_bin / L"spike_worker_test.exe";
    fs::copy_file(worker_source, worker_executable, fs::copy_options::none, filesystem_error);
    if (filesystem_error) {
        std::cerr << "[FAIL] Cannot prepare worker executable\n";
        return 2;
    }

    std::vector<fs::path> files;
    const auto options = fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it(corpus_root, options, filesystem_error), end;
         it != end && files.size() <= max_files; it.increment(filesystem_error)) {
        if (filesystem_error) {
            filesystem_error.clear();
            continue;
        }
        const auto filename = it->path().filename().wstring();
        if (it->is_directory(filesystem_error) &&
            (filename == L"test-reports" || filename == L"_reports")) {
            it.disable_recursion_pending();
            continue;
        }
        const auto entry_status = it->symlink_status(filesystem_error);
        if (filesystem_error) {
            filesystem_error.clear();
            continue;
        }
        if (fs::is_directory(entry_status)) continue;
        const auto extension_wide = it->path().extension().wstring();
        std::string extension;
        extension.reserve(extension_wide.size());
        for (wchar_t c : extension_wide) {
            if (c > 0x7f) {
                extension.clear();
                break;
            }
            extension.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (HasProbeExtension(extension)) files.push_back(it->path());
    }
    if (files.size() > max_files) {
        std::cerr << "[FAIL] Corpus exceeds local probe limit\n";
        return 2;
    }
    std::sort(files.begin(), files.end(), [](const fs::path& left, const fs::path& right) {
        return left.generic_wstring() < right.generic_wstring();
    });
    if (files.empty()) {
        std::cerr << "[FAIL] No supported source samples found in local corpus\n";
        return 2;
    }

    std::vector<LocalProbeRecord> records;
    records.reserve(files.size());
    uint64_t corpus_bytes = 0;
    bool all_verified = true;
    for (size_t index = 0; index < files.size(); ++index) {
        LocalProbeRecord record;
        record.id = "sample-" + std::to_string(index + 1);
        const auto extension_wide = files[index].extension().wstring();
        for (wchar_t c : extension_wide) {
            if (c <= 0x7f) record.extension.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        const auto started = std::chrono::steady_clock::now();
        filesystem_error.clear();
        record.source_bytes = fs::file_size(files[index], filesystem_error);
        if (filesystem_error) {
            record.result = "source_stat_failed";
        } else if (record.source_bytes > max_source_bytes ||
                   record.source_bytes > max_corpus_bytes - corpus_bytes) {
            record.result = "resource_limit";
        } else {
            corpus_bytes += record.source_bytes;
            SourceSnapshotFailure snapshot_failure = SourceSnapshotFailure::None;
            auto snapshot = SourceSnapshot::Create(
                files[index].wstring(), max_source_bytes, &snapshot_failure);
            if (!snapshot) {
                record.result = SnapshotFailureName(snapshot_failure);
            } else {
                const auto profile = L"PikDraw.ImportProbe." +
                    std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(index + 1);
                PSID raw_sid = nullptr;
                const HRESULT derive = DeriveAppContainerSidFromAppContainerName(
                    profile.c_str(), &raw_sid);
                if (FAILED(derive) || raw_sid == nullptr) {
                    if (raw_sid) FreeSid(raw_sid);
                    record.result = "sandbox_sid_failed";
                } else {
                    ScopedSid sid(raw_sid);
                    if (!AppContainerProfileGuard::GrantDirectoryAccess(
                            worker_bin.wstring(), sid.Get())) {
                        record.result = "sandbox_acl_failed";
                    } else {
                        WorkerLaunchConfig config;
                        config.worker_executable_path = worker_executable.wstring();
                        config.working_directory = worker_bin.wstring();
                        config.use_appcontainer = true;
                        config.appcontainer_profile_name = profile;
                        config.source_snapshot = &*snapshot;
                        config.arguments = {
                            L"--test-source-probe",
                            L"--test-source-size",
                            std::to_wstring(snapshot->SizeBytes()),
                            L"--test-source-sha256",
                            DigestArgument(snapshot->Sha256())
                        };
                        WorkerProcessHost worker;
                        if (!worker.Launch(config)) {
                            record.result = "sandbox_launch_failed";
                        } else if (!worker.WaitForExit(std::chrono::seconds(60))) {
                            worker.Terminate();
                            record.result = "worker_timeout";
                        } else {
                            record.result = worker.LastWorkerExitCode() == 0
                                ? "read_only_verified" : "worker_read_or_write_probe_failed";
                            worker.Terminate();
                        }
                    }
                }
            }
        }
        record.elapsed_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
        if (record.result != "read_only_verified") all_verified = false;
        records.push_back(std::move(record));
    }

    const auto report_directory = corpus_root / L"test-reports";
    fs::create_directories(report_directory, filesystem_error);
    if (filesystem_error) {
        std::cerr << "[FAIL] Cannot create local report directory\n";
        return 2;
    }
    const auto report_path = report_directory / L"worker-source-probe.json";
    const auto temporary_report = report_directory /
        (L"worker-source-probe-" + run_id + L".tmp");
    {
        std::ofstream report(temporary_report, std::ios::binary);
        if (!report) {
            std::cerr << "[FAIL] Cannot write local probe report\n";
            return 2;
        }
        report << "{\"schema_version\":1,\"test_scope\":\"appcontainer_source_read_only_transport\","
               << "\"parser_executed\":false,\"max_source_bytes\":" << max_source_bytes
               << ",\"total_source_bytes\":" << corpus_bytes << ",\"samples\":[";
        for (size_t i = 0; i < records.size(); ++i) {
            if (i) report << ',';
            const auto& record = records[i];
            report << "{\"id\":\"" << record.id << "\",\"extension\":\""
                   << escaped(record.extension) << "\",\"bytes\":" << record.source_bytes
                   << ",\"result\":\"" << escaped(record.result)
                   << "\",\"elapsed_ms\":" << record.elapsed_ms << '}';
        }
        report << "]}\n";
        if (!report) {
            std::cerr << "[FAIL] Cannot finish local probe report\n";
            return 2;
        }
    }
    if (!MoveFileExW(
            temporary_report.c_str(),
            report_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::cerr << "[FAIL] Cannot finalize local probe report\n";
        return 2;
    }

    std::map<std::string, std::pair<uint32_t, uint32_t>> summary;
    for (const auto& record : records) {
        auto& counts = summary[record.extension];
        ++counts.first;
        if (record.result == "read_only_verified") ++counts.second;
    }
    for (const auto& [extension, counts] : summary) {
        std::cout << extension << ": " << counts.second << "/" << counts.first
                  << " read-only worker probes passed\n";
    }
    std::cout << "Parser/fidelity import tests: not run; local report is under cdr/test-reports\n";
    return all_verified ? 0 : 1;
}
