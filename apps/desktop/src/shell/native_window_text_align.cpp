#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_canvas_frame.hpp"
#ifdef _WIN32
#include "../clipboard/native_clipboard.hpp"
#endif
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pikdraw::desktop {

namespace {
const char* kFontFamilies[] = {
    "Be Vietnam Pro",
    "Segoe UI",
    "Arial",
    "Times New Roman",
    "JetBrains Mono"
};
const int kNumFontFamilies = 5;
}

void NativeWindow::start_inline_text_editing(geometry::Point2D pos_mm, document::ObjectId existing_id) {
    is_inline_text_editing_ = true;
    inline_editing_text_id_ = existing_id;
    inline_text_pos_mm_ = pos_mm;

    if (existing_id != document::kInvalidObjectId) {
        auto snap = root().document().current_snapshot();
        auto opt = snap->objects().get(existing_id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) {
            const auto& to = std::get<document::TextObject>(*opt);
            inline_text_buffer_ = to.text;
            inline_text_pos_mm_ = to.position;
            font_size_pt_ = to.font.size_pt;
            font_bold_ = (to.font.weight >= 700);
            font_italic_ = to.font.italic;
            font_underline_ = to.underline;
            font_alignment_ = to.alignment;
            for (int i = 0; i < kNumFontFamilies; ++i) {
                if (to.font.family_name == kFontFamilies[i]) {
                    font_family_index_ = i;
                    break;
                }
            }
        }
    } else {
        inline_text_buffer_.clear();
    }
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đang gõ văn bản trực tiếp trên trang giấy · Nhấn Enter để hoàn tất, Esc để hủy"
        : "Typing text directly on canvas · Press Enter to commit, Esc to cancel";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::commit_inline_text_editing() {
    if (!is_inline_text_editing_) return;

    if (!inline_text_buffer_.empty()) {
        const std::string font_family = kFontFamilies[font_family_index_ % kNumFontFamilies];
        if (inline_editing_text_id_ != document::kInvalidObjectId) {
            workflow().update_text_content(inline_editing_text_id_, inline_text_buffer_);
            workflow().update_text_style(
                inline_editing_text_id_, font_family, font_size_pt_,
                font_bold_, font_italic_, font_underline_, font_alignment_
            );
        } else {
            auto id = workflow().add_vietnamese_text(
                inline_text_pos_mm_.x, inline_text_pos_mm_.y,
                inline_text_buffer_, font_family, font_size_pt_, "#00f2c3"
            );
            if (id != document::kInvalidObjectId) {
                workflow().update_text_style(
                    id, font_family, font_size_pt_,
                    font_bold_, font_italic_, font_underline_, font_alignment_
                );
                if (is_in_isolation_mode()) {
                    auto iso_id = *current_isolated_group_id();
                    auto snap = root().document().current_snapshot();
                    auto obj = snap->objects().get(iso_id);
                    if (obj && std::holds_alternative<document::GroupObject>(*obj)) {
                        document::GroupObject updated_grp = std::get<document::GroupObject>(*obj);
                        updated_grp.children_ids.push_back(id);
                        auto map = snap->objects().set(iso_id, std::move(updated_grp));
                        root().document().apply_mutation(snap->page(), std::move(map));
                    }
                }
                root().vector_controller().selection().select(id);
            }
        }
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã cập nhật văn bản trên trang · Ctrl+Z để hoàn tác"
            : "Text committed on canvas · Ctrl+Z to undo";
    }

    is_inline_text_editing_ = false;
    inline_editing_text_id_ = document::kInvalidObjectId;
    inline_text_buffer_.clear();
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::cancel_inline_text_editing() {
    is_inline_text_editing_ = false;
    inline_editing_text_id_ = document::kInvalidObjectId;
    inline_text_buffer_.clear();
    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy soạn thảo văn bản" : "Canceled text editing";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::cycle_font_family() {
    font_family_index_ = (font_family_index_ + 1) % kNumFontFamilies;
    const std::string fam = kFontFamilies[font_family_index_];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = std::string("Phông chữ / Font: ") + fam;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::adjust_font_size(double delta_pt) {
    font_size_pt_ = std::clamp(font_size_pt_ + delta_pt, 6.0, 144.0);
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = std::string("Cỡ chữ / Font Size: ") + std::to_string(static_cast<int>(font_size_pt_)) + " pt";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_bold() {
    font_bold_ = !font_bold_;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = font_bold_ ? "In đậm: Bật (Bold: ON)" : "In đậm: Tắt (Bold: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_italic() {
    font_italic_ = !font_italic_;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = font_italic_ ? "In nghiêng: Bật (Italic: ON)" : "In nghiêng: Tắt (Italic: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_underline() {
    font_underline_ = !font_underline_;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = font_underline_ ? "Gạch chân: Bật (Underline: ON)" : "Gạch chân: Tắt (Underline: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::set_font_alignment(typography::TextAlignment align) {
    font_alignment_ = align;
    const std::string fam = kFontFamilies[font_family_index_ % kNumFontFamilies];
    if (is_text_object_selected()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            workflow().update_text_style(id, fam, font_size_pt_, font_bold_, font_italic_, font_underline_, font_alignment_);
        }
    }
    status_msg_ = (align == typography::TextAlignment::Center) ? "Căn giữa (Center Align)"
                : (align == typography::TextAlignment::Right) ? "Căn phải (Right Align)"
                : "Căn trái (Left Align)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::convert_selected_text_to_curves() {
    const auto& sel = root().vector_controller().selection().selected_ids();
    if (sel.empty()) return;
    int converted = 0;
    for (auto id : sel) {
        if (workflow().convert_text_to_curves(id)) {
            converted++;
        }
    }
    if (converted > 0) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã chuyển văn bản thành đường cong vector (Convert to Curves · Ctrl+Q)"
            : "Converted text to vector curves (Ctrl+Q)";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::insert_table(int rows, int cols) {
    auto id = workflow().create_table(40.0, 40.0, 160.0, 75.0, rows, cols);
    if (id != document::kInvalidObjectId) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã tạo bảng biểu vector chuyên nghiệp (Corel Table " + std::to_string(rows) + "×" + std::to_string(cols) + ")"
            : "Created vector table (" + std::to_string(rows) + "x" + std::to_string(cols) + ")";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::is_text_object_selected() const {
    const auto& sel = root().vector_controller().selection().selected_ids();
    if (sel.empty()) return false;
    const auto snap = root().document().current_snapshot();
    for (auto id : sel) {
        auto opt = snap->objects().get(id);
        if (opt && std::holds_alternative<document::TextObject>(*opt)) return true;
    }
    return false;
}

void NativeWindow::align_selected_objects(geometry::AlignMode mode) {
    bool ok = workflow().align_selected(mode, align_reference_, key_object_id_);
    if (ok) {
        std::string mode_str = "";
        switch (mode) {
            case geometry::AlignMode::Left: mode_str = "Căn trái (Align Left · Shift+L)"; break;
            case geometry::AlignMode::CenterHorizontal: mode_str = "Căn tâm theo phương ngang (Align Center Horizontally · C)"; break;
            case geometry::AlignMode::Right: mode_str = "Căn phải (Align Right · R)"; break;
            case geometry::AlignMode::Top: mode_str = "Căn trên (Align Top · T)"; break;
            case geometry::AlignMode::CenterVertical: mode_str = "Căn tâm theo phương dọc (Align Center Vertically · E)"; break;
            case geometry::AlignMode::Bottom: mode_str = "Căn dưới (Align Bottom · B)"; break;
            case geometry::AlignMode::CenterPage: mode_str = "Căn giữa trang in (Center to Page · P)"; break;
            case geometry::AlignMode::CenterPageHorizontal: mode_str = "Căn giữa trang theo chiều ngang (Center Page X)"; break;
            case geometry::AlignMode::CenterPageVertical: mode_str = "Căn giữa trang theo chiều dọc (Center Page Y)"; break;
        }
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thực hiện: " + mode_str + " · Nhấn Ctrl+Z để hoàn tác"
            : "Executed: " + mode_str + " · Press Ctrl+Z to undo";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::distribute_selected_objects(geometry::DistributeMode mode) {
    bool ok = workflow().distribute_selected(mode, -1.0);
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã phân bổ đều các đối tượng (Distribute Objects) · Nhấn Ctrl+Z để hoàn tác"
            : "Distributed selected objects · Press Ctrl+Z to undo";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::apply_specified_spacing(bool horizontal) {
    auto mode = horizontal ? geometry::DistributeMode::SpacingHorizontal : geometry::DistributeMode::SpacingVertical;
    bool ok = workflow().distribute_selected(mode, distribute_spacing_mm_);
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã phân bổ khoảng hở đều: " + std::to_string(static_cast<int>(distribute_spacing_mm_)) + " mm"
            : "Applied fixed spacing gap: " + std::to_string(static_cast<int>(distribute_spacing_mm_)) + " mm";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_inspector_properties_panel() noexcept {
    active_dock_tab_ = 0; // 0: Inspector & Properties
    show_style_inspector_ = false;
    show_align_panel_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã mở bảng thuộc tính đối tượng"
        : "Opened object Properties";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_object_properties_panel() noexcept {
    active_dock_tab_ = 0;
    show_style_inspector_ = true;
    show_align_panel_ = false;
    style_inspector_scroll_ = 0;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Appearance · Opacity, Blend, Gaussian Blur, Drop Shadow · Enter áp dụng · Ctrl+Z hoàn tác"
        : "Appearance · Opacity, Blend, Gaussian Blur, Drop Shadow · Enter applies · Ctrl+Z undoes";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_align_distribute_panel() noexcept {
    active_dock_tab_ = 0;
    show_style_inspector_ = false;
    show_align_panel_ = true;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Bảng Căn chỉnh & Phân bổ · chọn mốc rồi chọn lệnh · Ctrl+Z để hoàn tác"
        : "Align & Distribute · choose a reference and operation · Ctrl+Z to undo";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
} // namespace pikdraw::desktop
