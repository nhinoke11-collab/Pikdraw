#include "automation_pipe_host.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <sddl.h>
#include <windows.h>
#endif

namespace pikdraw::desktop {

struct AutomationPipeHost::Impl {
    struct Pending {
        automation::JsonValue request;
        automation::JsonValue response;
        std::mutex mutex;
        std::condition_variable completed;
        bool done{false};
    };
    Handler handler;
    std::mutex queue_mutex;
    std::deque<std::shared_ptr<Pending>> queue;
    std::shared_ptr<Pending> active_pending;
    std::atomic<bool> stopping{false};
    std::thread worker;
#ifdef _WIN32
    std::mutex startup_mutex;
    std::condition_variable startup_changed;
    bool startup_complete{false};
    bool startup_success{false};
    std::string startup_error;
    HWND window{nullptr};
    UINT_PTR timer_id{0};
    std::wstring pipe;
    std::filesystem::path manifest;
    std::atomic<HANDLE> active_pipe{INVALID_HANDLE_VALUE};
    static constexpr DWORD kMaximumFrameBytes = 1024 * 1024;
    static constexpr UINT_PTR kTimerBase = 0xA170;

    static std::mutex registry_mutex;
    static std::unordered_map<HWND, Impl*> registry;

    void report_startup(bool success, std::string message = {}) {
        {
            std::lock_guard guard(startup_mutex);
            if (startup_complete) return;
            startup_complete = true;
            startup_success = success;
            startup_error = std::move(message);
        }
        startup_changed.notify_all();
    }

    static VOID CALLBACK timer_proc(HWND hwnd, UINT, UINT_PTR timer, DWORD) {
        Impl* self = nullptr;
        {
            std::lock_guard guard(registry_mutex);
            const auto it = registry.find(hwnd);
            if (it != registry.end() && it->second->timer_id == timer) self = it->second;
        }
        if (self) self->dispatch_one();
    }

    void dispatch_one() {
        std::shared_ptr<Pending> pending;
        {
            std::lock_guard guard(queue_mutex);
            if (queue.empty()) return;
            pending = std::move(queue.front()); queue.pop_front();
            active_pending = pending;
        }
        automation::JsonValue response;
        try { response = handler(pending->request); }
        catch (const std::exception& error) {
            response = automation::JsonValue::Object{{"status", "error"}, {"code", "INTERNAL_ERROR"},
                                                      {"message", error.what()}};
        } catch (...) {
            response = automation::JsonValue::Object{{"status", "error"}, {"code", "INTERNAL_ERROR"},
                                                      {"message", "Automation request failed."}};
        }
        {
            std::lock_guard guard(pending->mutex);
            pending->response = std::move(response); pending->done = true;
        }
        {
            std::lock_guard guard(queue_mutex);
            active_pending.reset();
        }
        pending->completed.notify_one();
    }

    static constexpr DWORD kPipeTimeoutMs = 5000;

    static bool read_exact(HANDLE pipe, void* data, DWORD size, DWORD timeout_ms = kPipeTimeoutMs) {
        auto* bytes = static_cast<std::byte*>(data); DWORD received = 0;
        const auto start = GetTickCount64();
        while (received < size) {
            DWORD bytes_avail = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes_avail, nullptr)) return false;
            if (bytes_avail == 0) {
                if (GetTickCount64() - start > timeout_ms) return false;
                Sleep(5);
                continue;
            }
            const DWORD to_read = (std::min)(size - received, bytes_avail);
            DWORD count = 0;
            if (!ReadFile(pipe, bytes + received, to_read, &count, nullptr) || count == 0) return false;
            received += count;
        }
        return true;
    }
    static bool write_exact(HANDLE pipe, const void* data, DWORD size) {
        const auto* bytes = static_cast<const std::byte*>(data); DWORD sent = 0;
        while (sent < size) {
            DWORD count = 0;
            if (!WriteFile(pipe, bytes + sent, size - sent, &count, nullptr) || count == 0) return false;
            sent += count;
        }
        return true;
    }
    bool same_user_client(HANDLE pipe_handle) {
        ULONG client_pid = 0;
        if (!GetNamedPipeClientProcessId(pipe_handle, &client_pid) || client_pid == 0) return false;
        if (!ImpersonateNamedPipeClient(pipe_handle)) return false;
        HANDLE token = nullptr;
        bool same = false;
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
            DWORD required = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &required);
            std::vector<std::byte> client(required);
            DWORD server_required = 0;
            HANDLE process_token = nullptr;
            if (required && GetTokenInformation(token, TokenUser, client.data(), required, &required) &&
                OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token)) {
                GetTokenInformation(process_token, TokenUser, nullptr, 0, &server_required);
                std::vector<std::byte> server(server_required);
                if (server_required && GetTokenInformation(process_token, TokenUser, server.data(),
                                                           server_required, &server_required)) {
                    const auto* a = reinterpret_cast<const TOKEN_USER*>(client.data());
                    const auto* b = reinterpret_cast<const TOKEN_USER*>(server.data());
                    same = EqualSid(a->User.Sid, b->User.Sid) != FALSE;
                }
                CloseHandle(process_token);
            }
            CloseHandle(token);
        }
        RevertToSelf();
        return same;
    }
    bool process_client(HANDLE pipe_handle) {
        std::array<uint8_t, 4> frame{};
        if (!same_user_client(pipe_handle) || !read_exact(pipe_handle, frame.data(), 4)) return false;
        const uint32_t size = static_cast<uint32_t>(frame[0]) |
            (static_cast<uint32_t>(frame[1]) << 8) | (static_cast<uint32_t>(frame[2]) << 16) |
            (static_cast<uint32_t>(frame[3]) << 24);
        if (size == 0 || size > kMaximumFrameBytes) return false;
        std::string body(size, '\0');
        if (!read_exact(pipe_handle, body.data(), size)) return false;
        auto pending = std::make_shared<Pending>();
        std::string parse_error;
        if (!automation::JsonValue::parse(body, pending->request, parse_error)) {
            pending->response = automation::JsonValue::Object{{"status", "error"},
                {"code", "INVALID_ARGUMENT"}, {"message", "Malformed automation JSON."}};
            pending->done = true;
        } else {
            std::lock_guard guard(queue_mutex);
            if (queue.size() >= 32 || stopping.load(std::memory_order_acquire)) {
                pending->response = automation::JsonValue::Object{{"status", "error"},
                    {"code", "RESOURCE_LIMIT"}, {"message", "The app automation queue is full or closing."}};
                pending->done = true;
            } else queue.push_back(pending);
        }
        std::unique_lock lock(pending->mutex);
        pending->completed.wait(lock, [&] { return pending->done || stopping.load(std::memory_order_acquire); });
        if (!pending->done) return false;
        lock.unlock();
        const auto output = pending->response.serialize();
        if (output.empty() || output.size() > kMaximumFrameBytes) return false;
        const auto out_size = static_cast<uint32_t>(output.size());
        const std::array<uint8_t, 4> out_frame{
            static_cast<uint8_t>(out_size), static_cast<uint8_t>(out_size >> 8),
            static_cast<uint8_t>(out_size >> 16), static_cast<uint8_t>(out_size >> 24)};
        return write_exact(pipe_handle, out_frame.data(), 4) &&
               write_exact(pipe_handle, output.data(), out_size);
    }
    void run() {
        const auto name = pipe;
        while (!stopping.load(std::memory_order_acquire)) {
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            HANDLE token = nullptr;
            DWORD required = 0;
            std::wstring sid_text;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                report_startup(false, "Could not read the current user token."); break;
            }
            GetTokenInformation(token, TokenUser, nullptr, 0, &required);
            std::vector<std::byte> user(required);
            if (!required || !GetTokenInformation(token, TokenUser, user.data(), required, &required)) {
                CloseHandle(token); report_startup(false, "Could not read the current user SID."); break;
            }
            LPWSTR sid_string = nullptr;
            const auto* user_info = reinterpret_cast<const TOKEN_USER*>(user.data());
            if (!ConvertSidToStringSidW(user_info->User.Sid, &sid_string)) {
                CloseHandle(token); report_startup(false, "Could not encode the current user SID."); break;
            }
            sid_text = sid_string; LocalFree(sid_string); CloseHandle(token);
            const std::wstring sddl = L"D:P(A;;GA;;;" + sid_text + L")";
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
                report_startup(false, "Could not create a per-user pipe ACL."); break;
            }
            SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
            HANDLE handle = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                4, kMaximumFrameBytes + 4, kMaximumFrameBytes + 4, 0, &attributes);
            LocalFree(descriptor);
            if (handle == INVALID_HANDLE_VALUE) {
                report_startup(false, "Could not create the named pipe endpoint."); break;
            }
            report_startup(true);
            active_pipe.store(handle, std::memory_order_release);
            const BOOL connected = ConnectNamedPipe(handle, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
            if (connected && !stopping.load(std::memory_order_acquire)) (void)process_client(handle);
            FlushFileBuffers(handle); DisconnectNamedPipe(handle); CloseHandle(handle);
            active_pipe.store(INVALID_HANDLE_VALUE, std::memory_order_release);
        }
        report_startup(false, "The named pipe stopped before startup completed.");
        stopping.store(true, std::memory_order_release);
        std::lock_guard guard(queue_mutex);
        for (auto& pending : queue) pending->completed.notify_one();
        queue.clear();
        if (!manifest.empty()) {
            std::error_code ec; std::filesystem::remove(manifest, ec);
        }
    }

    static std::filesystem::path manifest_path() {
        const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (!size) return {};
        std::wstring value(size, L'\0');
        const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), size);
        if (!copied || copied >= size) return {};
        value.resize(copied);
        return std::filesystem::path(value) / L"PikDraw" / L"automation" /
            (std::to_wstring(GetCurrentProcessId()) + L".json");
    }
    bool write_manifest(std::string_view scopes) {
        manifest = manifest_path();
        if (manifest.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(manifest.parent_path(), ec);
        if (ec) return false;
        std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        const auto escaped_pipe = [&] {
            std::string utf8;
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, pipe.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) return utf8;
            utf8.resize(static_cast<size_t>(bytes));
            const int written = WideCharToMultiByte(CP_UTF8, 0, pipe.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
            if (written != bytes) return std::string{};
            utf8.resize(static_cast<size_t>(bytes - 1));
            return utf8;
        }();
        automation::JsonValue::Array scope_values;
        if (scopes.find("read") != std::string_view::npos) scope_values.emplace_back("document.read");
        if (scopes.find("create") != std::string_view::npos) scope_values.emplace_back("document.create");
        if (scopes.find("edit") != std::string_view::npos) scope_values.emplace_back("document.edit");
        if (scopes.find("file-read") != std::string_view::npos) scope_values.emplace_back("file.read");
        if (scopes.find("file-write") != std::string_view::npos) scope_values.emplace_back("file.write");
        automation::JsonValue value(automation::JsonValue::Object{
            {"instance_id", std::to_string(GetCurrentProcessId())}, {"pipe_name", escaped_pipe},
            {"granted_scopes", std::move(scope_values)}});
        output << value.serialize(); output.close();
        return output.good();
    }
#endif

    ~Impl() = default;
};

#ifdef _WIN32
std::mutex AutomationPipeHost::Impl::registry_mutex;
std::unordered_map<HWND, AutomationPipeHost::Impl*> AutomationPipeHost::Impl::registry;
#endif

AutomationPipeHost::AutomationPipeHost() : impl_(std::make_unique<Impl>()) {}
AutomationPipeHost::~AutomationPipeHost() { stop(); }

#ifdef _WIN32
bool AutomationPipeHost::start(HWND window, Handler handler, std::string_view granted_scopes, std::string& error) {
    if (!window || impl_->worker.joinable()) { error = "Automation pipe host is already active or has no window."; return false; }
    impl_->window = window;
    impl_->handler = std::move(handler);
    impl_->pipe = L"\\\\.\\pipe\\PikDraw.Automation.v1." + std::to_wstring(GetCurrentProcessId());
    impl_->manifest = Impl::manifest_path();
    if (impl_->manifest.empty()) { error = "LOCALAPPDATA is unavailable."; return false; }
    impl_->timer_id = Impl::kTimerBase + GetCurrentProcessId() % 0x7000;
    if (!impl_->write_manifest(granted_scopes)) { error = "Could not create the per-user automation instance manifest."; return false; }
    {
        std::lock_guard guard(Impl::registry_mutex);
        if (Impl::registry.contains(window)) {
            std::error_code ec; std::filesystem::remove(impl_->manifest, ec);
            error = "Automation timer already registered for this window."; return false;
        }
        Impl::registry.emplace(window, impl_.get());
    }
    if (!SetTimer(window, impl_->timer_id, 12, Impl::timer_proc)) {
        std::lock_guard guard(Impl::registry_mutex); Impl::registry.erase(window);
        std::error_code ec; std::filesystem::remove(impl_->manifest, ec);
        error = "Could not start the automation dispatch timer."; return false;
    }
    impl_->stopping.store(false, std::memory_order_release);
    {
        std::lock_guard guard(impl_->startup_mutex);
        impl_->startup_complete = false;
        impl_->startup_success = false;
        impl_->startup_error.clear();
    }
    impl_->worker = std::thread([self = impl_.get()] { self->run(); });
    std::unique_lock startup_lock(impl_->startup_mutex);
    const bool completed = impl_->startup_changed.wait_for(startup_lock, std::chrono::seconds(2),
        [this] { return impl_->startup_complete; });
    const bool started = completed && impl_->startup_success;
    if (!started) {
        error = completed ? impl_->startup_error : "Timed out while starting the named pipe endpoint.";
        startup_lock.unlock();
        stop();
        return false;
    }
    return true;
}

bool AutomationPipeHost::invoke(const automation::JsonValue& request,
                                automation::JsonValue& response, std::string& error,
                                const std::atomic<bool>* cancellation) {
    if (!impl_ || !impl_->window || impl_->stopping.load(std::memory_order_acquire)) {
        error = "PikDraw UI automation queue is not available.";
        return false;
    }
    if (cancellation && cancellation->load(std::memory_order_acquire)) {
        error = "The MCP request was cancelled because access was revoked.";
        return false;
    }
    const DWORD owner_thread = GetWindowThreadProcessId(impl_->window, nullptr);
    if (owner_thread != 0 && owner_thread == GetCurrentThreadId()) {
        try {
            response = impl_->handler(request);
            return true;
        } catch (const std::exception& exception) {
            response = automation::JsonValue::Object{{"status", "error"}, {"code", "INTERNAL_ERROR"},
                {"message", exception.what()}};
            return true;
        } catch (...) {
            response = automation::JsonValue::Object{{"status", "error"}, {"code", "INTERNAL_ERROR"},
                {"message", "Automation request failed."}};
            return true;
        }
    }

    auto pending = std::make_shared<Impl::Pending>();
    pending->request = request;
    {
        std::lock_guard guard(impl_->queue_mutex);
        if (impl_->queue.size() >= 32 || impl_->stopping.load(std::memory_order_acquire)) {
            error = "The app automation queue is full or closing.";
            return false;
        }
        impl_->queue.push_back(pending);
    }

    for (;;) {
        {
            std::unique_lock lock(pending->mutex);
            if (pending->completed.wait_for(lock, std::chrono::milliseconds(20),
                                            [&pending] { return pending->done; })) {
                response = pending->response;
                return true;
            }
        }
        const bool cancelled = cancellation && cancellation->load(std::memory_order_acquire);
        if (!cancelled && !impl_->stopping.load(std::memory_order_acquire)) continue;

        bool removed = false;
        {
            std::lock_guard queue_guard(impl_->queue_mutex);
            const auto queued = std::find(impl_->queue.begin(), impl_->queue.end(), pending);
            if (queued != impl_->queue.end()) {
                impl_->queue.erase(queued);
                std::lock_guard pending_guard(pending->mutex);
                pending->response = automation::JsonValue::Object{{"status", "error"},
                    {"code", "CANCELLED"}, {"message", "PikDraw MCP access was revoked."}};
                pending->done = true;
                removed = true;
            }
        }
        if (removed) pending->completed.notify_all();
        error = cancelled ? "The MCP request was cancelled because access was revoked."
                          : "PikDraw stopped accepting automation requests.";
        return false;
    }
}

void AutomationPipeHost::stop() noexcept {
    if (!impl_) return;
    impl_->stopping.store(true, std::memory_order_release);
    {
        std::lock_guard queue_guard(impl_->queue_mutex);
        const auto cancel = [](const std::shared_ptr<Impl::Pending>& pending) {
            if (!pending) return;
            {
                std::lock_guard guard(pending->mutex);
                pending->response = automation::JsonValue::Object{{"status", "error"},
                    {"code", "CANCELLED"}, {"message", "PikDraw is shutting down."}};
                pending->done = true;
            }
            pending->completed.notify_all();
        };
        for (const auto& pending : impl_->queue) cancel(pending);
        impl_->queue.clear(); cancel(impl_->active_pending);
    }
    if (impl_->window && impl_->timer_id) KillTimer(impl_->window, impl_->timer_id);
    {
        std::lock_guard guard(Impl::registry_mutex);
        if (impl_->window) Impl::registry.erase(impl_->window);
    }
    if (impl_->worker.joinable()) {
        (void)CancelSynchronousIo(impl_->worker.native_handle());
        impl_->worker.join();
    }
    if (!impl_->manifest.empty()) {
        std::error_code ec; std::filesystem::remove(impl_->manifest, ec);
    }
    impl_->window = nullptr; impl_->timer_id = 0;
}

const std::wstring& AutomationPipeHost::pipe_name() const noexcept { return impl_->pipe; }

size_t AutomationPipeHost::pending_request_count_for_e2e() noexcept {
    if (!impl_) return 0;
    std::lock_guard guard(impl_->queue_mutex);
    return impl_->queue.size();
}
#else
bool AutomationPipeHost::start(Handler, std::string& error) {
    error = "PikDraw MCP bridge is available only on Windows."; return false;
}
void AutomationPipeHost::stop() noexcept {}
#endif

} // namespace pikdraw::desktop
