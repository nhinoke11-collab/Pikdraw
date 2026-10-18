#pragma once

#include "win32_common.hpp"
#include "job_object.hpp"
#include "appcontainer.hpp"
#include "source_snapshot.hpp"
#include "pikdraw/interop/ipc_dto.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>

namespace pikdraw::workers {

struct WorkerLaunchConfig {
    std::wstring worker_executable_path;
    std::wstring working_directory;
    bool use_appcontainer{true};
    // Only repository-owned smoke-test binaries may use this opt-in.
    // Production parsers/plugins must never receive this launch configuration.
    bool trusted_test_worker{false};
    HANDLE test_inherited_handle{INVALID_HANDLE_VALUE}; // Positive control only; never sandboxed/plugin launches.
    std::vector<std::wstring> arguments;
    std::wstring appcontainer_profile_name;
    const SourceSnapshot* source_snapshot{nullptr};
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

    // A v2 request is never downgraded to the legacy raw-byte protocol.
    [[nodiscard]] bool NegotiateV2(
        uint32_t required_features,
        std::chrono::milliseconds timeout);

    // Streams bounded v2 proposal frames to one consumer with backpressure.
    [[nodiscard]] bool TransactV2ProposalStream(
        std::span<const uint8_t> request_bytes,
        std::chrono::milliseconds timeout,
        const pikdraw::interop::V2ProposalStreamLimits& limits,
        const std::function<bool(const pikdraw::interop::IpcV2Frame&)>& consumer,
        std::stop_token cancellation = {});

    // Sends framed packet and waits for response up to timeout
    [[nodiscard]] std::optional<std::vector<uint8_t>> TransactBinaryPayload(
        std::span<const uint8_t> request_bytes,
        std::chrono::milliseconds timeout);

    [[nodiscard]] bool IsAlive() const noexcept;
    [[nodiscard]] bool WaitForExit(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] const char* LastTransportFailure() const noexcept { return last_transport_failure_; }
    [[nodiscard]] DWORD LastWorkerExitCode() const noexcept { return last_worker_exit_code_; }
    [[nodiscard]] const AppContainerProfileGuard& GetAppContainer() const noexcept { return app_container_; }

private:
    ScopedHandle process_handle_;
    ScopedHandle thread_handle_;
    ScopedHandle stdin_write_handle_;
    ScopedHandle stdout_read_handle_;
    JobObjectGuard job_object_;
    AppContainerProfileGuard app_container_;
    uint32_t process_id_{0};
    bool has_source_capability_{false};
    uint64_t source_size_{0};
    SourceToken source_token_{};
    SourceDigest source_sha256_{};
    bool v2_negotiated_{false};
    uint32_t v2_features_{0};
    const char* last_transport_failure_{"none"};
    DWORD last_worker_exit_code_{STILL_ACTIVE};
};

} // namespace pikdraw::workers
