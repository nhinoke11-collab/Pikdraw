#include "pikdraw/interop/ipc_dto.hpp"
#include "pikdraw/interop/payload_validator.hpp"
#ifdef PIKDRAW_WORKER_TEST_HOOKS
#include "pikdraw/workers/canary_definitions.hpp"
#endif
#include "pikdraw/workers/source_snapshot.hpp"
#include "v2_worker_protocol.hpp"
#include "pikdraw/workers/win32_common.hpp"

#include <iostream>
#include <charconv>
#include <limits>
#include <vector>
#ifdef PIKDRAW_WORKER_TEST_HOOKS
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <io.h>
#include <fcntl.h>

using namespace pikdraw::interop;
using namespace pikdraw::workers;

namespace {

#ifdef PIKDRAW_WORKER_TEST_HOOKS
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
#endif

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

#ifdef PIKDRAW_WORKER_TEST_HOOKS
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

#endif


bool ParseUnsigned(std::string_view text, uint64_t& value) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

#ifdef PIKDRAW_WORKER_TEST_HOOKS
bool ParseDigest(std::string_view text, SourceDigest& digest) {
    if (text.size() != digest.size() * 2) return false;
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < digest.size(); ++i) {
        const int high = nibble(text[i * 2]);
        const int low = nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        digest[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

bool ProbeReadOnlySource(
    HANDLE source,
    uint64_t expected_size,
    const SourceDigest& expected_digest) {
    uint64_t actual_size = 0;
    SourceDigest actual_digest{};
    if (!HashReadOnlyHandle(
            source,
            1024ULL * 1024ULL * 1024ULL,
            actual_size,
            actual_digest) ||
        actual_size != expected_size ||
        actual_digest != expected_digest) {
        return false;
    }

    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(source, beginning, nullptr, FILE_BEGIN)) return false;
    const uint8_t marker = 0xA5;
    DWORD written = 0;
    if (WriteFile(source, &marker, sizeof(marker), &written, nullptr)) return false;
    return GetLastError() == ERROR_ACCESS_DENIED;
}

#endif

} // namespace

int main(int argc, char** argv) {
#ifdef PIKDRAW_WORKER_TEST_HOOKS
    if (argc==2 && std::string_view(argv[1])=="--test-child-exit") return 0;
    // Deterministic repository-owned failure fixtures, never a source-format adapter.
    if (argc==2 && std::string_view(argv[1])=="--test-stall-before-read") { Sleep(10000); return 0; }
    if (argc==2 && std::string_view(argv[1])=="--test-truncated-response") {
        uint8_t byte=0; DWORD written=0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),&byte,1,&written,nullptr);
        return 0;
    }
#endif
    HANDLE source_handle = INVALID_HANDLE_VALUE;
#ifdef PIKDRAW_WORKER_TEST_HOOKS
    bool test_source_probe = false;
    bool has_expected_size = false;
    bool has_expected_digest = false;
    bool mismatch = false;
    bool no_secret = false;
    uint64_t expected_size = 0;
    SourceDigest expected_digest{};
    std::string_view v2_stream_fixture;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        mismatch |= arg == "--test-mismatched-envelope";
        no_secret |= arg == "--test-no-secret-env";
    }
    if (no_secret && GetEnvironmentVariableW(L"PIKDRAW_TEST_HOST_SECRET", nullptr, 0) > 0)
        return 9;
#else
    (void)argc;
#endif
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--pikdraw-source-handle" && i + 1 < argc) {
            uint64_t value = 0;
            if (!ParseUnsigned(argv[++i], value) || value == 0 ||
                value > std::numeric_limits<uintptr_t>::max()) return 2;
            source_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
#ifdef PIKDRAW_WORKER_TEST_HOOKS
        } else if (argument == "--test-mismatched-envelope") {
            mismatch = true;
        } else if (argument == "--test-no-secret-env") {
            no_secret = true;
        } else if (argument == "--test-source-probe") {
            test_source_probe = true;
        } else if (argument == "--test-source-size" && i + 1 < argc) {
            if (!ParseUnsigned(argv[++i], expected_size)) return 2;
            has_expected_size = true;
        } else if (argument == "--test-source-sha256" && i + 1 < argc) {
            if (!ParseDigest(argv[++i], expected_digest)) return 2;
            has_expected_digest = true;
        } else if (argument.starts_with("--test-v2-stream-")) {
            if (!v2_stream_fixture.empty()) return 2;
            v2_stream_fixture = argument;
#endif
        } else {
            return 2;
        }
    }
#ifdef PIKDRAW_WORKER_TEST_HOOKS
    if (!v2_stream_fixture.empty()) {
        return source_handle != INVALID_HANDLE_VALUE
            ? RunV2StreamFixture(v2_stream_fixture, source_handle)
            : 42;
    }
    if (test_source_probe) {
        return source_handle != INVALID_HANDLE_VALUE && has_expected_size &&
            has_expected_digest && ProbeReadOnlySource(source_handle, expected_size, expected_digest)
            ? 0 : 41;
    }
#endif

    HANDLE stdin_h = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);

    if (stdin_h == INVALID_HANDLE_VALUE || stdout_h == INVALID_HANDLE_VALUE) {
        return 1;
    }

    bool v2_negotiated = false;
    uint32_t v2_accepted_features = 0;
    while (true) {
        IpcHeader hdr{};
        if (!ReadExact(stdin_h, &hdr, sizeof(IpcHeader))) {
            break; // EOF or pipe closed by host
        }

        if (hdr.wire_version == IPC_WIRE_VERSION_V2) {
            if (!ValidateV2FrameHeader(hdr) || !HandleV2WorkerFrame(
                    stdin_h, stdout_h, source_handle, hdr, v2_negotiated, v2_accepted_features)) return 3;
            if (hdr.message_type == static_cast<uint16_t>(MessageType::V2ImportBegin)) return 0;
            continue;
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
#ifdef PIKDRAW_WORKER_TEST_HOOKS
            auto req_opt = DeserializeCanaryRequest(full_packet);
            if (req_opt) {
                auto resp = SerializeCanaryResponse(HandleCanary(*req_opt), hdr.sequence_id);
                WriteExact(stdout_h, resp.data(), static_cast<DWORD>(resp.size()));
            }
#else
            return 2;
#endif
        } else if (hdr.message_type == static_cast<uint16_t>(MessageType::ImportRequest)) {
            auto req_opt = DeserializeImportRequest(full_packet);
            if (req_opt) {
                ImportPayloadResponse resp{};
                resp.job = req_opt->job;
#ifdef PIKDRAW_WORKER_TEST_HOOKS
                if (mismatch) ++resp.job.epoch;
#endif
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
