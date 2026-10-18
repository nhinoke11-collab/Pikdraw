#include "pikdraw/workers/job_object.hpp"
#include "pikdraw/workers/process_launcher.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

using namespace pikdraw::workers;

std::wstring executable_path() {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    assert(size && size < path.size());
    path.resize(size);
    return path;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--memory") {
        // Given a 64 MiB job budget, a 128 MiB committed allocation must fail.
        void* allocation = VirtualAlloc(nullptr, 128ULL * 1024 * 1024,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (allocation) VirtualFree(allocation, 0, MEM_RELEASE);
        return allocation ? 11 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--spawn") {
        auto command = L"\"" + executable_path() + L"\" --sleep";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE;
        if (launched) {
            TerminateProcess(child.hProcess, 99);
            CloseHandle(child.hThread); CloseHandle(child.hProcess);
        }
        return launched ? 12 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--sleep") {
        Sleep(10000);
        return 0;
    }

    JobLimitsConfig limits;
    limits.max_memory_bytes = 64ULL * 1024 * 1024;
    JobObjectGuard job;
    assert(job.Create(limits));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION actual{};
    assert(QueryInformationJobObject(job.Get(), JobObjectExtendedLimitInformation,
                                     &actual, sizeof(actual), nullptr));
    assert(actual.BasicLimitInformation.ActiveProcessLimit == 1);
    assert(actual.ProcessMemoryLimit == limits.max_memory_bytes);
    assert(actual.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE);
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    assert(QueryInformationJobObject(job.Get(), JobObjectBasicUIRestrictions,
                                     &ui, sizeof(ui), nullptr));
    const DWORD required = JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                           JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
    assert((ui.UIRestrictionsClass & required) == required);

    // Exercise the shipping launch path (test fixture permits only the AppContainer bypass).
    for (const auto* mode : {L"--memory", L"--spawn"}) {
        WorkerLaunchConfig config;
        config.worker_executable_path = executable_path();
        config.use_appcontainer = false;
        config.trusted_test_worker = true;
        config.job_limits = limits;
        config.arguments = {mode};
        WorkerProcessHost host;
        assert(host.Launch(config));
        assert(host.WaitForExit(std::chrono::seconds(5)));
        assert(host.LastWorkerExitCode() == 0);
    }

    // Given a suspended process assigned before execution, closing the job kills it.
    auto command = L"\"" + executable_path() + L"\" --sleep";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    assert(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    ScopedHandle process_handle(process.hProcess), thread_handle(process.hThread);
    assert(job.AssignProcess(process_handle.Get()));
    assert(ResumeThread(thread_handle.Get()) != static_cast<DWORD>(-1));
    job.Reset();
    assert(WaitForSingleObject(process_handle.Get(), 5000) == WAIT_OBJECT_0);
    std::cout << "PASS Job Object memory/process/UI limits and kill-on-close\n";
}
