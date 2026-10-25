#pragma once

#include "win32_common.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <stop_token>

namespace pikdraw::workers {

struct JobLimitsConfig {
    // Zero disables the per-process memory cap; import paths then use the
    // system-wide 95% RAM pressure waiter instead.
    size_t max_memory_bytes{256 * 1024 * 1024};
    uint32_t active_process_limit{1};
    bool kill_on_job_close{true};
    bool prevent_child_processes{true};
};

/// Temporarily uses a caller's explicit per-job memory quota in place of the
/// shared machine-wide 95% pressure gate on the current thread.
class ScopedSystemMemoryPressureBypass final {
public:
    explicit ScopedSystemMemoryPressureBypass(bool enabled) noexcept;
    ~ScopedSystemMemoryPressureBypass() noexcept;
    ScopedSystemMemoryPressureBypass(const ScopedSystemMemoryPressureBypass&) = delete;
    ScopedSystemMemoryPressureBypass& operator=(const ScopedSystemMemoryPressureBypass&) = delete;

private:
    bool enabled_{false};
};

// Import workers may use available system RAM up to the shared 95% pressure
// threshold. A zero headroom result means the system is already at that limit
// or Windows could not provide a reliable memory snapshot.
[[nodiscard]] size_t SystemMemoryHeadroomBeforePressureLimit() noexcept;
[[nodiscard]] size_t AvailablePhysicalMemoryBytes() noexcept;
[[nodiscard]] bool IsSystemMemoryPressureLimitReached() noexcept;

enum class SystemMemoryWaitResult : uint8_t { Ready, Cancelled, TimedOut };
inline constexpr auto kSystemMemoryPressureWaitLimit = std::chrono::minutes(5);

/// Cooperative import backpressure. A waiter is shared across an import so
/// repeated pressure pauses consume one cumulative five-minute allowance.
class SystemMemoryPressureWaiter final {
public:
    using WaitStateCallback = std::function<void(bool)>;
    using WaitRequirementCallback = std::function<void(size_t)>;
    using HeadroomProbe = std::function<size_t()>;
    using Sleeper = std::function<void(std::chrono::milliseconds)>;

    explicit SystemMemoryPressureWaiter(
        std::chrono::milliseconds maximum_wait = kSystemMemoryPressureWaitLimit,
        WaitStateCallback on_wait_state = {},
        HeadroomProbe headroom_probe = {},
        Sleeper sleeper = {},
        WaitRequirementCallback on_wait_requirement = {});

    [[nodiscard]] SystemMemoryWaitResult WaitForHeadroom(
        size_t required_bytes,
        std::stop_token cancellation = {},
        std::chrono::steady_clock::time_point* active_deadline = nullptr) noexcept;
    [[nodiscard]] std::chrono::milliseconds remaining_wait() const noexcept {
        return remaining_wait_;
    }

private:
    std::chrono::milliseconds remaining_wait_{};
    WaitStateCallback on_wait_state_;
    HeadroomProbe headroom_probe_;
    Sleeper sleeper_;
    WaitRequirementCallback on_wait_requirement_;
    bool waiting_{false};
};

class JobObjectGuard {
public:
    JobObjectGuard() noexcept = default;
    ~JobObjectGuard() noexcept = default;

    JobObjectGuard(const JobObjectGuard&) = delete;
    JobObjectGuard& operator=(const JobObjectGuard&) = delete;

    JobObjectGuard(JobObjectGuard&&) noexcept = default;
    JobObjectGuard& operator=(JobObjectGuard&&) noexcept = default;

    [[nodiscard]] bool Create(const JobLimitsConfig& config);
    [[nodiscard]] bool AssignProcess(HANDLE process_handle) noexcept;
    [[nodiscard]] bool Terminate(uint32_t exit_code) noexcept;
    [[nodiscard]] size_t GetPeakMemoryUsage() const noexcept;

    [[nodiscard]] HANDLE Get() const noexcept { return job_handle_.Get(); }
    [[nodiscard]] bool IsValid() const noexcept { return job_handle_.IsValid(); }
    void Reset() noexcept { job_handle_.Close(); }

private:
    ScopedHandle job_handle_;
};

} // namespace pikdraw::workers
