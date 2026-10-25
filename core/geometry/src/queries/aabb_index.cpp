#include "pikdraw/geometry/queries/aabb_index.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace pikdraw::geometry {
namespace {
constexpr uint32_t kLeafSize = 6;

bool indexable(const Rect2D& bounds) noexcept {
    return bounds.is_finite() && bounds.width >= 0.0 && bounds.height >= 0.0 &&
           std::isfinite(bounds.right()) && std::isfinite(bounds.bottom());
}

Rect2D union_bounds(const Rect2D& a, const Rect2D& b) noexcept {
    const double left = std::min(a.x, b.x);
    const double top = std::min(a.y, b.y);
    const double right = std::max(a.right(), b.right());
    const double bottom = std::max(a.bottom(), b.bottom());
    return {left, top, right - left, bottom - top};
}

double center_axis(const AabbIndexEntry& entry, bool x_axis) noexcept {
    return x_axis ? entry.bounds.x + entry.bounds.width * 0.5
                  : entry.bounds.y + entry.bounds.height * 0.5;
}
} // namespace

bool AabbIndex::rebuild(std::vector<AabbIndexEntry> entries) {
    clear();
    if (entries.size() > kMaximumEntries)
        return false;

    entries_.reserve(entries.size());
    conservative_fallback_.reserve(entries.size());
    for (auto& entry : entries) {
        if (indexable(entry.bounds))
            entries_.push_back(std::move(entry));
        else
            conservative_fallback_.push_back(std::move(entry));
    }
    if (entries_.empty())
        return true;

    const size_t maximum_leaves = (entries_.size() + 2) / 3;
    nodes_.reserve(maximum_leaves * 2 - 1);
    root_ = build_node(0, static_cast<uint32_t>(entries_.size()));
    return true;
}

void AabbIndex::clear() noexcept {
    entries_.clear();
    conservative_fallback_.clear();
    nodes_.clear();
    root_ = -1;
}

size_t AabbIndex::size() const noexcept {
    return entries_.size() + conservative_fallback_.size();
}

int32_t AabbIndex::build_node(uint32_t first, uint32_t count) {
    if (count == 0)
        return -1;

    const auto node_index = static_cast<int32_t>(nodes_.size());
    nodes_.emplace_back();
    Rect2D bounds = entries_[first].bounds;
    for (uint32_t i = 1; i < count; ++i)
        bounds = union_bounds(bounds, entries_[first + i].bounds);
    nodes_[node_index].bounds = bounds;

    if (count <= kLeafSize) {
        nodes_[node_index].first = first;
        nodes_[node_index].count = count;
        return node_index;
    }

    const bool split_x = bounds.width >= bounds.height;
    const uint32_t middle = first + count / 2;
    std::nth_element(entries_.begin() + first, entries_.begin() + middle,
                     entries_.begin() + first + count,
        [split_x](const AabbIndexEntry& a, const AabbIndexEntry& b) {
            const auto a_center = center_axis(a, split_x);
            const auto b_center = center_axis(b, split_x);
            if (a_center != b_center)
                return a_center < b_center;
            if (a.z_order != b.z_order)
                return a.z_order < b.z_order;
            return a.id < b.id;
        });

    const auto left = build_node(first, count / 2);
    const auto right = build_node(middle, count - count / 2);
    nodes_[node_index].left = left;
    nodes_[node_index].right = right;
    return node_index;
}

void AabbIndex::query_node(int32_t node_index, const Rect2D& region,
                           std::vector<AabbIndexEntry>& output) const {
    if (node_index < 0)
        return;
    const auto& node = nodes_[node_index];
    if (!node.bounds.intersects(region))
        return;
    if (node.is_leaf()) {
        for (uint32_t i = 0; i < node.count; ++i) {
            const auto& entry = entries_[node.first + i];
            if (entry.bounds.intersects(region))
                output.push_back(entry);
        }
        return;
    }
    query_node(node.left, region, output);
    query_node(node.right, region, output);
}

std::vector<AabbIndexEntry> AabbIndex::query(const Rect2D& region) const {
    std::vector<AabbIndexEntry> result;
    if (!region.is_finite() || region.width < 0.0 || region.height < 0.0)
        return result;
    result.reserve(32 + conservative_fallback_.size());
    result.insert(result.end(), conservative_fallback_.begin(),
                  conservative_fallback_.end());
    query_node(root_, region, result);
    std::sort(result.begin(), result.end(),
        [](const AabbIndexEntry& a, const AabbIndexEntry& b) {
            if (a.z_order != b.z_order)
                return a.z_order < b.z_order;
            return a.id < b.id;
        });
    return result;
}

} // namespace pikdraw::geometry
