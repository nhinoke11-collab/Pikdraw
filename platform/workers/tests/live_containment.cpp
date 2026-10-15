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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <array>

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
}

int RunLiveContainment() {
    const auto evidence=fs::current_path()/"live-containment-report.json";
    std::ostringstream rows;
    int passed=0;
    const auto write_report=[&](const char* status,const std::string& error) {
        std::ofstream out(evidence,std::ios::binary);
        require(static_cast<bool>(out),"Cannot write containment evidence");
        out << "{\"status\":\"" << status << "\",\"denied_canaries\":" << passed
            << ",\"required_canaries\":5,\"full_spike_accepted\":false,\"error\":\"" << escaped(error)
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
        const auto executable=bin/L"spike_worker.exe";
        auto worker_src = fs::current_path() / L"spike_worker.exe";
        if (!fs::exists(worker_src)) {
            worker_src = fs::current_path() / L"build/win-dev/platform/workers/spike_worker.exe";
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
        // A Job quota probe can terminate the offending process; run it last.
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
