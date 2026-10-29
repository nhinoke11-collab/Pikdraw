#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/document/commands/tac_commands.hpp"
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
#include "../tools/artboard_tool_controller.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"
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
document::TACScope tac_scope_from_settings(int scope) noexcept {
    switch (scope) {
    case 0: return document::TACScope::Document;
    case 1: return document::TACScope::ActivePage;
    case 2: return document::TACScope::Selection;
    default: return document::TACScope::Document;
    }
}

std::string tac_condition_id(double limit_percent) {
    return "manual-limit-" + std::to_string(static_cast<int>(limit_percent));
}
} // namespace

void NativeWindow::set_settings_tac_scope_choice(int scope) noexcept {
    if (scope < 0 || scope > 2) return;
    if (scope == 2 && root().vector_controller().selection().selected_ids().empty()) {
        settings_tac_error_ = language_ == UILanguage::Vietnamese
            ? "Chọn ít nhất một đối tượng trước khi kiểm tra vùng chọn"
            : "Select at least one object before checking the selection";
        settings_tac_summary_.reset();
        status_msg_ = settings_tac_error_;
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    settings_tac_scope_choice_ = scope;
    settings_tac_summary_.reset();
    settings_tac_error_.clear();
    static constexpr const char* en_scope[] = { "Document scope selected", "Active page scope selected", "Selection scope selected" };
    static constexpr const char* vi_scope[] = { "Đã chọn phạm vi tài liệu", "Đã chọn phạm vi trang hiện tại", "Đã chọn phạm vi vùng chọn" };
    status_msg_ = language_ == UILanguage::Vietnamese ? vi_scope[scope] : en_scope[scope];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::move_settings_tac_focus(int direction) noexcept {
    if (direction == 0) return;
    const auto& selection = root().vector_controller().selection().selected_ids();
    const int step = direction < 0 ? -1 : 1;
    int next = settings_tac_focus_index_;
    for (int attempt = 0; attempt < 6; ++attempt) {
        next = (next + step + 6) % 6;
        if (next != 2 || !selection.empty()) break;
    }
    settings_tac_focus_index_ = next;
    static constexpr const char* en_help[] = {
        "TAC scope: every document page · Enter or Space selects",
        "TAC scope: active page · Enter or Space selects",
        "TAC scope: current selection · Enter or Space selects",
        "Manual TAC limit 100–400% · Enter edits · not bound to ICC",
        "Run a read-only TAC check · spot and raster are reported separately",
        "Fix current over-limit process colors · one Undo · appearance may change"
    };
    static constexpr const char* vi_help[] = {
        "Phạm vi TAC: mọi trang · Enter hoặc Space để chọn",
        "Phạm vi TAC: trang hiện tại · Enter hoặc Space để chọn",
        "Phạm vi TAC: vùng chọn hiện tại · Enter hoặc Space để chọn",
        "Ngưỡng TAC thủ công 100–400% · Enter để nhập · chưa gắn ICC",
        "Kiểm tra TAC chỉ đọc · spot và raster được báo riêng",
        "Sửa màu process hiện hành · một bước Undo · ngoại hình có thể đổi"
    };
    status_msg_ = language_ == UILanguage::Vietnamese ? vi_help[next] : en_help[next];
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::activate_settings_tac_focus() noexcept {
    switch (settings_tac_focus_index_) {
    case 0: set_settings_tac_scope_choice(0); return;
    case 1: set_settings_tac_scope_choice(1); return;
    case 2: set_settings_tac_scope_choice(2); return;
    case 3:
        start_numeric_input(NumericFieldId::TacLimitPercent);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập ngưỡng TAC 100–400% · Enter áp dụng · Esc hủy"
            : "Enter a TAC limit from 100–400% · Enter applies · Esc cancels";
        break;
    case 4: validate_settings_tac(); return;
    case 5: fix_settings_tac(); return;
    default: return;
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::validate_settings_tac() noexcept {
    settings_tac_summary_.reset();
    settings_tac_error_.clear();
    try {
        if (!std::isfinite(settings_tac_limit_percent_) ||
            settings_tac_limit_percent_ < 100.0 || settings_tac_limit_percent_ > 400.0 ||
            std::floor(settings_tac_limit_percent_) != settings_tac_limit_percent_) {
            settings_tac_error_ = language_ == UILanguage::Vietnamese
                ? "Ngưỡng TAC thủ công phải là số nguyên từ 100 đến 400%"
                : "Manual TAC limit must be a whole number from 100 to 400%";
            return;
        }
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) {
            settings_tac_error_ = language_ == UILanguage::Vietnamese
                ? "Không có tài liệu để kiểm tra" : "There is no document to check";
            return;
        }
        const auto selection = root().vector_controller().selection().selected_ids();
        const bool selection_scope = settings_tac_scope_choice_ == 2;
        const color::TACOutputCondition condition{
            tac_condition_id(settings_tac_limit_percent_), settings_tac_limit_percent_};
        auto checked = document::validate_document_tac(*snapshot, condition,
            tac_scope_from_settings(settings_tac_scope_choice_),
            selection_scope ? selection : std::vector<document::ObjectId>{});
        if (!checked.success) {
            settings_tac_error_ = checked.error_message;
            return;
        }
        settings_tac_summary_ = std::move(checked.summary);
        settings_tac_report_epoch_ = snapshot->epoch();
        settings_tac_report_revision_ = snapshot->revision();
        settings_tac_report_document_id_ = snapshot->document_id();
        settings_tac_report_active_page_ = snapshot->active_page_id();
        settings_tac_report_selection_ = selection_scope ? selection : std::vector<document::ObjectId>{};
        settings_tac_report_limit_percent_ = settings_tac_limit_percent_;
        settings_tac_report_scope_ = settings_tac_scope_choice_;

        std::ostringstream report;
        report << (language_ == UILanguage::Vietnamese
            ? "TAC thủ công: " : "Manual TAC check: ")
            << settings_tac_summary_->process_colors_over_limit
            << (language_ == UILanguage::Vietnamese ? " màu vượt ngưỡng; " : " colors over limit; ")
            << settings_tac_summary_->spot_colors_not_assessed
            << (language_ == UILanguage::Vietnamese ? " spot, " : " spot, ")
            << settings_tac_summary_->raster_objects_not_assessed
            << (language_ == UILanguage::Vietnamese ? " raster chưa đánh giá" : " raster not assessed");
        status_msg_ = report.str();
    } catch (...) {
        settings_tac_error_ = language_ == UILanguage::Vietnamese
            ? "Không thể hoàn tất kiểm tra TAC" : "TAC inspection could not be completed";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::fix_settings_tac() noexcept {
    try {
        const auto snapshot = root().document().current_snapshot();
        const auto selection = root().vector_controller().selection().selected_ids();
        const bool selection_matches = settings_tac_report_scope_ != 2 ||
            selection == settings_tac_report_selection_;
        const bool report_is_current = settings_tac_summary_ && snapshot &&
            snapshot->epoch() == settings_tac_report_epoch_ &&
            snapshot->revision() == settings_tac_report_revision_ &&
            snapshot->document_id() == settings_tac_report_document_id_ &&
            snapshot->active_page_id() == settings_tac_report_active_page_ &&
            selection_matches &&
            settings_tac_report_limit_percent_ == settings_tac_limit_percent_ &&
            settings_tac_report_scope_ == settings_tac_scope_choice_;
        if (!report_is_current) {
            settings_tac_summary_.reset();
            settings_tac_error_ = language_ == UILanguage::Vietnamese
                ? "Tài liệu hoặc vùng chọn đã đổi · hãy kiểm tra lại trước khi sửa"
                : "Document or selection changed · run the check again before fixing";
        } else if (settings_tac_summary_->process_colors_over_limit == 0) {
            settings_tac_error_ = language_ == UILanguage::Vietnamese
                ? "Không có màu process CMYK vượt ngưỡng để sửa"
                : "No over-limit process CMYK colors are available to fix";
        } else {
            const color::TACOutputCondition condition{
                tac_condition_id(settings_tac_limit_percent_), settings_tac_limit_percent_};
            auto result = root().document().execute_command(
                std::make_unique<document::FixDocumentTacCommand>(condition,
                    tac_scope_from_settings(settings_tac_scope_choice_),
                    settings_tac_scope_choice_ == 2 ? selection
                        : std::vector<document::ObjectId>{}));
            if (!result.success || !result.tac_summary) {
                settings_tac_error_ = result.error_message.empty()
                    ? (language_ == UILanguage::Vietnamese
                        ? "Không thể sửa TAC" : "TAC fix could not be applied")
                    : std::move(result.error_message);
            } else {
                settings_tac_summary_ = std::move(result.tac_summary);
                const auto updated = root().document().current_snapshot();
                settings_tac_report_epoch_ = updated->epoch();
                settings_tac_report_revision_ = updated->revision();
                settings_tac_report_document_id_ = updated->document_id();
                settings_tac_report_active_page_ = updated->active_page_id();
                settings_tac_report_selection_ = settings_tac_scope_choice_ == 2
                    ? selection : std::vector<document::ObjectId>{};
                settings_tac_report_limit_percent_ = settings_tac_limit_percent_;
                settings_tac_report_scope_ = settings_tac_scope_choice_;
                settings_tac_error_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã sửa TAC trong một bước Undo · ngoại hình có thể thay đổi · spot/raster chưa đánh giá"
                    : "TAC updated in one undo step · appearance may change · spot/raster not assessed";
            }
        }
    } catch (...) {
        settings_tac_error_ = language_ == UILanguage::Vietnamese
            ? "Không thể hoàn tất thao tác sửa TAC"
            : "The TAC fix could not be completed";
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

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
    if (tab == 3) settings_tac_focus_index_ = 0;
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

void NativeWindow::set_settings_unit_choice(int unit) {
    if (unit < 0 || unit > 7) return;
    if (settings_unit_choice_ != unit && active_num_field_ != NumericFieldId::None)
        cancel_numeric_field();
    using geometry::units::LengthUnit;
    constexpr LengthUnit choices[] = {LengthUnit::Millimeter, LengthUnit::Centimeter,
        LengthUnit::Inch, LengthUnit::Point, LengthUnit::Meter, LengthUnit::Foot,
        LengthUnit::Yard, LengthUnit::Pica};
    constexpr const char* units[] = { "mm (Milimét)", "cm (Xentimét)", "inch (Inches)",
        "pt (Điểm in)", "m (Mét)", "ft (Feet)", "yd (Yard)", "pc (Pica)" };
    if (!workflow().set_page_display_length_unit(root().document().active_page_id(), choices[unit])) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể đổi đơn vị của trang hiện tại" : "Could not change the current page unit";
        return;
    }
    display_length_unit_ = choices[unit];
    settings_unit_choice_ = unit;
    apply_preferred_dimension_unit();
    try {
        auto preferences = tools::load_drawing_preferences(tools::drawing_preferences_path()).value;
        preferences.default_length_unit = display_length_unit_;
        std::string ignored;
        (void)tools::save_drawing_preferences(tools::drawing_preferences_path(), preferences, &ignored);
    } catch (...) {
        // The selected unit remains active for this session if persistence is unavailable.
    }
    status_msg_ = (language_ == UILanguage::Vietnamese) ?
        std::string("Đơn vị trang: ") + units[unit] + " · hình học vẫn lưu bằng mm"
        : std::string("Page unit: ") + units[unit] + " · geometry remains stored in mm";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, NULL, FALSE);
#endif
}

void NativeWindow::sync_display_unit_from_active_page() noexcept {
    if (tabs_.empty() || !active_tab().root) return;
    const auto snapshot = active_tab().root->document().current_snapshot();
    if (!snapshot) return;
    display_length_unit_ = snapshot->page().display_length_unit;
    using geometry::units::LengthUnit;
    constexpr LengthUnit choices[] = {LengthUnit::Millimeter, LengthUnit::Centimeter,
        LengthUnit::Inch, LengthUnit::Point, LengthUnit::Meter, LengthUnit::Foot,
        LengthUnit::Yard, LengthUnit::Pica};
    for (int index = 0; index < 8; ++index) {
        if (choices[index] == display_length_unit_) {
            settings_unit_choice_ = index;
            break;
        }
    }
    apply_preferred_dimension_unit();
}

void NativeWindow::apply_preferred_dimension_unit() noexcept {
    if (!dimension_tool_) return;
    auto style = dimension_tool_->current_style();
    using document::UnitType;
    switch (display_length_unit_) {
    case geometry::units::LengthUnit::Millimeter: style.units = UnitType::Millimeter; break;
    case geometry::units::LengthUnit::Centimeter: style.units = UnitType::Centimeter; break;
    case geometry::units::LengthUnit::Meter: style.units = UnitType::Meter; break;
    case geometry::units::LengthUnit::Inch: style.units = UnitType::Inch; break;
    case geometry::units::LengthUnit::Foot: style.units = UnitType::Foot; break;
    case geometry::units::LengthUnit::Yard: style.units = UnitType::Yard; break;
    case geometry::units::LengthUnit::Point: style.units = UnitType::Point; break;
    case geometry::units::LengthUnit::Pica: style.units = UnitType::Pica; break;
    case geometry::units::LengthUnit::Pixel: return;
    }
    dimension_tool_->set_current_style(std::move(style));
}

void NativeWindow::set_active_tool(int tool_index) noexcept {
    if (tool_index < 0 || tool_index > 24)
        return;
    toolbox_keyboard_focus_ = false;
    toolbox_open_group_ = 0;
    const bool path_tool = tool_index == 6 || tool_index == 7;
    const bool pen_tool = tool_index == 8;
    const bool dimension_tool = tool_index == 10;
    const bool node_tool = tool_index == 11;
    const bool contour_tool = tool_index == 12;
    const bool gradient_tool = tool_index == 13;
    const bool artboard_tool = tool_index == 14;
    const bool eyedropper_tool = tool_index == 15;
    if (tool_index == active_tool_ &&
        ((!path_tool || (drawing_tool_ && drawing_tool_->active())) &&
         (!pen_tool || (pen_tool_ && pen_tool_->active())) &&
         (!dimension_tool || (dimension_tool_ && dimension_tool_->active())) &&
         (!node_tool || (node_tool_ && node_tool_->active())) &&
         (!contour_tool || (contour_tool_ && contour_tool_->active())) &&
         (!gradient_tool || (gradient_tool_ && gradient_tool_->active())) &&
         (!artboard_tool || !artboard_tool_ || !artboard_tool_->active()) &&
         !eyedropper_tool))
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
            if (!result.committed() || !result.object_id) {
                status_msg_ = result.message;
                return;
            }
            const auto snapshot = root().document().current_snapshot();
            if (snapshot)
                root().vector_controller().set_selection({*result.object_id}, *snapshot);
        } else if (pen_tool_->anchor_count() == 1) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Pen đang có một điểm · Enter hoàn tất hoặc Esc hủy trước khi đổi công cụ"
                : "Pen has one pending anchor · press Enter to finish or Esc to cancel before switching tools";
            return;
        } else {
            pen_tool_->cancel();
        }
    }
    if (workflow().has_effect_draft()) {
        workflow().cancel_effect_draft();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã hủy bản nháp hiệu ứng sau khi xác nhận chuyển công cụ"
            : "Effect draft cancelled after the tool change was accepted";
    }
    if (active_tool_ == 20 && tool_index != 20)
        reset_trim_objects_roles();
    if (active_tool_ == 22 && tool_index != 22)
        shape_builder_seed_points_.clear();
    if (tool_index != active_tool_)
        discard_pending_cut_geometry_proposal();
    if (drawing_tool_) drawing_tool_->cancel();
    if (pen_tool_) pen_tool_->cancel();
    if (dimension_tool_ && tool_index != 10) dimension_tool_->deactivate();
    if (node_tool_) node_tool_->deactivate();
    if (contour_tool_) contour_tool_->deactivate();
    if (gradient_tool_ && tool_index != 13) {
        gradient_tool_->cancel();
        gradient_tool_->deactivate();
    }
    if (artboard_tool_) artboard_tool_->cancel();
    if (annotation_tool_ && tool_index != 17) annotation_tool_->deactivate();
    pen_numeric_buffer_.clear();
    if (active_num_field_ == NumericFieldId::PenExpression) {
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
    }
#ifdef _WIN32
    if (tool_index != active_tool_)
        update_smart_trim_hover_preview({}, false);
    cancel_canvas_interaction();
    if (hwnd_) KillTimer(hwnd_, 9);
#endif
    if (is_inline_text_editing_ && tool_index != 4) {
        commit_inline_text_editing();
    }
    active_tool_ = tool_index;
    if (tool_index == 1 || tool_index == 2 || tool_index == 9 || tool_index == 18)
        quick_shapes_last_tool_ = tool_index;
    if (tool_index == 18)
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đường tròn tâm–bán kính · Nhấp/kéo từ tâm đến bán kính · Có thể bật Snap"
            : "Circle center–radius · Click/drag from center to radius · Snap is available";
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
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhấp chọn node hoặc kéo quét nhiều node · Double-click đoạn: thêm node · Del: xóa node · Esc: hủy"
            : "Click to select node · Double-click segment: add node · Del: delete node · Esc: cancel";
    }
    if (contour_tool) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kéo tay nắm trên canvas để chỉnh viền · Chọn thông số trên Properties · Enter: Áp dụng · Esc: Hủy"
            : "Drag handle on canvas to adjust contour · Set parameters in Properties · Enter: Apply · Esc: Cancel";
    }
    if (eyedropper_tool) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Công cụ Hút màu (Eyedropper · I) · Di chuột để soi màu · Click hút màu · Shift+Click áp viền · Alt+Click đổ màu"
            : "Color Eyedropper (I) · Hover to inspect · Click to sample · Shift+Click for stroke · Alt+Click to pour";
    }
    if (tool_index == 20) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Cắt bằng đối tượng · nhấp Target, rồi một hoặc nhiều Source · bật/tắt Giữ Source · Áp dụng để cắt"
            : "Trim Objects · click a Target, then one or more Sources · set Keep Source · Apply to trim";
    } else if (tool_index == 21) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Smart Trim · di chuột để xem đỏ đoạn giữa hai giao điểm, nhấp để cắt · path có fill được giữ nguyên"
            : "Smart Trim · hover to preview the interval between intersections, click to trim · filled paths are preserved";
    } else if (tool_index == 22) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Shape Builder · nhấp các mặt kín để chọn · Apply hợp nhất hoặc xóa mặt · Esc hủy"
            : "Shape Builder · click closed faces to select · Apply merges or erases faces · Esc cancels";
    } else if (tool_index == 23) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Smart Fill · nhấp vào một mặt kín để tạo path mới; nguồn được giữ nguyên"
            : "Smart Fill · click inside a closed face to create a new editable path; sources remain";
    } else if (tool_index == 24) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Raster Eraser · kéo trên bitmap đã chọn để không phá hủy alpha · Esc hủy"
            : "Raster Eraser · drag across the selected bitmap for non-destructive alpha erasing · Esc cancels";
    } else if (tool_index == 5 || tool_index == 16 || tool_index == 19) {
        status_msg_ = tool_index == 5
            ? (language_ == UILanguage::Vietnamese
                ? "Knife · Kéo qua path kín có fill · Esc hủy"
                : "Knife · Drag across a closed filled path · Esc cancels")
            : tool_index == 16
                ? (language_ == UILanguage::Vietnamese
                    ? "Tẩy vector · Kéo quét để xóa vùng trên path kín · Esc hủy"
                    : "Vector Eraser · Sweep to erase filled path regions · Esc cancels")
                : (language_ == UILanguage::Vietnamese
                    ? "Scissors · Nhấp node/đoạn path để tách · X · Esc hủy"
                    : "Scissors · Click a path node/segment to split · X · Esc cancels");
    }
    if (tool_index == 1) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kéo chuột để vẽ hình chữ nhật · Giữ Shift: hình vuông · Giữ Ctrl: vẽ từ tâm · Esc: hủy"
            : "Drag to draw rectangle · Shift: square · Ctrl: from center · Esc: cancel";
    }
    if (tool_index == 9) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kéo chuột để vẽ đa giác đều · Giữ Shift: bước 15° · Giữ Ctrl: từ tâm · Esc: hủy"
            : "Drag to draw regular polygon · Shift: 15° steps · Ctrl: from center · Esc: cancel";
    }
    if (gradient_tool) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
    }
    if (artboard_tool) {
        active_dock_tab_ = 0;
        show_style_inspector_ = false;
        show_align_panel_ = false;
        show_snap_properties_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Artboard Tool · nhấp vùng trống rồi kéo để tạo · kéo khung di chuyển · kéo tay nắm đổi cỡ · Shift+O · Esc hủy"
            : "Artboard Tool · drag empty canvas to create · drag a board to move · drag a handle to resize · Shift+O · Esc cancels";
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
                apply_preferred_dimension_unit();
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
            ? "Gradient · Kéo trên canvas đổi góc & bán kính · Giữ Shift khóa 15° · Thuộc tính tinh chỉnh stops/hình học"
            : "Gradient · drag on canvas for axis & radius · Shift snaps 15° · Properties customizes stops/geometry";
    } else if (tool_index == 4) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Text · chọn phông trên thanh thuộc tính · khung và giãn dòng trong Properties"
            : "Editing text · choose a font on the property bar · frame and spacing in Properties";
    } else if (tool_index == 14) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Artboard Tool · nhấp vùng trống rồi kéo để tạo · kéo khung di chuyển · kéo tay nắm đổi cỡ · Shift+O · Esc hủy"
            : "Artboard Tool · drag empty canvas to create · drag a board to move · drag a handle to resize · Shift+O · Esc cancels";
    } else if (tool_index == 17) {
        if (!annotation_tool_) {
            try {
                annotation_tool_ = std::make_unique<tools::AnnotationToolController>(root().document());
            } catch (...) {
                active_tool_ = 0;
                return;
            }
        }
        if (!annotation_tool_->active()) {
            annotation_tool_->activate(tools::AnnotationToolMode::DetailView);
        }
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chú thích · Kéo tạo Ô chi tiết hoặc nhấp đặt mũi tên chỉ dẫn / ghi chú"
            : "Annotation · Drag to place Detail View or click to add Leader Note / Plain Note";
    } else if (tool_index >= 0 && tool_index < 10) {
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

void NativeWindow::activate_annotation_tool(tools::AnnotationToolMode mode) {
    if (!annotation_tool_) {
        try {
            annotation_tool_ = std::make_unique<tools::AnnotationToolController>(root().document());
        } catch (...) {
            return;
        }
    }
    annotation_tool_->activate(mode);
    set_active_tool(17);
    status_msg_ = (mode == tools::AnnotationToolMode::DetailView)
        ? (language_ == UILanguage::Vietnamese
            ? "Ô chi tiết · Kéo chọn vùng nguồn trên bản vẽ, sau đó kéo khung chi tiết đích"
            : "Detail View · Drag to define source region, then drag or click to place detail frame")
        : (mode == tools::AnnotationToolMode::LeaderNote)
            ? (language_ == UILanguage::Vietnamese
                ? "Ghi chú mũi tên · Nhấp điểm neo đầu mũi tên, nhấp điểm uốn, sau đó nhấp đặt ghi chú"
                : "Leader Note · Click anchor point for arrowhead, click elbow bend, then click to place text")
            : (language_ == UILanguage::Vietnamese
                ? "Ghi chú nhanh · Nhấp trên bản vẽ để đặt khối chú thích"
                : "Plain Note · Click on canvas to place note block");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
} // namespace pikdraw::desktop
