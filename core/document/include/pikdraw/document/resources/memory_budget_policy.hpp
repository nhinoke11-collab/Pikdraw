#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace pikdraw::document::resources {

struct SystemMemoryStatus {
  uint64_t total_physical_bytes{};
  uint64_t available_physical_bytes{};
  uint64_t available_commit_bytes{};
  uint64_t available_virtual_bytes{};
};

[[nodiscard]] SystemMemoryStatus query_system_memory_status() noexcept;

/// A process-shared admission policy for transient raster work and caches.
/// It does not own or limit authoritative document/source/Undo data.
class MemoryBudgetPolicy final {
  struct State;
 public:
  using StatusProvider = std::function<SystemMemoryStatus()>;
  class Reservation final {
   public:
    ~Reservation();
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
   private:
    friend class MemoryBudgetPolicy;
    Reservation(std::shared_ptr<State> state, uint64_t bytes)
        : state_(std::move(state)), bytes_(bytes) {}
    std::shared_ptr<State> state_;
    uint64_t bytes_{};
  };

  explicit MemoryBudgetPolicy(StatusProvider provider = query_system_memory_status);
  [[nodiscard]] std::shared_ptr<Reservation> try_reserve(uint64_t bytes);
  [[nodiscard]] uint64_t available_bytes() const noexcept;
  [[nodiscard]] uint64_t working_limit_bytes() const noexcept;
  [[nodiscard]] uint64_t cache_limit_bytes() const noexcept;
  [[nodiscard]] uint64_t reserved_bytes() const noexcept;

 private:
  std::shared_ptr<State> state_;
};

[[nodiscard]] std::shared_ptr<MemoryBudgetPolicy> process_memory_budget_policy();

/// Zero GPU budget means no retention; callers without DXGI information use
/// the RAM limit directly. Existing cached allocations are part of usage.
[[nodiscard]] uint64_t video_cache_limit_bytes(
    uint64_t ram_limit, uint64_t video_budget, uint64_t video_usage,
    uint64_t cached_video_bytes) noexcept;

}  // namespace pikdraw::document::resources
