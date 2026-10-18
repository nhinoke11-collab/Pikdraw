#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "native_window_text_properties.hpp"
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

void NativeWindow::cycle_font_family() {
    font_family_index_ = (font_family_index_ + 1) % kNumFontFamilies;
    const std::string fam = kFontFamilies[font_family_index_];
    apply_text_style_controls(true);
    status_msg_ = std::string("Phông chữ / Font: ") + fam;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::open_font_picker(int anchor_x, int anchor_y) {
    if (!font_picker_) font_picker_ = std::make_unique<NativeFontPicker>();
    if (font_picker_->is_open()) {
        close_font_picker();
        return;
    }
    const std::string curr = is_inline_text_editing_ && text_editor_
        ? text_editor_->draft.font.family_name : text_font_family_at(font_family_index_);
    const std::string sample = is_inline_text_editing_ && text_editor_ && !text_editor_->draft.text.empty()
        ? text_editor_->draft.text.substr(0, std::min<size_t>(60, text_editor_->draft.text.size()))
        : "PikDraw Tiếng Việt Ánh Sáng 123";

    font_picker_->set_on_preview([this](const std::string& fam) {
        if (is_inline_text_editing_ && text_editor_) {
            text_editor_->draft.font.family_name = fam;
            refresh_inline_text_preview();
        }
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Xem thử: " : "Preview: ") + fam;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    });

    font_picker_->set_on_select([this](const std::string& fam) {
        font_family_index_ = remember_text_font_family(fam);
        apply_text_style_controls(true);
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Đã chọn: " : "Selected: ") + fam;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    });

    font_picker_->set_on_cancel([this]() {
        if (is_inline_text_editing_ && text_editor_ && font_picker_) {
            text_editor_->draft.font.family_name = font_picker_->baseline_font();
            refresh_inline_text_preview();
        }
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    });

    font_picker_->open(anchor_x, anchor_y, width_, height_, curr, sample);
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::close_font_picker() {
    if (font_picker_ && font_picker_->is_open()) {
        font_picker_->close();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}

bool NativeWindow::is_font_picker_open() const noexcept {
    return font_picker_ && font_picker_->is_open();
}

void NativeWindow::adjust_font_size(double delta_pt) {
    font_size_pt_ = std::clamp(font_size_pt_ + delta_pt, 1.0, 2880.0);
    apply_text_style_controls();
    status_msg_ = std::string("Cỡ chữ / Font Size: ") + std::to_string(static_cast<int>(font_size_pt_)) + " pt";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_bold() {
    font_bold_ = !font_bold_;
    apply_text_style_controls();
    status_msg_ = font_bold_ ? "In đậm: Bật (Bold: ON)" : "In đậm: Tắt (Bold: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_italic() {
    font_italic_ = !font_italic_;
    apply_text_style_controls();
    status_msg_ = font_italic_ ? "In nghiêng: Bật (Italic: ON)" : "In nghiêng: Tắt (Italic: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_underline() {
    font_underline_ = !font_underline_;
    apply_text_style_controls();
    status_msg_ = font_underline_ ? "Gạch chân: Bật (Underline: ON)" : "Gạch chân: Tắt (Underline: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_strikethrough() {
    font_strikethrough_ = !font_strikethrough_;
    apply_text_style_controls();
    status_msg_ = font_strikethrough_ ? "Gạch ngang: Bật (Strikethrough: ON)" : "Gạch ngang: Tắt (Strikethrough: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_text_bullets() {
    const auto toggle_line_bullets = [](std::string text) {
        std::istringstream stream(text);
        std::string line;
        std::string result;
        bool has_bullets = true;
        std::vector<std::string> lines;
        while (std::getline(stream, line)) {
            lines.push_back(line);
            if (line.rfind("• ", 0) != 0 && line.rfind("- ", 0) != 0) has_bullets = false;
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            if (has_bullets) {
                if (lines[i].rfind("• ", 0) == 0) lines[i] = lines[i].substr(std::string("• ").size());
                else if (lines[i].rfind("- ", 0) == 0) lines[i] = lines[i].substr(2);
            } else {
                lines[i] = "• " + lines[i];
            }
            result += lines[i];
            if (i + 1 < lines.size() || (!text.empty() && text.back() == '\n')) result += "\n";
        }
        return result;
    };

    if (text_editor_ && is_inline_text_editing_) {
        text_editor_->remember();
        text_editor_->draft.text = toggle_line_bullets(text_editor_->draft.text);
        refresh_inline_text_preview();
    } else {
        const auto& sel = root().vector_controller().selection().selected_ids();
        auto snap = root().document().current_snapshot();
        for (auto id : sel) {
            const auto obj = snap->objects().get(id);
            if (obj && std::holds_alternative<document::TextObject>(*obj)) {
                auto t = std::get<document::TextObject>(*obj);
                std::string updated = toggle_line_bullets(t.text);
                workflow().update_text_content(id, updated);
            }
        }
    }
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã chuyển đổi danh sách dấu chấm (Bullets · •=)"
        : "Toggled bulleted list (Bullets · •=)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_text_numbering() {
    const auto toggle_line_num = [](std::string text) {
        std::istringstream stream(text);
        std::string line;
        std::vector<std::string> lines;
        bool has_nums = true;
        while (std::getline(stream, line)) {
            lines.push_back(line);
            size_t dot = line.find(". ");
            if (dot == std::string::npos || dot == 0 || dot > 3) has_nums = false;
            else {
                for (size_t k = 0; k < dot; ++k) {
                    if (!std::isdigit(static_cast<unsigned char>(line[k]))) has_nums = false;
                }
            }
        }
        std::string result;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (has_nums) {
                size_t dot = lines[i].find(". ");
                if (dot != std::string::npos) lines[i] = lines[i].substr(dot + 2);
            } else {
                lines[i] = std::to_string(i + 1) + ". " + lines[i];
            }
            result += lines[i];
            if (i + 1 < lines.size() || (!text.empty() && text.back() == '\n')) result += "\n";
        }
        return result;
    };

    if (text_editor_ && is_inline_text_editing_) {
        text_editor_->remember();
        text_editor_->draft.text = toggle_line_num(text_editor_->draft.text);
        refresh_inline_text_preview();
    } else {
        const auto& sel = root().vector_controller().selection().selected_ids();
        auto snap = root().document().current_snapshot();
        for (auto id : sel) {
            const auto obj = snap->objects().get(id);
            if (obj && std::holds_alternative<document::TextObject>(*obj)) {
                auto t = std::get<document::TextObject>(*obj);
                std::string updated = toggle_line_num(t.text);
                workflow().update_text_content(id, updated);
            }
        }
    }
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã chuyển đổi danh sách số (Numbering · 1.)"
        : "Toggled numbered list (Numbering · 1.)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_non_printing_characters() {
    show_non_printing_characters_ = !show_non_printing_characters_;
    status_msg_ = show_non_printing_characters_
        ? (language_ == UILanguage::Vietnamese ? "Hiện ký tự ẩn (Show Non-Printing Characters: Bật)" : "Show Non-Printing Characters: ON")
        : (language_ == UILanguage::Vietnamese ? "Ẩn ký tự không in (Show Non-Printing Characters: Tắt)" : "Show Non-Printing Characters: OFF");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_convert_artistic_paragraph() {
    if (text_editor_ && is_inline_text_editing_) {
        text_editor_->remember();
        if (text_editor_->draft.frame_width_mm > 0.0) {
            text_editor_->draft.frame_width_mm = 0.0;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Chuyển thành Văn bản Tự do (Artistic Text)"
                : "Converted to Artistic Text";
        } else {
            text_editor_->draft.frame_width_mm = 100.0;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Chuyển thành Văn bản Khung (Paragraph Text)"
                : "Converted to Paragraph Text";
        }
        refresh_inline_text_preview();
    } else {
        const auto& sel = root().vector_controller().selection().selected_ids();
        auto snap = root().document().current_snapshot();
        for (auto id : sel) {
            const auto obj = snap->objects().get(id);
            if (obj && std::holds_alternative<document::TextObject>(*obj)) {
                const auto& t = std::get<document::TextObject>(*obj);
                double target_width = (t.frame_width_mm > 0.0) ? 0.0 : 100.0;
                workflow().set_selected_text_layout(target_width, std::nullopt);
                status_msg_ = (target_width == 0.0)
                    ? (language_ == UILanguage::Vietnamese ? "Đã chuyển thành Văn bản Tự do (Artistic Text · Ctrl+F8)" : "Converted to Artistic Text (Ctrl+F8)")
                    : (language_ == UILanguage::Vietnamese ? "Đã chuyển thành Văn bản Khung (Paragraph Text · Ctrl+F8)" : "Converted to Paragraph Text (Ctrl+F8)");
                break;
            }
        }
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::set_font_alignment(typography::TextAlignment align) {
    font_alignment_ = align;
    apply_text_style_controls();
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
