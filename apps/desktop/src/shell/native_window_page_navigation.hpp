#pragma once

#include <algorithm>
#include <cstddef>

namespace pikdraw::desktop::page_navigation {

inline constexpr int kStatusBarHeight = 26;
inline constexpr int kPageRowHeight = 28;
inline constexpr int kBottomChromeHeight = kStatusBarHeight + kPageRowHeight;

struct Span {
    int left{0};
    int right{0};
    [[nodiscard]] bool contains(int x) const noexcept { return x >= left && x < right; }
};

struct Layout {
    Span first{8, 34};
    Span previous{37, 63};
    Span counter{67, 125};
    Span next{129, 155};
    Span last{158, 184};
    Span add{190, 216};
    Span remove{219, 245};
    int tabs_left{254};
    int tab_width{0};
    size_t first_visible{0};
    size_t visible_count{0};

    [[nodiscard]] Span tab(size_t page_index) const noexcept {
        if (page_index < first_visible || page_index >= first_visible + visible_count)
            return {};
        const auto visible_index = page_index - first_visible;
        const int left = tabs_left + static_cast<int>(visible_index) * tab_width;
        return {left, left + tab_width};
    }
};

[[nodiscard]] inline Layout make_layout(int width, size_t page_count,
                                        size_t active_index) noexcept {
    Layout result;
    const int available = std::max(0, width - result.tabs_left - 8);
    const size_t by_width = static_cast<size_t>(available / 72);
    result.visible_count = std::min(page_count, std::min<size_t>(12, std::max<size_t>(1, by_width)));
    result.tab_width = result.visible_count == 0 ? 0
        : std::max(56, std::min(132, available / static_cast<int>(result.visible_count)));
    if (result.visible_count > 0) {
        const size_t centered = active_index > result.visible_count / 2
            ? active_index - result.visible_count / 2 : 0;
        result.first_visible = std::min(centered, page_count - result.visible_count);
    }
    return result;
}

} // namespace pikdraw::desktop::page_navigation
