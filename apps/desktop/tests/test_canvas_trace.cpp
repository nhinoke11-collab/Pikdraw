#include "../src/performance/canvas_trace.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

using pikdraw::desktop::CanvasTrace;
using pikdraw::desktop::CanvasTraceEvent;
using pikdraw::desktop::CanvasTraceKind;

namespace {

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    std::abort();
}

#define CHECK(expression) ((expression) ? static_cast<void>(0) : fail(#expression, __LINE__))

struct TestClock {
    std::atomic<std::int64_t> ticks{1000};
    std::atomic<bool> fail_reads{false};
};

bool read_test_clock(void* context, std::int64_t& ticks) noexcept {
    auto& clock = *static_cast<TestClock*>(context);
    if (clock.fail_reads.load(std::memory_order_relaxed)) {
        return false;
    }
    ticks = clock.ticks.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void test_enable_disable_and_clock_failure() {
    TestClock clock;
    CanvasTrace trace(4, &read_test_clock, &clock, 1'000'000);

    CHECK(!trace.enabled());
    CHECK(!trace.record(CanvasTraceKind::Input, 1, 0));
    CHECK(trace.overflow_count() == 0);
    CHECK(trace.clock_failure_count() == 0);

    trace.set_enabled(true);
    CHECK(trace.enabled());
    clock.fail_reads.store(true, std::memory_order_relaxed);
    CHECK(!trace.record(CanvasTraceKind::PaintBegin, 2, 7));
    CHECK(trace.clock_failure_count() == 1);
    CHECK(trace.overflow_count() == 0);

    clock.fail_reads.store(false, std::memory_order_relaxed);
    CHECK(trace.record(CanvasTraceKind::Input, 3, 0));
    trace.set_enabled(false);
    CHECK(!trace.enabled());
    CHECK(!trace.record(CanvasTraceKind::PaintEnd, 3, 7));

    CanvasTraceEvent event{};
    CHECK(trace.try_pop(event));
    CHECK(event.event_id == 3);
    CHECK(event.frame_id == 0);
    CHECK(event.qpc_ticks == 1000);
    CHECK(!trace.try_pop(event));
}

void test_overflow_order_ids_and_wraparound() {
    TestClock clock;
    CanvasTrace trace(2, &read_test_clock, &clock, 10'000);
    trace.set_enabled(true);

    CHECK(trace.record(CanvasTraceKind::Input, 41, 0));
    CHECK(trace.record(CanvasTraceKind::InputDequeued, 41, 8));
    CHECK(!trace.record(CanvasTraceKind::PaintBegin, 41, 8));
    CHECK(trace.overflow_count() == 1);

    CanvasTraceEvent first{};
    CanvasTraceEvent second{};
    CHECK(trace.try_pop(first));
    CHECK(trace.try_pop(second));
    CHECK(first.sequence == 0);
    CHECK(second.sequence == 1);
    CHECK(first.event_id == 41 && second.event_id == 41);
    CHECK(first.frame_id == 0 && second.frame_id == 8);
    CHECK(first.kind == CanvasTraceKind::Input);
    CHECK(second.kind == CanvasTraceKind::InputDequeued);
    CHECK(first.qpc_ticks == 1000 && second.qpc_ticks == 1001);

    CHECK(trace.record(CanvasTraceKind::RenderBegin, 41, 8));
    CanvasTraceEvent wrapped{};
    CHECK(trace.try_pop(wrapped));
    CHECK(wrapped.sequence == 2);
    CHECK(wrapped.kind == CanvasTraceKind::RenderBegin);
    CHECK(trace.overflow_count() == 1);
}

void test_multiple_producers_and_single_background_consumer() {
    constexpr std::uint64_t producer_count = 4;
    constexpr std::uint64_t events_per_producer = 3000;
    constexpr std::uint64_t expected_attempts = producer_count * events_per_producer;

    TestClock clock;
    CanvasTrace trace(128, &read_test_clock, &clock, 10'000'000);
    trace.set_enabled(true);

    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> finished{0};
    std::vector<CanvasTraceEvent> drained;
    drained.reserve(expected_attempts);

    std::thread consumer([&] {
        CanvasTraceEvent event{};
        while (finished.load(std::memory_order_acquire) != producer_count) {
            if (trace.try_pop(event)) {
                drained.push_back(event);
            } else {
                std::this_thread::yield();
            }
        }
        while (trace.try_pop(event)) {
            drained.push_back(event);
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::uint64_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            for (std::uint64_t index = 0; index < events_per_producer; ++index) {
                const auto event_id = producer * events_per_producer + index + 1;
                if (trace.record(CanvasTraceKind::RenderEnd, event_id, producer + 1)) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
            }
            finished.fetch_add(1, std::memory_order_release);
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    consumer.join();

    const auto accepted_count = accepted.load(std::memory_order_relaxed);
    CHECK(accepted_count + trace.overflow_count() == expected_attempts);
    CHECK(drained.size() == accepted_count);

    std::unordered_set<std::uint64_t> sequences;
    sequences.reserve(drained.size());
    std::uint64_t previous_sequence = 0;
    bool is_first = true;
    for (const auto& event : drained) {
        CHECK(event.event_id > 0);
        CHECK(event.frame_id >= 1 && event.frame_id <= producer_count);
        CHECK(event.qpc_ticks > 0);
        CHECK(sequences.insert(event.sequence).second);
        if (!is_first) {
            CHECK(event.sequence > previous_sequence);
        }
        previous_sequence = event.sequence;
        is_first = false;
    }
}

void test_invalid_capacity_rejected() {
    TestClock clock;
    bool threw = false;
    try {
        CanvasTrace trace(0, &read_test_clock, &clock, 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    test_enable_disable_and_clock_failure();
    test_overflow_order_ids_and_wraparound();
    test_multiple_producers_and_single_background_consumer();
    test_invalid_capacity_rejected();
    std::cout << "[PASS] canvas trace bounded ring\n";
    return 0;
}
