#pragma once
#include "pikdraw/workers/win32_common.hpp"
#include <filesystem>
#include <stdexcept>
inline std::filesystem::path WorkerExecutable(const wchar_t *name) {
  std::wstring path(32768, L'\0');
  const DWORD size =
      GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!size || size >= path.size())
    throw std::runtime_error("Cannot locate test executable");
  path.resize(size);
  return std::filesystem::path(path).parent_path() / name;
}

inline std::filesystem::path WorkerTestExecutable() {
  return WorkerExecutable(L"spike_worker_test.exe");
}

inline std::filesystem::path WorkerProductionExecutable() {
  return WorkerExecutable(L"spike_worker.exe");
}
