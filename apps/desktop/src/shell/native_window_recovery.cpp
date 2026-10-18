#include "pikdraw/desktop/shell/native_window.hpp"
#include "../utf8_path.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {
struct RecoveryModalLayout {
    int x{};
    int y{};
    int width{};
    int height{};
    int list_x{};
    int list_top{};
    int row_height{52};
    int visible_rows{};
    int button_top{};
    int button_width{};
    int button_gap{10};
};

RecoveryModalLayout recovery_layout(int width, int height) {
    RecoveryModalLayout layout;
    layout.width = std::min(680, std::max(300, width - 32));
    layout.height = std::min(480, std::max(320, height - 32));
    layout.x = (width - layout.width) / 2;
    layout.y = (height - layout.height) / 2;
    layout.list_x = layout.x + 20;
    layout.list_top = layout.y + 116;
    layout.button_top = layout.y + layout.height - 54;
    layout.button_width = std::clamp((layout.width - 60) / 3, 76, 112);
    const int list_bottom = layout.button_top - 96;
    layout.visible_rows = std::max(1, (list_bottom - layout.list_top) / layout.row_height);
    return layout;
}

std::wstring short_document_id(const std::string& id) {
    const auto wide = NativeWindow::utf8_to_wide(id);
    if (wide.size() <= 20) return wide;
    return wide.substr(0, 8) + L"…" + wide.substr(wide.size() - 8);
}
}

std::string NativeWindow::recovery_root_path() const {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!size) return {};
    std::wstring local(size, L'\0');
    const auto copied = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), size);
    if (!copied || copied >= size) return {};
    local.resize(copied);
    const auto utf8 = (std::filesystem::path(local) / L"PikDraw" / L"Recovery").u8string();
    return {utf8.begin(), utf8.end()};
}

void NativeWindow::scan_recovery_checkpoints(bool show_when_found) {
    const auto recovery_root = recovery_root_path();
    recovery_candidates_ = recovery_root.empty()
        ? std::vector<document::RecoveryInfo>{}
        : document::SaveRecoveryService::list_recovery_checkpoints(
              recovery_root, &recovery_candidates_truncated_);
    if (recovery_root.empty()) recovery_candidates_truncated_ = false;
    recovery_selected_index_ = 0;
    recovery_first_visible_ = 0;
    recovery_focus_index_ = 0;
    show_recovery_modal_ = show_when_found && !recovery_candidates_.empty();
    if (show_recovery_modal_) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Tìm thấy bản tự lưu từ phiên trước · chọn để phục hồi hoặc để sau"
            : "Autosaved documents found · recover one or choose Later";
    } else if (recovery_candidates_.empty() && show_when_found) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không tìm thấy bản tự lưu hợp lệ" : "No valid recovery checkpoints found";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::close_recovery_modal() noexcept {
    show_recovery_modal_ = false;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::recover_selected_checkpoint() {
    if (recovery_selected_index_ >= recovery_candidates_.size()) return;
    const auto candidate = recovery_candidates_[recovery_selected_index_];
    if (restored_recovery_paths_.contains(candidate.path)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bản này đã được mở trong phiên hiện tại"
            : "This checkpoint is already open in this session";
        return;
    }
    const auto snapshot = document::SaveRecoveryService::load(candidate.path);
    if (!snapshot || snapshot->document_id() != candidate.document_id ||
        snapshot->revision() != candidate.revision ||
        snapshot->epoch() != candidate.epoch) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bản tự lưu không còn hợp lệ; hãy quét lại danh sách"
            : "This checkpoint is no longer valid; scan the list again";
        scan_recovery_checkpoints(false);
        show_recovery_modal_ = true;
        return;
    }

    try {
        std::ostringstream title;
        title << (language_ == UILanguage::Vietnamese ? "Phục hồi " : "Recovered ")
              << candidate.timestamp;
        create_new_tab(title.str(), candidate.page_width_mm, candidate.page_height_mm);
        if (!root().document().reset_from_snapshot(snapshot)) {
            close_tab(active_tab_index_);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể mở bản tự lưu" : "Could not open the recovery checkpoint";
            return;
        }
        root().document().mark_recovered_unsaved();
        root().vector_controller().selection().clear();
        const auto parent = detail::path_from_utf8(candidate.path).parent_path().u8string();
        active_tab().recovery_directory.assign(parent.begin(), parent.end());
        active_tab().recovery_epoch = root().document().epoch();
        active_tab().recovery_revision = root().document().revision();
        active_tab().recovery_written = true;
        active_tab().file_path.clear();
        document_path_.clear();
        restored_recovery_paths_.insert(candidate.path);
        close_recovery_modal();
        fit_page_to_canvas();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã phục hồi vào tab chưa lưu · dùng Lưu thành để chọn tệp chính thức"
            : "Recovered to an unsaved tab · use Save As to choose its file";
    } catch (...) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể tạo tab phục hồi" : "Could not create a recovery tab";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::discard_selected_checkpoint() {
    if (recovery_selected_index_ >= recovery_candidates_.size()) return;
    const auto candidate = recovery_candidates_[recovery_selected_index_];
    if (restored_recovery_paths_.contains(candidate.path)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể xóa bản đang dùng; hãy lưu tài liệu trước"
            : "This checkpoint is in use; save the recovered document first";
        return;
    }
    const auto prompt = utf8_to_wide(language_ == UILanguage::Vietnamese
        ? "Xóa checkpoint đã chọn? Tài liệu đang mở không bị thay đổi."
        : "Discard the selected checkpoint? Open documents will not change.");
    const auto title = utf8_to_wide(language_ == UILanguage::Vietnamese
        ? "Xóa bản tự lưu" : "Discard recovery checkpoint");
    if (MessageBoxW(hwnd_, prompt.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    std::string error;
    if (!document::SaveRecoveryService::discard_recovery_checkpoint(
            recovery_root_path(), candidate.path, &error)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể xóa bản tự lưu hợp lệ" : "Could not discard the valid checkpoint";
        return;
    }
    recovery_candidates_.erase(recovery_candidates_.begin() + recovery_selected_index_);
    if (recovery_selected_index_ >= recovery_candidates_.size() && recovery_selected_index_ > 0)
        --recovery_selected_index_;
    if (recovery_first_visible_ > recovery_selected_index_)
        recovery_first_visible_ = recovery_selected_index_;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đã xóa checkpoint đã chọn" : "Selected checkpoint discarded";
    if (recovery_candidates_.empty())
        close_recovery_modal();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::handle_recovery_key(WPARAM key) {
    if (!show_recovery_modal_) return false;
    const auto layout = recovery_layout(width_, height_);
    const size_t count = recovery_candidates_.size();
    const size_t visible = static_cast<size_t>(layout.visible_rows);
    if (key == VK_ESCAPE) {
        close_recovery_modal();
    } else if (key == VK_TAB) {
        recovery_focus_index_ = (recovery_focus_index_ +
            ((GetKeyState(VK_SHIFT) & 0x8000) ? 3 : 1)) % 4;
    } else if (key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END ||
               key == VK_PRIOR || key == VK_NEXT) {
        recovery_focus_index_ = 0;
        if (count > 0) {
            if (key == VK_UP && recovery_selected_index_ > 0) --recovery_selected_index_;
            else if (key == VK_DOWN && recovery_selected_index_ + 1 < count) ++recovery_selected_index_;
            else if (key == VK_HOME) recovery_selected_index_ = 0;
            else if (key == VK_END) recovery_selected_index_ = count - 1;
            else if (key == VK_PRIOR)
                recovery_selected_index_ = recovery_selected_index_ > visible
                    ? recovery_selected_index_ - visible : 0;
            else if (key == VK_NEXT)
                recovery_selected_index_ = std::min(count - 1, recovery_selected_index_ + visible);
            if (recovery_selected_index_ < recovery_first_visible_)
                recovery_first_visible_ = recovery_selected_index_;
            else if (recovery_selected_index_ >= recovery_first_visible_ + visible)
                recovery_first_visible_ = recovery_selected_index_ - visible + 1;
        }
    } else if (key == VK_DELETE) {
        discard_selected_checkpoint();
    } else if (key == VK_RETURN || key == VK_SPACE) {
        if (recovery_focus_index_ == 2) discard_selected_checkpoint();
        else if (recovery_focus_index_ == 3) close_recovery_modal();
        else recover_selected_checkpoint();
    } else {
        return true;
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

bool NativeWindow::handle_recovery_mouse_down(int x, int y) {
    if (!show_recovery_modal_) return false;
    const auto layout = recovery_layout(width_, height_);
    const RECT card{layout.x, layout.y, layout.x + layout.width, layout.y + layout.height};
    if (x < card.left || x >= card.right || y < card.top || y >= card.bottom)
        return true;
    if (x >= layout.list_x && x < layout.x + layout.width - 20 && y >= layout.list_top) {
        const auto row = static_cast<size_t>((y - layout.list_top) / layout.row_height);
        if (row < static_cast<size_t>(layout.visible_rows)) {
            const auto index = recovery_first_visible_ + row;
            if (index < recovery_candidates_.size()) {
                recovery_selected_index_ = index;
                recovery_focus_index_ = 0;
            }
        }
    }
    const int right = layout.x + layout.width - 20;
    const int left = right - 3 * layout.button_width - 2 * layout.button_gap;
    if (y >= layout.button_top && y < layout.button_top + 34) {
        if (x >= left && x < left + layout.button_width) {
            recovery_focus_index_ = 1;
            recover_selected_checkpoint();
        } else if (x >= left + layout.button_width + layout.button_gap &&
                   x < left + 2 * (layout.button_width + layout.button_gap)) {
            recovery_focus_index_ = 2;
            discard_selected_checkpoint();
        } else if (x >= left + 2 * (layout.button_width + layout.button_gap) && x <= right) {
            recovery_focus_index_ = 3;
            close_recovery_modal();
        }
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

void NativeWindow::paint_recovery_modal(HDC dc, int width, int height) {
    const auto layout = recovery_layout(width, height);
    const bool vietnamese = language_ == UILanguage::Vietnamese;
    const bool dark = theme_ == ThemeMode::CyberDark;
    const COLORREF background = dark ? RGB(15, 23, 34) : RGB(255, 255, 255);
    const COLORREF border = dark ? RGB(54, 67, 86) : RGB(203, 213, 225);
    const COLORREF accent = dark ? RGB(0, 220, 180) : RGB(0, 135, 106);
    const COLORREF text = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF muted = dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF row = dark ? RGB(20, 32, 48) : RGB(241, 245, 249);
    const COLORREF selected = dark ? RGB(22, 45, 62) : RGB(236, 253, 245);
    HDC shade_dc = CreateCompatibleDC(dc);
    HBITMAP shade_bitmap = CreateCompatibleBitmap(dc, dip(width), dip(height));
    HGDIOBJ old_shade_bitmap = SelectObject(shade_dc, shade_bitmap);
    RECT whole{0, 0, width, height};
    HBRUSH shade = CreateSolidBrush(RGB(2, 4, 8));
    FillRect(shade_dc, &whole, shade);
    DeleteObject(shade);
    const BLENDFUNCTION blend{AC_SRC_OVER, 0, 185, 0};
    AlphaBlend(dc, 0, 0, width, height, shade_dc, 0, 0, width, height, blend);
    SelectObject(shade_dc, old_shade_bitmap);
    DeleteObject(shade_bitmap);
    DeleteDC(shade_dc);
    HBRUSH card_brush = CreateSolidBrush(background);
    HPEN card_pen = CreatePen(PS_SOLID, 1, border);
    auto old_brush = SelectObject(dc, card_brush);
    auto old_pen = SelectObject(dc, card_pen);
    RoundRect(dc, layout.x, layout.y, layout.x + layout.width,
              layout.y + layout.height, 12, 12);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(card_brush);
    DeleteObject(card_pen);

    HFONT title_font = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT normal_font = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT bold_font = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    auto old_font = SelectObject(dc, title_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, accent);
    RECT title_rect{layout.x + 20, layout.y + 14, layout.x + layout.width - 20,
                    layout.y + 42};
    DrawTextW(dc, vietnamese ? L"Phục hồi tài liệu" : L"Recover documents",
              -1, &title_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, normal_font);
    SetTextColor(dc, text);
    RECT intro{layout.x + 20, layout.y + 52, layout.x + layout.width - 20,
               layout.y + 96};
    DrawTextW(dc, vietnamese
        ? L"Bản tự lưu sẽ mở ở tab mới chưa lưu. Lưu tài liệu để chọn tệp chính thức."
        : L"An autosave opens in a new unsaved tab. Save it to choose a permanent file.",
        -1, &intro, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    const size_t end = std::min(recovery_candidates_.size(),
        recovery_first_visible_ + static_cast<size_t>(layout.visible_rows));
    int row_y = layout.list_top;
    for (size_t index = recovery_first_visible_; index < end; ++index, row_y += layout.row_height) {
        const auto& candidate = recovery_candidates_[index];
        const bool is_selected = index == recovery_selected_index_;
        RECT item{layout.list_x, row_y, layout.x + layout.width - 20,
                  row_y + layout.row_height - 4};
        HBRUSH item_brush = CreateSolidBrush(is_selected ? selected : row);
        HPEN item_pen = CreatePen(PS_SOLID, 1, is_selected ? accent : border);
        old_brush = SelectObject(dc, item_brush);
        old_pen = SelectObject(dc, item_pen);
        RoundRect(dc, item.left, item.top, item.right, item.bottom, 6, 6);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(item_brush);
        DeleteObject(item_pen);

        const auto id = short_document_id(candidate.document_id);
        SelectObject(dc, bold_font);
        SetTextColor(dc, text);
        RECT name{item.left + 10, item.top + 6, item.right - 10, item.top + 23};
        DrawTextW(dc, id.c_str(), -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        std::wostringstream details;
        details << utf8_to_wide(candidate.timestamp) << L" · "
                << std::fixed << std::setprecision(2)
                << candidate.page_width_mm << L" × " << candidate.page_height_mm
                << L" mm · " << candidate.object_count << L" objects · r"
                << candidate.revision;
        SelectObject(dc, normal_font);
        SetTextColor(dc, muted);
        RECT detail{item.left + 10, item.top + 25, item.right - 10, item.bottom - 3};
        DrawTextW(dc, details.str().c_str(), -1, &detail,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (is_selected && recovery_focus_index_ == 0) {
            RECT focus = item;
            InflateRect(&focus, -4, -4);
            DrawFocusRect(dc, &focus);
        }
    }

    SelectObject(dc, normal_font);
    SetTextColor(dc, muted);
    RECT footer{layout.x + 20, layout.button_top - 62,
                layout.x + layout.width - 20, layout.button_top - 8};
    std::wstring footer_text;
    if (recovery_candidates_.empty()) {
        footer_text = vietnamese ? L"Không có bản tự lưu hợp lệ." : L"No valid recovery checkpoints.";
    } else if (recovery_candidates_truncated_) {
        footer_text = vietnamese ? L"Đang hiển thị các bản gần nhất; danh sách đã đạt giới hạn an toàn."
                                : L"Showing recent checkpoints; the safe scan limit was reached.";
    } else {
        footer_text = vietnamese ? L"Mũi tên chọn · Tab chuyển nút · Enter thực hiện · Esc để sau"
                                : L"Arrows select · Tab changes action · Enter runs · Esc for later";
    }
    DrawTextW(dc, footer_text.c_str(), -1, &footer,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    const int right = layout.x + layout.width - 20;
    const int left = right - 3 * layout.button_width - 2 * layout.button_gap;
    const wchar_t* labels[] = {vietnamese ? L"Phục hồi" : L"Recover",
                               vietnamese ? L"Xóa…" : L"Discard…",
                               vietnamese ? L"Để sau" : L"Later"};
    for (int i = 0; i < 3; ++i) {
        const int x = left + i * (layout.button_width + layout.button_gap);
        const bool unavailable = i == 0 &&
            (recovery_candidates_.empty() ||
             restored_recovery_paths_.contains(recovery_candidates_[recovery_selected_index_].path));
        const COLORREF button_bg = unavailable ? row : (i == 0 ? accent : background);
        const COLORREF button_text = unavailable ? muted : (i == 0 ? background : text);
        HBRUSH button_brush = CreateSolidBrush(button_bg);
        HPEN button_pen = CreatePen(PS_SOLID, 1,
            recovery_focus_index_ == i + 1 ? accent : border);
        old_brush = SelectObject(dc, button_brush);
        old_pen = SelectObject(dc, button_pen);
        RoundRect(dc, x, layout.button_top, x + layout.button_width,
                  layout.button_top + 34, 6, 6);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(button_brush);
        DeleteObject(button_pen);
        SelectObject(dc, bold_font);
        SetTextColor(dc, button_text);
        RECT button_text_rect{x + 4, layout.button_top + 2,
                              x + layout.button_width - 4, layout.button_top + 32};
        DrawTextW(dc, labels[i], -1, &button_text_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (recovery_focus_index_ == i + 1) {
            RECT focus{ x + 4, layout.button_top + 4,
                        x + layout.button_width - 4, layout.button_top + 30 };
            DrawFocusRect(dc, &focus);
        }
    }
    SelectObject(dc, old_font);
    DeleteObject(title_font);
    DeleteObject(normal_font);
    DeleteObject(bold_font);
}
#endif

} // namespace pikdraw::desktop
