#include "resource_budget.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pikdraw::document::storage_detail {

uint64_t native_read_memory_budget_bytes() noexcept {
  uint64_t available = 0;
#ifdef _WIN32
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status))
    available = status.ullAvailPhys;
#elif defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
  const auto pages = sysconf(_SC_AVPHYS_PAGES);
  const auto page_size = sysconf(_SC_PAGESIZE);
  if (pages > 0 && page_size > 0)
    available = static_cast<uint64_t>(pages) *
                static_cast<uint64_t>(page_size);
#endif
  return available / 2;
}

} // namespace pikdraw::document::storage_detail
