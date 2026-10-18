#include "pikdraw/workers/job_object.hpp"

namespace pikdraw::workers {

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
