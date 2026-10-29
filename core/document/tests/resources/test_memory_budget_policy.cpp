#include "pikdraw/document/resources/memory_budget_policy.hpp"

#include <atomic>
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

using namespace pikdraw::document::resources;
constexpr uint64_t GiB = 1024ull * 1024 * 1024;

int main() {
  SystemMemoryStatus status{};
  auto policy = std::make_shared<MemoryBudgetPolicy>([&] { return status; });
  uint64_t previous_limit = 0;
  for (const auto ram : {4ull, 8ull, 32ull, 128ull}) {
    status = {ram * GiB, ram * GiB * 3 / 4, ram * GiB, ram * GiB};
    const auto working = policy->working_limit_bytes();
    assert(working > previous_limit && working == ram * GiB / 4);
    previous_limit = working;
    // Simulate a large image allocation without actually allocating the bytes.
    auto job = policy->try_reserve(working);
    assert(job && policy->reserved_bytes() == working);
    assert(!policy->try_reserve(1));
    job.reset();
    assert(policy->reserved_bytes() == 0);
    for (const auto video : {0ull, 2ull, 24ull}) {
      const auto limit = video_cache_limit_bytes(
          policy->cache_limit_bytes(), video * GiB, 0, 0);
      assert(limit <= policy->cache_limit_bytes() && limit <= video * GiB / 4);
      if (video) assert(limit > 0);
      else assert(limit == 0); // WARP callers use the RAM policy directly.
    }
  }

  status = {8 * GiB, 6 * GiB, 6 * GiB, 6 * GiB};
  auto preview = policy->try_reserve(GiB);
  auto histogram = policy->try_reserve(GiB / 2);
  auto upload = policy->try_reserve(GiB / 2);
  assert(preview && histogram && upload && !policy->try_reserve(1));
  histogram.reset();
  upload.reset();
  // A low-memory transition must preserve held data, refuse new allocations,
  // shrink cache retention, and recover without restarting the policy/service.
  status.available_physical_bytes = GiB / 4;
  assert(policy->available_bytes() == 0 && policy->cache_limit_bytes() == 0);
  assert(!policy->try_reserve(4));
  assert(policy->reserved_bytes() == GiB);
  preview.reset();
  status.available_physical_bytes = 6 * GiB;
  assert(policy->try_reserve(768ull * 1024 * 1024)); // above the old 512 MiB cap
  status.available_commit_bytes = 32;
  assert(!policy->try_reserve(17) && policy->try_reserve(16));
  status.available_commit_bytes = 6 * GiB;
  status.available_virtual_bytes = 16;
  assert(!policy->try_reserve(9) && policy->try_reserve(8));

  status = {};
  assert(!policy->try_reserve(1) && policy->cache_limit_bytes() == 0);
  status = {GiB, 2 * GiB, GiB, GiB}; // invalid sample fails closed
  assert(!policy->try_reserve(1));
  status = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
  assert(policy->available_bytes() <= UINT64_MAX / 4);
  assert(policy->try_reserve(UINT64_MAX / 4));
  MemoryBudgetPolicy unavailable([]() -> SystemMemoryStatus { throw 1; });
  assert(!unavailable.try_reserve(1));
  assert(video_cache_limit_bytes(GiB, 2 * GiB, 2 * GiB, 0) == 0);
  assert(video_cache_limit_bytes(GiB, 2 * GiB, 2 * GiB, GiB / 2) == GiB / 4);

  // Concurrent consumers cannot each spend the same headroom in the pool.
  status = {4 * GiB, 3 * GiB, 3 * GiB, 3 * GiB};
  std::atomic<unsigned> ready{};
  std::atomic<bool> release{};
  std::vector<std::jthread> threads;
  for (int index = 0; index < 8; ++index) {
    threads.emplace_back([&] {
      auto held = policy->try_reserve(GiB / 4);
      ready.fetch_add(1);
      while (!release.load()) std::this_thread::yield();
    });
  }
  while (ready.load() != threads.size()) std::this_thread::yield();
  assert(policy->reserved_bytes() == GiB);
  release = true;
  threads.clear();
  assert(policy->reserved_bytes() == 0);
  const auto live = query_system_memory_status();
  assert(live.total_physical_bytes && live.available_physical_bytes <= live.total_physical_bytes);
  std::cout << "[PASS] adaptive RAM/commit/VRAM policy, shared admission, pressure and recovery\n";
}
