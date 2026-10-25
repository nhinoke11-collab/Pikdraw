#pragma once

#include <cstdint>
#include <optional>

namespace pikdraw::desktop {

struct CoalescedPointerMove {
    int x{0};
    int y{0};
    unsigned modifiers{0};

    friend bool operator==(const CoalescedPointerMove&,
                           const CoalescedPointerMove&) = default;
};

// Keeps only the newest pointer sample while one UI timer is pending. Button
// handlers must flush before committing a gesture so the final position wins.
class PointerMoveCoalescer final {
public:
    [[nodiscard]] bool offer(CoalescedPointerMove move) noexcept {
        const bool should_schedule = !pending_;
        latest_ = move;
        pending_ = true;
        return should_schedule;
    }

    [[nodiscard]] std::optional<CoalescedPointerMove> take() noexcept {
        if (!pending_) return std::nullopt;
        pending_ = false;
        return latest_;
    }

    [[nodiscard]] bool pending() const noexcept { return pending_; }

private:
    CoalescedPointerMove latest_{};
    bool pending_{false};
};

} // namespace pikdraw::desktop
