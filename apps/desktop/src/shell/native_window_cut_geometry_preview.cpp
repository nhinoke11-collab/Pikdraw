#include "pikdraw/desktop/shell/native_window.hpp"

#ifdef _WIN32

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <vector>

namespace pikdraw::desktop {

void NativeWindow::paint_cut_geometry_preview_overlay(HDC dc, const RECT& area) {
    if (!pending_cut_geometry_proposal_ || area.right <= area.left ||
        area.bottom <= area.top)
        return;
    const auto& pending = *pending_cut_geometry_proposal_;
    const auto& state = pending.state;
    if (!state || state->tab_id != active_tab().id ||
        state->base_snapshot != root().document().current_snapshot() ||
        state->generation != cut_geometry_generation_)
        return;

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    SetBkMode(dc, TRANSPARENT);
    const auto& camera = root().renderer().camera();
    for (const auto& contour : pending.proposal.preview_contours) {
        if (contour.points_world.size() < 2 ||
            contour.points_world.size() > 250000)
            continue;
        const COLORREF color = contour.removed ? RGB(239, 68, 68) : RGB(0, 190, 165);
        HPEN pen = CreatePen(contour.removed ? PS_DOT : PS_DASH, 2, color);
        if (!pen) continue;
        const HGDIOBJ old_pen = SelectObject(dc, pen);
        std::vector<POINT> points;
        points.reserve(contour.points_world.size());
        bool finite_points = true;
        for (const auto world : contour.points_world) {
            const auto screen = camera.world_to_screen(world);
            const double x = static_cast<double>(area.left) + screen.x;
            const double y = static_cast<double>(area.top) + screen.y;
            if (!std::isfinite(x) || !std::isfinite(y) ||
                x < static_cast<double>(std::numeric_limits<LONG>::min()) ||
                x > static_cast<double>(std::numeric_limits<LONG>::max()) ||
                y < static_cast<double>(std::numeric_limits<LONG>::min()) ||
                y > static_cast<double>(std::numeric_limits<LONG>::max())) {
                finite_points = false;
                break;
            }
            points.push_back({static_cast<LONG>(std::lround(x)),
                              static_cast<LONG>(std::lround(y))});
        }
        if (finite_points && points.size() >= 2 &&
            points.size() <= static_cast<std::size_t>(INT_MAX)) {
            Polyline(dc, points.data(), static_cast<int>(points.size()));
            if (contour.closed) {
                MoveToEx(dc, points.back().x, points.back().y, nullptr);
                LineTo(dc, points.front().x, points.front().y);
            }
        }
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
    RestoreDC(dc, saved);
}

}  // namespace pikdraw::desktop

#endif
