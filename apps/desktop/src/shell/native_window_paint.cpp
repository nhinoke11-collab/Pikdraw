#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <variant>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {

std::optional<geometry::Rect2D> selected_bounds(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) -> std::optional<geometry::Rect2D> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::GroupObject>)
            return std::nullopt;
        else
            return value.bounds();
    }, object);
}

void draw_tool_icon(HDC dc, int tool, int center_x, int center_y, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (tool == 0) {
        MoveToEx(dc, center_x - 6, center_y - 9, nullptr);
        LineTo(dc, center_x - 6, center_y + 8);
        LineTo(dc, center_x - 1, center_y + 3);
        LineTo(dc, center_x + 3, center_y + 10);
        LineTo(dc, center_x + 6, center_y + 8);
        LineTo(dc, center_x + 2, center_y + 1);
        LineTo(dc, center_x + 9, center_y + 1);
        LineTo(dc, center_x - 6, center_y - 9);
    } else if (tool == 1) {
        Rectangle(dc, center_x - 8, center_y - 6, center_x + 8, center_y + 6);
    } else if (tool == 2) {
        Ellipse(dc, center_x - 8, center_y - 6, center_x + 8, center_y + 6);
    } else {
        MoveToEx(dc, center_x - 6, center_y + 2, nullptr);
        LineTo(dc, center_x - 8, center_y - 1);
        LineTo(dc, center_x - 6, center_y - 3);
        LineTo(dc, center_x - 3, center_y);
        LineTo(dc, center_x - 3, center_y - 9);
        LineTo(dc, center_x - 1, center_y - 10);
        LineTo(dc, center_x + 1, center_y - 9);
        LineTo(dc, center_x + 1, center_y - 2);
        LineTo(dc, center_x + 3, center_y - 8);
        LineTo(dc, center_x + 5, center_y - 8);
        LineTo(dc, center_x + 6, center_y - 1);
        LineTo(dc, center_x + 8, center_y - 6);
        LineTo(dc, center_x + 10, center_y - 5);
        LineTo(dc, center_x + 9, center_y + 3);
        LineTo(dc, center_x + 6, center_y + 9);
        LineTo(dc, center_x - 4, center_y + 9);
        LineTo(dc, center_x - 7, center_y + 5);
        LineTo(dc, center_x - 6, center_y + 2);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

} // namespace


void NativeWindow::paint(HDC hdc, int w, int h) {
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    // Primary font: Be Vietnam Pro; Secondary: Google Sans; Numeric: JetBrains Mono
    auto make_font = [](int sz, int weight, const wchar_t* name) {
        return CreateFontW(sz, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, name);
    };
    HFONT font_be_vietnam = make_font(14, FW_NORMAL, L"Segoe UI");
    HFONT font_be_vietnam_bold = make_font(14, FW_BOLD, L"Segoe UI");
    HFONT font_secondary = make_font(13, FW_NORMAL, L"Segoe UI");
    HFONT font_mono = make_font(12, FW_NORMAL, L"JetBrains Mono");

    HGDIOBJ old_font = SelectObject(mem_dc, font_be_vietnam);

    bool is_dark = (theme_ == ThemeMode::CyberDark);
    bool is_vi = (language_ == UILanguage::Vietnamese);

    // 2026 Dual Theme Palette: Dark (Obsidian Teal) / Light (Silver Tech)
    COLORREF col_canvas = is_dark ? RGB(7, 12, 18) : RGB(241, 245, 249);
    COLORREF col_header = is_dark ? RGB(12, 19, 29) : RGB(255, 255, 255);
    COLORREF col_ribbon = is_dark ? RGB(14, 22, 34) : RGB(248, 250, 252);
    COLORREF col_statusbar = is_dark ? RGB(7, 11, 16) : RGB(226, 232, 240);
    COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    COLORREF col_btn_bg = is_dark ? RGB(18, 28, 42) : RGB(241, 245, 249);
    COLORREF col_btn_bdr = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);

    int topbar_h = 38, ribbon_h = 34, status_h = 26, tool_w = 52, inspect_w = 280;

    SetBkMode(mem_dc, TRANSPARENT);

    if (show_home_screen_) {
        // Render Home Screen (Trang Home)
        paint_home_screen(mem_dc, w, h);
    } else {
        // The canvas is rasterized from the current immutable document snapshot.
        HBRUSH br_canvas = CreateSolidBrush(col_canvas);
        RECT rc_full = { 0, 0, w, h };
        FillRect(mem_dc, &rc_full, br_canvas);
        DeleteObject(br_canvas);
        paint_document_canvas(mem_dc, w, h);

        // 4. Context Tool Ribbon
        HBRUSH br_ribbon = CreateSolidBrush(col_ribbon);
        RECT rc_ribbon = { 0, topbar_h, w, topbar_h + ribbon_h };
        FillRect(mem_dc, &rc_ribbon, br_ribbon);
        DeleteObject(br_ribbon);

        const auto snapshot = root_.document().current_snapshot();
        const auto& selection = root_.vector_controller().selection().selected_ids();
        RECT rc_rib_ind = { 16, topbar_h, 340, topbar_h + ribbon_h };
        SetTextColor(mem_dc, col_teal);
        SelectObject(mem_dc, font_be_vietnam_bold);
        std::wstring document_label = is_vi ? L"VECTOR 2D · " : L"VECTOR 2D · ";
        document_label += std::to_wstring(snapshot->object_count());
        document_label += is_vi ? L" đối tượng" : L" objects";
        document_label += L"   ·   ";
        document_label += std::to_wstring(static_cast<int>(std::lround(snapshot->page().width_mm)));
        document_label += L" × ";
        document_label += std::to_wstring(static_cast<int>(std::lround(snapshot->page().height_mm)));
        document_label += L" mm";
        DrawTextW(mem_dc, document_label.c_str(), -1, &rc_rib_ind,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(mem_dc, font_mono);
        SetTextColor(mem_dc, col_text_white);
        std::wostringstream wgeom;
        wgeom << (is_vi ? L"Chọn: " : L"Selected: ") << selection.size()
              << L"   |   Zoom: " << zoom_level_ << L"%";
        if (selection.size() == 1) {
            const auto object = snapshot->objects().get(selection.front());
            if (object) {
                if (const auto bounds = selected_bounds(*object)) {
                    wgeom << L"   X: " << std::fixed << std::setprecision(1) << bounds->x
                          << L"   Y: " << bounds->y << L"   W: " << bounds->width
                          << L"   H: " << bounds->height << L" mm";
                }
            }
        }
        std::wstring geom_str = wgeom.str();
        RECT rc_rib_geom = { 350, topbar_h, w - 16, topbar_h + ribbon_h };
        DrawTextW(mem_dc, geom_str.c_str(), -1, &rc_rib_geom, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // 5. Left Tool Strip: only tools with working canvas behavior.
        HBRUSH br_tools = CreateSolidBrush(col_header);
        RECT rc_tools = { 0, topbar_h + ribbon_h, tool_w, h - status_h };
        FillRect(mem_dc, &rc_tools, br_tools);
        DeleteObject(br_tools);

        for (int i = 0; i < 4; ++i) {
            int ty = topbar_h + ribbon_h + 10 + i * 42;
            RECT rc_tb = { 6, ty, tool_w - 6, ty + 36 };
            if (active_tool_ == i) {
                HBRUSH br_t_act = CreateSolidBrush(col_active_tab);
                HPEN pen_t_act = CreatePen(PS_SOLID, 1, col_teal);
                SelectObject(mem_dc, pen_t_act);
                SelectObject(mem_dc, br_t_act);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6); // bo nhẹ 6px
                DeleteObject(pen_t_act);
                DeleteObject(br_t_act);
                draw_tool_icon(mem_dc, i, (rc_tb.left + rc_tb.right) / 2,
                               (rc_tb.top + rc_tb.bottom) / 2, col_teal);
            } else {
                HBRUSH br_t_inact = CreateSolidBrush(col_header);
                HPEN pen_t_inact = CreatePen(PS_SOLID, 1, col_border);
                SelectObject(mem_dc, pen_t_inact);
                SelectObject(mem_dc, br_t_inact);
                RoundRect(mem_dc, rc_tb.left, rc_tb.top, rc_tb.right, rc_tb.bottom, 6, 6); // bo nhẹ 6px
                DeleteObject(pen_t_inact);
                DeleteObject(br_t_inact);
                draw_tool_icon(mem_dc, i, (rc_tb.left + rc_tb.right) / 2,
                               (rc_tb.top + rc_tb.bottom) / 2, col_text_muted);
            }
        }

        // 6. Right Inspector & Layers Dock
        int insp_x = w - inspect_w;
        HBRUSH br_dock = CreateSolidBrush(col_header);
        RECT rc_dock = { insp_x, topbar_h + ribbon_h, w, h - status_h };
        FillRect(mem_dc, &rc_dock, br_dock);
        DeleteObject(br_dock);

        int tab_w = inspect_w / 3;
        const wchar_t* tab_names_vi[] = { L"Thuộc tính", L"Lớp (Layers)", L"Lịch sử" };
        const wchar_t* tab_names_en[] = { L"Inspector", L"Layers", L"History" };
        for (int i = 0; i < 3; ++i) {
            RECT rc_tab = { insp_x + i * tab_w, topbar_h + ribbon_h, insp_x + (i + 1) * tab_w, topbar_h + ribbon_h + 28 };
            if (active_dock_tab_ == i) {
                HBRUSH br_tab = CreateSolidBrush(col_active_tab);
                HPEN p_tab = CreatePen(PS_SOLID, 1, col_teal);
                SelectObject(mem_dc, p_tab);
                SelectObject(mem_dc, br_tab);
                RoundRect(mem_dc, rc_tab.left + 2, rc_tab.top + 2, rc_tab.right - 2, rc_tab.bottom, 6, 6);
                DeleteObject(p_tab);
                DeleteObject(br_tab);
                SetTextColor(mem_dc, col_teal);
                SelectObject(mem_dc, font_be_vietnam_bold);
            } else {
                SetTextColor(mem_dc, col_text_muted);
                SelectObject(mem_dc, font_secondary);
            }
            DrawTextW(mem_dc, is_vi ? tab_names_vi[i] : tab_names_en[i], -1, &rc_tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        const int content_y = topbar_h + ribbon_h + 36;
        SelectObject(mem_dc, font_be_vietnam_bold);
        SetTextColor(mem_dc, col_teal);
        RECT rc_head = { insp_x + 14, content_y, w - 14, content_y + 22 };
        SelectObject(mem_dc, font_secondary);
        SetTextColor(mem_dc, col_text_white);

        std::wostringstream content;
        if (active_dock_tab_ == 0) {
            DrawTextW(mem_dc, is_vi ? L"THUỘC TÍNH ĐANG CHỌN" : L"SELECTION", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            content << (is_vi ? L"Số đối tượng: " : L"Selected objects: ") << selection.size();
            if (selection.size() == 1) {
                const auto object = snapshot->objects().get(selection.front());
                if (object) {
                    const auto name = std::visit([](const auto& value) { return value.name; }, *object);
                    content << L"\n" << utf8_to_wide(name);
                    if (const auto bounds = selected_bounds(*object)) {
                        content << L"\nX: " << std::fixed << std::setprecision(1) << bounds->x
                                << L" mm\nY: " << bounds->y << L" mm\nW: " << bounds->width
                                << L" mm\nH: " << bounds->height << L" mm";
                    }
                }
            } else {
                content << (is_vi ? L"\nNhấp một đối tượng để xem kích thước (mm)."
                                  : L"\nSelect an object to inspect its size in mm.");
            }
            RECT body = { insp_x + 14, content_y + 28, w - 14, content_y + 190 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
            SelectObject(mem_dc, font_be_vietnam_bold);
            SetTextColor(mem_dc, col_teal);
            RECT tools_head = { insp_x + 14, content_y + 205, w - 14, content_y + 228 };
            DrawTextW(mem_dc, is_vi ? L"CÔNG CỤ ĐANG CÓ" : L"AVAILABLE TOOLS", -1,
                      &tools_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(mem_dc, font_secondary);
            SetTextColor(mem_dc, col_text_white);
            RECT tools_body = { insp_x + 14, content_y + 232, w - 14, content_y + 370 };
            DrawTextW(mem_dc, is_vi
                ? L"Chọn và di chuyển (V)\nHình chữ nhật (R)\nHình elip (E)\nPan: H / Space + kéo / chuột giữa\nF: Vừa trang · 1: 100%\nCtrl+Z / Ctrl+Y"
                : L"Select and move (V)\nRectangle (R)\nEllipse (E)\nPan: H / Space + drag / middle mouse\nF: Fit page · 1: 100%\nCtrl+Z / Ctrl+Y",
                -1, &tools_body, DT_LEFT | DT_NOPREFIX);
        } else if (active_dock_tab_ == 1) {
            DrawTextW(mem_dc, is_vi ? L"ĐỐI TƯỢNG TRONG TÀI LIỆU" : L"DOCUMENT OBJECTS", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            size_t shown = 0;
            for (const auto& [id, object] : snapshot->objects().all()) {
                const auto name = std::visit([](const auto& value) { return value.name; }, object);
                content << L"• " << utf8_to_wide(name) << L"  (#" << id << L")\n";
                if (++shown == 20)
                    break;
            }
            if (snapshot->object_count() == 0)
                content << (is_vi ? L"Tài liệu chưa có đối tượng." : L"The document is empty.");
            else if (snapshot->object_count() > shown)
                content << L"…";
            RECT body = { insp_x + 14, content_y + 28, w - 14, h - status_h - 8 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
        } else {
            DrawTextW(mem_dc, is_vi ? L"LỊCH SỬ LỆNH" : L"COMMAND HISTORY", -1,
                      &rc_head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            content << L"Revision: " << snapshot->revision() << L"\n"
                    << (root_.document().is_dirty()
                            ? (is_vi ? L"● Có thay đổi chưa lưu\n" : L"● Unsaved changes\n")
                            : (is_vi ? L"○ Đã lưu\n" : L"○ Saved\n"))
                    << (root_.document().can_undo()
                            ? (is_vi ? L"● Có thể hoàn tác\n" : L"● Undo available\n")
                            : (is_vi ? L"○ Không có lệnh Undo\n" : L"○ Nothing to undo\n"))
                    << (root_.document().can_redo()
                            ? (is_vi ? L"● Có thể làm lại" : L"● Redo available")
                            : (is_vi ? L"○ Không có lệnh Redo" : L"○ Nothing to redo"));
            RECT body = { insp_x + 14, content_y + 28, w - 14, content_y + 220 };
            const auto text = content.str();
            DrawTextW(mem_dc, text.c_str(), -1, &body, DT_LEFT | DT_NOPREFIX);
        }

        // Hairline borders between layout blocks
        HPEN pen_bdr = CreatePen(PS_SOLID, 1, col_border);
        HGDIOBJ old_bdr = SelectObject(mem_dc, pen_bdr);

        MoveToEx(mem_dc, 0, topbar_h + ribbon_h, NULL); LineTo(mem_dc, w, topbar_h + ribbon_h);
        MoveToEx(mem_dc, tool_w, topbar_h + ribbon_h, NULL); LineTo(mem_dc, tool_w, h - status_h);
        MoveToEx(mem_dc, insp_x, topbar_h + ribbon_h, NULL); LineTo(mem_dc, insp_x, h - status_h);

        SelectObject(mem_dc, old_bdr);
        DeleteObject(pen_bdr);
    }

    // 3. Topbar (Always on top)
    HBRUSH br_topbar = CreateSolidBrush(col_header);
    RECT rc_topbar = { 0, 0, w, topbar_h };
    FillRect(mem_dc, &rc_topbar, br_topbar);
    DeleteObject(br_topbar);

    // Hairline border under topbar
    HPEN pen_bdr_tb = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_bdr_tb = SelectObject(mem_dc, pen_bdr_tb);
    MoveToEx(mem_dc, 0, topbar_h, NULL); LineTo(mem_dc, w, topbar_h);
    SelectObject(mem_dc, old_bdr_tb);
    DeleteObject(pen_bdr_tb);

    // Logo PK badge (bo nhẹ 6px)
    HBRUSH br_logo = CreateSolidBrush(col_teal);
    HPEN pen_logo = CreatePen(PS_SOLID, 1, col_teal);
    SelectObject(mem_dc, pen_logo);
    SelectObject(mem_dc, br_logo);
    RoundRect(mem_dc, 10, 6, 36, 32, 6, 6);
    DeleteObject(pen_logo);
    DeleteObject(br_logo);
    SelectObject(mem_dc, font_be_vietnam_bold);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_logo = { 10, 6, 36, 32 };
    DrawTextW(mem_dc, L"PK", -1, &rc_logo, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // App brand title
    SetTextColor(mem_dc, col_text_white);
    RECT rc_app_title = { 42, 6, 145, 32 };
    DrawTextW(mem_dc, L"PikDraw PRO", -1, &rc_app_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    auto draw_action_button = [&](int left, int right, const wchar_t* label, bool active) {
        RECT rect{left + 1, 6, right - 1, 32};
        HBRUSH brush = CreateSolidBrush(active ? col_active_tab : col_btn_bg);
        HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_btn_bdr);
        HGDIOBJ old_pen = SelectObject(mem_dc, pen);
        HGDIOBJ old_brush = SelectObject(mem_dc, brush);
        RoundRect(mem_dc, rect.left, rect.top, rect.right, rect.bottom, 6, 6);
        SelectObject(mem_dc, old_pen);
        SelectObject(mem_dc, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);
        SetTextColor(mem_dc, active ? col_teal : col_text_white);
        SelectObject(mem_dc, active ? font_be_vietnam_bold : font_be_vietnam);
        DrawTextW(mem_dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };
    draw_action_button(148, 198, is_vi ? L"Mới" : L"New", false);
    draw_action_button(198, 248, is_vi ? L"Mở" : L"Open", false);
    draw_action_button(248, 298, is_vi ? L"Lưu" : L"Save", false);
    draw_action_button(298, 350, L"Undo", false);
    draw_action_button(350, 402, L"Redo", false);

    // The File menu contains only implemented document commands.
    RECT rc_m_file = { 402, 6, 456, 32 };
    if (show_file_menu_) {
        HBRUSH br_mf = CreateSolidBrush(col_active_tab);
        HPEN pen_mf = CreatePen(PS_SOLID, 1, col_teal);
        SelectObject(mem_dc, pen_mf);
        SelectObject(mem_dc, br_mf);
        RoundRect(mem_dc, rc_m_file.left, rc_m_file.top, rc_m_file.right, rc_m_file.bottom, 6, 6);
        DeleteObject(pen_mf);
        DeleteObject(br_mf);
        SetTextColor(mem_dc, col_teal);
        SelectObject(mem_dc, font_be_vietnam_bold);
    } else {
        HBRUSH br_mf = CreateSolidBrush(col_header);
        HPEN pen_mf = CreatePen(PS_SOLID, 1, col_header);
        SelectObject(mem_dc, pen_mf);
        SelectObject(mem_dc, br_mf);
        RoundRect(mem_dc, rc_m_file.left, rc_m_file.top, rc_m_file.right, rc_m_file.bottom, 6, 6);
        DeleteObject(pen_mf);
        DeleteObject(br_mf);
        SetTextColor(mem_dc, col_text_white);
        SelectObject(mem_dc, font_be_vietnam);
    }
    DrawTextW(mem_dc, is_vi ? L"Tệp" : L"File", -1, &rc_m_file,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Other workspaces are not exposed until their workflows are implemented.
    draw_action_button(520, 620, L"Vector 2D", true);

    // Topbar Button 1: Settings [ ⚙ Cài đặt (Ctrl+J) ] (w - 215 to w - 105, bo nhẹ 6px)
    int sett_x = w - 215;
    HBRUSH br_sett = CreateSolidBrush(col_btn_bg);
    HPEN p_sett = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_sett);
    SelectObject(mem_dc, br_sett);
    RoundRect(mem_dc, sett_x, 6, sett_x + 110, 32, 6, 6);
    DeleteObject(p_sett);
    DeleteObject(br_sett);
    SelectObject(mem_dc, font_be_vietnam);
    SetTextColor(mem_dc, col_text_white);
    RECT rc_sett = { sett_x, 6, sett_x + 110, 32 };
    DrawTextW(mem_dc, is_vi ? L"⚙ Cài đặt (Ctrl+J)" : L"⚙ Settings (Ctrl+J)", -1, &rc_sett, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Topbar Button 4: Export Button [ Xuất file ] (w - 95 to w - 12, bo nhẹ 6px)
    HBRUSH br_export = CreateSolidBrush(col_teal);
    HPEN p_export = CreatePen(PS_SOLID, 1, col_teal);
    SelectObject(mem_dc, p_export);
    SelectObject(mem_dc, br_export);
    RoundRect(mem_dc, w - 95, 6, w - 12, 32, 6, 6);
    DeleteObject(p_export);
    DeleteObject(br_export);
    SetTextColor(mem_dc, is_dark ? RGB(7, 11, 16) : RGB(255, 255, 255));
    RECT rc_export = { w - 95, 6, w - 12, 32 };
    DrawTextW(mem_dc, is_vi ? L"Xuất file" : L"Export", -1, &rc_export, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 7. Bottom Status Bar (Always at bottom)
    HBRUSH br_status = CreateSolidBrush(col_statusbar);
    RECT rc_status = { 0, h - status_h, w, h };
    FillRect(mem_dc, &rc_status, br_status);
    DeleteObject(br_status);

    HPEN pen_st_bdr = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_st_bdr = SelectObject(mem_dc, pen_st_bdr);
    MoveToEx(mem_dc, 0, h - status_h, NULL); LineTo(mem_dc, w, h - status_h);
    SelectObject(mem_dc, old_st_bdr);
    DeleteObject(pen_st_bdr);

    // Live coordinates
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_muted);
    std::wostringstream wcoord;
    wcoord << L"X: " << std::fixed << std::setprecision(1) << cursor_mm_x_ << L" mm   "
           << L"Y: " << std::fixed << std::setprecision(1) << cursor_mm_y_ << L" mm";
    std::wstring coord_str = wcoord.str();
    RECT rc_st_coord = { 16, h - status_h, 210, h };
    DrawTextW(mem_dc, coord_str.c_str(), -1, &rc_st_coord, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Theme Mode Badge Chip (bo nhẹ 5px)
    SelectObject(mem_dc, font_be_vietnam);
    HBRUSH br_tbadge = CreateSolidBrush(col_btn_bg);
    HPEN p_tb = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_tb);
    SelectObject(mem_dc, br_tbadge);
    RoundRect(mem_dc, 215, h - status_h + 3, 400, h - 3, 5, 5);
    DeleteObject(p_tb);
    DeleteObject(br_tbadge);
    SetTextColor(mem_dc, col_teal);
    const wchar_t* thm_badge = is_dark ? L"Theme: Dark Mode (Obsidian)" : L"Theme: Light Mode (Silver)";
    RECT rc_tbadge = { 215, h - status_h + 3, 400, h - 3 };
    DrawTextW(mem_dc, thm_badge, -1, &rc_tbadge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Live status message
    SetTextColor(mem_dc, col_text_white);
    std::wstring wmsg = utf8_to_wide(status_msg_);
    RECT rc_st_msg = { 410, h - status_h, w - 230, h };
    DrawTextW(mem_dc, wmsg.c_str(), -1, &rc_st_msg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Zoom buttons [-] 100% [+] (bo nhẹ 5px)
    SelectObject(mem_dc, font_mono);
    SetTextColor(mem_dc, col_text_white);
    std::wstring wzoom = std::to_wstring(zoom_level_) + L"%";
    RECT rc_zm_fit = { w - 210, h - status_h + 3, w - 145, h - 3 };
    RECT rc_zm_btn_minus = { w - 120, h - status_h + 3, w - 95, h - 3 };
    RECT rc_zm_txt = { w - 90, h - status_h, w - 50, h };
    RECT rc_zm_btn_plus = { w - 45, h - status_h + 3, w - 20, h - 3 };

    HBRUSH br_zm_btn = CreateSolidBrush(col_btn_bg);
    HPEN p_zm = CreatePen(PS_SOLID, 1, col_btn_bdr);
    SelectObject(mem_dc, p_zm);
    SelectObject(mem_dc, br_zm_btn);
    RoundRect(mem_dc, rc_zm_fit.left, rc_zm_fit.top, rc_zm_fit.right, rc_zm_fit.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_minus.left, rc_zm_btn_minus.top, rc_zm_btn_minus.right, rc_zm_btn_minus.bottom, 5, 5);
    RoundRect(mem_dc, rc_zm_btn_plus.left, rc_zm_btn_plus.top, rc_zm_btn_plus.right, rc_zm_btn_plus.bottom, 5, 5);
    DeleteObject(p_zm);
    DeleteObject(br_zm_btn);

    DrawTextW(mem_dc, L"-", -1, &rc_zm_btn_minus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, is_vi ? L"Vừa trang" : L"Fit page", -1, &rc_zm_fit,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, wzoom.c_str(), -1, &rc_zm_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, L"+", -1, &rc_zm_btn_plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 8. Paint Settings Modal Dialog if open
    if (show_settings_modal_) {
        paint_settings_modal(mem_dc, w, h);
    }

    // 9. Paint File Menu Dropdown if open (CorelDRAW Reference)
    if (show_file_menu_) {
        paint_file_menu(mem_dc, 402, topbar_h, 270, file_menu_height());
    }

    BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_font);
    for (auto* f : { font_be_vietnam, font_be_vietnam_bold, font_secondary, font_mono }) {
        DeleteObject(f);
    }
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

#endif

} // namespace pikdraw::desktop
