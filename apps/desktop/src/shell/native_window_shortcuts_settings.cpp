#include "native_window_menus.hpp"
#include "pikdraw/desktop/shell/native_window_shortcuts_settings.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include <algorithm>
#include <sstream>

namespace pikdraw::desktop {

#ifdef _WIN32

static ShortcutsSettingsState g_shortcuts_state{};

ShortcutsSettingsState& get_shortcuts_settings_state() {
    return g_shortcuts_state;
}

std::string format_key_shortcut_string(WPARAM key, bool ctrl, bool shift, bool alt) {
    if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU ||
        key == VK_LCONTROL || key == VK_RCONTROL || key == VK_LSHIFT ||
        key == VK_RSHIFT || key == VK_LMENU || key == VK_RMENU) {
        return "";
    }

    std::string result;
    if (ctrl) result += "Ctrl+";
    if (alt) result += "Alt+";
    if (shift) result += "Shift+";

    if (key >= 'A' && key <= 'Z') {
        result += static_cast<char>(key);
    } else if (key >= '0' && key <= '9') {
        result += static_cast<char>(key);
    } else if (key >= VK_F1 && key <= VK_F12) {
        result += "F" + std::to_string(key - VK_F1 + 1);
    } else {
        switch (key) {
        case VK_PRIOR: result += "PgUp"; break;
        case VK_NEXT: result += "PgDn"; break;
        case VK_UP: result += "Up"; break;
        case VK_DOWN: result += "Down"; break;
        case VK_LEFT: result += "Left"; break;
        case VK_RIGHT: result += "Right"; break;
        case VK_DELETE: result += "Delete"; break;
        case VK_BACK: result += "Backspace"; break;
        case VK_TAB: result += "Tab"; break;
        case VK_SPACE: result += "Space"; break;
        case VK_RETURN: result += "Enter"; break;
        case VK_HOME: result += "Home"; break;
        case VK_END: result += "End"; break;
        case VK_INSERT: result += "Insert"; break;
        case VK_ESCAPE: result += "Esc"; break;
        case VK_ADD: result += "+"; break;
        case VK_SUBTRACT: result += "-"; break;
        case VK_OEM_PLUS: result += "+"; break;
        case VK_OEM_MINUS: result += "-"; break;
        case VK_OEM_4: result += "["; break;
        case VK_OEM_6: result += "]"; break;
        case VK_OEM_1: result += ";"; break;
        case VK_OEM_PERIOD: result += "."; break;
        case VK_OEM_COMMA: result += ","; break;
        default: return "";
        }
    }
    return result;
}

bool reorder_selected_objects(NativeWindow& win, document::ReorderObjectInLayerCommand::Step step) {
    const auto& sel = win.root().vector_controller().selection().selected_ids();
    if (sel.empty()) return false;

    auto targets = sel;
    if (step == document::ReorderObjectInLayerCommand::Step::BringForward ||
        step == document::ReorderObjectInLayerCommand::Step::BringToFront) {
        std::reverse(targets.begin(), targets.end());
    }

    for (auto id : targets) {
        win.root().document().execute_command(
            std::make_unique<document::ReorderObjectInLayerCommand>(id, step));
    }

    InvalidateRect(win.hwnd(), nullptr, FALSE);
    return true;
}

bool nudge_selected_objects(NativeWindow& win, double dx_mm, double dy_mm) {
    const auto& sel = win.root().vector_controller().selection().selected_ids();
    if (sel.empty()) return false;

    for (auto id : sel) {
        win.workflow().transform_object(id, dx_mm, dy_mm);
    }

    InvalidateRect(win.hwnd(), nullptr, FALSE);
    return true;
}

void ensure_shortcuts_initialized(NativeWindow& win) {
    auto& state = get_shortcuts_settings_state();
    if (state.initialized) return;

    auto& dispatcher = win.root().window().action_dispatcher();
    using namespace actions;

    // Define all core customizable actions with their CorelDRAW default shortcuts
    struct Def {
        std::string_view id;
        std::string title_vi;
        std::string title_en;
        std::string shortcut;
        ShortcutCategory cat;
        std::function<void()> handler;
    };

    Def defs[] = {
        // Z-order (Thứ tự hiển thị)
        { ActionId::kObjectOrderToFront, "Lên trên cùng (To Front)", "Bring to Front", "Shift+PgUp", ShortcutCategory::Order,
          [&win] { reorder_selected_objects(win, document::ReorderObjectInLayerCommand::Step::BringToFront); } },
        { ActionId::kObjectOrderToBack, "Xuống dưới cùng (To Back)", "Send to Back", "Shift+PgDn", ShortcutCategory::Order,
          [&win] { reorder_selected_objects(win, document::ReorderObjectInLayerCommand::Step::SendToBack); } },
        { ActionId::kObjectOrderForwardOne, "Lên một lớp (Forward One)", "Bring Forward One", "Ctrl+PgUp", ShortcutCategory::Order,
          [&win] { reorder_selected_objects(win, document::ReorderObjectInLayerCommand::Step::BringForward); } },
        { ActionId::kObjectOrderBackOne, "Xuống một lớp (Back One)", "Send Backward One", "Ctrl+PgDn", ShortcutCategory::Order,
          [&win] { reorder_selected_objects(win, document::ReorderObjectInLayerCommand::Step::SendBackward); } },

        // Nudge (Dịch chuyển hình học)
        { ActionId::kObjectNudgeUp, "Dịch lên trên (Nudge Up)", "Nudge Up", "Up", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 0.0, -1.0); } },
        { ActionId::kObjectNudgeDown, "Dịch xuống dưới (Nudge Down)", "Nudge Down", "Down", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 0.0, 1.0); } },
        { ActionId::kObjectNudgeLeft, "Dịch sang trái (Nudge Left)", "Nudge Left", "Left", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, -1.0, 0.0); } },
        { ActionId::kObjectNudgeRight, "Dịch sang phải (Nudge Right)", "Nudge Right", "Right", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 1.0, 0.0); } },
        { ActionId::kObjectSuperNudgeUp, "Dịch bước lớn lên (Super Up)", "Super Nudge Up", "Shift+Up", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 0.0, -10.0); } },
        { ActionId::kObjectSuperNudgeDown, "Dịch bước lớn xuống (Super Down)", "Super Nudge Down", "Shift+Down", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 0.0, 10.0); } },
        { ActionId::kObjectSuperNudgeLeft, "Dịch bước lớn sang trái (Super Left)", "Super Nudge Left", "Shift+Left", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, -10.0, 0.0); } },
        { ActionId::kObjectSuperNudgeRight, "Dịch bước lớn sang phải (Super Right)", "Super Nudge Right", "Shift+Right", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 10.0, 0.0); } },
        { ActionId::kObjectMicroNudgeUp, "Dịch bước nhỏ lên (Micro Up)", "Micro Nudge Up", "Ctrl+Up", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 0.0, -0.1); } },
        { ActionId::kObjectMicroNudgeDown, "Dịch bước nhỏ xuống (Micro Down)", "Micro Nudge Down", "Ctrl+Down", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 0.0, 0.1); } },
        { ActionId::kObjectMicroNudgeLeft, "Dịch bước nhỏ sang trái (Micro Left)", "Micro Nudge Left", "Ctrl+Left", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, -0.1, 0.0); } },
        { ActionId::kObjectMicroNudgeRight, "Dịch bước nhỏ sang phải (Micro Right)", "Micro Nudge Right", "Ctrl+Right", ShortcutCategory::Nudge,
          [&win] { nudge_selected_objects(win, 0.1, 0.0); } },

        // Edit
        { ActionId::kEditUndo, "Hoàn tác (Undo)", "Undo", "Ctrl+Z", ShortcutCategory::Edit,
          [&win] { win.execute_menu_action(static_cast<int>(MenuActionId::EditUndo)); } },
        { ActionId::kEditRedo, "Làm lại (Redo)", "Redo", "Ctrl+Y", ShortcutCategory::Edit,
          [&win] { win.execute_menu_action(static_cast<int>(MenuActionId::EditRedo)); } },
        { ActionId::kEditCut, "Cắt đối tượng (Cut)", "Cut", "Ctrl+X", ShortcutCategory::Edit,
          [&win] { win.workflow().cut_selected(win.hwnd()); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kEditCopy, "Sao chép (Copy)", "Copy", "Ctrl+C", ShortcutCategory::Edit,
          [&win] { win.workflow().copy_selected(win.hwnd()); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kEditPaste, "Dán từ Clipboard (Paste)", "Paste", "Ctrl+V", ShortcutCategory::Edit,
          [&win] { win.workflow().paste(win.hwnd()); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kEditDuplicate, "Nhân đôi (Duplicate)", "Duplicate", "Ctrl+D", ShortcutCategory::Edit,
          [&win] { win.workflow().duplicate_selected(5.0, 5.0); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kEditDelete, "Xóa đối tượng (Delete)", "Delete", "Delete", ShortcutCategory::Edit,
          [&win] { if (!win.delete_selected_guideline()) { win.workflow().delete_selected(); } InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kObjectGroup, "Nhóm đối tượng (Group)", "Group Objects", "Ctrl+G", ShortcutCategory::Edit,
          [&win] { win.workflow().group_selected(); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kObjectUngroup, "Rã nhóm (Ungroup)", "Ungroup Objects", "Ctrl+U", ShortcutCategory::Edit,
          [&win] { win.workflow().ungroup_selected(); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kFileImport, "Nhập tệp (Import)", "Import...", "Ctrl+I", ShortcutCategory::Edit,
          [&win] { win.open_import_dialog(); } },
        { ActionId::kFileExport, "Xuất tệp (Export)", "Export...", "Ctrl+E", ShortcutCategory::Edit,
          [&win] { win.trigger_export(); } },

        // Tools
        { ActionId::kToolSelect, "Công cụ Chọn (Pick Tool)", "Pick Tool", "V", ShortcutCategory::Tools,
          [&win] { win.set_active_tool(0); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kToolRectangle, "Công cụ Hình chữ nhật", "Rectangle Tool", "R", ShortcutCategory::Tools,
          [&win] { win.set_active_tool(1); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kToolEllipse, "Công cụ Hình elip", "Ellipse Tool", "E", ShortcutCategory::Tools,
          [&win] { win.set_active_tool(2); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kToolText, "Công cụ Văn bản (Text)", "Text Tool", "A", ShortcutCategory::Tools,
          [&win] { win.set_active_tool(4); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kToolKnife, "Công cụ Knife", "Knife Tool", "K", ShortcutCategory::Tools,
          [&win] { win.set_active_tool(5); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kToolVectorEraser, "Tẩy vector", "Vector Eraser", "Shift+E", ShortcutCategory::Tools,
          [&win] { win.set_active_tool(16); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kToolScissors, "Kéo Scissors", "Scissors Tool", "X", ShortcutCategory::Tools,
          [&win] { win.set_active_tool(19); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kViewFitAll, "Căn vừa trang (Fit Page)", "Fit to Page", "F", ShortcutCategory::Tools,
          [&win] { win.fit_page_to_canvas(); InvalidateRect(win.hwnd(), nullptr, FALSE); } },
        { ActionId::kViewZoom100, "Thu phóng 100%", "Zoom 100%", "1", ShortcutCategory::Tools,
          [&win] { win.zoom_to_actual_size(); } },
        // View / Color Proofing
        { ActionId::kViewProofColors, "Mô phỏng màu in (Proof Colors)", "Proof Colors", "Ctrl+Alt+Y", ShortcutCategory::View,
          [&win] { win.execute_menu_action(static_cast<int>(MenuActionId::ViewProofColors)); } },
        { ActionId::kViewGamutWarning, "Cảnh báo màu ngoài gamut ICC", "ICC Gamut Warning", "Ctrl+Shift+Y", ShortcutCategory::View,
          [&win] { win.execute_menu_action(static_cast<int>(MenuActionId::ViewGamutWarning)); } },
        { ActionId::kViewOverprintPreview, "Xem trước in đè (chưa khả dụng)", "Overprint Preview (Unavailable)", "Alt+Shift+Ctrl+Y", ShortcutCategory::View,
          [&win] { win.execute_menu_action(static_cast<int>(MenuActionId::ViewOverprintPreview)); } }
    };

    state.items.clear();
    for (const auto& d : defs) {
        dispatcher.register_action({std::string(d.id), d.title_vi, d.title_en, d.shortcut, d.handler, nullptr});
        state.items.push_back({std::string(d.id), d.title_vi, d.title_en, d.shortcut, d.shortcut, d.cat});
    }

    state.initialized = true;
}

bool dispatch_canvas_shortcuts(NativeWindow& win, WPARAM key) {
    if (win.is_text_input_context()) {
        return false;
    }

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    // In CorelDRAW profile: alignment keys (R, E, L, T, B, C, P) take precedence when >= 2 objects are selected
    if (!ctrl && !shift && !alt) {
        const auto sel_count = win.root().vector_controller().selection().count();
        if (sel_count >= 2 || win.align_reference() == geometry::AlignReference::Page) {
            if (key == 'R' || key == 'E' || key == 'C' || key == 'T' || key == 'B' || key == 'L' || key == 'P') {
                return false;
            }
        }
    }

    std::string sc = format_key_shortcut_string(key, ctrl, shift, alt);
    if (sc.empty()) return false;

    ensure_shortcuts_initialized(win);

    auto& dispatcher = win.root().window().action_dispatcher();
    if (dispatcher.dispatch_shortcut(sc)) {
        InvalidateRect(win.hwnd(), nullptr, FALSE);
        return true;
    }

    return false;
}

void paint_shortcuts_settings_tab(
    NativeWindow& win, HDC mem_dc, int cx, int cy, bool is_vi, bool is_dark,
    COLORREF m_text_pri, COLORREF m_text_mut, COLORREF m_teal, COLORREF m_border,
    HFONT font_vn_bold, HFONT font_vn_norm, HFONT font_sec, HFONT font_mono) {

    ensure_shortcuts_initialized(win);
    auto& state = get_shortcuts_settings_state();
    auto& dispatcher = win.root().window().action_dispatcher();

    // 1. Profile chooser (CorelDRAW Standard / Adobe Illustrator / PikDraw)
    SelectObject(mem_dc, font_vn_bold);
    SetTextColor(mem_dc, m_text_pri);
    RECT rc_prof_t = { cx, cy, cx + 490, cy + 18 };
    DrawTextW(mem_dc, is_vi ? L"Hồ sơ phím tắt thao tác:" : L"Keyboard Shortcut Profile:", -1, &rc_prof_t, DT_LEFT | DT_NOPREFIX);

    const wchar_t* profiles_vi[] = { L"CorelDRAW Standard", L"Adobe Illustrator", L"PikDraw Mặc định" };
    const wchar_t* profiles_en[] = { L"CorelDRAW Standard", L"Adobe Illustrator", L"PikDraw Default" };
    const int profile_idx = state.active_profile;

    for (int i = 0; i < 3; ++i) {
        int px = cx + i * 162;
        RECT rc_p = { px, cy + 22, px + 152, cy + 46 };
        bool act = (profile_idx == i);
        HBRUSH br_p = CreateSolidBrush(act ? (is_dark ? RGB(18, 38, 48) : RGB(236, 253, 245))
                                           : (is_dark ? RGB(10, 16, 24) : RGB(248, 250, 252)));
        FillRect(mem_dc, &rc_p, br_p); DeleteObject(br_p);
        HPEN pen_p = CreatePen(PS_SOLID, 1, act ? m_teal : m_border);
        SelectObject(mem_dc, pen_p); SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        RoundRect(mem_dc, rc_p.left, rc_p.top, rc_p.right, rc_p.bottom, 6, 6);
        DeleteObject(pen_p);
        SelectObject(mem_dc, act ? font_vn_bold : font_vn_norm);
        SetTextColor(mem_dc, act ? m_teal : m_text_mut);
        DrawTextW(mem_dc, is_vi ? profiles_vi[i] : profiles_en[i], -1, &rc_p, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 2. Category filter pills
    const wchar_t* cats_vi[] = { L"Tất cả", L"Sắp xếp", L"Di chuyển", L"Chỉnh sửa", L"Công cụ", L"Xem & Màu" };
    const wchar_t* cats_en[] = { L"All", L"Z-Order", L"Nudge", L"Editing", L"Tools", L"View & Color" };
    const int cat_y = cy + 52;
    for (int c = 0; c < 6; ++c) {
        int cx_btn = cx + c * 80;
        RECT rc_c = { cx_btn, cat_y, cx_btn + 76, cat_y + 22 };
        bool is_c_act = (static_cast<uint8_t>(state.active_category) == c);
        HBRUSH br_c = CreateSolidBrush(is_c_act ? m_teal : (is_dark ? RGB(14, 22, 33) : RGB(241, 245, 249)));
        FillRect(mem_dc, &rc_c, br_c); DeleteObject(br_c);
        SelectObject(mem_dc, is_c_act ? font_sec : font_vn_norm);
        SetTextColor(mem_dc, is_c_act ? RGB(255, 255, 255) : m_text_mut);
        DrawTextW(mem_dc, is_vi ? cats_vi[c] : cats_en[c], -1, &rc_c, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 3. Filtered items
    std::vector<size_t> visible_indices;
    for (size_t i = 0; i < state.items.size(); ++i) {
        if (state.active_category == ShortcutCategory::All ||
            state.items[i].category == state.active_category) {
            visible_indices.push_back(i);
        }
    }

    // 4. Table Header
    const int tbl_y = cat_y + 28;
    RECT rc_th = { cx, tbl_y, cx + 486, tbl_y + 20 };
    HBRUSH br_th = CreateSolidBrush(is_dark ? RGB(20, 30, 44) : RGB(226, 232, 240));
    FillRect(mem_dc, &rc_th, br_th); DeleteObject(br_th);
    SelectObject(mem_dc, font_vn_bold);
    SetTextColor(mem_dc, m_teal);
    RECT rc_th_col1 = { cx + 8, tbl_y, cx + 290, tbl_y + 20 };
    RECT rc_th_col2 = { cx + 295, tbl_y, cx + 480, tbl_y + 20 };
    DrawTextW(mem_dc, is_vi ? L"CHỨC NĂNG / THAO TÁC" : L"FUNCTION / ACTION", -1, &rc_th_col1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(mem_dc, is_vi ? L"PHÍM TẮT ĐÃ GÁN" : L"ASSIGNED SHORTCUT", -1, &rc_th_col2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 5. Table Rows
    const int row_h = 24;
    const int max_visible_rows = 9;
    const int rows_start_y = tbl_y + 22;

    int total_vis = static_cast<int>(visible_indices.size());
    if (state.selected_row >= total_vis && total_vis > 0) state.selected_row = total_vis - 1;
    if (state.selected_row < 0) state.selected_row = 0;

    int start_i = std::clamp(state.scroll_offset_y, 0, (std::max)(0, total_vis - max_visible_rows));

    for (int r = 0; r < max_visible_rows && (start_i + r) < total_vis; ++r) {
        size_t idx = visible_indices[static_cast<size_t>(start_i + r)];
        auto& item = state.items[idx];
        const int ry = rows_start_y + r * row_h;
        RECT rc_row = { cx, ry, cx + 486, ry + row_h - 1 };

        const bool is_sel = (state.selected_row == (start_i + r));
        HBRUSH br_row = CreateSolidBrush(is_sel ? (is_dark ? RGB(24, 44, 62) : RGB(219, 234, 254))
                                                : (r % 2 == 1 ? (is_dark ? RGB(13, 21, 32) : RGB(250, 250, 252))
                                                              : (is_dark ? RGB(10, 16, 24) : RGB(255, 255, 255))));
        FillRect(mem_dc, &rc_row, br_row); DeleteObject(br_row);

        if (is_sel) {
            HPEN pen_sel = CreatePen(PS_SOLID, 1, m_teal);
            SelectObject(mem_dc, pen_sel); SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            Rectangle(mem_dc, rc_row.left, rc_row.top, rc_row.right, rc_row.bottom);
            DeleteObject(pen_sel);
        }

        // Action title
        SelectObject(mem_dc, is_sel ? font_vn_bold : font_vn_norm);
        SetTextColor(mem_dc, is_sel ? m_teal : m_text_pri);
        RECT rc_txt = { cx + 8, ry, cx + 290, ry + row_h };
        std::wstring wtitle(item.title_vi.begin(), item.title_vi.end());
        if (!is_vi) wtitle.assign(item.title_en.begin(), item.title_en.end());
        DrawTextW(mem_dc, wtitle.c_str(), -1, &rc_txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Shortcut badge
        std::string cur_sc = dispatcher.get_action_shortcut(item.action_id);
        RECT rc_sc_bg = { cx + 295, ry + 2, cx + 475, ry + row_h - 2 };

        if (is_sel && state.is_recording) {
            HBRUSH br_rec = CreateSolidBrush(RGB(220, 38, 38));
            FillRect(mem_dc, &rc_sc_bg, br_rec); DeleteObject(br_rec);
            SelectObject(mem_dc, font_vn_bold);
            SetTextColor(mem_dc, RGB(255, 255, 255));
            const wchar_t* rec_hint = is_vi ? L"● ĐANG BẮT PHÍM MỚI..." : L"● PRESS NEW KEY...";
            DrawTextW(mem_dc, rec_hint, -1, &rc_sc_bg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else if (!cur_sc.empty()) {
            HBRUSH br_sc = CreateSolidBrush(is_dark ? RGB(16, 36, 46) : RGB(224, 242, 254));
            FillRect(mem_dc, &rc_sc_bg, br_sc); DeleteObject(br_sc);
            HPEN pen_sc = CreatePen(PS_SOLID, 1, is_dark ? RGB(14, 165, 233) : RGB(56, 189, 248));
            SelectObject(mem_dc, pen_sc); SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, rc_sc_bg.left, rc_sc_bg.top, rc_sc_bg.right, rc_sc_bg.bottom, 4, 4);
            DeleteObject(pen_sc);
            SelectObject(mem_dc, font_mono);
            SetTextColor(mem_dc, is_dark ? RGB(56, 189, 248) : RGB(2, 132, 199));
            std::wstring wsc(cur_sc.begin(), cur_sc.end());
            DrawTextW(mem_dc, wsc.c_str(), -1, &rc_sc_bg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else {
            SelectObject(mem_dc, font_sec);
            SetTextColor(mem_dc, m_text_mut);
            DrawTextW(mem_dc, is_vi ? L"— Chưa gán —" : L"— None —", -1, &rc_sc_bg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    // 6. Action Buttons Bar
    const int btns_y = rows_start_y + max_visible_rows * row_h + 10;

    auto draw_btn = [&](int bx, int by, int bw, int bh, const wchar_t* label, bool highlight) {
        RECT rc = { bx, by, bx + bw, by + bh };
        HBRUSH br = CreateSolidBrush(highlight ? m_teal : (is_dark ? RGB(18, 28, 42) : RGB(241, 245, 249)));
        FillRect(mem_dc, &rc, br); DeleteObject(br);
        HPEN pen = CreatePen(PS_SOLID, 1, highlight ? m_teal : m_border);
        SelectObject(mem_dc, pen); SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        RoundRect(mem_dc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        DeleteObject(pen);
        SelectObject(mem_dc, highlight ? font_vn_bold : font_vn_norm);
        SetTextColor(mem_dc, highlight ? RGB(255, 255, 255) : m_text_pri);
        DrawTextW(mem_dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    draw_btn(cx, btns_y, 140, 26, is_vi ? L"⌨ Gán phím mới..." : L"⌨ Assign Key...", state.is_recording);
    draw_btn(cx + 148, btns_y, 100, 26, is_vi ? L"✕ Xóa phím" : L"✕ Clear", false);
    draw_btn(cx + 256, btns_y, 110, 26, is_vi ? L"↺ Đặt lại" : L"↺ Reset", false);
    draw_btn(cx + 374, btns_y, 112, 26, is_vi ? L"⚙ Mặc định tất cả" : L"⚙ Reset All", false);
}

bool handle_shortcuts_settings_click(NativeWindow& win, int x, int y) {
    auto& state = get_shortcuts_settings_state();
    auto& dispatcher = win.root().window().action_dispatcher();
    const int modal_w = 720, modal_h = 500;
    const int modal_x = (win.width() - modal_w) / 2;
    const int modal_y = (win.height() - modal_h) / 2;
    const int cx = modal_x + 205;
    const int cy = modal_y + 54;

    // 1. Profile click
    if (y >= cy + 22 && y <= cy + 46) {
        for (int i = 0; i < 3; ++i) {
            int px = cx + i * 162;
            if (x >= px && x <= px + 152) {
                state.active_profile = i;
                if (i == 0) { // CorelDRAW profile
                    dispatcher.reset_to_default_shortcuts();
                } else if (i == 1) { // Illustrator profile
                    dispatcher.set_action_shortcut(actions::ActionId::kObjectOrderToFront, "Ctrl+Shift+]");
                    dispatcher.set_action_shortcut(actions::ActionId::kObjectOrderToBack, "Ctrl+Shift+[");
                    dispatcher.set_action_shortcut(actions::ActionId::kObjectOrderForwardOne, "Ctrl+]");
                    dispatcher.set_action_shortcut(actions::ActionId::kObjectOrderBackOne, "Ctrl+[");
                    dispatcher.set_action_shortcut(actions::ActionId::kObjectUngroup, "Ctrl+Shift+G");
                } else { // PikDraw Default
                    dispatcher.reset_to_default_shortcuts();
                }
                state.is_recording = false;
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            }
        }
    }

    // 2. Category filter pills
    const int cat_y = cy + 52;
    if (y >= cat_y && y <= cat_y + 22) {
        for (int c = 0; c < 6; ++c) {
            int cx_btn = cx + c * 80;
            if (x >= cx_btn && x <= cx_btn + 76) {
                state.active_category = static_cast<ShortcutCategory>(c);
                state.selected_row = 0;
                state.scroll_offset_y = 0;
                state.is_recording = false;
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            }
        }
    }

    // 3. Table Rows click
    const int tbl_y = cat_y + 28;
    const int rows_start_y = tbl_y + 22;
    const int row_h = 24;
    const int max_visible_rows = 9;

    std::vector<size_t> visible_indices;
    for (size_t i = 0; i < state.items.size(); ++i) {
        if (state.active_category == ShortcutCategory::All ||
            state.items[i].category == state.active_category) {
            visible_indices.push_back(i);
        }
    }

    int total_vis = static_cast<int>(visible_indices.size());
    if (x >= cx && x <= cx + 486 && y >= rows_start_y && y < rows_start_y + max_visible_rows * row_h) {
        int clicked_row = (y - rows_start_y) / row_h + state.scroll_offset_y;
        if (clicked_row >= 0 && clicked_row < total_vis) {
            state.selected_row = clicked_row;
            state.is_recording = false;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
    }

    // 4. Action Buttons Bar
    const int btns_y = rows_start_y + max_visible_rows * row_h + 10;
    if (y >= btns_y && y <= btns_y + 26) {
        if (x >= cx && x <= cx + 140) { // Assign Key
            state.is_recording = true;
            state.recording_key_display = "...";
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
        if (x >= cx + 148 && x <= cx + 248) { // Clear
            if (state.selected_row >= 0 && state.selected_row < total_vis) {
                size_t idx = visible_indices[static_cast<size_t>(state.selected_row)];
                dispatcher.clear_action_shortcut(state.items[idx].action_id);
            }
            state.is_recording = false;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
        if (x >= cx + 256 && x <= cx + 366) { // Reset
            if (state.selected_row >= 0 && state.selected_row < total_vis) {
                size_t idx = visible_indices[static_cast<size_t>(state.selected_row)];
                const auto def_sc = dispatcher.get_default_shortcut(state.items[idx].action_id);
                dispatcher.set_action_shortcut(state.items[idx].action_id, def_sc);
            }
            state.is_recording = false;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
        if (x >= cx + 374 && x <= cx + 486) { // Reset All
            dispatcher.reset_to_default_shortcuts();
            state.is_recording = false;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
    }

    return false;
}

bool handle_shortcuts_settings_key(NativeWindow& win, WPARAM key) {
    auto& state = get_shortcuts_settings_state();
    auto& dispatcher = win.root().window().action_dispatcher();

    std::vector<size_t> visible_indices;
    for (size_t i = 0; i < state.items.size(); ++i) {
        if (state.active_category == ShortcutCategory::All ||
            state.items[i].category == state.active_category) {
            visible_indices.push_back(i);
        }
    }
    int total_vis = static_cast<int>(visible_indices.size());

    if (state.is_recording) {
        if (key == VK_ESCAPE) {
            state.is_recording = false;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }

        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        std::string new_sc = format_key_shortcut_string(key, ctrl, shift, alt);
        if (!new_sc.empty() && state.selected_row >= 0 && state.selected_row < total_vis) {
            size_t idx = visible_indices[static_cast<size_t>(state.selected_row)];
            dispatcher.set_action_shortcut(state.items[idx].action_id, new_sc);
            state.is_recording = false;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
        return false;
    }

    if (key == VK_UP) {
        if (state.selected_row > 0) state.selected_row--;
        InvalidateRect(win.hwnd(), nullptr, FALSE);
        return true;
    }
    if (key == VK_DOWN) {
        if (state.selected_row + 1 < total_vis) state.selected_row++;
        InvalidateRect(win.hwnd(), nullptr, FALSE);
        return true;
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        state.is_recording = true;
        InvalidateRect(win.hwnd(), nullptr, FALSE);
        return true;
    }

    return false;
}

bool handle_shortcuts_settings_wheel(NativeWindow& win, int delta) {
    auto& state = get_shortcuts_settings_state();
    std::vector<size_t> visible_indices;
    for (size_t i = 0; i < state.items.size(); ++i) {
        if (state.active_category == ShortcutCategory::All ||
            state.items[i].category == state.active_category) {
            visible_indices.push_back(i);
        }
    }
    int total_vis = static_cast<int>(visible_indices.size());
    int max_scroll = (std::max)(0, total_vis - 9);

    if (delta > 0 && state.scroll_offset_y > 0) {
        state.scroll_offset_y--;
        InvalidateRect(win.hwnd(), nullptr, FALSE);
        return true;
    } else if (delta < 0 && state.scroll_offset_y < max_scroll) {
        state.scroll_offset_y++;
        InvalidateRect(win.hwnd(), nullptr, FALSE);
        return true;
    }
    return false;
}

#endif

} // namespace pikdraw::desktop
