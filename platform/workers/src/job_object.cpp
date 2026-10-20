#include "pikdraw/workers/job_object.hpp"

#include <algorithm>
#include <limits>

namespace pikdraw::workers {

namespace {
constexpr unsigned kSystemMemoryPressureLimitPercent = 90;

bool ReadSystemMemoryStatus(MEMORYSTATUSEX& status) noexcept {
    status = {};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) != FALSE && status.ullTotalPhys != 0 &&
           status.ullAvailPhys <= status.ullTotalPhys;
}
}

size_t SystemMemoryHeadroomBeforePressureLimit() noexcept {
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

bool IsSystemMemoryPressureLimitReached() noexcept {
    MEMORYSTATUSEX status{};
    if (!ReadSystemMemoryStatus(status)) return false;
    const uint64_t threshold = (status.ullTotalPhys / 100) * kSystemMemoryPressureLimitPercent +
        ((status.ullTotalPhys % 100) * kSystemMemoryPressureLimitPercent) / 100;
    return status.ullTotalPhys - status.ullAvailPhys >= threshold;
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
