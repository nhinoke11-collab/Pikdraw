#include "pikdraw/document/resources/memory_budget_policy.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pikdraw::document::resources {
namespace {
uint64_t additional_headroom(const SystemMemoryStatus& status) noexcept {
  if (!status.total_physical_bytes ||
      status.available_physical_bytes > status.total_physical_bytes) return 0;
  // Preserve at least 5% physical RAM for the OS and untracked allocations,
  // and admit only half of the remaining physical/commit/address headroom.
  const auto reserve = status.total_physical_bytes / 20;
  const auto physical = status.available_physical_bytes > reserve
      ? status.available_physical_bytes - reserve : 0;
  return std::min({physical, status.available_commit_bytes,
                   status.available_virtual_bytes}) / 2;
}

SystemMemoryStatus sample(const MemoryBudgetPolicy::StatusProvider& provider) noexcept {
  try { return provider ? provider() : SystemMemoryStatus{}; }
  catch (...) { return {}; }
}
}  // namespace

SystemMemoryStatus query_system_memory_status() noexcept {
#ifdef _WIN32
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status))
    return {status.ullTotalPhys, status.ullAvailPhys,
            status.ullAvailPageFile, status.ullAvailVirtual};
#elif defined(_SC_PHYS_PAGES) && defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
  const auto total = sysconf(_SC_PHYS_PAGES);
  const auto available = sysconf(_SC_AVPHYS_PAGES);
  const auto page_size = sysconf(_SC_PAGESIZE);
  if (total > 0 && available >= 0 && page_size > 0 &&
      static_cast<uint64_t>(total) <= std::numeric_limits<uint64_t>::max() /
          static_cast<uint64_t>(page_size))
    return {static_cast<uint64_t>(total) * static_cast<uint64_t>(page_size),
            static_cast<uint64_t>(available) * static_cast<uint64_t>(page_size),
            std::numeric_limits<uint64_t>::max(),
            std::numeric_limits<uint64_t>::max()};
#endif
  return {};
}

struct MemoryBudgetPolicy::State {
  explicit State(StatusProvider query) : provider(std::move(query)) {}
  mutable std::mutex mutex;
  StatusProvider provider;
  uint64_t reserved{};
};

MemoryBudgetPolicy::MemoryBudgetPolicy(StatusProvider provider)
    : state_(std::make_shared<State>(std::move(provider))) {}

MemoryBudgetPolicy::Reservation::~Reservation() {
  std::lock_guard lock(state_->mutex);
  state_->reserved -= bytes_;
}

std::shared_ptr<MemoryBudgetPolicy::Reservation>
MemoryBudgetPolicy::try_reserve(uint64_t bytes) {
  std::unique_lock lock(state_->mutex);
  const auto status = sample(state_->provider);
  const auto limit = status.total_physical_bytes / 4;
  if (state_->reserved > limit || bytes > limit - state_->reserved ||
      bytes > additional_headroom(status)) return {};
  state_->reserved += bytes;
  // Construct unique ownership first: if the shared_ptr control block throws,
  // the reservation destructor releases its charge exactly once.
  std::unique_ptr<Reservation> reservation;
  try { reservation.reset(new Reservation(state_, bytes)); }
  catch (...) { state_->reserved -= bytes; throw; }
  // Unlock before shared_ptr construction can invoke the destructor on failure.
  lock.unlock();
  return std::shared_ptr<Reservation>(std::move(reservation));
}

uint64_t MemoryBudgetPolicy::available_bytes() const noexcept {
  std::lock_guard lock(state_->mutex);
  const auto status = sample(state_->provider);
  const auto limit = status.total_physical_bytes / 4;
  return std::min(state_->reserved < limit ? limit - state_->reserved : 0,
                  additional_headroom(status));
}

uint64_t MemoryBudgetPolicy::working_limit_bytes() const noexcept {
  std::lock_guard lock(state_->mutex);
  return sample(state_->provider).total_physical_bytes / 4;
}

uint64_t MemoryBudgetPolicy::cache_limit_bytes() const noexcept {
  std::lock_guard lock(state_->mutex);
  const auto status = sample(state_->provider);
  return std::min(status.total_physical_bytes / 16, additional_headroom(status));
}

uint64_t MemoryBudgetPolicy::reserved_bytes() const noexcept {
  std::lock_guard lock(state_->mutex);
  return state_->reserved;
}

std::shared_ptr<MemoryBudgetPolicy> process_memory_budget_policy() {
  static const auto policy = std::make_shared<MemoryBudgetPolicy>();
  return policy;
}

uint64_t video_cache_limit_bytes(uint64_t ram_limit, uint64_t video_budget,
    uint64_t video_usage, uint64_t cached_video_bytes) noexcept {
  const auto other_usage = video_usage > cached_video_bytes
      ? video_usage - cached_video_bytes : 0;
  const auto headroom = other_usage < video_budget ? video_budget - other_usage : 0;
  return std::min({ram_limit, video_budget / 4, headroom / 2});
}
}  // namespace pikdraw::document::resources
