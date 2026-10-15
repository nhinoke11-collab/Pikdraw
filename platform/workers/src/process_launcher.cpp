#include "pikdraw/workers/process_launcher.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include "pikdraw/interop/payload_validator.hpp"

namespace pikdraw::workers {
namespace {
bool CreateHostPipe(bool host_writes, ScopedHandle& server, ScopedHandle& child) {
    static std::atomic<uint64_t> next{0};
    const std::wstring name=L"\\\\.\\pipe\\PikDrawSpike-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++next);
    server=ScopedHandle(CreateNamedPipeW(name.c_str(),
        (host_writes?PIPE_ACCESS_OUTBOUND:PIPE_ACCESS_INBOUND)|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr));
    if (!server.IsValid()) return false;
    ScopedHandle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if (!event.IsValid()) return false;
    OVERLAPPED connection{}; connection.hEvent=event.Get();
    const BOOL connected=ConnectNamedPipe(server.Get(),&connection);
    const DWORD error=connected?ERROR_SUCCESS:GetLastError();
    if (!connected && error!=ERROR_IO_PENDING && error!=ERROR_PIPE_CONNECTED) return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    child=ScopedHandle(CreateFileW(name.c_str(),host_writes?GENERIC_READ:GENERIC_WRITE,
        0,&attributes,OPEN_EXISTING,0,nullptr));
    if (!connected && error==ERROR_IO_PENDING) {
        if (!child.IsValid() || WaitForSingleObject(event.Get(),1000)!=WAIT_OBJECT_0) {
            CancelIoEx(server.Get(),&connection);
            DWORD ignored=0; GetOverlappedResult(server.Get(),&connection,&ignored,TRUE);
            return false;
        }
        DWORD ignored=0;
        if (!GetOverlappedResult(server.Get(),&connection,&ignored,FALSE)) return false;
    }
    return child.IsValid();
}

bool Transfer(HANDLE pipe, HANDLE process, uint8_t* bytes, size_t count, bool write,
              std::chrono::steady_clock::time_point deadline) {
    ScopedHandle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if (!event.IsValid()) return false;
    while (count) {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        if (remaining.count()<=0) return false;
        ResetEvent(event.Get());
        OVERLAPPED operation{}; operation.hEvent=event.Get();
        DWORD transferred=0;
        const DWORD chunk=static_cast<DWORD>(std::min<size_t>(count,65536));
        BOOL complete=write?WriteFile(pipe,bytes,chunk,&transferred,&operation):ReadFile(pipe,bytes,chunk,&transferred,&operation);
        if (!complete) {
            if (GetLastError()!=ERROR_IO_PENDING) return false;
            const HANDLE waits[]={event.Get(),process};
            const DWORD waited=WaitForMultipleObjects(2,waits,FALSE,static_cast<DWORD>(remaining.count()));
            if (waited!=WAIT_OBJECT_0) {
                CancelIoEx(pipe,&operation);
                GetOverlappedResult(pipe,&operation,&transferred,TRUE); // Drain before OVERLAPPED storage expires.
                return false;
            }
            if (!GetOverlappedResult(pipe,&operation,&transferred,FALSE)) return false;
        }
        if (!transferred) return false;
        bytes+=transferred; count-=transferred;
    }
    return true;
}
}


bool WorkerProcessHost::Launch(const WorkerLaunchConfig& config) {
    Terminate();
    if (config.worker_executable_path.empty() ||
        (!config.use_appcontainer && !config.trusted_test_worker) ||
        (config.use_appcontainer && config.appcontainer_profile_name.empty())) return false;

    // Parent endpoints use overlapped I/O; both reads and writes have a deadline.
    ScopedHandle auto_stdin_read, auto_stdout_write;
    if (!CreateHostPipe(true,stdin_write_handle_,auto_stdin_read) ||
        !CreateHostPipe(false,stdout_read_handle_,auto_stdout_write)) return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    ScopedHandle child_stderr(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
        &attributes,OPEN_EXISTING,0,nullptr));
    if (!child_stderr.IsValid()) return false;

    // Create Job Object with configured limits
    if (!job_object_.Create(config.job_limits)) {
        return false;
    }

    // Prepare AppContainer if requested
    SECURITY_CAPABILITIES sec_caps{};
    if (config.use_appcontainer) {
        if (!app_container_.Create(config.appcontainer_profile_name)) return false;
            sec_caps.AppContainerSid = app_container_.GetSid();
            sec_caps.Capabilities = nullptr;
            sec_caps.CapabilityCount = 0;
            sec_caps.Reserved = 0;
    }

    // Prepare STARTUPINFOEXW with explicit handle inheritance whitelist
    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = auto_stdin_read.Get();
    siex.StartupInfo.hStdOutput = auto_stdout_write.Get();
    siex.StartupInfo.hStdError = child_stderr.Get();

    SIZE_T attribute_list_size = 0;
    DWORD attribute_count = 1; // PROC_THREAD_ATTRIBUTE_HANDLE_LIST
    if (app_container_.IsValid()) {
        attribute_count++; // PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES
    }

    InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &attribute_list_size);
    std::vector<uint8_t> attr_buffer(attribute_list_size);
    siex.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attr_buffer.data());
    if (!InitializeProcThreadAttributeList(siex.lpAttributeList, attribute_count, 0, &attribute_list_size)) {
        return false;
    }

    std::array<HANDLE, 3> inherit_handles = { auto_stdin_read.Get(), auto_stdout_write.Get(), child_stderr.Get() };
    if (!UpdateProcThreadAttribute(
        siex.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherit_handles.data(), inherit_handles.size() * sizeof(HANDLE),
        nullptr, nullptr
    )) { DeleteProcThreadAttributeList(siex.lpAttributeList); return false; }

    if (app_container_.IsValid()) {
        if (!UpdateProcThreadAttribute(
            siex.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
            &sec_caps, sizeof(sec_caps),
            nullptr, nullptr
        )) { DeleteProcThreadAttributeList(siex.lpAttributeList); return false; }
    }

    std::wstring cmd = QuoteCommandLineArgument(config.worker_executable_path);
    for (const auto& argument : config.arguments) cmd += L" " + QuoteCommandLineArgument(argument);
    PROCESS_INFORMATION pi{};
    DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW;

    BOOL ok = CreateProcessW(
        config.worker_executable_path.c_str(),
        cmd.data(),
        nullptr, nullptr,
        TRUE, // inherit handles in whitelist
        creation_flags,
        nullptr,
        config.working_directory.empty() ? nullptr : config.working_directory.c_str(),
        &siex.StartupInfo,
        &pi
    );

    DeleteProcThreadAttributeList(siex.lpAttributeList);

    if (!ok) {
        return false;
    }

    process_handle_ = ScopedHandle(pi.hProcess);
    thread_handle_ = ScopedHandle(pi.hThread);
    process_id_ = pi.dwProcessId;

    // Assign to Job Object before resuming execution
    if (!job_object_.AssignProcess(process_handle_.Get()) ||
        ResumeThread(thread_handle_.Get()) == static_cast<DWORD>(-1)) {
        Terminate(); return false;
    }

    return true;
}

void WorkerProcessHost::Terminate() noexcept {
    if (job_object_.IsValid()) {
        (void)job_object_.Terminate(1);
    }
    if (process_handle_.IsValid()) {
        TerminateProcess(process_handle_.Get(), 1);
        WaitForSingleObject(process_handle_.Get(), 1000);
    }
    process_handle_.Close();
    thread_handle_.Close();
    stdin_write_handle_.Close();
    stdout_read_handle_.Close();
    app_container_.Cleanup();
    process_id_ = 0;
}

bool WorkerProcessHost::IsAlive() const noexcept {
    if (!process_handle_.IsValid()) return false;
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process_handle_.Get(), &exit_code)) {
        return (exit_code == STILL_ACTIVE);
    }
    return false;
}

std::optional<std::vector<uint8_t>> WorkerProcessHost::TransactBinaryPayload(
    std::span<const uint8_t> request_bytes,
    std::chrono::milliseconds timeout)
{
    using namespace pikdraw::interop;
    const auto reject=[&]() -> std::optional<std::vector<uint8_t>> { Terminate(); return std::nullopt; };
    if (!IsAlive() || !stdin_write_handle_.IsValid() || !stdout_read_handle_.IsValid() ||
        timeout.count()<=0 || timeout>std::chrono::minutes(1) || request_bytes.size()<sizeof(IpcHeader) ||
        request_bytes.size()>sizeof(IpcHeader)+IPC_MAX_PAYLOAD_BYTES) return reject();
    IpcHeader request{};
    std::memcpy(&request,request_bytes.data(),sizeof(request));
    if (!PayloadValidator::ValidateHeader(request).IsOk() ||
        request_bytes.size()!=sizeof(request)+request.payload_bytes) return reject();
    uint16_t expected=0;
    switch (static_cast<MessageType>(request.message_type)) {
        case MessageType::Ping: expected=static_cast<uint16_t>(MessageType::Pong); break;
        case MessageType::ImportRequest: expected=static_cast<uint16_t>(MessageType::ImportResponse); break;
        case MessageType::CanaryRequest: expected=static_cast<uint16_t>(MessageType::CanaryResponse); break;
        default: return reject();
    }
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    if (!Transfer(stdin_write_handle_.Get(),process_handle_.Get(),const_cast<uint8_t*>(request_bytes.data()),
                  request_bytes.size(),true,deadline)) return reject();
    IpcHeader response{};
    if (!Transfer(stdout_read_handle_.Get(),process_handle_.Get(),reinterpret_cast<uint8_t*>(&response),
                  sizeof(response),false,deadline)) return reject();
    if (!PayloadValidator::ValidateHeader(response).IsOk() || response.sequence_id!=request.sequence_id ||
        response.message_type!=expected || (expected==static_cast<uint16_t>(MessageType::Pong) && response.payload_bytes)) return reject();
    std::vector<uint8_t> packet(sizeof(response)+response.payload_bytes);
    std::memcpy(packet.data(),&response,sizeof(response));
    if (!Transfer(stdout_read_handle_.Get(),process_handle_.Get(),packet.data()+sizeof(response),
                  response.payload_bytes,false,deadline)) return reject();
    return packet;
}

} // namespace pikdraw::workers
