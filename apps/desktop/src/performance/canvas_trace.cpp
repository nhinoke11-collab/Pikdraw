#include "canvas_trace.hpp"

#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <chrono>
#endif

namespace pikdraw::desktop {

CanvasTrace::CanvasTrace(std::size_t capacity)
    : CanvasTrace(capacity, &CanvasTrace::read_qpc, nullptr, qpc_frequency()) {}

CanvasTrace::CanvasTrace(std::size_t capacity,
                         CounterReader counter_reader,
                         void* counter_context,
                         std::uint64_t counter_frequency)
    : slots_(capacity == 0 ? nullptr : std::make_unique<Slot[]>(capacity)),
      capacity_(capacity),
      counter_reader_(counter_reader),
      counter_context_(counter_context),
      counter_frequency_(counter_frequency) {
    if (capacity_ == 0) {
        throw std::invalid_argument("CanvasTrace capacity must be greater than zero");
    }
    if (counter_reader_ == nullptr) {
        throw std::invalid_argument("CanvasTrace counter reader must not be null");
    }

    for (std::size_t i = 0; i < capacity_; ++i) {
        slots_[i].sequence.store(static_cast<std::uint64_t>(i), std::memory_order_relaxed);
    }
}

void CanvasTrace::set_enabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_release);
}

bool CanvasTrace::enabled() const noexcept {
    return enabled_.load(std::memory_order_acquire);
}

bool CanvasTrace::record(CanvasTraceKind kind,
                         std::uint64_t event_id,
                         std::uint64_t frame_id) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) {
        return false;
    }

    std::int64_t ticks = 0;
    if (!counter_reader_(counter_context_, ticks)) {
        clock_failure_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto position = enqueue_position_.load(std::memory_order_relaxed);
    for (unsigned attempt = 0; attempt < kMaxReservationAttempts; ++attempt) {
        Slot& slot = slots_[static_cast<std::size_t>(position % capacity_)];
        const auto slot_sequence = slot.sequence.load(std::memory_order_acquire);

        if (slot_sequence == position) {
            if (enqueue_position_.compare_exchange_weak(position,
                                                        position + 1,
                                                        std::memory_order_relaxed,
                                                        std::memory_order_relaxed)) {
                slot.event = CanvasTraceEvent{position, event_id, frame_id, ticks, kind};
                slot.sequence.store(position + 1, std::memory_order_release);
                return true;
            }
            // compare_exchange_weak updates position when it observes a newer
            // reservation. Retry only within the fixed attempt budget.
            continue;
        }

        if (slot_sequence < position) {
            overflow_count_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        position = enqueue_position_.load(std::memory_order_relaxed);
    }

    // Contention is treated as a dropped event once the bounded reservation
    // budget is exhausted. The producer never waits for another writer.
    overflow_count_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool CanvasTrace::try_pop(CanvasTraceEvent& event) noexcept {
    Slot& slot = slots_[static_cast<std::size_t>(dequeue_position_ % capacity_)];
    const auto expected_sequence = dequeue_position_ + 1;
    if (slot.sequence.load(std::memory_order_acquire) != expected_sequence) {
        return false;
    }

    event = slot.event;
    slot.sequence.store(dequeue_position_ + static_cast<std::uint64_t>(capacity_),
                        std::memory_order_release);
    ++dequeue_position_;
    return true;
}

std::size_t CanvasTrace::capacity() const noexcept {
    return capacity_;
}

std::uint64_t CanvasTrace::overflow_count() const noexcept {
    return overflow_count_.load(std::memory_order_relaxed);
}

std::uint64_t CanvasTrace::clock_failure_count() const noexcept {
    return clock_failure_count_.load(std::memory_order_relaxed);
}

std::uint64_t CanvasTrace::counter_frequency() const noexcept {
    return counter_frequency_;
}

bool CanvasTrace::read_qpc(void*, std::int64_t& ticks) noexcept {
#if defined(_WIN32)
    LARGE_INTEGER counter{};
    if (QueryPerformanceCounter(&counter) == 0) {
        return false;
    }
    ticks = counter.QuadPart;
    return true;
#else
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    return true;
#endif
}

std::uint64_t CanvasTrace::qpc_frequency() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) == 0 || frequency.QuadPart <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(frequency.QuadPart);
#else
    // The desktop runtime targets Win32/QPC. This steady-clock frequency is
    // only a portable fallback for non-Windows unit-test builds.
    return 1'000'000'000ULL;
#endif
}

} // namespace pikdraw::desktop
