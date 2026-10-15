#pragma once

#include "win32_common.hpp"
#include "job_object.hpp"
#include "appcontainer.hpp"
#include "pikdraw/interop/ipc_dto.hpp"
#include <chrono>
#include <optional>
#include <span>

namespace pikdraw::workers {

struct WorkerLaunchConfig {
    std::wstring worker_executable_path;
    std::wstring working_directory;
    bool use_appcontainer{true};
    // Only repository-owned smoke-test binaries may use this opt-in.
    // Production parsers/plugins must never receive this launch configuration.
    bool trusted_test_worker{false};
    std::vector<std::wstring> arguments;
    std::wstring appcontainer_profile_name;
    JobLimitsConfig job_limits;
    std::chrono::milliseconds default_timeout{5000};
};

class WorkerProcessHost {
public:
    WorkerProcessHost() noexcept = default;
    ~WorkerProcessHost() noexcept {
        Terminate();
    }

    WorkerProcessHost(const WorkerProcessHost&) = delete;
    WorkerProcessHost& operator=(const WorkerProcessHost&) = delete;

    [[nodiscard]] bool Launch(const WorkerLaunchConfig& config);
    void Terminate() noexcept;

    // Sends framed packet and waits for response up to timeout
    [[nodiscard]] std::optional<std::vector<uint8_t>> TransactBinaryPayload(
        std::span<const uint8_t> request_bytes,
        std::chrono::milliseconds timeout);

    [[nodiscard]] bool IsAlive() const noexcept;
    [[nodiscard]] const AppContainerProfileGuard& GetAppContainer() const noexcept { return app_container_; }

private:
    ScopedHandle process_handle_;
    ScopedHandle thread_handle_;
    ScopedHandle stdin_write_handle_;
    ScopedHandle stdout_read_handle_;
    JobObjectGuard job_object_;
    AppContainerProfileGuard app_container_;
    uint32_t process_id_{0};
};

} // namespace pikdraw::workers
