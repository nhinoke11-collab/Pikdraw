#pragma once

#include "win32_common.hpp"
#include <cstdint>

namespace pikdraw::workers {

struct JobLimitsConfig {
    size_t max_memory_bytes{256 * 1024 * 1024}; // 256 MiB
    uint32_t active_process_limit{1};
    bool kill_on_job_close{true};
    bool prevent_child_processes{true};
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
