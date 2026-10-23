#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_text_properties.hpp"
#include "pikdraw/typography/shaping/font_resolver.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#ifdef _WIN32
#include <imm.h>
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pikdraw::desktop {
void NativeWindow::sync_inline_text_controls() {
    if (!text_editor_) return;
    const auto& text = text_editor_->draft;
    font_size_pt_ = text.font.size_pt;
    font_bold_ = text.font.weight >= 700;
    font_italic_ = text.font.italic;
    font_underline_ = text.underline;
    font_strikethrough_ = text.strikethrough;
    font_alignment_ = text.alignment;
    font_family_index_ = remember_text_font_family(text.font.family_name);
    inline_text_buffer_ = text.text;
}
void NativeWindow::start_inline_text_editing(geometry::Point2D position, document::ObjectId id) {
    if (is_inline_text_editing_) commit_inline_text_editing();
    workflow().cancel_effect_draft();
    tools::CanvasTextEditor editor;
    editor.base = root().document().current_snapshot();
    editor.existing = id != document::kInvalidObjectId;
    if (editor.existing) {
        const auto object = editor.base->objects().get(id);
        const auto inherited = tools::editable_text_parent(*editor.base, id);
        if (!object || !std::holds_alternative<document::TextObject>(*object) || !inherited) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể sửa chữ bị khóa hoặc ẩn" : "Cannot edit locked or hidden text";
            return;
        }
        editor.original = editor.draft = std::get<document::TextObject>(*object);
        editor.parent_transform = *inherited;
    } else {
        editor.draft.id = document::generate_object_id();
        editor.draft.name = "Text";
        editor.draft.font = {text_font_family_at(font_family_index_), "",
            static_cast<uint16_t>(font_bold_ ? 700 : 400), font_italic_, font_size_pt_};
        editor.draft.underline = font_underline_;
        editor.draft.strikethrough = font_strikethrough_;
        editor.draft.alignment = font_alignment_;
        editor.draft.fill = default_style_.fill;
        if (const auto parent = current_isolated_group_id()) {
            const auto inherited = tools::editable_text_parent(*editor.base, *parent);
            const auto object = editor.base->objects().get(*parent);
            if (!inherited || !object || !std::holds_alternative<document::GroupObject>(*object)) return;
            editor.parent = *parent;
            editor.parent_transform = *inherited * std::get<document::GroupObject>(*object).transform;
        }
        const auto inverse = editor.parent_transform.inverted();
        if (!inverse) return;
        editor.draft.position = inverse->map_point(position);
        try {
            const auto layout = editor.draft.layout();
            if (!layout.lines().empty()) editor.draft.position.y -= layout.lines()[0].run.ascent_mm;
        } catch (const std::exception& error) { status_msg_ = error.what(); return; }
        editor.original = editor.draft;
    }
    if (!editor.local_to_world().inverted()) return;
    set_active_tool(4);
    if (active_tool_ != 4) return;
    text_editor_ = std::move(editor);
    is_inline_text_editing_ = true;
    if (text_editor_->existing) root().vector_controller().selection().select(id);
    else root().vector_controller().selection().clear();
    sync_inline_text_controls();
    if (text_editor_->existing) {
        const auto point = text_editor_->local_to_world().inverted()->map_point(position);
        const auto hit = typography::hit_test_caret(text_editor_->draft.layout(), point);
        text_editor_->move(hit.byte_index, false);
    }
    refresh_inline_text_preview();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Soạn chữ trên canvas · Enter xuống dòng · Ctrl+Enter hoàn tất · Esc hủy"
        : "Edit text on canvas · Enter: new line · Ctrl+Enter: finish · Esc: cancel";
#ifdef _WIN32
    if (hwnd_) { SetTimer(hwnd_, 12, GetCaretBlinkTime(), nullptr); SetFocus(hwnd_); }
#endif
}
void NativeWindow::refresh_inline_text_preview() {
    if (!text_editor_ || !is_inline_text_editing_) return;
    if (active_tool_ != 4) { commit_inline_text_editing(); return; }
    auto preview = text_editor_->draft;
    if (!text_editor_->composition.empty())
        preview.text.replace(text_editor_->first(), text_editor_->last() - text_editor_->first(),
                             text_editor_->composition);
    if (!workflow().preview_text_edit(preview, text_editor_->base, text_editor_->existing,
                                      text_editor_->parent)) {
        cancel_inline_text_editing();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bản nháp đã cũ hoặc chữ không được hỗ trợ; tài liệu được giữ nguyên"
            : "Text draft is stale or unsupported; document preserved";
        return;
    }
    sync_inline_text_controls();
    text_editor_->caret_visible = true;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
void NativeWindow::commit_inline_text_editing() {
    if (!text_editor_ || !is_inline_text_editing_) return;
#ifdef _WIN32
    if (hwnd_) {
        if (auto context = ImmGetContext(hwnd_)) {
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
            ImmReleaseContext(hwnd_, context);
        }
    }
#endif
    if (!text_editor_->composition.empty()) {
        text_editor_->draft.text.replace(
            text_editor_->first(),
            text_editor_->last() - text_editor_->first(),
            text_editor_->composition);
        text_editor_->composition.clear();
    }
    const auto id = text_editor_->draft.id;
    const bool retain = text_editor_->existing || !text_editor_->draft.text.empty();
    const bool saved = workflow().commit_text_edit(text_editor_->draft, text_editor_->base,
        text_editor_->existing, text_editor_->parent);
    is_inline_text_editing_ = false;
    text_editor_.reset();
    inline_text_buffer_.clear();
    if (saved && retain) root().vector_controller().selection().select(id);
    status_msg_ = language_ == UILanguage::Vietnamese
        ? (saved ? "Đã lưu chữ · Ctrl+Z hoàn tác" : "Không lưu bản nháp đã cũ; tài liệu được giữ nguyên")
        : (saved ? "Text saved · Ctrl+Z undoes" : "Stale text draft discarded; document preserved");
#ifdef _WIN32
    if (hwnd_) { KillTimer(hwnd_, 12); InvalidateRect(hwnd_, nullptr, FALSE); }
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
#endif
}
void NativeWindow::cancel_inline_text_editing() {
    workflow().clear_text_preview();
    text_editor_.reset();
    is_inline_text_editing_ = false;
    inline_text_buffer_.clear();
    status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy soạn chữ" : "Text editing cancelled";
#ifdef _WIN32
    if (hwnd_) { KillTimer(hwnd_, 12); InvalidateRect(hwnd_, nullptr, FALSE); }
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
#endif
}
void NativeWindow::apply_text_style_controls(const vector::TextPropertiesDelta& delta) {
    if (!vector::valid_text_properties_delta(delta)) return;
    if (text_editor_ && is_inline_text_editing_) {
        auto& editor = *text_editor_;
        editor.remember();
        auto& text = editor.draft;
        if (delta.family) {
            text.font.family_name = *delta.family;
            text.font.postscript_name.clear();
        }
        if (delta.size_pt) text.font.size_pt = *delta.size_pt;
        if (delta.weight) text.font.weight = static_cast<uint16_t>(*delta.weight);
        if (delta.italic) text.font.italic = *delta.italic;
        if (delta.underline) text.underline = *delta.underline;
        if (delta.strikethrough) text.strikethrough = *delta.strikethrough;
        if (delta.alignment) text.alignment = *delta.alignment;
        if (delta.frame_width_mm) text.frame_width_mm = *delta.frame_width_mm;
        if (delta.line_spacing_ratio) text.line_spacing_ratio = *delta.line_spacing_ratio;
        if (delta.letter_spacing_mm) text.letter_spacing_mm = *delta.letter_spacing_mm;
        refresh_inline_text_preview();
    } else {
        workflow().update_selected_text_properties(delta);
    }
}
} // namespace pikdraw::desktop
