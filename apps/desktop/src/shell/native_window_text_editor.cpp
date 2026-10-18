#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_text_properties.hpp"
#include "pikdraw/typography/shaping/font_resolver.hpp"
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
        editor.draft.font.postscript_name = editor.draft.font.family_name + "-Regular";
        editor.draft.underline = font_underline_;
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
void NativeWindow::apply_text_style_controls(bool change_family) {
    if (text_editor_ && is_inline_text_editing_) {
        auto& editor = *text_editor_;
        editor.remember();
        auto& text = editor.draft;
        if (change_family) {
            text.font.family_name = text_font_family_at(font_family_index_);
            text.font.postscript_name = text.font.family_name + "-Regular";
        }
        text.font.size_pt = font_size_pt_;
        text.font.weight = static_cast<uint16_t>(font_bold_ ? 700 : 400);
        text.font.italic = font_italic_;
        text.underline = font_underline_;
        text.alignment = font_alignment_;
        refresh_inline_text_preview();
    } else {
        if (change_family)
            workflow().set_selected_text_font(text_font_family_at(font_family_index_), font_size_pt_,
                                              font_bold_, font_italic_);
        else
            workflow().style_selected_text("", font_size_pt_, font_bold_, font_italic_,
                                           font_underline_, font_alignment_);
    }
}
} // namespace pikdraw::desktop
