#include "pikdraw/workers/job_object.hpp"

#include <algorithm>
#include <limits>
#include <thread>
#include <utility>

namespace pikdraw::workers {

namespace {
constexpr unsigned kSystemMemoryPressureLimitPercent = 95;
thread_local uint32_t system_memory_pressure_bypass_depth = 0;

bool ReadSystemMemoryStatus(MEMORYSTATUSEX& status) noexcept {
    status = {};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) != FALSE && status.ullTotalPhys != 0 &&
           status.ullAvailPhys <= status.ullTotalPhys;
}
}

ScopedSystemMemoryPressureBypass::ScopedSystemMemoryPressureBypass(bool enabled) noexcept
    : enabled_(enabled) {
    if (enabled_) ++system_memory_pressure_bypass_depth;
}

ScopedSystemMemoryPressureBypass::~ScopedSystemMemoryPressureBypass() noexcept {
    if (enabled_ && system_memory_pressure_bypass_depth > 0)
        --system_memory_pressure_bypass_depth;
}

size_t SystemMemoryHeadroomBeforePressureLimit() noexcept {
    if (system_memory_pressure_bypass_depth > 0)
        return std::numeric_limits<size_t>::max();
    MEMORYSTATUSEX status{};
    if (!ReadSystemMemoryStatus(status)) return 0;
    const uint64_t threshold = (status.ullTotalPhys / 100) * kSystemMemoryPressureLimitPercent +
        ((status.ullTotalPhys % 100) * kSystemMemoryPressureLimitPercent) / 100;
    const uint64_t used = status.ullTotalPhys - status.ullAvailPhys;
    if (used >= threshold) return 0;
    const uint64_t headroom = threshold - used;
    return static_cast<size_t>((std::min)(headroom,
        static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
}

size_t AvailablePhysicalMemoryBytes() noexcept {
    MEMORYSTATUSEX status{};
    if (!ReadSystemMemoryStatus(status)) return 0;
    return static_cast<size_t>((std::min)(status.ullAvailPhys,
        static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
}

bool IsSystemMemoryPressureLimitReached() noexcept {
    if (system_memory_pressure_bypass_depth > 0) return false;
    MEMORYSTATUSEX status{};
    if (!ReadSystemMemoryStatus(status)) return false;
    const uint64_t threshold = (status.ullTotalPhys / 100) * kSystemMemoryPressureLimitPercent +
        ((status.ullTotalPhys % 100) * kSystemMemoryPressureLimitPercent) / 100;
    return status.ullTotalPhys - status.ullAvailPhys >= threshold;
}

SystemMemoryPressureWaiter::SystemMemoryPressureWaiter(
    std::chrono::milliseconds maximum_wait,
    WaitStateCallback on_wait_state,
    HeadroomProbe headroom_probe,
    Sleeper sleeper,
    WaitRequirementCallback on_wait_requirement)
    : remaining_wait_(std::clamp(maximum_wait, std::chrono::milliseconds::zero(),
          std::chrono::duration_cast<std::chrono::milliseconds>(
              kSystemMemoryPressureWaitLimit))),
      on_wait_state_(std::move(on_wait_state)),
      headroom_probe_(std::move(headroom_probe)),
      sleeper_(std::move(sleeper)),
      on_wait_requirement_(std::move(on_wait_requirement)) {
    if (!headroom_probe_)
        headroom_probe_ = [] { return SystemMemoryHeadroomBeforePressureLimit(); };
    if (!sleeper_)
        sleeper_ = [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); };
}

SystemMemoryWaitResult SystemMemoryPressureWaiter::WaitForHeadroom(
    size_t required_bytes,
    std::stop_token cancellation,
    std::chrono::steady_clock::time_point* active_deadline) noexcept {
    const size_t required = (std::max)(required_bytes, size_t{1});
    const auto finish = [this](SystemMemoryWaitResult result) {
        if (waiting_) {
            waiting_ = false;
            if (on_wait_state_) {
                try { on_wait_state_(false); } catch (...) {}
            }
        }
        return result;
    };
    for (;;) {
        if (cancellation.stop_requested())
            return finish(SystemMemoryWaitResult::Cancelled);
        if (headroom_probe_() >= required)
            return finish(SystemMemoryWaitResult::Ready);
        if (remaining_wait_.count() <= 0)
            return finish(SystemMemoryWaitResult::TimedOut);
        if (!waiting_) {
            waiting_ = true;
            if (on_wait_state_) {
                try { on_wait_state_(true); } catch (...) {}
            }
            if (on_wait_requirement_) {
                try { on_wait_requirement_(required); } catch (...) {}
            }
        }

        const auto pause = (std::min)(remaining_wait_, std::chrono::milliseconds(100));
        const auto started = std::chrono::steady_clock::now();
        try {
            sleeper_(pause);
        } catch (...) {
            return finish(SystemMemoryWaitResult::TimedOut);
        }
        const auto actual = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        const auto charged = (std::min)(remaining_wait_, (std::max)(pause, actual));
        remaining_wait_ -= charged;
        if (active_deadline) *active_deadline += charged;
    }
}

bool JobObjectGuard::Create(const JobLimitsConfig& config) {
    HANDLE h = CreateJobObjectW(nullptr, nullptr);
    if (!h) return false;
    job_handle_ = ScopedHandle(h);

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = 0;

    if (config.kill_on_job_close) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    }
    if (config.max_memory_bytes > 0) {
        limits.BasicLimitInformation.LimitFlags |= (JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY);
        limits.ProcessMemoryLimit = config.max_memory_bytes;
        limits.JobMemoryLimit = config.max_memory_bytes;
    }
    const auto process_limit=config.prevent_child_processes ? 1u : config.active_process_limit;
    if (process_limit > 0) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = process_limit;
    }

    if (!SetInformationJobObject(
            job_handle_.Get(),
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits))) {
        job_handle_.Close();
        return false;
    }

    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_HANDLES |
        JOB_OBJECT_UILIMIT_GLOBALATOMS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
    if (!SetInformationJobObject(job_handle_.Get(), JobObjectBasicUIRestrictions,
                                 &ui, sizeof(ui))) {
        job_handle_.Close();
        return false;
    }
    return true;
}

bool JobObjectGuard::AssignProcess(HANDLE process_handle) noexcept {
    if (!job_handle_.IsValid()) return false;
    return AssignProcessToJobObject(job_handle_.Get(), process_handle) != FALSE;
}

bool JobObjectGuard::Terminate(uint32_t exit_code) noexcept {
    if (!job_handle_.IsValid()) return false;
    return TerminateJobObject(job_handle_.Get(), exit_code) != FALSE;
}

size_t JobObjectGuard::GetPeakMemoryUsage() const noexcept {
    if (!job_handle_.IsValid()) return 0;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (QueryInformationJobObject(
            job_handle_.Get(),
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits),
            nullptr)) {
        return limits.PeakJobMemoryUsed;
    }
    return 0;
}

} // namespace pikdraw::workers
