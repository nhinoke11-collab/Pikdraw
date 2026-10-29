#include "pikdraw/workers/process_launcher.hpp"
#include "v2_proposal_validator.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <thread>
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
              std::chrono::steady_clock::time_point& deadline,
              std::stop_token cancellation = {},
              SystemMemoryPressureWaiter* memory_waiter = nullptr) {
    ScopedHandle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if (!event.IsValid()) return false;
    while (count) {
        if (cancellation.stop_requested()) return false;
        if (memory_waiter && memory_waiter->WaitForHeadroom(1, cancellation, &deadline) !=
                SystemMemoryWaitResult::Ready)
            return false;
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
            const DWORD wait_ms = memory_waiter
                ? static_cast<DWORD>((std::min<int64_t>)(remaining.count(), 100))
                : static_cast<DWORD>(remaining.count());
            const DWORD waited=WaitForMultipleObjects(2,waits,FALSE,wait_ms);
            if (waited == WAIT_TIMEOUT && memory_waiter) {
                if (memory_waiter->WaitForHeadroom(1, cancellation, &deadline) !=
                        SystemMemoryWaitResult::Ready) {
                    CancelIoEx(pipe,&operation);
                    GetOverlappedResult(pipe,&operation,&transferred,TRUE);
                    return false;
                }
                continue;
            }
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
#ifndef PIKDRAW_WORKER_TEST_HOOKS
    // The shipping host cannot disable the sandbox, inherit test handles, or select a test worker.
    if (!config.use_appcontainer || config.trusted_test_worker ||
        config.test_inherited_handle != INVALID_HANDLE_VALUE) return false;
#endif
    bool committed=false;
    struct FailedLaunchCleanup {
        WorkerProcessHost* host; bool& committed;
        ~FailedLaunchCleanup() { if (!committed) host->Terminate(); }
    } cleanup{this,committed};
    const auto valid_text = [](const std::wstring& value) {
        return value.size() < 32767 && value.find(L'\0') == std::wstring::npos;
    };
    if (!valid_text(config.worker_executable_path) ||
        !valid_text(config.working_directory) ||
        !valid_text(config.appcontainer_profile_name) || config.arguments.size() > 64 ||
        !std::filesystem::path(config.worker_executable_path).is_absolute() ||
        (!config.working_directory.empty() &&
         !std::filesystem::path(config.working_directory).is_absolute()) ||
        (!config.use_appcontainer && !config.trusted_test_worker) ||
        (config.use_appcontainer && config.appcontainer_profile_name.empty()) ||
        (config.use_appcontainer &&
         (!config.job_limits.kill_on_job_close || !config.job_limits.prevent_child_processes ||
          config.job_limits.active_process_limit != 1)) ||
        (config.source_snapshot != nullptr && !config.source_snapshot->IsValid()) ||
        (config.source_snapshot != nullptr && !config.use_appcontainer && !config.trusted_test_worker)) return false;
    if (!config.temporary_directory.empty() &&
        !std::filesystem::path(config.temporary_directory).is_absolute()) return false;
    for (const auto& directory : config.writable_directories) {
        if (!valid_text(directory) || !std::filesystem::path(directory).is_absolute()) return false;
    }

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
        if (config.grant_worker_directory_access) {
            const auto bin_path = std::filesystem::path(config.worker_executable_path).parent_path();
            if (!AppContainerProfileGuard::GrantDirectoryAccess(bin_path.wstring(), app_container_.GetSid()) ||
                !AppContainerProfileGuard::GrantDirectoryAccess(config.worker_executable_path, app_container_.GetSid())) {
                return false;
            }
        }
        for (const auto& directory : config.writable_directories) {
            if (!AppContainerProfileGuard::GrantDirectoryModifyAccess(
                    directory, app_container_.GetSid())) return false;
        }
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

    std::vector<HANDLE> inherit_handles = { auto_stdin_read.Get(), auto_stdout_write.Get(), child_stderr.Get() };
    ScopedHandle inherited_source;
    if (config.source_snapshot != nullptr) {
        HANDLE duplicate = INVALID_HANDLE_VALUE;
        if (!DuplicateHandle(GetCurrentProcess(), config.source_snapshot->ReadHandle(),
                             GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            DeleteProcThreadAttributeList(siex.lpAttributeList);
            return false;
        }
        inherited_source = ScopedHandle(duplicate);
        if (GetFileType(duplicate) != FILE_TYPE_DISK) {
            DeleteProcThreadAttributeList(siex.lpAttributeList);
            return false;
        }
        inherit_handles.push_back(duplicate);
    }
    if (config.test_inherited_handle!=INVALID_HANDLE_VALUE) {
        if (!config.trusted_test_worker || config.use_appcontainer || !config.test_inherited_handle) {
            DeleteProcThreadAttributeList(siex.lpAttributeList); return false;
        }
        inherit_handles.push_back(config.test_inherited_handle);
    }
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

    std::wstring effective_working_directory = config.working_directory;
    if (effective_working_directory.empty()) {
        std::wstring current_directory(32768, L'\0');
        const DWORD current_size = GetCurrentDirectoryW(
            static_cast<DWORD>(current_directory.size()), current_directory.data());
        if (!current_size || current_size >= current_directory.size()) return false;
        current_directory.resize(current_size);
        effective_working_directory = std::move(current_directory);
    }
    wchar_t windows_buffer[32768]{};
    const UINT windows_size = GetWindowsDirectoryW(windows_buffer, 32768);
    if (!windows_size || windows_size >= 32768) return false;
    const std::wstring windows(windows_buffer, windows_size);
    // Give the AppContainer only runtime/profile paths it needs; never copy the
    // host environment wholesale (which may contain credentials or test secrets).
    std::wstring environment;
    if (effective_working_directory.size() >= 3 &&
        effective_working_directory[1] == L':' &&
        (effective_working_directory[2] == L'\\' || effective_working_directory[2] == L'/')) {
        environment = L"=" + effective_working_directory.substr(0, 2) + L"=" +
            effective_working_directory;
        environment.push_back(L'\0');
    }
    const auto append_environment_value = [&](const wchar_t* name) {
        std::wstring value(32768, L'\0');
        const DWORD size = GetEnvironmentVariableW(name, value.data(),
                                                    static_cast<DWORD>(value.size()));
        if (size && size < value.size()) {
            value.resize(size);
            environment += name;
            environment.push_back(L'=');
            environment += value;
            environment.push_back(L'\0');
        }
    };
    append_environment_value(L"APPDATA");
    append_environment_value(L"HOMEDRIVE");
    append_environment_value(L"HOMEPATH");
    append_environment_value(L"LOCALAPPDATA");
    environment += L"PATH=" + windows + L"\\System32";
    environment.push_back(L'\0');
    append_environment_value(L"SystemDrive");
    environment += L"SystemRoot=" + windows;
    environment.push_back(L'\0');
    if (config.temporary_directory.empty()) {
        append_environment_value(L"TEMP");
        append_environment_value(L"TMP");
    } else {
        environment += L"TEMP=" + config.temporary_directory;
        environment.push_back(L'\0');
        environment += L"TMP=" + config.temporary_directory;
        environment.push_back(L'\0');
    }
    append_environment_value(L"USERPROFILE");
    environment += L"WINDIR=" + windows;
    environment.push_back(L'\0');
    environment.push_back(L'\0');

    std::wstring cmd = QuoteCommandLineArgument(config.worker_executable_path);
    for (const auto& argument : config.arguments) {
        if (!valid_text(argument)) return false;
        cmd += L" " + QuoteCommandLineArgument(argument);
        if (cmd.size() >= 32767) return false;
    }
    if (inherited_source.IsValid()) {
        const auto value = reinterpret_cast<uintptr_t>(inherited_source.Get());
        cmd += L" --pikdraw-source-handle " + QuoteCommandLineArgument(std::to_wstring(value));
    }
    PROCESS_INFORMATION pi{};
    DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;

    BOOL ok = CreateProcessW(
        config.worker_executable_path.c_str(),
        cmd.data(),
        nullptr, nullptr,
        TRUE, // inherit handles in whitelist
        creation_flags,
        environment.data(),
        effective_working_directory.c_str(),
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

    if (config.source_snapshot != nullptr) {
        has_source_capability_ = true;
        source_size_ = config.source_snapshot->SizeBytes();
        source_token_ = config.source_snapshot->Token();
        source_sha256_ = config.source_snapshot->Sha256();
    }
    committed=true;
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
    job_object_.Reset();
    app_container_.Cleanup();
    process_id_ = 0;
    has_source_capability_ = false;
    source_size_ = 0;
    source_token_.fill(0);
    source_sha256_.fill(0);
    v2_negotiated_ = false;
    v2_features_ = 0;
}

bool WorkerProcessHost::WaitForExit(std::chrono::milliseconds timeout) noexcept {
    if (!process_handle_.IsValid() || timeout.count() <= 0) return false;
    const auto bounded = static_cast<DWORD>(std::min<int64_t>(timeout.count(), MAXDWORD - 1));
    if (WaitForSingleObject(process_handle_.Get(), bounded) != WAIT_OBJECT_0) return false;
    return GetExitCodeProcess(process_handle_.Get(), &last_worker_exit_code_) != FALSE;
}

bool WorkerProcessHost::TransactRawProcessBytes(
    std::span<const uint8_t> input, uint64_t max_output_bytes,
    std::chrono::milliseconds timeout, std::vector<uint8_t>& output,
    std::stop_token cancellation, SystemMemoryPressureWaiter* memory_waiter) {
    output.clear();
    last_transport_failure_ = "none";
    last_worker_exit_code_ = STILL_ACTIVE;
    if (!IsAlive() || !app_container_.IsValid() || v2_negotiated_ ||
        has_source_capability_ || input.empty() ||
        max_output_bytes == 0 ||
        timeout.count() <= 0 || !stdin_write_handle_.IsValid() || !stdout_read_handle_.IsValid()) {
        last_transport_failure_ = "raw process stream requires a bounded unsourced AppContainer process";
        return false;
    }
    auto deadline = std::chrono::steady_clock::now() + timeout;
    SystemMemoryPressureWaiter local_memory_waiter;
    auto* active_memory_waiter = memory_waiter ? memory_waiter : &local_memory_waiter;
    auto input_deadline = deadline + kSystemMemoryPressureWaitLimit;
    std::atomic<bool> input_ok{false};
    std::thread input_writer([&] {
        input_ok.store(Transfer(stdin_write_handle_.Get(), process_handle_.Get(),
            const_cast<uint8_t*>(input.data()), input.size(), true,
            input_deadline, cancellation),
            std::memory_order_relaxed);
        stdin_write_handle_.Close();
    });

    bool output_ok = true;
    bool eof = false;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (!eof && output_ok) {
        if (cancellation.stop_requested()) {
            output_ok = false;
            last_transport_failure_ = "raw process stream cancelled";
            break;
        }
        if (active_memory_waiter->WaitForHeadroom(1, cancellation, &deadline) !=
                SystemMemoryWaitResult::Ready) {
            output_ok = false;
            last_transport_failure_ = cancellation.stop_requested()
                ? "raw process stream cancelled"
                : "system RAM pressure wait timed out after 5 minutes";
            break;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            output_ok = false;
            last_transport_failure_ = "raw process stream timed out";
            break;
        }
        ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.IsValid()) {
            output_ok = false;
            last_transport_failure_ = "raw process stream could not create a read event";
            break;
        }
        OVERLAPPED operation{};
        operation.hEvent = event.Get();
        DWORD received = 0;
        BOOL read_ok = ReadFile(stdout_read_handle_.Get(), buffer.data(),
                                static_cast<DWORD>(buffer.size()), &received, &operation);
        if (!read_ok) {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
                eof = true;
                break;
            }
            if (error != ERROR_IO_PENDING) {
                output_ok = false;
                last_transport_failure_ = "raw process stream read failed";
                break;
            }
            const HANDLE waits[]{event.Get(), process_handle_.Get()};
            const DWORD bounded_wait = static_cast<DWORD>((std::min<int64_t>)(
                (std::min<int64_t>)(remaining.count(), 100), MAXDWORD - 1));
            const DWORD waited = WaitForMultipleObjects(2, waits, FALSE, bounded_wait);
            if (waited == WAIT_TIMEOUT) {
                const auto memory_result = active_memory_waiter->WaitForHeadroom(
                    1, cancellation, &deadline);
                if (memory_result == SystemMemoryWaitResult::Ready) continue;
                CancelIoEx(stdout_read_handle_.Get(), &operation);
                DWORD ignored = 0;
                GetOverlappedResult(stdout_read_handle_.Get(), &operation, &ignored, TRUE);
                output_ok = false;
                last_transport_failure_ = memory_result == SystemMemoryWaitResult::Cancelled
                    ? "raw process stream cancelled"
                    : "system RAM pressure wait timed out after 5 minutes";
                break;
            }
            if (waited == WAIT_OBJECT_0 + 1) {
                if (!GetOverlappedResult(stdout_read_handle_.Get(), &operation, &received, TRUE)) {
                    const DWORD read_error = GetLastError();
                    if (read_error == ERROR_BROKEN_PIPE || read_error == ERROR_NO_DATA) {
                        eof = true;
                        break;
                    }
                    output_ok = false;
                    last_transport_failure_ = "raw process stream did not close cleanly";
                    break;
                }
            } else if (waited != WAIT_OBJECT_0) {
                output_ok = false;
                last_transport_failure_ = "raw process stream completion failed";
                break;
            } else if (!GetOverlappedResult(stdout_read_handle_.Get(), &operation,
                                           &received, FALSE)) {
                const DWORD read_error = GetLastError();
                if (read_error == ERROR_BROKEN_PIPE || read_error == ERROR_NO_DATA ||
                    (read_error == ERROR_OPERATION_ABORTED &&
                     WaitForSingleObject(process_handle_.Get(), 0) == WAIT_OBJECT_0)) {
                    eof = true;
                    break;
                }
                output_ok = false;
                last_transport_failure_ = "raw process stream completion failed";
                break;
            }
        }
        if (received == 0) {
            eof = true;
            break;
        }
        if (output.size() > max_output_bytes ||
            received > max_output_bytes - output.size()) {
            TerminateProcess(process_handle_.Get(), ERROR_FILE_TOO_LARGE);
            output_ok = false;
            last_transport_failure_ = "raw process output exceeded its bounded byte limit";
            break;
        }
        try {
            output.insert(output.end(), buffer.begin(), buffer.begin() + received);
        } catch (...) {
            output_ok = false;
            last_transport_failure_ = "raw process output could not be retained within host memory";
            break;
        }
    }

    if (!output_ok && process_handle_.IsValid())
        TerminateProcess(process_handle_.Get(), ERROR_PROCESS_ABORTED);
    input_writer.join();
    const auto wait_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (output_ok && eof && wait_remaining.count() > 0 && WaitForExit(wait_remaining)) {
        if (last_worker_exit_code_ == 0 && input_ok.load(std::memory_order_relaxed)) return true;
        last_transport_failure_ = "raw process exited unsuccessfully or did not consume all input";
    } else if (output_ok) {
        last_transport_failure_ = "raw process did not complete within its bounded time";
    }
    output.clear();
    Terminate();
    return false;
}

bool WorkerProcessHost::IsAlive() const noexcept {
    if (!process_handle_.IsValid()) return false;
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process_handle_.Get(), &exit_code)) {
        return (exit_code == STILL_ACTIVE);
    }
    return false;
}

bool WorkerProcessHost::NegotiateV2(
    uint32_t required_features,
    std::chrono::milliseconds timeout,
    SystemMemoryPressureWaiter* memory_waiter,
    std::stop_token cancellation) {
    last_transport_failure_ = "none";
    last_worker_exit_code_ = STILL_ACTIVE;
    if (v2_negotiated_) {
        last_transport_failure_ = "v2 negotiation already completed";
        Terminate();
        return false;
    }
    uint32_t accepted = 0;
    std::string_view failure;
    SystemMemoryPressureWaiter local_memory_waiter;
    auto& active_memory_waiter = memory_waiter ? *memory_waiter : local_memory_waiter;
    const bool ok = NegotiateV2WorkerProcess(
        stdin_write_handle_.Get(), stdout_read_handle_.Get(), process_handle_.Get(),
        required_features, has_source_capability_, timeout, accepted, failure,
        cancellation, active_memory_waiter);
    if (!ok) {
        if (process_handle_.IsValid()) (void)GetExitCodeProcess(process_handle_.Get(), &last_worker_exit_code_);
        Terminate();
        last_transport_failure_ = failure.data();
        return false;
    }
    v2_negotiated_ = true;
    v2_features_ = accepted;
    return true;
}

bool WorkerProcessHost::TransactV2ProposalStream(
    std::span<const uint8_t> request_bytes,
    std::chrono::milliseconds timeout,
    const pikdraw::interop::V2ProposalStreamLimits& limits,
    const std::function<bool(const pikdraw::interop::IpcV2Frame&)>& consumer,
    std::stop_token cancellation,
    SystemMemoryPressureWaiter* memory_waiter) {
    using namespace pikdraw::interop;
    last_transport_failure_ = "none";
    last_worker_exit_code_ = STILL_ACTIVE;
    const auto request = DeserializeV2ImportBegin(request_bytes);
    constexpr uint32_t required = IPC_V2_FEATURE_SOURCE_HANDLE | IPC_V2_FEATURE_CHECKED_PROPOSAL_STREAM;
    if (!IsAlive() || !v2_negotiated_ || (v2_features_ & required) != required ||
        !has_source_capability_ || !request || request->source_bytes != source_size_ ||
        request->job.source_token != source_token_ || request->source_sha256 != source_sha256_) {
        last_transport_failure_ = "v2 import requires negotiated source capability and matching snapshot";
        if (process_handle_.IsValid()) (void)GetExitCodeProcess(process_handle_.Get(), &last_worker_exit_code_);
        Terminate();
        return false;
    }

    SystemMemoryPressureWaiter local_memory_waiter;
    auto& active_memory_waiter = memory_waiter ? *memory_waiter : local_memory_waiter;
    std::string_view failure;
    if (!TransactV2ProposalStreamProcess(
            stdin_write_handle_.Get(), stdout_read_handle_.Get(), process_handle_.Get(),
            *request, request_bytes, timeout, limits, consumer, failure, cancellation,
            active_memory_waiter)) {
        if (process_handle_.IsValid()) (void)GetExitCodeProcess(process_handle_.Get(), &last_worker_exit_code_);
        Terminate();
        last_transport_failure_ = failure.data();
        return false;
    }
    return true;
}
std::optional<std::vector<uint8_t>> WorkerProcessHost::TransactBinaryPayload(
    std::span<const uint8_t> request_bytes,
    std::chrono::milliseconds timeout)
{
    using namespace pikdraw::interop;
    const auto reject=[&](const char* reason) -> std::optional<std::vector<uint8_t>> {
        last_transport_failure_=reason;
        if (process_handle_.IsValid()) (void)GetExitCodeProcess(process_handle_.Get(),&last_worker_exit_code_);
        Terminate(); return std::nullopt;
    };
    last_transport_failure_="none"; last_worker_exit_code_=STILL_ACTIVE;
    if (!IsAlive() || !stdin_write_handle_.IsValid() || !stdout_read_handle_.IsValid() ||
        timeout.count()<=0 || timeout>std::chrono::minutes(1) || request_bytes.size()<sizeof(IpcHeader) ||
        request_bytes.size()>sizeof(IpcHeader)+IPC_MAX_PAYLOAD_BYTES) return reject("invalid request/lifecycle");
    IpcHeader request{};
    std::memcpy(&request,request_bytes.data(),sizeof(request));
    if (!PayloadValidator::ValidateHeader(request).IsOk() ||
        request_bytes.size()!=sizeof(request)+request.payload_bytes) return reject("invalid request frame");
    uint16_t expected=0;
    switch (static_cast<MessageType>(request.message_type)) {
        case MessageType::Ping: expected=static_cast<uint16_t>(MessageType::Pong); break;
        case MessageType::ImportRequest: expected=static_cast<uint16_t>(MessageType::ImportResponse); break;
        case MessageType::CanaryRequest:
#ifndef PIKDRAW_WORKER_TEST_HOOKS
            return reject("test-only canary requests disabled");
#else
            expected=static_cast<uint16_t>(MessageType::CanaryResponse); break;
#endif
        default: return reject("unknown request type");
    }
    auto deadline=std::chrono::steady_clock::now()+timeout;
    if (!Transfer(stdin_write_handle_.Get(),process_handle_.Get(),const_cast<uint8_t*>(request_bytes.data()),
                  request_bytes.size(),true,deadline)) return reject("request write failed/timeout");
    IpcHeader response{};
    if (!Transfer(stdout_read_handle_.Get(),process_handle_.Get(),reinterpret_cast<uint8_t*>(&response),
                  sizeof(response),false,deadline)) return reject("response header failed/timeout");
    if (response.payload_bytes>IPC_MAX_PAYLOAD_BYTES || !PayloadValidator::ValidateHeader(response).IsOk() || response.sequence_id!=request.sequence_id ||
        response.message_type!=expected || (expected==static_cast<uint16_t>(MessageType::Pong) && response.payload_bytes)) return reject("invalid response frame");
    std::vector<uint8_t> packet;
    try { packet.resize(sizeof(response)+response.payload_bytes); }
    catch (const std::bad_alloc&) { return reject("response allocation failed"); }
    std::memcpy(packet.data(),&response,sizeof(response));
    if (!Transfer(stdout_read_handle_.Get(),process_handle_.Get(),packet.data()+sizeof(response),
                  response.payload_bytes,false,deadline)) return reject("response body failed/timeout");
    return packet;
}

} // namespace pikdraw::workers
