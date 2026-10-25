#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pikdraw::geometry {

struct AabbIndexEntry {
    uint64_t id{0};
    Rect2D bounds{};
    uint32_t z_order{0};
};

/// Snapshot-independent BVH over finite or conservative 2D bounds.
/// Query results are returned in ascending painter z-order.
class AabbIndex final {
public:
    static constexpr size_t kMaximumEntries = 1'000'000;

    /// Replaces the index. Returns false and leaves it empty when the bounded
    /// index budget is exceeded; callers can then use their exhaustive path.
    [[nodiscard]] bool rebuild(std::vector<AabbIndexEntry> entries);
    void clear() noexcept;

    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] std::vector<AabbIndexEntry> query(const Rect2D& region) const;

private:
    struct Node {
        Rect2D bounds{};
        int32_t left{-1};
        int32_t right{-1};
        uint32_t first{0};
        uint32_t count{0};

        [[nodiscard]] bool is_leaf() const noexcept {
            return left < 0 && right < 0;
        }
    };

    [[nodiscard]] int32_t build_node(uint32_t first, uint32_t count);
    void query_node(int32_t node, const Rect2D& region,
                    std::vector<AabbIndexEntry>& output) const;

    std::vector<AabbIndexEntry> entries_;
    std::vector<AabbIndexEntry> conservative_fallback_;
    std::vector<Node> nodes_;
    int32_t root_{-1};
};

} // namespace pikdraw::geometry
