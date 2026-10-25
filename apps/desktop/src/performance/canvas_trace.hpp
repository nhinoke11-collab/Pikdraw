#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace pikdraw::desktop {

// Internal timing markers only. In particular, PaintEnd and RenderEnd are not
// evidence that the corresponding pixels have been displayed by the OS.
enum class CanvasTraceKind : std::uint8_t {
    Input,
    InputDequeued,
    PaintBegin,
    PaintEnd,
    RenderBegin,
    RenderEnd,
    StartupBegin,
    StartupReady,
    FirstFrame,
    RecoveryScanBegin,
    RecoveryScanEnd,
    NativeSaveBegin,
    NativeSaveEnd,
    NativeOpenBegin,
    NativeOpenEnd,
};

struct CanvasTraceEvent {
    std::uint64_t sequence = 0;
    std::uint64_t event_id = 0;
    std::uint64_t frame_id = 0;
    std::int64_t qpc_ticks = 0;
    CanvasTraceKind kind = CanvasTraceKind::Input;
};

// Bounded multi-producer/single-consumer trace buffer.
//
// record() never waits for capacity or takes a mutex. It makes at most a
// bounded number of reservation attempts; events dropped because the ring is
// full or contention exhausts those attempts are counted by overflow_count().
// The consumer must call try_pop() from a background/drain thread, not from a
// UI/input/paint callback. Disabling does not wait for already-entered writers
// and does not discard queued events. Stop producers before destroying this
// object. On Windows the default clock is QueryPerformanceCounter (QPC).
class CanvasTrace final {
public:
    static constexpr std::size_t kDefaultCapacity = 4096;
    static constexpr unsigned kMaxReservationAttempts = 16;

    using CounterReader = bool (*)(void* context, std::int64_t& ticks) noexcept;

    explicit CanvasTrace(std::size_t capacity = kDefaultCapacity);

    // Clock injection is for deterministic tests. Production callers should
    // use the default QPC-backed constructor.
    CanvasTrace(std::size_t capacity,
                CounterReader counter_reader,
                void* counter_context,
                std::uint64_t counter_frequency);

    CanvasTrace(const CanvasTrace&) = delete;
    CanvasTrace& operator=(const CanvasTrace&) = delete;
    CanvasTrace(CanvasTrace&&) = delete;
    CanvasTrace& operator=(CanvasTrace&&) = delete;
    ~CanvasTrace() = default;

    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;

    // event_id correlates an input/command across trace points. frame_id is
    // zero when no frame has been assigned. A successful call assigns a
    // monotonically increasing ring sequence and records one QPC timestamp.
    [[nodiscard]] bool record(CanvasTraceKind kind,
                              std::uint64_t event_id,
                              std::uint64_t frame_id = 0) noexcept;

    // Exactly one consumer may drain the ring. This call is nonblocking and
    // performs no formatting, file I/O, or allocation.
    [[nodiscard]] bool try_pop(CanvasTraceEvent& event) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::uint64_t overflow_count() const noexcept;
    [[nodiscard]] std::uint64_t clock_failure_count() const noexcept;
    [[nodiscard]] std::uint64_t counter_frequency() const noexcept;

private:
    struct Slot {
        std::atomic<std::uint64_t> sequence{0};
        CanvasTraceEvent event{};
    };

    static bool read_qpc(void* context, std::int64_t& ticks) noexcept;
    static std::uint64_t qpc_frequency() noexcept;

    std::unique_ptr<Slot[]> slots_;
    const std::size_t capacity_;
    CounterReader counter_reader_;
    void* counter_context_;
    const std::uint64_t counter_frequency_;

    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> enqueue_position_{0};
    std::uint64_t dequeue_position_ = 0; // single-consumer state
    std::atomic<std::uint64_t> overflow_count_{0};
    std::atomic<std::uint64_t> clock_failure_count_{0};
};

} // namespace pikdraw::desktop
