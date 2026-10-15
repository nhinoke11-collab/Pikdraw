#include "pikdraw/interop/ipc_dto.hpp"
#include "pikdraw/interop/payload_validator.hpp"
#include "pikdraw/workers/canary_definitions.hpp"
#include "pikdraw/workers/win32_common.hpp"

#include <iostream>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>

using namespace pikdraw::interop;
using namespace pikdraw::workers;

namespace {

BOOL ProbeHandleInformation(HANDLE handle,DWORD* flags,bool* strict_exception) noexcept {
    *strict_exception=false;
#if defined(_MSC_VER) || (defined(__clang__) && defined(_WIN64))
    // Windows strict-handle mitigation can raise this status instead of returning FALSE.
    // Catch only this expected probe fault; never reinterpret arbitrary crashes as denial.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
    __try { return GetHandleInformation(handle,flags); }
    __except(GetExceptionCode()==0xC0000008u ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        *strict_exception=true; SetLastError(ERROR_INVALID_HANDLE); return FALSE;
    }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#else
    PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY policy{};
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),ProcessStrictHandleCheckPolicy,&policy,sizeof(policy)) ||
        policy.RaiseExceptionOnInvalidHandleReference) {
        SetLastError(ERROR_NOT_SUPPORTED); return FALSE; // No unsupported compiler crash can pass a gate.
    }
    return GetHandleInformation(handle,flags);
#endif
}

bool ReadExact(HANDLE h, void* buf, DWORD bytes) {
    DWORD total = 0;
    while (total < bytes) {
        DWORD read_now = 0;
        if (!ReadFile(h, static_cast<char*>(buf) + total, bytes - total, &read_now, nullptr) || read_now == 0) {
            return false;
        }
        total += read_now;
    }
    return true;
}

bool WriteExact(HANDLE h, const void* buf, DWORD bytes) {
    DWORD total = 0;
    while (total < bytes) {
        DWORD written = 0;
        if (!WriteFile(h, static_cast<const char*>(buf) + total, bytes - total, &written, nullptr) || written == 0) {
            return false;
        }
        total += written;
    }
    return true;
}

CanaryPayloadResponse HandleCanary(const CanaryPayloadRequest& req) {
    CanaryPayloadResponse resp{};
    resp.canary_type = req.canary_type;
    resp.action_attempted = true;

    switch (static_cast<CanaryType>(req.canary_type)) {
        case CanaryFileRead: {
            std::wstring path = Utf8ToWide(req.target_path);
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                resp.os_error_code = GetLastError();
                resp.denied_by_os = (resp.os_error_code == ERROR_ACCESS_DENIED);
                resp.diagnostic_message = "CreateFileW GENERIC_READ failed with OS error: " + std::to_string(resp.os_error_code);
            } else {
                CloseHandle(h);
                resp.denied_by_os = false;
                resp.os_error_code = 0;
                resp.diagnostic_message = "FAIL: File was opened successfully (access was granted)";
            }
            break;
        }
        case CanaryFileWrite: {
            std::wstring path = Utf8ToWide(req.target_path);
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                resp.os_error_code = GetLastError();
                resp.denied_by_os = (resp.os_error_code == ERROR_ACCESS_DENIED);
                resp.diagnostic_message = "CreateFileW GENERIC_WRITE failed with OS error: " + std::to_string(resp.os_error_code);
            } else {
                CloseHandle(h);
                DeleteFileW(path.c_str());
                resp.denied_by_os = false;
                resp.os_error_code = 0;
                resp.diagnostic_message = "FAIL: File write succeeded (unauthorized write occurred)";
            }
            break;
        }
        case CanaryNetworkOutbound: {
            WSADATA wsa;
            int init_res = WSAStartup(MAKEWORD(2, 2), &wsa);
            if (init_res != 0) {
                resp.os_error_code = static_cast<uint32_t>(init_res);
                resp.denied_by_os = (resp.os_error_code == WSAEACCES);
                resp.diagnostic_message = "WSAStartup failed: " + std::to_string(init_res);
                break;
            }

            // Probe 1: Attempt outbound connection to documentation external address (192.0.2.1).
            // AppContainer processes without internetClient/privateNetworkClientServer capabilities
            // are explicitly denied by Windows kernel security with WSAEACCES (10013).
            // Unconfined processes are permitted by OS security policy (returning WSAEWOULDBLOCK without WSAEACCES).
            SOCKET s_out = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            int outbound_err = 0;
            if (s_out != INVALID_SOCKET) {
                u_long nonblocking = 1;
                ioctlsocket(s_out, FIONBIO, &nonblocking);
                sockaddr_in out_addr{};
                out_addr.sin_family = AF_INET;
                out_addr.sin_port = htons(80);
                inet_pton(AF_INET, "192.0.2.1", &out_addr.sin_addr);
                int c_res = connect(s_out, reinterpret_cast<sockaddr*>(&out_addr), sizeof(out_addr));
                if (c_res == SOCKET_ERROR) {
                    outbound_err = WSAGetLastError();
                    if (outbound_err == WSAEWOULDBLOCK) {
                        fd_set w, e;
                        FD_ZERO(&w); FD_ZERO(&e);
                        FD_SET(s_out, &w); FD_SET(s_out, &e);
                        timeval tv{0, 200000}; // 200ms
                        int r = select(0, nullptr, &w, &e, &tv);
                        if (r > 0) {
                            int len = sizeof(outbound_err);
                            getsockopt(s_out, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&outbound_err), &len);
                        }
                    }
                }
                closesocket(s_out);
            }

            if (outbound_err == WSAEACCES) {
                resp.os_error_code = WSAEACCES;
                resp.denied_by_os = true;
                resp.diagnostic_message = "Outbound connect explicitly denied by OS security: WSAEACCES (10013)";
                WSACleanup();
                break;
            }
            // Probe 2: Connect to positive-control listener endpoint (for unconfined worker)
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) {
                resp.os_error_code = static_cast<uint32_t>(WSAGetLastError());
                resp.denied_by_os = (resp.os_error_code == WSAEACCES);
                resp.diagnostic_message = "socket() failed with error: " + std::to_string(resp.os_error_code);
            } else {
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(req.network_port);
                inet_pton(AF_INET, req.network_address.c_str(), &addr.sin_addr);
                u_long nonblocking=1;
                int error=0;
                if (ioctlsocket(s,FIONBIO,&nonblocking)==SOCKET_ERROR) error=WSAGetLastError();
                else if (connect(s,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==SOCKET_ERROR) {
                    error=WSAGetLastError();
                    if (error==WSAEWOULDBLOCK) {
                        fd_set writable,failed; FD_ZERO(&writable); FD_ZERO(&failed); FD_SET(s,&writable); FD_SET(s,&failed);
                        timeval deadline{1,0};
                        const int ready=select(0,nullptr,&writable,&failed,&deadline);
                        if (ready>0) {
                            int bytes=sizeof(error);
                            if (getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&bytes)==SOCKET_ERROR) error=WSAGetLastError();
                        } else error=ready==0 ? WSAETIMEDOUT : WSAGetLastError();
                    }
                }
                resp.os_error_code=static_cast<uint32_t>(error);
                resp.denied_by_os=error==WSAEACCES;
                resp.diagnostic_message="Bounded connect() result: "+std::to_string(error);
                closesocket(s);
            }
            WSACleanup();
            break;
        }
        case CanaryChildProcess: {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            const std::wstring executable=Utf8ToWide(req.target_path);
            std::wstring cmd=QuoteCommandLineArgument(executable)+L" --test-child-exit";
            BOOL ok=CreateProcessW(executable.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
            if (!ok) {
                resp.os_error_code = GetLastError();
                resp.denied_by_os = (resp.os_error_code == ERROR_ACCESS_DENIED || resp.os_error_code == ERROR_NOT_ENOUGH_QUOTA);
                resp.diagnostic_message = "CreateProcessW denied with error: " + std::to_string(resp.os_error_code);
            } else {
                WaitForSingleObject(pi.hProcess,2000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                resp.denied_by_os = false;
                resp.os_error_code = 0;
                resp.diagnostic_message = "FAIL: Child process was spawned successfully";
            }
            break;
        }
        case CanaryUngrantedHandle: {
            HANDLE h = reinterpret_cast<HANDLE>(req.test_handle_value);
            DWORD flags = 0;
            bool strict_exception=false;
            BOOL ok = ProbeHandleInformation(h, &flags, &strict_exception);
            if (!ok) {
                resp.os_error_code = GetLastError();
                resp.denied_by_os = (resp.os_error_code == ERROR_INVALID_HANDLE);
                resp.diagnostic_message = strict_exception ? "OS strict handle check raised STATUS_INVALID_HANDLE; probe caught expected fault" : "Ungranted handle is invalid (not inherited)";
            } else {
                resp.denied_by_os = false;
                resp.os_error_code = 0;
                resp.diagnostic_message = "FAIL: Ungranted handle was leaked to child process";
            }
            break;
        }
        default: {
            resp.action_attempted = false;
            resp.denied_by_os = false;
            resp.diagnostic_message = "Unknown canary type";
            break;
        }
    }
    return resp;
}

} // namespace

int main(int argc, char** argv) {
    if (argc==2 && std::string_view(argv[1])=="--test-child-exit") return 0;
    // Deterministic repository-owned failure fixtures, never a source-format adapter.
    if (argc==2 && std::string_view(argv[1])=="--test-stall-before-read") { Sleep(10000); return 0; }
    if (argc==2 && std::string_view(argv[1])=="--test-truncated-response") {
        uint8_t byte=0; DWORD written=0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),&byte,1,&written,nullptr);
        return 0;
    }
    if (argc!=1) return 2;
    HANDLE stdin_h = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);

    if (stdin_h == INVALID_HANDLE_VALUE || stdout_h == INVALID_HANDLE_VALUE) {
        return 1;
    }

    while (true) {
        IpcHeader hdr{};
        if (!ReadExact(stdin_h, &hdr, sizeof(IpcHeader))) {
            break; // EOF or pipe closed by host
        }

        auto diag = PayloadValidator::ValidateHeader(hdr);
        if (!diag.IsOk()) {
            return 2;
        }

        std::vector<uint8_t> payload(hdr.payload_bytes);
        if (hdr.payload_bytes > 0) {
            if (!ReadExact(stdin_h, payload.data(), hdr.payload_bytes)) {
                break;
            }
        }

        std::vector<uint8_t> full_packet(sizeof(IpcHeader) + hdr.payload_bytes);
        std::memcpy(full_packet.data(), &hdr, sizeof(IpcHeader));
        std::memcpy(full_packet.data() + sizeof(IpcHeader), payload.data(), hdr.payload_bytes);

        if (hdr.message_type == static_cast<uint16_t>(MessageType::Ping)) {
            IpcHeader pong_hdr = hdr;
            pong_hdr.message_type = static_cast<uint16_t>(MessageType::Pong);
            pong_hdr.payload_bytes = 0;
            WriteExact(stdout_h, &pong_hdr, sizeof(IpcHeader));
        } else if (hdr.message_type == static_cast<uint16_t>(MessageType::CanaryRequest)) {
            auto req_opt = DeserializeCanaryRequest(full_packet);
            if (req_opt) {
                auto resp = HandleCanary(*req_opt);
                auto resp_bytes = SerializeCanaryResponse(resp, hdr.sequence_id);
                WriteExact(stdout_h, resp_bytes.data(), static_cast<DWORD>(resp_bytes.size()));
            }
        } else if (hdr.message_type == static_cast<uint16_t>(MessageType::ImportRequest)) {
            auto req_opt = DeserializeImportRequest(full_packet);
            if (req_opt) {
                ImportPayloadResponse resp{};
                resp.job = req_opt->job;
                resp.success = false;
                resp.error_message = "Unsupported source format: no parser adapter is implemented";
                resp.loss_reports.push_back("Import rejected; no geometry generated or fidelity claimed");

                auto resp_bytes = SerializeImportResponse(resp, hdr.sequence_id);
                WriteExact(stdout_h, resp_bytes.data(), static_cast<DWORD>(resp_bytes.size()));
            }
        }
    }

    return 0;
}
