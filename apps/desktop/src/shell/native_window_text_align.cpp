#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/typography/layout/case_conversion.hpp"
#include "pikdraw/desktop/shell/native_font_picker.hpp"
#include "native_window_text_properties.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_length_input.hpp"
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

void NativeWindow::cycle_font_family() {
    const auto view = text_properties_view(*this);
    const std::string current = is_inline_text_editing_ && text_editor_
        ? text_editor_->draft.font.family_name
        : view.context ? view.family : text_font_family_at(font_family_index_);
    const int current_index = remember_text_font_family(current);
    const auto& families = text_font_registry();
    if (families.empty()) return;
    font_family_index_ = (current_index + 1) % static_cast<int>(families.size());
    const std::string fam = families[static_cast<size_t>(font_family_index_)];
    if (is_inline_text_editing_ && text_editor_) {
        text_editor_->remember();
        text_editor_->draft.font.family_name = fam;
        text_editor_->draft.font.postscript_name.clear();
        refresh_inline_text_preview();
    } else {
        vector::TextPropertiesDelta delta;
        delta.family = fam;
        workflow().update_selected_text_properties(delta);
    }
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
    const auto view = text_properties_view(*this);
    const std::string curr = is_inline_text_editing_ && text_editor_
        ? text_editor_->draft.font.family_name
        : view.context ? view.family : text_font_family_at(font_family_index_);
    const std::optional<typography::FontIdentity> inline_font_baseline =
        is_inline_text_editing_ && text_editor_
            ? std::optional<typography::FontIdentity>{text_editor_->draft.font} : std::nullopt;
    const std::string sample = is_inline_text_editing_ && text_editor_ && !text_editor_->draft.text.empty()
        ? text_editor_->draft.text.substr(0, std::min<size_t>(60, text_editor_->draft.text.size()))
        : "PikDraw Tiếng Việt Ánh Sáng 123";

    font_picker_->set_on_preview([this](const std::string& fam) {
        if (is_inline_text_editing_ && text_editor_) {
            text_editor_->draft.font.family_name = fam;
            text_editor_->draft.font.postscript_name.clear();
            refresh_inline_text_preview();
        }
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Xem thử: " : "Preview: ") + fam;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    });

    font_picker_->set_on_select([this, inline_font_baseline](const std::string& fam) {
        font_family_index_ = remember_text_font_family(fam);
        if (is_inline_text_editing_ && text_editor_) {
            if (inline_font_baseline) text_editor_->draft.font = *inline_font_baseline;
            text_editor_->remember();
            text_editor_->draft.font.family_name = fam;
            text_editor_->draft.font.postscript_name.clear();
            refresh_inline_text_preview();
        } else {
            vector::TextPropertiesDelta delta;
            delta.family = fam;
            workflow().update_selected_text_properties(delta);
        }
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Đã chọn: " : "Selected: ") + fam;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    });

    font_picker_->set_on_cancel([this, inline_font_baseline]() {
        if (is_inline_text_editing_ && text_editor_ && inline_font_baseline) {
            text_editor_->draft.font = *inline_font_baseline;
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
    if (is_inline_text_editing_ && text_editor_) {
        text_editor_->remember();
        text_editor_->draft.font.size_pt = std::clamp(text_editor_->draft.font.size_pt + delta_pt, 1.0, 2880.0);
        font_size_pt_ = text_editor_->draft.font.size_pt;
        refresh_inline_text_preview();
    } else {
        const auto view = text_properties_view(*this);
        if (view.context) {
            double new_size = std::clamp(view.size_pt + delta_pt, 1.0, 2880.0);
            font_size_pt_ = new_size;
            vector::TextPropertiesDelta delta;
            delta.size_pt = new_size;
            workflow().update_selected_text_properties(delta);
        } else {
            font_size_pt_ = std::clamp(font_size_pt_ + delta_pt, 1.0, 2880.0);
        }
    }
    status_msg_ = std::string("Cỡ chữ / Font Size: ") + std::to_string(static_cast<int>(font_size_pt_)) + " pt";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_bold() {
    if (is_inline_text_editing_ && text_editor_) {
        text_editor_->remember();
        bool new_val = !(text_editor_->draft.font.weight >= 700);
        text_editor_->draft.font.weight = static_cast<uint16_t>(new_val ? 700 : 400);
        font_bold_ = new_val;
        refresh_inline_text_preview();
    } else {
        const auto view = text_properties_view(*this);
        bool new_val = view.context ? (view.mixed_bold || !view.bold) : !font_bold_;
        font_bold_ = new_val;
        if (view.context) {
            vector::TextPropertiesDelta delta;
            delta.weight = new_val ? 700 : 400;
            workflow().update_selected_text_properties(delta);
        }
    }
    status_msg_ = font_bold_ ? "In đậm: Bật (Bold: ON)" : "In đậm: Tắt (Bold: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_italic() {
    if (is_inline_text_editing_ && text_editor_) {
        text_editor_->remember();
        text_editor_->draft.font.italic = !text_editor_->draft.font.italic;
        font_italic_ = text_editor_->draft.font.italic;
        refresh_inline_text_preview();
    } else {
        const auto view = text_properties_view(*this);
        bool new_val = view.context ? (view.mixed_italic || !view.italic) : !font_italic_;
        font_italic_ = new_val;
        if (view.context) {
            vector::TextPropertiesDelta delta;
            delta.italic = new_val;
            workflow().update_selected_text_properties(delta);
        }
    }
    status_msg_ = font_italic_ ? "In nghiêng: Bật (Italic: ON)" : "In nghiêng: Tắt (Italic: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_underline() {
    if (is_inline_text_editing_ && text_editor_) {
        text_editor_->remember();
        text_editor_->draft.underline = !text_editor_->draft.underline;
        font_underline_ = text_editor_->draft.underline;
        refresh_inline_text_preview();
    } else {
        const auto view = text_properties_view(*this);
        bool new_val = view.context ? (view.mixed_underline || !view.underline) : !font_underline_;
        font_underline_ = new_val;
        if (view.context) {
            vector::TextPropertiesDelta delta;
            delta.underline = new_val;
            workflow().update_selected_text_properties(delta);
        }
    }
    status_msg_ = font_underline_ ? "Gạch chân: Bật (Underline: ON)" : "Gạch chân: Tắt (Underline: OFF)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_font_strikethrough() {
    if (is_inline_text_editing_ && text_editor_) {
        text_editor_->remember();
        text_editor_->draft.strikethrough = !text_editor_->draft.strikethrough;
        font_strikethrough_ = text_editor_->draft.strikethrough;
        refresh_inline_text_preview();
    } else {
        const auto view = text_properties_view(*this);
        bool new_val = view.context ? (view.mixed_strikethrough || !view.strikethrough) : !font_strikethrough_;
        font_strikethrough_ = new_val;
        if (view.context) {
            vector::TextPropertiesDelta delta;
            delta.strikethrough = new_val;
            workflow().update_selected_text_properties(delta);
        }
    }
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
            const auto layout_width = text_editor_->draft.layout().bounds().width;
            text_editor_->draft.frame_width_mm = std::max(
                layout_width, text_editor_->draft.font.size_mm() * 12.0);
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Chuyển thành Văn bản Khung (Paragraph Text)"
                : "Converted to Paragraph Text";
        }
        refresh_inline_text_preview();
    } else {
        const auto& sel = root().vector_controller().selection().selected_ids();
        const auto snapshot = root().document().current_snapshot();
        if (sel.size() != 1 || !snapshot) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn đúng một đối tượng Text để đổi giữa chữ tự do và khung đoạn văn"
                : "Select exactly one Text object to switch between point text and a paragraph frame";
        } else if (const auto object = snapshot->objects().get(sel.front());
                   object && std::holds_alternative<document::TextObject>(*object)) {
            const auto& text = std::get<document::TextObject>(*object);
            const double target_width = text.frame_width_mm > 0.0 ? 0.0
                : std::max(text.layout().bounds().width, text.font.size_mm() * 12.0);
            const bool changed = workflow().set_selected_text_layout(target_width, std::nullopt);
            status_msg_ = changed
                ? (target_width == 0.0
                    ? (language_ == UILanguage::Vietnamese ? "Đã chuyển thành Văn bản Tự do (Artistic Text · Ctrl+F8) · Ctrl+Z hoàn tác" : "Converted to Artistic Text (Ctrl+F8) · Ctrl+Z undoes")
                    : (language_ == UILanguage::Vietnamese ? "Đã chuyển thành Văn bản Khung (Paragraph Text · Ctrl+F8) · Ctrl+Z hoàn tác" : "Converted to Paragraph Text (Ctrl+F8) · Ctrl+Z undoes"))
                : (language_ == UILanguage::Vietnamese
                    ? "Không thể đổi kiểu khung; kiểm tra đối tượng có bị khóa hay không"
                    : "Could not change text frame; check whether the object is locked");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Lệnh này chỉ áp dụng cho đối tượng Text"
                : "This command applies only to Text objects";
        }
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::set_font_alignment(typography::TextAlignment align) {
    if (is_inline_text_editing_ && text_editor_) {
        text_editor_->remember();
        text_editor_->draft.alignment = align;
        font_alignment_ = align;
        refresh_inline_text_preview();
    } else {
        const auto view = text_properties_view(*this);
        font_alignment_ = align;
        if (view.context) {
            vector::TextPropertiesDelta delta;
            delta.alignment = align;
            workflow().update_selected_text_properties(delta);
        }
    }
    status_msg_ = (align == typography::TextAlignment::Center) ? "Căn giữa (Center Align)"
                : (align == typography::TextAlignment::Right) ? "Căn phải (Right Align)"
                : (align == typography::TextAlignment::Justify) ? "Căn đều (Justify Align · Ctrl+J)"
                : "Căn trái (Left Align)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::apply_change_case(typography::ChangeCaseMode mode) {
    bool applied = false;
    if (is_inline_text_editing_ && text_editor_) {
        auto& editor = *text_editor_;
        if (editor.has_selection()) {
            const std::string selected = editor.selected_text();
            std::string converted = typography::convert_case_utf8(selected, mode);
            if (converted != selected) {
                editor.replace(std::move(converted));
                applied = true;
            }
        } else {
            const std::string original = editor.draft.text;
            std::string converted = typography::convert_case_utf8(original, mode);
            if (converted != original) {
                const size_t caret = std::min(editor.caret, original.size());
                const size_t converted_caret = typography::convert_case_utf8(
                    std::string_view(original).substr(0, caret), mode).size();
                editor.remember();
                editor.draft.text = std::move(converted);
                editor.caret = editor.anchor = std::min(converted_caret, editor.draft.text.size());
                applied = true;
            }
        }
        refresh_inline_text_preview();
    } else {
        const auto& selected = root().vector_controller().selection().selected_ids();
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot || selected.empty()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn văn bản hoặc vào chế độ soạn chữ để đổi hoa / thường"
                : "Select text or enter text editing to change case";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
        bool all_text = true;
        bool any_change = false;
        for (const auto id : selected) {
            const auto object = snapshot->objects().get(id);
            if (!object || !std::holds_alternative<document::TextObject>(*object)) {
                all_text = false;
                break;
            }
            const auto& text = std::get<document::TextObject>(*object).text;
            any_change = any_change || typography::convert_case_utf8(text, mode) != text;
        }
        if (!all_text) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn riêng các đối tượng văn bản có thể sửa rồi thử lại"
                : "Select only editable text objects, then try again";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
        if (!any_change) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Văn bản đã ở kiểu hoa / thường này; không có gì thay đổi"
                : "Text already uses this case; nothing changed";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
        applied = workflow().change_case_selected_text(mode);
        if (!applied) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể đổi chữ; kiểm tra khóa đối tượng và quyền sửa"
                : "Could not change case; check whether the text is locked or editable";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
    }
    std::string mode_str = (mode == typography::ChangeCaseMode::Uppercase) ? "CHỮ HOA"
        : (mode == typography::ChangeCaseMode::Lowercase) ? "chữ thường"
        : (mode == typography::ChangeCaseMode::TitleCase) ? "Viết Hoa Từng Từ"
        : (mode == typography::ChangeCaseMode::SentenceCase) ? "Viết hoa đầu câu"
        : "Đảo ngược hoa/thường";
    status_msg_ = applied
        ? (language_ == UILanguage::Vietnamese
            ? "Đã đổi kiểu chữ: " + mode_str + " · Shift+F3 xoay lowercase → Title Case → UPPERCASE"
            : "Changed text case: " + mode_str + " · Shift+F3 cycles lowercase → Title Case → UPPERCASE")
        : (language_ == UILanguage::Vietnamese
            ? "Văn bản đã ở kiểu hoa / thường này; không có gì thay đổi"
            : "Text already uses this case; nothing changed");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::cycle_change_case() {
    std::string sample;
    if (is_inline_text_editing_ && text_editor_) {
        sample = text_editor_->has_selection() ? text_editor_->selected_text() : text_editor_->draft.text;
    } else {
        const auto snap = root().document().current_snapshot();
        for (auto id : root().vector_controller().selection().selected_ids()) {
            auto opt = snap->objects().get(id);
            if (opt && std::holds_alternative<document::TextObject>(*opt)) {
                sample = std::get<document::TextObject>(*opt).text;
                break;
            }
        }
    }
    auto next_mode = typography::next_cycled_case_mode(sample);
    apply_change_case(next_mode);
}

void NativeWindow::sync_selected_text_controls() {
    const auto view = text_properties_view(*this);
    if (!view.context) return;
    font_size_pt_ = view.size_pt;
    font_bold_ = view.bold;
    font_italic_ = view.italic;
    font_underline_ = view.underline;
    font_strikethrough_ = view.strikethrough;
    font_alignment_ = view.alignment;
    if (!view.family.empty()) {
        font_family_index_ = remember_text_font_family(view.family);
    }
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
            ? "Đã phân bổ khoảng hở đều: " + format_mm_display_utf8(distribute_spacing_mm_, display_length_unit_, 5)
            : "Applied fixed spacing gap: " + format_mm_display_utf8(distribute_spacing_mm_, display_length_unit_, 5);
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

void NativeWindow::link_selected_text_frames() {
    bool ok = workflow().link_selected_text_frames();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã nối luồng văn bản tràn (Linked Text Frames) · Ctrl+Z để hoàn tác"
            : "Linked text frames · Ctrl+Z to undo";
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Vui lòng chọn đúng 2 khung văn bản để nối luồng"
            : "Please select exactly two text frames to link";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::unlink_selected_text_frames() {
    bool ok = workflow().unlink_selected_text_frame();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã ngắt liên kết khung văn bản (Unlinked Frame)"
            : "Unlinked text frame";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::combine_selected_text_objects() {
    bool ok = workflow().combine_selected_text_objects();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã gộp các đối tượng văn bản thành 1 đoạn (Combine Text)"
            : "Combined text objects";
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Cần chọn từ 2 đối tượng văn bản trở lên để gộp"
            : "Select at least two text objects to combine";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::split_selected_text_lines() {
    bool ok = workflow().split_selected_text_lines();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã tách văn bản nhiều dòng thành các đối tượng độc lập (Split by Lines)"
            : "Split multiline text into separate lines";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::fit_text_to_path() {
    bool ok = workflow().attach_selected_text_to_path();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã uốn chữ theo Path · Nhấp phải chữ để Đảo chiều hoặc Hủy uốn · Ctrl+Z hoàn tác"
            : "Fitted text to path · Right-click the text to reverse or straighten it · Ctrl+Z undoes";
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Cách dùng: chọn 1 Text và 1 Path chưa khóa (Shift+click để chọn thêm), rồi chọn Text > Fit Text to Path"
            : "How to use: select one unlocked Text and one unlocked Path (Shift-click to add one), then choose Text > Fit Text to Path";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::straighten_text_from_path() {
    bool ok = workflow().detach_selected_text_from_path();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã đưa chữ về dạng ngang thông thường (Straighten Text)"
            : "Straightened text";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::reverse_text_on_path() {
    bool ok = workflow().toggle_selected_text_path_reverse();
    if (ok) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã lật/đảo chiều chữ men theo đường cong"
            : "Reversed text on path";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

} // namespace pikdraw::desktop
