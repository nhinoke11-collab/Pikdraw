#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace pikdraw::raster {

/// Shared reservation budget for decoded tiles, codec scratch, and upload staging.
/// A reservation lives as long as every consumer holding the associated data.
class RasterMemoryBudget final {
 private:
  struct State {
    mutable std::mutex mutex;
    uint64_t capacity_bytes{0};
    uint64_t reserved_bytes{0};
    std::function<bool(uint64_t)> admission_callback;
  };

 public:
  class Reservation final {
   public:
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    ~Reservation();

    [[nodiscard]] uint64_t bytes() const noexcept { return bytes_; }

   private:
    friend class RasterMemoryBudget;
    Reservation(std::shared_ptr<State> state, uint64_t bytes)
        : state_(std::move(state)), bytes_(bytes) {}
    std::shared_ptr<State> state_;
    uint64_t bytes_{0};
  };

  explicit RasterMemoryBudget(
      uint64_t capacity_bytes,
      std::function<bool(uint64_t)> admission_callback = {});

  [[nodiscard]] std::shared_ptr<Reservation> try_reserve(uint64_t bytes);
  /// Admit against freshly measured external headroom while preserving bytes
  /// already reserved by this budget. The value is additional capacity, not a
  /// replacement for existing reservations.
  [[nodiscard]] std::shared_ptr<Reservation> try_reserve_with_headroom(
      uint64_t bytes, uint64_t additional_headroom_bytes);
  void set_capacity(uint64_t capacity_bytes) noexcept;
  [[nodiscard]] uint64_t capacity_bytes() const noexcept;
  [[nodiscard]] uint64_t reserved_bytes() const noexcept;
  [[nodiscard]] uint64_t available_bytes() const noexcept;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace pikdraw::raster
