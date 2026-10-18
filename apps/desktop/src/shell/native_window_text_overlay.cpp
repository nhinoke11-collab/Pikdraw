#include "pikdraw/desktop/shell/native_window.hpp"
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <imm.h>

namespace pikdraw::desktop {
void NativeWindow::paint_inline_text_overlay(HDC dc) {
    if (!is_inline_text_editing_ || !text_editor_) return;
    const auto& editor = *text_editor_;
    auto text = editor.draft;
    size_t caret_offset = editor.caret;
    if (!editor.composition.empty()) {
        text.text.replace(editor.first(), editor.last() - editor.first(), editor.composition);
        caret_offset = editor.first() + editor.composition.size();
    }
    const auto layout = text.layout();
    if (layout.lines().empty()) return;
    const auto area = canvas_rect(width_, height_);
    const auto& camera = root().renderer().camera();
    const auto transform = editor.local_to_world();
    const auto screen = [&](geometry::Point2D point) {
        const auto pixel = camera.world_to_screen(transform.map_point(point));
        return POINT{area.left + static_cast<LONG>(std::lround(pixel.x)),
                     area.top + static_cast<LONG>(std::lround(pixel.y))};
    };
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    // Selection follows the shaped lines and transforms, without a text-entry box.
    if (editor.has_selection() && editor.composition.empty()) {
        for (const auto& line : layout.lines()) {
            const auto start = line.source_byte_offset;
            const auto end = start + line.run.text_utf8.size();
            if (editor.last() <= start || editor.first() > end) continue;
            double left = line.baseline_origin.x, right = left;
            double position = left;
            for (const auto& glyph : line.run.glyphs) {
                const auto offset = start + glyph.cluster_byte_offset;
                if (offset < editor.first()) left += glyph.x_advance_mm;
                position += glyph.x_advance_mm;
                if (offset < editor.last()) right = position;
            }
            if (editor.last() > end) right += text.font.size_mm() * 0.3;
            POINT points[]{screen({left, line.line_bounds.y}), screen({right, line.line_bounds.y}),
                screen({right, line.line_bounds.bottom()}), screen({left, line.line_bounds.bottom()})};
            HRGN region = CreatePolygonRgn(points, 4, WINDING);
            InvertRgn(dc, region);
            DeleteObject(region);
        }
    }
    const COLORREF ink = theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(0, 105, 170);
    const auto caret = tools::text_caret_metrics(layout, caret_offset);
    const auto top = screen(caret.caret_position);
    const auto bottom = screen({caret.caret_position.x, caret.caret_position.y + caret.caret_height_mm});
    if (editor.caret_visible && (!hwnd_ || GetFocus() == hwnd_)) {
        HPEN pen = CreatePen(PS_SOLID, 1, ink);
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, top.x, top.y, nullptr);
        LineTo(dc, bottom.x, bottom.y);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    if (!editor.composition.empty()) {
        const auto begin = tools::text_caret_metrics(layout, editor.first());
        const auto from = screen({begin.caret_position.x, begin.caret_position.y + begin.caret_height_mm});
        HPEN pen = CreatePen(PS_DOT, 1, ink);
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, from.x, from.y, nullptr);
        LineTo(dc, bottom.x, bottom.y);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    if (hwnd_) {
        if (auto context = ImmGetContext(hwnd_)) {
            COMPOSITIONFORM composition{};
            composition.dwStyle = CFS_POINT;
            composition.ptCurrentPos = {dip(bottom.x), dip(bottom.y)};
            ImmSetCompositionWindow(context, &composition);
            CANDIDATEFORM candidate{};
            candidate.dwStyle = CFS_CANDIDATEPOS;
            candidate.ptCurrentPos = composition.ptCurrentPos;
            ImmSetCandidateWindow(context, &candidate);
            ImmReleaseContext(hwnd_, context);
        }
    }
    RestoreDC(dc, saved);
}
} // namespace pikdraw::desktop
#endif
