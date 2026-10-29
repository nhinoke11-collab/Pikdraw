#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"
#ifdef _WIN32
#include "../clipboard/native_clipboard.hpp"
#include <imm.h>
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pikdraw::desktop {
bool NativeWindow::handle_inline_text_character(uint32_t character) {
    if (!is_inline_text_editing_ || !text_editor_) return false;
    auto& editor = *text_editor_;
    if (character == editor.suppress_character) {
        editor.suppress_character = 0;
        return true;
    }
    editor.suppress_character = 0;
    if (!editor.composition.empty()) return true;
    if (character == 8) editor.erase(true);
    else if (character == 13 || character == 10) editor.replace("\n");
    else if (character == 9) editor.replace("    ");
    else if (character == 27) { cancel_inline_text_editing(); return true; }
    else if (character >= 32 && character <= 0x10ffff) {
        if (character >= 0xd800 && character <= 0xdbff) {
            editor.high_surrogate = static_cast<char16_t>(character);
            return true;
        }
        if (character >= 0xdc00 && character <= 0xdfff) {
            if (!editor.high_surrogate) return true;
            character = 0x10000 + ((editor.high_surrogate - 0xd800) << 10) + character - 0xdc00;
        }
        editor.high_surrogate = 0;
        std::string utf8;
        if (character <= 0x7f) utf8 += static_cast<char>(character);
        else if (character <= 0x7ff) {
            utf8 += static_cast<char>(0xc0 | (character >> 6));
            utf8 += static_cast<char>(0x80 | (character & 0x3f));
        } else if (character <= 0xffff) {
            utf8 += static_cast<char>(0xe0 | (character >> 12));
            utf8 += static_cast<char>(0x80 | ((character >> 6) & 0x3f));
            utf8 += static_cast<char>(0x80 | (character & 0x3f));
        } else {
            utf8 += static_cast<char>(0xf0 | (character >> 18));
            utf8 += static_cast<char>(0x80 | ((character >> 12) & 0x3f));
            utf8 += static_cast<char>(0x80 | ((character >> 6) & 0x3f));
            utf8 += static_cast<char>(0x80 | (character & 0x3f));
        }
        editor.replace(std::move(utf8));
    } else return true;
    refresh_inline_text_preview();
    return true;
}

bool NativeWindow::handle_inline_text_key(uint32_t key, bool control, bool shift) {
#ifdef _WIN32
    if (!is_inline_text_editing_ || !text_editor_ || active_num_field_ != NumericFieldId::None)
        return false;
    auto& editor = *text_editor_;
    if (!editor.composition.empty() && key != VK_ESCAPE) return true;
    if (key == VK_ESCAPE) {
        if (!editor.composition.empty()) {
            if (auto context = ImmGetContext(hwnd_)) {
                ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                ImmReleaseContext(hwnd_, context);
            }
            editor.composition.clear();
            refresh_inline_text_preview();
        } else cancel_inline_text_editing();
        return true;
    }
    if (control && (key == 'S' || key == 'O' || key == 'N' || key == 'W' || key == VK_TAB)) {
        commit_inline_text_editing();
        return false;
    }
    if (control && key == VK_RETURN) { commit_inline_text_editing(); return true; }
    if (control && key == 'A') { editor.anchor = 0; editor.caret = editor.draft.text.size(); }
    else if (control && (key == 'C' || key == 'X')) {
        if (editor.has_selection()) {
            auto text = editor.selected_text();
            // Windows plain text clipboard uses CRLF. Internal source uses LF for new input.
            std::string windows_text;
            for (char c : text) { if (c == '\n') windows_text += '\r'; windows_text += c; }
            if (NativeClipboard::write(hwnd_, {}, {}, windows_text) && key == 'X') editor.replace({});
        }
    } else if (control && key == 'V') {
        if (const auto text = NativeClipboard::read_text()) {
            try { if (!editor.replace(*text)) status_msg_ = "Text exceeds the 1 MiB limit"; }
            catch (const std::exception&) { status_msg_ = "Clipboard text is not valid Unicode"; }
        }
    } else if (control && (key == 'Z' || key == 'Y')) {
        if (editor.restore(key == 'Y' || shift)) sync_inline_text_controls();
    }
    else if (control && key == 'B') toggle_font_bold();
    else if (control && key == 'I') { toggle_font_italic(); editor.suppress_character = 9; }
    else if (control && key == 'U') toggle_font_underline();
    else if (control && (key == 'L' || key == 'E' || key == 'R' || key == 'J'))
        set_font_alignment(key == 'L' ? typography::TextAlignment::Left : key == 'E'
            ? typography::TextAlignment::Center : key == 'J'
            ? typography::TextAlignment::Justify : typography::TextAlignment::Right);
    else if (shift && key == VK_F3) {
        cycle_change_case();
        return true;
    }
    else if (control && key == VK_F8) {
        editor.remember();
        editor.draft.frame_width_mm = editor.draft.frame_width_mm > 0 ? 0
            : std::max(editor.draft.layout().bounds().width, editor.draft.font.size_mm() * 12);
    } else if (key == VK_RETURN) {
        editor.replace("\n");
        editor.suppress_character = 13;
    } else if (key == VK_TAB) {
        editor.replace("    ");
        editor.suppress_character = 9;
    } else if (key == VK_BACK || key == VK_DELETE) {
        editor.erase(key == VK_BACK, control);
        if (key == VK_BACK) editor.suppress_character = 8;
    } else if (key == VK_LEFT || key == VK_RIGHT) {
        size_t offset = editor.caret;
        if (!shift && editor.has_selection()) offset = key == VK_LEFT ? editor.first() : editor.last();
        else if (key == VK_LEFT) offset = control ? editor.word_left(offset)
            : typography::prev_grapheme_cluster(editor.draft.text, offset);
        else offset = control ? editor.word_right(offset)
            : typography::next_grapheme_cluster(editor.draft.text, offset);
        editor.move(offset, shift);
        editor.desired_x.reset();
    } else if (key == VK_HOME || key == VK_END || key == VK_UP || key == VK_DOWN) {
        const auto layout = editor.draft.layout();
        size_t index = 0;
        for (size_t i = 0; i < layout.lines().size(); ++i)
            if (layout.lines()[i].source_byte_offset <= editor.caret) index = i;
        const auto& line = layout.lines()[index];
        if (key == VK_HOME || key == VK_END) {
            const size_t offset = key == VK_HOME
                ? (control ? 0 : line.source_byte_offset)
                : (control ? editor.draft.text.size() : line.source_byte_offset + line.run.text_utf8.size());
            editor.move(offset, shift);
            editor.desired_x.reset();
        } else {
            const auto caret = tools::text_caret_metrics(layout, editor.caret);
            if (!editor.desired_x) editor.desired_x = caret.caret_position.x;
            const size_t next = key == VK_UP ? (index == 0 ? 0 : index - 1)
                : std::min(index + 1, layout.lines().size() - 1);
            const auto hit = typography::hit_test_caret(layout,
                {*editor.desired_x, layout.lines()[next].line_bounds.center().y});
            editor.move(hit.byte_index, shift);
        }
    } else return true; // Character input arrives through WM_CHAR; never run drawing shortcuts.
    refresh_inline_text_preview();
    return true;
#else
    (void)key; (void)control; (void)shift;
    return false;
#endif
}

bool NativeWindow::handle_inline_text_mouse(int x, int y, int phase, bool shift) {
#ifdef _WIN32
    if (!text_editor_ || !is_inline_text_editing_) return false;
    auto& editor = *text_editor_;
    const auto inverse = editor.local_to_world().inverted();
    if (!inverse) return false;
    auto point = inverse->map_point(canvas_to_world(x, y));
    const auto layout = editor.draft.layout();
    auto box = layout.bounds();
    if (editor.draft.frame_width_mm > 0) { box.x = 0; box.width = editor.draft.frame_width_mm; }
    const double margin = 6.0 / std::max(0.01, root().renderer().camera().zoom());
    const geometry::Rect2D hit_box{box.x - margin, box.y - margin,
        box.width + 2 * margin, box.height + 2 * margin};
    if (phase == 0 && !hit_box.contains(point)) return false;
    if (phase == 0) {
        editor.selecting = true;
        const auto hit = typography::hit_test_caret(layout, point);
        editor.move(hit.byte_index, shift);
        if (hwnd_) SetCapture(hwnd_);
    } else if (phase == 3) {
        editor.select_word(typography::hit_test_caret(layout, point).byte_index);
    } else if (editor.selecting) {
        if (editor.creating_frame && !editor.existing && editor.draft.text.empty()) {
            const auto parent_inverse = editor.parent_transform.inverted();
            const auto end = parent_inverse->map_point(canvas_to_world(x, y));
            const auto start = editor.drag_start;
            const double width = std::abs(end.x - start.x);
            if (width * root().renderer().camera().zoom() > 5) {
                editor.draft.frame_width_mm = std::min(width, geometry::kMaxObjectDimensionMm);
                editor.draft.position = {std::min(start.x, end.x),
                    std::max(start.y, end.y) - layout.lines().front().run.ascent_mm};
            }
        } else editor.move(typography::hit_test_caret(layout, point).byte_index, true);
    } else return false;
    if (phase == 2) {
        editor.selecting = editor.creating_frame = false;
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
    }
    refresh_inline_text_preview();
    return true;
#else
    (void)x; (void)y; (void)phase; (void)shift;
    return false;
#endif
}
#ifdef _WIN32
bool NativeWindow::handle_inline_text_ime(LPARAM flags) {
    if (!text_editor_ || !is_inline_text_editing_) return false;
    auto context = ImmGetContext(hwnd_);
    if (!context) return false;
    auto read = [&](DWORD field) {
        const LONG bytes = ImmGetCompositionStringW(context, field, nullptr, 0);
        if (bytes <= 0 || bytes > 2 * 1024 * 1024) return std::wstring{};
        std::wstring value(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
        ImmGetCompositionStringW(context, field, value.data(), bytes);
        return value;
    };
    if (flags & GCS_RESULTSTR) {
        const auto result = wide_to_utf8(read(GCS_RESULTSTR));
        text_editor_->composition.clear();
        text_editor_->replace(result);
    }
    if (flags & GCS_COMPSTR) text_editor_->composition = wide_to_utf8(read(GCS_COMPSTR));
    ImmReleaseContext(hwnd_, context);
    refresh_inline_text_preview();
    return true;
}

bool NativeWindow::handle_annotation_note_ime(LPARAM flags) {
#ifdef _WIN32
    if (!annotation_tool_ || !annotation_tool_->note_text_editing() ||
        !(flags & GCS_RESULTSTR)) return false;
    auto context = ImmGetContext(hwnd_);
    if (!context) return false;
    const LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
    if (bytes > 0 && bytes <= 128 * 1024 && bytes % sizeof(wchar_t) == 0) {
        std::wstring result(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
        const LONG copied = ImmGetCompositionStringW(
            context, GCS_RESULTSTR, result.data(), static_cast<DWORD>(bytes));
        if (copied == bytes) {
            for (const wchar_t unit : result)
                annotation_tool_->append_note_character(
                    static_cast<uint32_t>(unit));
        }
    }
    ImmReleaseContext(hwnd_, context);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
#else
    (void)flags;
    return false;
#endif
}
#endif
} // namespace pikdraw::desktop
