#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include <vector>
#include <optional>
#include <algorithm>
#include <cstdint>

namespace pikdraw::geometry {

enum class AlignMode {
    Left,
    CenterHorizontal,
    Right,
    Top,
    CenterVertical,
    Bottom,
    CenterPage,
    CenterPageHorizontal,
    CenterPageVertical
};

enum class AlignReference {
    Selection,
    KeyObject,
    Page
};

enum class DistributeMode {
    LeftEdges,
    CentersHorizontal,
    RightEdges,
    TopEdges,
    CentersVertical,
    BottomEdges,
    SpacingHorizontal,
    SpacingVertical
};

struct ObjectAlignmentItem {
    uint64_t id{0};
    Rect2D bounds{};
};

struct AlignmentDelta {
    uint64_t id{0};
    double dx_mm{0.0};
    double dy_mm{0.0};
};

/// Computes translation deltas for a set of items based on AlignMode and reference
inline std::vector<AlignmentDelta> compute_alignment(
    const std::vector<ObjectAlignmentItem>& items,
    AlignMode mode,
    AlignReference reference = AlignReference::Selection,
    std::optional<uint64_t> key_object_id = std::nullopt,
    std::optional<Rect2D> page_bounds = std::nullopt
) {
    std::vector<AlignmentDelta> deltas;
    if (items.empty()) return deltas;

    // 1. Special Handling for Center to Page (CorelDRAW 'P' shortcut semantics)
    if (mode == AlignMode::CenterPage ||
        mode == AlignMode::CenterPageHorizontal ||
        mode == AlignMode::CenterPageVertical) {
        if (!page_bounds.has_value()) return deltas;

        Rect2D union_box = items.front().bounds;
        for (size_t i = 1; i < items.size(); ++i) {
            union_box = union_box.united(items[i].bounds);
        }

        const double dx = (mode == AlignMode::CenterPage || mode == AlignMode::CenterPageHorizontal)
            ? (page_bounds->center().x - union_box.center().x)
            : 0.0;
        const double dy = (mode == AlignMode::CenterPage || mode == AlignMode::CenterPageVertical)
            ? (page_bounds->center().y - union_box.center().y)
            : 0.0;

        deltas.reserve(items.size());
        for (const auto& item : items) {
            deltas.push_back({item.id, dx, dy});
        }
        return deltas;
    }

    // 2. Normal Align (Left, Center, Right, Top, Middle, Bottom)
    Rect2D target_ref;
    if (reference == AlignReference::Page && page_bounds.has_value()) {
        target_ref = *page_bounds;
    } else if (reference == AlignReference::KeyObject && key_object_id.has_value()) {
        bool found_key = false;
        for (const auto& it : items) {
            if (it.id == *key_object_id) {
                target_ref = it.bounds;
                found_key = true;
                break;
            }
        }
        if (!found_key) {
            target_ref = items.front().bounds;
            for (size_t i = 1; i < items.size(); ++i) {
                target_ref = target_ref.united(items[i].bounds);
            }
        }
    } else {
        // Selection bounds
        target_ref = items.front().bounds;
        for (size_t i = 1; i < items.size(); ++i) {
            target_ref = target_ref.united(items[i].bounds);
        }
    }

    deltas.reserve(items.size());
    for (const auto& item : items) {
        // Key Object stays fixed when aligning to key object
        if (reference == AlignReference::KeyObject && key_object_id.has_value() && item.id == *key_object_id) {
            deltas.push_back({item.id, 0.0, 0.0});
            continue;
        }

        double dx = 0.0;
        double dy = 0.0;

        switch (mode) {
            case AlignMode::Left:
                dx = target_ref.left() - item.bounds.left();
                break;
            case AlignMode::CenterHorizontal:
                dx = target_ref.center().x - item.bounds.center().x;
                break;
            case AlignMode::Right:
                dx = target_ref.right() - item.bounds.right();
                break;
            case AlignMode::Top:
                dy = target_ref.top() - item.bounds.top();
                break;
            case AlignMode::CenterVertical:
                dy = target_ref.center().y - item.bounds.center().y;
                break;
            case AlignMode::Bottom:
                dy = target_ref.bottom() - item.bounds.bottom();
                break;
            default:
                break;
        }

        deltas.push_back({item.id, dx, dy});
    }

    return deltas;
}

/// Computes translation deltas for distributing items
inline std::vector<AlignmentDelta> compute_distribution(
    std::vector<ObjectAlignmentItem> items,
    DistributeMode mode,
    double specified_spacing_mm = -1.0
) {
    std::vector<AlignmentDelta> deltas;
    if (items.size() < 2) return deltas;

    const bool horizontal = mode == DistributeMode::SpacingHorizontal ||
        mode == DistributeMode::LeftEdges ||
        mode == DistributeMode::CentersHorizontal ||
        mode == DistributeMode::RightEdges;

    if (horizontal) {
        // The target coordinate defines order for edge/center distribution. Gap
        // spacing is ordered by the leading edge so it remains a left-to-right
        // sequence even when object sizes differ.
        const auto position = [mode](const ObjectAlignmentItem& item) {
            switch (mode) {
                case DistributeMode::CentersHorizontal: return item.bounds.center().x;
                case DistributeMode::RightEdges: return item.bounds.right();
                default: return item.bounds.left();
            }
        };
        std::sort(items.begin(), items.end(), [&](const auto& a, const auto& b) {
            const double a_position = position(a);
            const double b_position = position(b);
            return a_position == b_position ? a.id < b.id : a_position < b_position;
        });

        if (specified_spacing_mm >= 0.0) {
            // Fixed gap spacing from left to right, keeping first item fixed
            double current_right = items.front().bounds.right();
            deltas.push_back({items.front().id, 0.0, 0.0});
            for (size_t i = 1; i < items.size(); ++i) {
                double target_left = current_right + specified_spacing_mm;
                double dx = target_left - items[i].bounds.left();
                deltas.push_back({items[i].id, dx, 0.0});
                current_right = items[i].bounds.right() + dx;
            }
        } else if (mode == DistributeMode::SpacingHorizontal) {
            // Auto calculate equal gap
            double total_width = 0.0;
            for (const auto& it : items) {
                total_width += it.bounds.width;
            }
            double span = items.back().bounds.right() - items.front().bounds.left();
            double total_gap = span - total_width;
            double gap_each = total_gap / static_cast<double>(items.size() - 1);

            double current_right = items.front().bounds.right();
            deltas.push_back({items.front().id, 0.0, 0.0});
            for (size_t i = 1; i < items.size(); ++i) {
                double target_left = current_right + gap_each;
                double dx = target_left - items[i].bounds.left();
                deltas.push_back({items[i].id, dx, 0.0});
                current_right = items[i].bounds.right() + dx;
            }
        } else if (mode == DistributeMode::CentersHorizontal) {
            double first_c = items.front().bounds.center().x;
            double last_c = items.back().bounds.center().x;
            double step = (last_c - first_c) / static_cast<double>(items.size() - 1);

            for (size_t i = 0; i < items.size(); ++i) {
                double target_c = first_c + static_cast<double>(i) * step;
                double dx = target_c - items[i].bounds.center().x;
                deltas.push_back({items[i].id, dx, 0.0});
            }
        } else if (mode == DistributeMode::LeftEdges) {
            double first_l = items.front().bounds.left();
            double last_l = items.back().bounds.left();
            double step = (last_l - first_l) / static_cast<double>(items.size() - 1);

            for (size_t i = 0; i < items.size(); ++i) {
                double target_l = first_l + static_cast<double>(i) * step;
                double dx = target_l - items[i].bounds.left();
                deltas.push_back({items[i].id, dx, 0.0});
            }
        } else if (mode == DistributeMode::RightEdges) {
            double first_r = items.front().bounds.right();
            double last_r = items.back().bounds.right();
            double step = (last_r - first_r) / static_cast<double>(items.size() - 1);

            for (size_t i = 0; i < items.size(); ++i) {
                double target_r = first_r + static_cast<double>(i) * step;
                double dx = target_r - items[i].bounds.right();
                deltas.push_back({items[i].id, dx, 0.0});
            }
        }
    } else {
        // As above, order edge/center modes by the same coordinate being
        // distributed. Fixed/equal gaps use the leading top edge.
        const auto position = [mode](const ObjectAlignmentItem& item) {
            switch (mode) {
                case DistributeMode::CentersVertical: return item.bounds.center().y;
                case DistributeMode::BottomEdges: return item.bounds.bottom();
                default: return item.bounds.top();
            }
        };
        std::sort(items.begin(), items.end(), [&](const auto& a, const auto& b) {
            const double a_position = position(a);
            const double b_position = position(b);
            return a_position == b_position ? a.id < b.id : a_position < b_position;
        });

        if (specified_spacing_mm >= 0.0) {
            double current_bottom = items.front().bounds.bottom();
            deltas.push_back({items.front().id, 0.0, 0.0});
            for (size_t i = 1; i < items.size(); ++i) {
                double target_top = current_bottom + specified_spacing_mm;
                double dy = target_top - items[i].bounds.top();
                deltas.push_back({items[i].id, 0.0, dy});
                current_bottom = items[i].bounds.bottom() + dy;
            }
        } else if (mode == DistributeMode::SpacingVertical) {
            double total_height = 0.0;
            for (const auto& it : items) {
                total_height += it.bounds.height;
            }
            double span = items.back().bounds.bottom() - items.front().bounds.top();
            double total_gap = span - total_height;
            double gap_each = total_gap / static_cast<double>(items.size() - 1);

            double current_bottom = items.front().bounds.bottom();
            deltas.push_back({items.front().id, 0.0, 0.0});
            for (size_t i = 1; i < items.size(); ++i) {
                double target_top = current_bottom + gap_each;
                double dy = target_top - items[i].bounds.top();
                deltas.push_back({items[i].id, 0.0, dy});
                current_bottom = items[i].bounds.bottom() + dy;
            }
        } else if (mode == DistributeMode::CentersVertical) {
            double first_c = items.front().bounds.center().y;
            double last_c = items.back().bounds.center().y;
            double step = (last_c - first_c) / static_cast<double>(items.size() - 1);

            for (size_t i = 0; i < items.size(); ++i) {
                double target_c = first_c + static_cast<double>(i) * step;
                double dy = target_c - items[i].bounds.center().y;
                deltas.push_back({items[i].id, 0.0, dy});
            }
        } else if (mode == DistributeMode::TopEdges) {
            double first_t = items.front().bounds.top();
            double last_t = items.back().bounds.top();
            double step = (last_t - first_t) / static_cast<double>(items.size() - 1);

            for (size_t i = 0; i < items.size(); ++i) {
                double target_t = first_t + static_cast<double>(i) * step;
                double dy = target_t - items[i].bounds.top();
                deltas.push_back({items[i].id, 0.0, dy});
            }
        } else if (mode == DistributeMode::BottomEdges) {
            double first_b = items.front().bounds.bottom();
            double last_b = items.back().bounds.bottom();
            double step = (last_b - first_b) / static_cast<double>(items.size() - 1);

            for (size_t i = 0; i < items.size(); ++i) {
                double target_b = first_b + static_cast<double>(i) * step;
                double dy = target_b - items[i].bounds.bottom();
                deltas.push_back({items[i].id, 0.0, dy});
            }
        }
    }

    return deltas;
}

} // namespace pikdraw::geometry
