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
#include "../tools/dimension_tool_controller.hpp"
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

void NativeWindow::open_settings_modal() noexcept {
#ifdef _WIN32
    if (pointer_active_ || is_panning_)
        cancel_canvas_interaction();
#endif
    settings_mcp_enabled_choice_ = mcp_listener_active();
    settings_mcp_edit_choice_ = mcp_edit_access_active();
    settings_ai_focus_index_ = 0;
    show_settings_modal_ = true;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã mở Cài đặt hệ thống (PikDraw Preferences)" : "Opened System Preferences (PikDraw Settings)";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::open_ai_connections_settings() noexcept {
    open_settings_modal();
    set_settings_tab(6);
}

void NativeWindow::close_settings_modal() noexcept {
    discard_ai_connections_choices();
    show_settings_modal_ = false;
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        "Đã đóng Cài đặt hệ thống" : "Closed System Preferences";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::toggle_settings_modal() noexcept {
    if (show_settings_modal_) close_settings_modal(); else open_settings_modal();
}

void NativeWindow::set_settings_tab(int tab) noexcept {
    if (tab < 0 || tab > 6) return;
    settings_active_tab_ = tab;
    const char* vi_tabs[] = { "Chung & Ngôn ngữ", "Giao diện (Theme)", "Đơn vị & Bắt điểm", "Quản lý màu & ICC", "Phím tắt & Thao tác", "Thi công & CNC", "Kết nối AI" };
    const char* en_tabs[] = { "General & Language", "Display & Theme", "Units & Snapping", "Color & ICC", "Shortcuts & Hotkeys", "Production & CAM", "AI Connections" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Tùy chọn: ") + vi_tabs[tab] : std::string("Options: ") + en_tabs[tab];
    if (tab == 6) settings_ai_focus_index_ = 0;
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::discard_ai_connections_choices() noexcept {
    settings_mcp_enabled_choice_ = mcp_listener_active();
    settings_mcp_edit_choice_ = mcp_edit_access_active();
}

void NativeWindow::toggle_ai_connections_read_choice() noexcept {
    settings_mcp_enabled_choice_ = !settings_mcp_enabled_choice_;
    if (!settings_mcp_enabled_choice_) settings_mcp_edit_choice_ = false;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Quyền MCP đang là bản nháp · chọn Áp dụng để xác nhận"
        : "MCP access is a draft · choose Apply to confirm";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_ai_connections_edit_choice() noexcept {
    if (!settings_mcp_enabled_choice_) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bật Cho phép MCP trước khi cấp quyền sửa"
            : "Enable MCP access before granting edit permission";
    } else {
        settings_mcp_edit_choice_ = !settings_mcp_edit_choice_;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Quyền MCP đang là bản nháp · chọn Áp dụng để xác nhận"
            : "MCP access is a draft · choose Apply to confirm";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::reset_ai_connections_choices() noexcept {
    settings_mcp_enabled_choice_ = false;
    settings_mcp_edit_choice_ = false;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "MCP đang tắt trong bản nháp · chọn Áp dụng để thu hồi"
        : "MCP is off in this draft · choose Apply to revoke";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::apply_ai_connections_choices() noexcept {
    if (!set_mcp_access(settings_mcp_enabled_choice_, settings_mcp_edit_choice_)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? std::string("Không thể khởi động MCP: ") + mcp_error_message()
            : std::string("Could not start MCP: ") + mcp_error_message();
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return false;
    }
    discard_ai_connections_choices();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? (mcp_listener_active() ? "Đã áp dụng quyền MCP" : "Đã thu hồi quyền MCP")
        : (mcp_listener_active() ? "MCP access applied" : "MCP access revoked");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

void NativeWindow::activate_ai_connections_focus() noexcept {
    const auto copy_value = [this](bool access_key) {
#ifdef _WIN32
        if (!mcp_listener_active() || !mcp_runtime_) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bật và áp dụng MCP trước khi chép thông tin kết nối"
                : "Enable and apply MCP before copying connection details";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto value = access_key ? mcp_runtime_->http.access_token()
                                      : mcp_runtime_->http.endpoint();
        const bool copied = !value.empty() && NativeClipboard::write(hwnd_, {}, {}, value);
        status_msg_ = copied
            ? (language_ == UILanguage::Vietnamese
                ? (access_key ? "Đã chép khóa MCP vào clipboard" : "Đã chép URL MCP vào clipboard")
                : (access_key ? "MCP access key copied to clipboard" : "MCP URL copied to clipboard"))
            : (language_ == UILanguage::Vietnamese
                ? "Không thể chép thông tin MCP vào clipboard"
                : "Could not copy MCP connection details to the clipboard");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#else
        (void)access_key;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "MCP clipboard chỉ dùng trên Windows" : "MCP clipboard is available on Windows";
#endif
    };

    switch (settings_ai_focus_index_) {
    case 0: toggle_ai_connections_read_choice(); return;
    case 1: toggle_ai_connections_edit_choice(); return;
    case 2: copy_value(false); return;
    case 3: copy_value(true); return;
    case 4: reset_ai_connections_choices(); return;
    case 5: close_settings_modal(); return;
    case 6: (void)apply_ai_connections_choices(); return;
    default: settings_ai_focus_index_ = 0; return;
    }
}

void NativeWindow::set_settings_theme_choice(int theme) noexcept {
    settings_theme_choice_ = theme;
    set_theme(theme == 1 ? ThemeMode::Light : ThemeMode::CyberDark);
}

void NativeWindow::set_settings_accent_choice(int accent) noexcept {
    settings_accent_choice_ = accent;
    const char* vi_accents[] = { "Cyber Mint (#00f2c3)", "Sky Blue (#0ea5e9)", "Purple Tech (#a855f7)", "Amber CAD (#f59e0b)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Màu điểm nhấn: ") + vi_accents[accent % 4] : std::string("Accent Color: ") + vi_accents[accent % 4];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_settings_unit_choice(int unit) noexcept {
    settings_unit_choice_ = unit;
    const char* units[] = { "mm (Milimét)", "cm (Xentimét)", "inch (Inches)", "pt (Điểm in)" };
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đơn vị mặc định: ") + units[unit % 4] : std::string("Default Units: ") + units[unit % 4];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::set_active_tool(int tool_index) noexcept {
    if (tool_index < 0 || (tool_index > 9 && tool_index != 10 && tool_index != 11 && tool_index != 12 && tool_index != 13))
        return;
    const bool path_tool = tool_index == 6 || tool_index == 7;
    const bool pen_tool = tool_index == 8;
    const bool dimension_tool = tool_index == 10;
    const bool node_tool = tool_index == 11;
    const bool contour_tool = tool_index == 12;
    const bool gradient_tool = tool_index == 13;
    if (tool_index == active_tool_ &&
        ((!path_tool || (drawing_tool_ && drawing_tool_->active())) &&
         (!pen_tool || (pen_tool_ && pen_tool_->active())) &&
         (!dimension_tool || (dimension_tool_ && dimension_tool_->active())) &&
         (!node_tool || (node_tool_ && node_tool_->active())) &&
         (!contour_tool || (contour_tool_ && contour_tool_->active())) &&
         (!gradient_tool || (gradient_tool_ && gradient_tool_->active()))))
        return;
    if (active_num_field_ != NumericFieldId::None &&
        active_num_field_ != NumericFieldId::PenExpression) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    if (active_tool_ == 8 && tool_index != 8 && pen_tool_ && pen_tool_->active()) {
        if (pen_tool_->anchor_count() >= 2) {
            const auto result = pen_tool_->finish_open();
            if (result.committed() && result.object_id) {
                const auto snapshot = root().document().current_snapshot();
                if (snapshot)
                    root().vector_controller().set_selection({*result.object_id}, *snapshot);
            }
        } else {
            pen_tool_->cancel();
        }
    }
    if (drawing_tool_) drawing_tool_->cancel();
    if (pen_tool_) pen_tool_->cancel();
    if (dimension_tool_ && tool_index != 10) dimension_tool_->deactivate();
    if (node_tool_) node_tool_->deactivate();
    if (contour_tool_) contour_tool_->deactivate();
    if (gradient_tool_ && tool_index != 13) {
        gradient_tool_->cancel();
        gradient_tool_->deactivate();
    }
    pen_numeric_buffer_.clear();
    if (active_num_field_ == NumericFieldId::PenExpression) {
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
    }
#ifdef _WIN32
    cancel_canvas_interaction();
    if (hwnd_) KillTimer(hwnd_, 9);
#endif
    active_tool_ = tool_index;
    if (tool_index == 1 || tool_index == 2 || tool_index == 9)
        quick_shapes_last_tool_ = tool_index;
    if (tool_index == 6 || tool_index == 7 || tool_index == 8 || tool_index == 11)
        path_drawing_last_tool_ = tool_index;
    if (dimension_tool) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
    }
    if (node_tool) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
    }
    if (contour_tool) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
    }
    if (gradient_tool) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
    }
    toolbox_open_group_ = 0;
    const auto mode = tool_index == 1 ? vector::ToolMode::Rectangle
                    : tool_index == 2 ? vector::ToolMode::Ellipse
                    : tool_index == 4 ? vector::ToolMode::Text
                                      : vector::ToolMode::Select;
    root().vector_controller().set_mode(mode);
    if (path_tool) {
        const auto kind = tool_index == 6 ? vector::tools::line::PathDrawingKind::Line
                                          : vector::tools::line::PathDrawingKind::Polyline;
        if (!drawing_tool_ || !drawing_tool_->activate(kind)) {
            active_tool_ = 0;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể bắt đầu công cụ vẽ đường" : "Could not start the path tool";
            return;
        }
#ifdef _WIN32
        if (hwnd_) SetTimer(hwnd_, 9, 33, nullptr);
#endif
    }
    if (pen_tool) {
        if (!pen_tool_) {
            try {
                pen_tool_ = std::make_unique<tools::PenToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Pen"
                    : "Not enough memory to start the Pen tool";
                return;
            }
        }
        if (drawing_tool_)
            (void)drawing_tool_->activate(vector::tools::line::PathDrawingKind::Line);
        const auto result = pen_tool_->activate();
        if (result.code != vector::tools::pen::PenDrawingResultCode::Applied) {
            active_tool_ = 0;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể bắt đầu công cụ Pen" : "Could not start the Pen tool";
            return;
        }
        auto preferences = tools::PenGesturePreferences{};
        preferences.profile = shortcut_profile_ == 0
            ? tools::PenShortcutProfile::Corel
            : shortcut_profile_ == 1 ? tools::PenShortcutProfile::Illustrator
                                     : tools::PenShortcutProfile::PikDraw;
        (void)pen_tool_->set_gesture_preferences(preferences);
    }
    if (node_tool) {
        if (!node_tool_) {
            try {
                node_tool_ = std::make_unique<tools::NodeToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Node"
                    : "Not enough memory to start the Node tool";
                return;
            }
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        const auto target = sel.empty() ? document::kInvalidObjectId : sel.front();
        (void)node_tool_->activate(target);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Công cụ Node (F10) · Nhấp chọn node · Shift/Ctrl+Click chọn nhiều · Kéo tay đòn Bézier / widget bo góc tròn"
            : "Node Tool (F10) · Click to select node · Shift/Ctrl+Click multi-select · Drag Bézier handles / Live Corner widgets";
    }
    if (contour_tool) {
        if (!contour_tool_) {
            try {
                contour_tool_ = std::make_unique<tools::ContourToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Contour"
                    : "Not enough memory to start the Contour tool";
                return;
            }
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        document::ObjectId target = sel.empty() ? document::kInvalidObjectId : sel.front();
        if (target != document::kInvalidObjectId) {
            const auto snap = root().document().current_snapshot();
            const auto obj = snap ? snap->objects().get(target) : std::nullopt;
            if (obj && std::holds_alternative<document::TextObject>(*obj)) {
                (void)workflow().convert_text_to_curves(target);
            }
        }
        (void)contour_tool_->activate(target);
    }
    if (gradient_tool) {
        if (!gradient_tool_) {
            try {
                gradient_tool_ = std::make_unique<tools::GradientToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Gradient"
                    : "Not enough memory to start the Gradient tool";
                return;
            }
        }
        const auto& sel = root().vector_controller().selection().selected_ids();
        (void)gradient_tool_->activate(sel);
    }
    if (dimension_tool) {
        if (!dimension_tool_) {
            try {
                dimension_tool_ = std::make_unique<tools::DimensionToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không đủ bộ nhớ để bắt đầu công cụ Dimension"
                    : "Not enough memory to start the Dimension tool";
                return;
            }
        }
        dimension_tool_->activate(document::DimensionKind::Aligned, false);
    }
    if (tool_index == 10) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kích thước (CAD) · Nhấp chọn 2 điểm cần đo rồi kéo đặt đường gióng · D"
            : "Dimension (CAD) · Click 2 points to measure, then place dimension line · D";
    } else if (tool_index == 11) {
        const auto snapshot = root().document().current_snapshot();
        const auto* target_value = snapshot && node_tool_
            ? snapshot->objects().lookup_ref(node_tool_->target_object()) : nullptr;
        const auto* target_path = target_value
            ? std::get_if<document::PathObject>(target_value) : nullptr;
        const bool can_edit_target = target_path && target_path->authoring_source &&
            !target_path->locked && target_path->visible;
        status_msg_ = can_edit_target
            ? (language_ == UILanguage::Vietnamese
                ? "Node · nhấp chọn, kéo node/tay nắm · nhập tọa độ X/Y trong Properties · F10"
                : "Node · click to select, drag nodes/handles · edit X/Y in Properties · F10")
            : (language_ == UILanguage::Vietnamese
                ? "Node · chọn một Path có thể chỉnh sửa rồi chọn node · F10"
                : "Node · select an editable Path, then choose a node · F10");
    } else if (tool_index == 12) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Contour · Sửa thông số trong Properties · Enter cập nhật xem trước · Áp dụng để lưu · Ctrl+K rã khối"
            : "Contour · Edit settings in Properties · Enter updates preview · Apply to save · Ctrl+K breaks apart";
    } else if (tool_index == 13) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Gradient · Properties sửa stop/hình học · Apply lưu · Cancel bỏ · kéo canvas khi bản nháp sạch"
            : "Gradient · edit stops/geometry in Properties · Apply to save · Cancel to discard · drag when draft is clean";
    } else if (tool_index == 4) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Text · chọn phông trên thanh thuộc tính · khung và giãn dòng trong Properties"
            : "Editing text · choose a font on the property bar · frame and spacing in Properties";
    } else {
        const char* vi_tools[] = { "Chọn và di chuyển · V", "Kéo vẽ chữ nhật · Shift vuông · Ctrl từ tâm",
            "Kéo vẽ elip · Shift tròn · Ctrl từ tâm", "Di chuyển khung nhìn · Space/chuột giữa",
            "Văn bản tiếng Việt · A/T", "Dao cắt · K", "Vẽ đoạn thẳng · L",
            "Vẽ đường gấp khúc · Shift+L",
            "Pen: nhấp/ kéo · @L<góc hoặc #X;Y · Enter kết thúc · Shift+Esc hủy",
            "Đa giác đều · chỉnh số cạnh trong thanh thuộc tính · Shift khóa góc 15° · Ctrl từ tâm" };
        const char* en_tools[] = { "Select and move · V", "Drag rectangle · Shift square · Ctrl from center",
            "Drag ellipse · Shift circle · Ctrl from center", "Pan view · Space/middle mouse", "Vietnamese text · A/T",
            "Knife tool · K", "Draw Line · L", "Draw Polyline · Shift+L",
            "Pen: click/drag · @L<angle or #X;Y · Enter finishes · Shift+Esc cancels",
            "Regular polygon · set sides in the property bar · Shift snaps 15° · Ctrl from center" };
        status_msg_ = language_ == UILanguage::Vietnamese ? vi_tools[tool_index] : en_tools[tool_index];
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::trigger_effects_contour() {
    set_active_tool(12);
}

void NativeWindow::trigger_break_contour_apart() {
    if (!contour_tool_) {
        try {
            contour_tool_ = std::make_unique<tools::ContourToolController>(root().document());
        } catch (...) {
            return;
        }
    }
    if (!contour_tool_->active()) {
        const auto& sel = root().vector_controller().selection().selected_ids();
        if (!sel.empty()) {
            contour_tool_->activate(sel.front());
        }
    }
    if (contour_tool_->break_apart()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã rã khối viền Contour thành các đối tượng độc lập (Ctrl+K)"
            : "Broke contour apart into independent objects (Ctrl+K)";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }
}
} // namespace pikdraw::desktop
