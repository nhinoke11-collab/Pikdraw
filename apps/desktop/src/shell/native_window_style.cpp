#include "pikdraw/desktop/shell/native_window.hpp"
#include <cmath>

namespace pikdraw::desktop {
bool NativeWindow::apply_style_patch(const vector::StylePatch &patch) {
    if ((patch.fill && !patch.fill->is_valid()) || (patch.stroke && !patch.stroke->is_valid()) ||
        (patch.width_mm &&
         (!std::isfinite(*patch.width_mm) || *patch.width_mm < 0 || *patch.width_mm > 10000)))
        return false;
#ifdef _WIN32
    cancel_canvas_interaction();
#endif
    const auto &ids = root_.vector_controller().selection().selected_ids();
    bool success = true;
    if (ids.empty()) {
        if (patch.fill) {
            default_style_.fill = *patch.fill;
            default_style_.has_fill = true;
        }
        if (patch.stroke) {
            default_style_.stroke = *patch.stroke;
            default_style_.has_stroke = true;
        }
        if (patch.width_mm)
            default_style_.width_mm = *patch.width_mm;
        if (patch.has_fill)
            default_style_.has_fill = *patch.has_fill;
        if (patch.has_stroke)
            default_style_.has_stroke = *patch.has_stroke;
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đặt kiểu cho đối tượng mới"
                                                          : "New object style updated";
    } else {
        const auto result = vector::apply_object_styles(root_.document(), ids, patch);
        success = result.success;
        status_msg_ =
            success ? (language_ == UILanguage::Vietnamese ? "Đã đổi thuộc tính · Ctrl+Z để hoàn tác"
                                                           : "Style updated · Ctrl+Z to undo")
                    : (language_ == UILanguage::Vietnamese ? "Không thể áp dụng: " : "Could not apply: ") +
                          result.error_message;
    }
    workflow_.sync_status_bar();
#ifdef _WIN32
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return success;
}
bool NativeWindow::apply_selection_color(bool stroke, color::ColorValue value) {
    vector::StylePatch patch;
    if (stroke)
        patch.stroke = std::move(value);
    else
        patch.fill = std::move(value);
    return apply_style_patch(patch);
}
bool NativeWindow::set_selection_fill_enabled(bool enabled) {
    vector::StylePatch patch;
    patch.has_fill = enabled;
    return apply_style_patch(patch);
}
bool NativeWindow::set_selection_stroke_enabled(bool enabled) {
    vector::StylePatch patch;
    patch.has_stroke = enabled;
    return apply_style_patch(patch);
}
bool NativeWindow::set_selection_stroke_width(double width_mm) {
    vector::StylePatch patch;
    patch.width_mm = width_mm;
    patch.has_stroke = true;
    return apply_style_patch(patch);
}
} // namespace pikdraw::desktop
