#include "pikdraw/raster/memory_budget.hpp"

#include <limits>
#include <utility>

namespace pikdraw::raster {

RasterMemoryBudget::Reservation::~Reservation() {
  if (!state_ || bytes_ == 0) return;
  std::lock_guard lock(state_->mutex);
  state_->reserved_bytes = bytes_ <= state_->reserved_bytes
      ? state_->reserved_bytes - bytes_ : 0;
}

RasterMemoryBudget::RasterMemoryBudget(
    uint64_t capacity_bytes, std::function<bool(uint64_t)> admission_callback,
    std::shared_ptr<document::resources::MemoryBudgetPolicy> adaptive_policy)
    : state_(std::make_shared<State>()) {
  state_->capacity_bytes = capacity_bytes;
  state_->admission_callback = std::move(admission_callback);
  state_->adaptive_policy = std::move(adaptive_policy);
}

std::shared_ptr<RasterMemoryBudget::Reservation>
RasterMemoryBudget::try_reserve(uint64_t bytes) {
  std::function<bool(uint64_t)> admission_callback;
  {
    std::lock_guard lock(state_->mutex);
    if (state_->adaptive_policy)
      state_->capacity_bytes = state_->adaptive_policy->working_limit_bytes();
    if (bytes > state_->capacity_bytes ||
        state_->reserved_bytes > state_->capacity_bytes - bytes)
      return {};
    admission_callback = state_->admission_callback;
  }
  if (admission_callback) {
    try {
      if (!admission_callback(bytes)) return {};
    } catch (...) {
      return {};
    }
  }
  std::unique_lock lock(state_->mutex);
  if (bytes > state_->capacity_bytes ||
      state_->reserved_bytes > state_->capacity_bytes - bytes)
    return {};
  auto system_reservation = state_->adaptive_policy
      ? state_->adaptive_policy->try_reserve(bytes) : nullptr;
  if (state_->adaptive_policy && !system_reservation) return {};
  state_->reserved_bytes += bytes;
  std::unique_ptr<Reservation> reservation;
  try {
    reservation.reset(new Reservation(state_, bytes));
  } catch (...) {
    state_->reserved_bytes -= bytes;
    throw;
  }
  reservation->system_reservation_ = std::move(system_reservation);
  lock.unlock();
  return std::shared_ptr<Reservation>(std::move(reservation));
}

std::shared_ptr<RasterMemoryBudget::Reservation>
RasterMemoryBudget::try_reserve_with_headroom(
    uint64_t bytes, uint64_t additional_headroom_bytes) {
  if (state_->adaptive_policy) return try_reserve(bytes);
  std::function<bool(uint64_t)> admission_callback;
  {
    std::lock_guard lock(state_->mutex);
    admission_callback = state_->admission_callback;
  }
  if (admission_callback) {
    try {
      if (!admission_callback(bytes)) return {};
    } catch (...) {
      return {};
    }
  }
  std::lock_guard lock(state_->mutex);
  const uint64_t capacity = additional_headroom_bytes >
          std::numeric_limits<uint64_t>::max() - state_->reserved_bytes
      ? std::numeric_limits<uint64_t>::max()
      : state_->reserved_bytes + additional_headroom_bytes;
  state_->capacity_bytes = capacity;
  if (bytes > state_->capacity_bytes ||
      state_->reserved_bytes > state_->capacity_bytes - bytes)
    return {};
  state_->reserved_bytes += bytes;
  try {
    return std::shared_ptr<Reservation>(new Reservation(state_, bytes));
  } catch (...) {
    state_->reserved_bytes -= bytes;
    throw;
  }
}

void RasterMemoryBudget::set_capacity(uint64_t capacity_bytes) noexcept {
  std::lock_guard lock(state_->mutex);
  state_->capacity_bytes = capacity_bytes;
}

uint64_t RasterMemoryBudget::capacity_bytes() const noexcept {
  std::lock_guard lock(state_->mutex);
  if (state_->adaptive_policy)
    return state_->adaptive_policy->working_limit_bytes();
  return state_->capacity_bytes;
}

uint64_t RasterMemoryBudget::reserved_bytes() const noexcept {
  std::lock_guard lock(state_->mutex);
  return state_->reserved_bytes;
}

uint64_t RasterMemoryBudget::available_bytes() const noexcept {
  std::lock_guard lock(state_->mutex);
  if (state_->adaptive_policy) return state_->adaptive_policy->available_bytes();
  if (state_->reserved_bytes >= state_->capacity_bytes) return 0;
  return state_->capacity_bytes - state_->reserved_bytes;
}

}  // namespace pikdraw::raster
