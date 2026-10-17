#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

bool NativeWindow::is_menu_action_enabled(MenuActionId action) const {
    const auto& selected = root().vector_controller().selection().selected_ids();
    switch (action) {
    case MenuActionId::ObjectGroup:
        return workflow().can_group_selected();
    case MenuActionId::ObjectUngroup:
        return selected.size() == 1 && workflow().is_group(selected.front());
    case MenuActionId::ObjectAlignLeft:
        return workflow().can_align_selected(geometry::AlignMode::Left);
    case MenuActionId::ObjectAlignCenter:
        return workflow().can_align_selected(geometry::AlignMode::CenterHorizontal);
    case MenuActionId::ObjectAlignRight:
        return workflow().can_align_selected(geometry::AlignMode::Right);
    case MenuActionId::ObjectAlignTop:
        return workflow().can_align_selected(geometry::AlignMode::Top);
    case MenuActionId::ObjectAlignMiddle:
        return workflow().can_align_selected(geometry::AlignMode::CenterVertical);
    case MenuActionId::ObjectAlignBottom:
        return workflow().can_align_selected(geometry::AlignMode::Bottom);
    case MenuActionId::ObjectCenterPage:
        return workflow().can_align_selected(geometry::AlignMode::CenterPage);
    case MenuActionId::ObjectDistributeHorizontal:
        return workflow().can_distribute_selected(geometry::DistributeMode::SpacingHorizontal);
    case MenuActionId::ObjectDistributeVertical:
        return workflow().can_distribute_selected(geometry::DistributeMode::SpacingVertical);
    case MenuActionId::ObjectDistributeLeftEdges:
        return workflow().can_distribute_selected(geometry::DistributeMode::LeftEdges);
    case MenuActionId::ObjectDistributeCentersHorizontal:
        return workflow().can_distribute_selected(geometry::DistributeMode::CentersHorizontal);
    case MenuActionId::ObjectDistributeRightEdges:
        return workflow().can_distribute_selected(geometry::DistributeMode::RightEdges);
    case MenuActionId::ObjectDistributeTopEdges:
        return workflow().can_distribute_selected(geometry::DistributeMode::TopEdges);
    case MenuActionId::ObjectDistributeCentersVertical:
        return workflow().can_distribute_selected(geometry::DistributeMode::CentersVertical);
    case MenuActionId::ObjectDistributeBottomEdges:
        return workflow().can_distribute_selected(geometry::DistributeMode::BottomEdges);
    case MenuActionId::ObjectBooleanUnion:
        return workflow().can_boolean_operation_selected(geometry::BooleanOp::Union);
    case MenuActionId::ObjectBooleanDifference:
        return workflow().can_boolean_operation_selected(geometry::BooleanOp::Difference);
    case MenuActionId::ObjectBooleanIntersection:
        return workflow().can_boolean_operation_selected(geometry::BooleanOp::Intersection);
    case MenuActionId::ObjectBooleanExclude:
        return workflow().can_boolean_operation_selected(geometry::BooleanOp::Xor);
    case MenuActionId::ObjectBooleanReverseDifference:
        return workflow().can_boolean_operation_selected(geometry::BooleanOp::BackMinusFront);
    case MenuActionId::ObjectToggleKeepSource:
    case MenuActionId::ObjectToggleKeepTarget:
        return workflow().can_boolean_operation_selected(geometry::BooleanOp::Union) ||
            workflow().can_boolean_operation_selected(geometry::BooleanOp::Difference) ||
            workflow().can_boolean_operation_selected(geometry::BooleanOp::Intersection) ||
            workflow().can_boolean_operation_selected(geometry::BooleanOp::Xor) ||
            workflow().can_boolean_operation_selected(geometry::BooleanOp::BackMinusFront);
    case MenuActionId::ObjectFlipHorizontal:
    case MenuActionId::ObjectFlipVertical:
    case MenuActionId::ObjectRotate90CW:
    case MenuActionId::ObjectRotate90CCW:
    case MenuActionId::ObjectRotate180:
    case MenuActionId::ObjectFillGradient:
        return !root().vector_controller().selection().selected_ids().empty();
    case MenuActionId::EffectsGaussianBlur:
        return workflow().can_activate_selected_effect(document::GaussianBlurEffect{1.0, 1.0});
    case MenuActionId::EffectsDropShadow:
        return workflow().can_activate_selected_effect(document::DropShadowEffect{});
    case MenuActionId::EffectsClear:
        return workflow().can_clear_selected_effects();
    case MenuActionId::None:
    case MenuActionId::Separator:
        return false;
    default:
        return true;
    }
}

bool NativeWindow::is_menu_action_checked(MenuActionId action) const noexcept {
    switch (action) {
    case MenuActionId::ObjectToggleKeepSource: return keep_source_object_;
    case MenuActionId::ObjectToggleKeepTarget: return keep_target_object_;
    default: return false;
    }
}

void NativeWindow::execute_menu_action(int action_id) {
    const auto action = static_cast<MenuActionId>(action_id);
    if (!is_menu_action_enabled(action)) return;
    const auto activate_effect = [this](document::EffectParameters parameters,
                                        const char* name_en, const char* name_vi) {
        size_t effect_index = 0;
        const auto result = workflow().activate_selected_effect(std::move(parameters), effect_index);
        if (result == DocumentWorkflow::SelectedEffectActivation::Unavailable) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể kích hoạt hiệu ứng cho vùng chọn hiện tại"
                : "This effect cannot be activated for the current selection";
        } else {
            property_effect_index_ = effect_index;
            open_object_properties_panel();
            const int viewport_height = std::max(0, height_ - 26 - 162);
            style_inspector_scroll_ = std::max(0,
                style_inspector_content_height() - viewport_height);
            const bool added = result == DocumentWorkflow::SelectedEffectActivation::Added;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? (added ? std::string("Đã thêm ") + name_vi +
                               " · mở Properties để chỉnh · Ctrl+Z hoàn tác"
                         : std::string("Đang chỉnh ") + name_vi +
                               " hiện có · không tạo node mới")
                : (added ? std::string("Added ") + name_en +
                               " · edit in Properties · Ctrl+Z undoes"
                         : std::string("Editing existing ") + name_en +
                               " · no new effect node") ;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    switch (action) {
    case MenuActionId::FileNew: open_new_doc_modal(); return;
    case MenuActionId::FileOpen: open_document_dialog(); return;
    case MenuActionId::FileSave: save_document_dialog(); return;
    case MenuActionId::FileSaveAs: save_document_dialog(true); return;
    case MenuActionId::FileExportSvg: export_svg_dialog(); return;
    case MenuActionId::FileToggleGuidelines: toggle_guidelines(); return;
    case MenuActionId::FileClearGuidelines: clear_guidelines(); return;
    case MenuActionId::FileExit: DestroyWindow(hwnd_); return;
    case MenuActionId::EditUndo:
        workflow().undo();
        root().vector_controller().selection().clear();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectProperties:
        open_object_properties_panel();
        return;
    case MenuActionId::EditRedo:
        workflow().redo();
        root().vector_controller().selection().clear();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditCut:
        if (workflow().cut_selected(hwnd_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã cắt vào Clipboard" : "Cut to clipboard";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditCopy:
        if (workflow().copy_selected(hwnd_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã sao chép vào Clipboard" : "Copied to clipboard";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditPaste:
        if (workflow().paste(hwnd_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán từ Clipboard" : "Pasted from clipboard";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditPasteInPlace:
        if (workflow().paste_in_place(hwnd_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán tại chỗ (Paste in Place)" : "Pasted in place";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditPasteInFront:
        if (workflow().paste_in_front(hwnd_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán lên trước (Paste in Front)" : "Pasted in front";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditPasteInBack:
        if (workflow().paste_in_back(hwnd_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã dán ra sau (Paste in Back)" : "Pasted in back";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditDelete:
        if (workflow().delete_selected())
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xóa vùng chọn · Ctrl+Z để hoàn tác"
                                                               : "Deleted selection · Ctrl+Z to undo";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditDuplicate:
        workflow().duplicate_selected(15.0);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditDuplicateAtOrigin:
        if (workflow().duplicate_at_origin()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã nhân bản tại chỗ (+)" : "Duplicated at origin";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditStepAndRepeat:
        workflow().duplicate_selected(10.0, 10.0);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditSelectAll: {
        const auto snapshot = root().document().current_snapshot();
        std::vector<document::ObjectId> ids;
        for (const auto& layer : snapshot->page().layers)
            ids.insert(ids.end(), layer.object_ids.begin(), layer.object_ids.end());
        root().vector_controller().set_selection(ids, *snapshot);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::EditDeselect:
        root().vector_controller().selection().clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ViewZoomIn: zoom_in(); return;
    case MenuActionId::ViewZoomOut: zoom_out(); return;
    case MenuActionId::ViewZoom100: {
        auto& camera = root().renderer().camera();
        const auto area = canvas_rect(width_, height_);
        camera.set_viewport_size(area.right - area.left, area.bottom - area.top);
        camera.set_zoom(1.0);
        zoom_level_ = 100;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ViewFitPage: fit_page_to_canvas(); return;
    case MenuActionId::ViewToggleGuidelines: toggle_guidelines(); return;
    case MenuActionId::ViewClearGuidelines: clear_guidelines(); return;
    case MenuActionId::LayoutOrientationLandscape:
        if (active_tab().page_width_mm < active_tab().page_height_mm) toggle_page_orientation();
        return;
    case MenuActionId::LayoutOrientationPortrait:
        if (active_tab().page_width_mm > active_tab().page_height_mm) toggle_page_orientation();
        return;
    case MenuActionId::LayoutPageA4: select_page_preset(0); return;
    case MenuActionId::LayoutPageA3: select_page_preset(1); return;
    case MenuActionId::LayoutPageBanner: select_page_preset(6); return;
    case MenuActionId::ObjectGroup:
        if (workflow().group_selected() == document::kInvalidObjectId)
            status_msg_ = language_ == UILanguage::Vietnamese ? "Chọn ít nhất hai đối tượng để nhóm"
                                                               : "Select at least two objects to group";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectUngroup:
        if (!workflow().ungroup_selected())
            status_msg_ = language_ == UILanguage::Vietnamese ? "Chọn một nhóm để rã nhóm"
                                                               : "Select a group to ungroup";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectConvertToCurves: convert_selected_text_to_curves(); return;
    case MenuActionId::ObjectAlignLeft: align_selected_objects(geometry::AlignMode::Left); return;
    case MenuActionId::ObjectAlignCenter: align_selected_objects(geometry::AlignMode::CenterHorizontal); return;
    case MenuActionId::ObjectAlignRight: align_selected_objects(geometry::AlignMode::Right); return;
    case MenuActionId::ObjectAlignTop: align_selected_objects(geometry::AlignMode::Top); return;
    case MenuActionId::ObjectAlignMiddle: align_selected_objects(geometry::AlignMode::CenterVertical); return;
    case MenuActionId::ObjectAlignBottom: align_selected_objects(geometry::AlignMode::Bottom); return;
    case MenuActionId::ObjectCenterPage: align_selected_objects(geometry::AlignMode::CenterPage); return;
    case MenuActionId::ObjectDistributeHorizontal:
        distribute_selected_objects(geometry::DistributeMode::SpacingHorizontal); return;
    case MenuActionId::ObjectDistributeVertical:
        distribute_selected_objects(geometry::DistributeMode::SpacingVertical); return;
    case MenuActionId::ObjectDistributeLeftEdges:
        distribute_selected_objects(geometry::DistributeMode::LeftEdges); return;
    case MenuActionId::ObjectDistributeCentersHorizontal:
        distribute_selected_objects(geometry::DistributeMode::CentersHorizontal); return;
    case MenuActionId::ObjectDistributeRightEdges:
        distribute_selected_objects(geometry::DistributeMode::RightEdges); return;
    case MenuActionId::ObjectDistributeTopEdges:
        distribute_selected_objects(geometry::DistributeMode::TopEdges); return;
    case MenuActionId::ObjectDistributeCentersVertical:
        distribute_selected_objects(geometry::DistributeMode::CentersVertical); return;
    case MenuActionId::ObjectDistributeBottomEdges:
        distribute_selected_objects(geometry::DistributeMode::BottomEdges); return;
    case MenuActionId::ObjectBooleanUnion: (void)trigger_boolean_operation(geometry::BooleanOp::Union); return;
    case MenuActionId::ObjectBooleanDifference: (void)trigger_boolean_operation(geometry::BooleanOp::Difference); return;
    case MenuActionId::ObjectBooleanIntersection:
        (void)trigger_boolean_operation(geometry::BooleanOp::Intersection); return;
    case MenuActionId::ObjectBooleanExclude: (void)trigger_boolean_operation(geometry::BooleanOp::Xor); return;
    case MenuActionId::ObjectBooleanReverseDifference:
        (void)trigger_boolean_operation(geometry::BooleanOp::BackMinusFront); return;
    case MenuActionId::ObjectToggleKeepSource:
        toggle_keep_source(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    case MenuActionId::ObjectToggleKeepTarget:
        toggle_keep_target(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    case MenuActionId::ObjectFlipHorizontal:
        if (workflow().flip_selected_horizontal()) {
            if (custom_pivot_world_) {
                const auto bounds = root().vector_controller().selection().bounds(*root().document().current_snapshot());
                if (bounds) custom_pivot_world_->x = 2 * bounds->center().x - custom_pivot_world_->x;
            }
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã lật ngang đối tượng · Ctrl+Z để hoàn tác"
                                                              : "Flipped selection horizontally · Ctrl+Z to undo";
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    case MenuActionId::ObjectFlipVertical:
        if (workflow().flip_selected_vertical()) {
            if (custom_pivot_world_) {
                const auto bounds = root().vector_controller().selection().bounds(*root().document().current_snapshot());
                if (bounds) custom_pivot_world_->y = 2 * bounds->center().y - custom_pivot_world_->y;
            }
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã lật dọc đối tượng · Ctrl+Z để hoàn tác"
                                                              : "Flipped selection vertically · Ctrl+Z to undo";
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    case MenuActionId::ObjectRotate90CW:
        if (workflow().rotate_selected_90_cw(custom_pivot_world_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xoay 90° thuận chiều kim đồng hồ · Ctrl+Z để hoàn tác"
                                                              : "Rotated 90° CW · Ctrl+Z to undo";
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    case MenuActionId::ObjectRotate90CCW:
        if (workflow().rotate_selected_90_ccw(custom_pivot_world_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xoay 90° ngược chiều kim đồng hồ · Ctrl+Z để hoàn tác"
                                                              : "Rotated 90° CCW · Ctrl+Z to undo";
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    case MenuActionId::ObjectRotate180:
        if (workflow().rotate_selected_180(custom_pivot_world_)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xoay 180° · Ctrl+Z để hoàn tác"
                                                              : "Rotated 180° · Ctrl+Z to undo";
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    case MenuActionId::EffectsGaussianBlur: {
        activate_effect(document::GaussianBlurEffect{1.0, 1.0}, "Gaussian Blur", "Gaussian Blur");
        return;
    }
    case MenuActionId::EffectsDropShadow: {
        activate_effect(document::DropShadowEffect{}, "Drop Shadow", "Drop Shadow");
        return;
    }
    case MenuActionId::EffectsContour: {
        trigger_effects_contour();
        return;
    }
    case MenuActionId::EffectsClear: {
        const bool cleared = workflow().clear_selected_effects();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (cleared ? "Đã xóa toàn bộ node hiệu ứng · Opacity và Blend được giữ · Ctrl+Z hoàn tác"
                       : "Không thể xóa hiệu ứng cho vùng chọn hiện tại")
            : (cleared ? "Cleared all effect nodes · Opacity and Blend were preserved · Ctrl+Z undoes"
                       : "Could not clear effects for the current selection");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::TextEdit: workflow().cancel_effect_draft(); set_active_tool(4); return;
    case MenuActionId::TextBold: toggle_font_bold(); return;
    case MenuActionId::TextItalic: toggle_font_italic(); return;
    case MenuActionId::TextConvertToCurves: convert_selected_text_to_curves(); return;
    case MenuActionId::ToolPick: workflow().cancel_effect_draft(); set_active_tool(0); return;
    case MenuActionId::ToolRectangle: workflow().cancel_effect_draft(); set_active_tool(1); return;
    case MenuActionId::ToolEllipse: workflow().cancel_effect_draft(); set_active_tool(2); return;
    case MenuActionId::ToolPolygon: workflow().cancel_effect_draft(); set_active_tool(9); return;
    case MenuActionId::ToolText: workflow().cancel_effect_draft(); set_active_tool(4); return;
    case MenuActionId::ToolKnife: workflow().cancel_effect_draft(); set_active_tool(5); return;
    case MenuActionId::ToolLine: workflow().cancel_effect_draft(); set_active_tool(6); return;
    case MenuActionId::ToolPolyline: workflow().cancel_effect_draft(); set_active_tool(7); return;
    case MenuActionId::ToolPen: workflow().cancel_effect_draft(); set_active_tool(8); return;
    case MenuActionId::ToolGradient: workflow().cancel_effect_draft(); set_active_tool(13); return;
    case MenuActionId::ObjectFillGradient: workflow().cancel_effect_draft(); set_active_tool(13); open_inspector_properties_panel(); return;
    case MenuActionId::WindowProperties: open_inspector_properties_panel(); return;
    case MenuActionId::WindowAlignDistributePanel: open_align_distribute_panel(); return;
    case MenuActionId::SettingsPreferences: open_settings_modal(); return;
    case MenuActionId::SettingsAiConnections: open_ai_connections_settings(); return;
    case MenuActionId::HelpAiAutomation: open_ai_connections_settings(); return;
    case MenuActionId::SettingsThemeDark: set_theme(ThemeMode::CyberDark); return;
    case MenuActionId::SettingsThemeLight: set_theme(ThemeMode::Light); return;
    case MenuActionId::SettingsLangVi: set_language(UILanguage::Vietnamese); return;
    case MenuActionId::SettingsLangEn: set_language(UILanguage::English); return;
    case MenuActionId::None:
    case MenuActionId::FileProperties:
    case MenuActionId::ViewSnapGrid:
    case MenuActionId::ViewSnapObjects:
    case MenuActionId::LayoutPageSetup:
    case MenuActionId::EffectsExtrude:
    case MenuActionId::EffectsBevel:
    case MenuActionId::BitmapsConvert:
    case MenuActionId::BitmapsEdit:
    case MenuActionId::BitmapsTrace:
    case MenuActionId::BitmapsColorMask:
    case MenuActionId::ToolNode:
    case MenuActionId::WindowTabs:
    case MenuActionId::WindowFullscreen:
    case MenuActionId::WindowResetWorkspace:
    case MenuActionId::HelpTopics:
    case MenuActionId::HelpShortcuts:
    case MenuActionId::HelpAbout:
    case MenuActionId::Separator:
        return;
    }
}

void NativeWindow::activate_toolbox_effect_item(int item) {
    if (item == 0) {
        toolbox_open_group_ = 0;
        execute_menu_action(static_cast<int>(MenuActionId::ObjectProperties));
        return;
    }
    if (item == 3) {
        toolbox_open_group_ = 0;
        workflow().cancel_effect_draft();
        set_active_tool(13);
        return;
    }
    if (item < 1 || item > 2) return;

    const auto action = item == 1 ? MenuActionId::EffectsGaussianBlur
                                  : MenuActionId::EffectsDropShadow;
    if (!is_menu_action_enabled(action)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn Path/Text không khóa có stack hiệu ứng tương thích; mỗi đối tượng chỉ có một Drop Shadow"
            : "Select unlocked Path/Text with a compatible effect stack; each object supports one Drop Shadow";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    toolbox_open_group_ = 0;
    execute_menu_action(static_cast<int>(action));
}

bool NativeWindow::handle_toolbox_key(WPARAM key, bool alt_pressed, bool shift_pressed) {
    if (active_num_field_ != NumericFieldId::None || is_menu_open()) return false;
    if (alt_pressed && key != VK_DOWN) return false;

    const auto set_group_hint = [&]() {
        const char *group = toolbox_focus_group_ == 1 ? "Quick Shapes"
                          : toolbox_focus_group_ == 2 ? "Path Drawing" : "Appearance";
        const char *group_vi = toolbox_focus_group_ == 1 ? "Quick Shapes"
                             : toolbox_focus_group_ == 2 ? "Đường vẽ" : "Diện mạo";
        status_msg_ = language_ == UILanguage::Vietnamese
            ? std::string(group_vi) + " · Tab/Shift+Tab đổi nhóm · Alt+Down mở · Esc trả focus"
            : std::string(group) + " · Tab/Shift+Tab groups · Alt+Down opens · Esc returns";
    };
    const auto set_item_hint = [&]() {
        const char *labels_en[3][4] = {
            {"Rectangle", "Ellipse", "Polygon", ""},
            {"Line", "Polyline", "Pen", ""},
            {"Appearance Properties", "Gaussian Blur", "Drop Shadow", "Gradient"}};
        const char *labels_vi[3][4] = {
            {"Hình chữ nhật", "Hình elip", "Đa giác", ""},
            {"Đoạn thẳng", "Đường gấp khúc", "Pen", ""},
            {"Thuộc tính diện mạo", "Gaussian Blur", "Drop Shadow", "Chuyển sắc"}};
        const int group_index = std::clamp(toolbox_focus_group_ - 1, 0, 2);
        const int max_items = toolbox_focus_group_ == 3 ? 4 : 3;
        const int item_index = std::clamp(toolbox_flyout_index_, 0, max_items - 1);
        if (toolbox_focus_group_ == 3 && item_index == 3) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Sửa màu chuyển sắc bằng tay nắm trên đối tượng · Enter chọn · Esc đóng"
                : "Edit a path fill using gradient handles · Enter choose · Esc closes";
        } else if (toolbox_focus_group_ == 3 && item_index > 0) {
            const auto parameters = item_index == 1
                ? document::EffectParameters{document::GaussianBlurEffect{1.0, 1.0}}
                : document::EffectParameters{document::DropShadowEffect{}};
            const bool existing = workflow().selected_effect_index(parameters).has_value();
            const bool available = workflow().can_activate_selected_effect(parameters);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? (available
                    ? std::string(existing ? "Chọn hiệu ứng hiện có để chỉnh · "
                                           : "Thêm hiệu ứng mặc định một lần · ") +
                          labels_vi[group_index][item_index] + " · Enter chọn · Esc đóng"
                    : std::string(labels_vi[group_index][item_index]) +
                          " · Chọn Path/Text tương thích, không khóa · Esc đóng")
                : (available
                    ? std::string(existing ? "Edit existing effect · "
                                           : "Add the default effect once · ") +
                          labels_en[group_index][item_index] + " · Enter choose · Esc closes"
                    : std::string(labels_en[group_index][item_index]) +
                          " · Select compatible unlocked Path/Text · Esc closes");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? std::string(labels_vi[group_index][item_index]) + " · Enter chọn · Esc đóng"
                : std::string(labels_en[group_index][item_index]) + " · Enter choose · Esc closes";
        }
    };
    const auto open_focused_group = [&]() {
        toolbox_focus_group_ = std::clamp(toolbox_focus_group_, 1, 3);
        toolbox_keyboard_focus_ = true;
        toolbox_open_group_ = toolbox_focus_group_;
        if (toolbox_open_group_ == 3) {
            toolbox_flyout_index_ = 0;
        } else {
            const int last = toolbox_open_group_ == 1 ? quick_shapes_last_tool_ : path_drawing_last_tool_;
            toolbox_flyout_index_ = toolbox_open_group_ == 1
                ? (last == 1 ? 0 : last == 2 ? 1 : 2)
                : (last == 6 ? 0 : last == 7 ? 1 : 2);
        }
        set_item_hint();
    };

    if (key == VK_TAB) {
        if (!toolbox_keyboard_focus_) {
            toolbox_focus_group_ = shift_pressed ? 3 : 1;
            toolbox_keyboard_focus_ = true;
        } else {
            toolbox_focus_group_ = (toolbox_focus_group_ - 1 + (shift_pressed ? 2 : 1) + 3) % 3 + 1;
        }
        toolbox_open_group_ = 0;
        set_group_hint();
        return true;
    }

    if (toolbox_open_group_ > 0) {
        if (key == VK_ESCAPE) {
            toolbox_open_group_ = 0;
            set_group_hint();
            return true;
        }
        if (key == VK_UP || key == VK_DOWN) {
            const int max_items = toolbox_open_group_ == 3 ? 4 : 3;
            toolbox_flyout_index_ = (toolbox_flyout_index_ + (key == VK_UP ? max_items - 1 : 1)) % max_items;
            set_item_hint();
            return true;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            const int index = toolbox_flyout_index_;
            if (toolbox_open_group_ == 3) {
                activate_toolbox_effect_item(index);
                return true;
            }
            const int tool = toolbox_open_group_ == 1
                ? (index == 0 ? 1 : index == 1 ? 2 : 9)
                : (index == 0 ? 6 : index == 1 ? 7 : 8);
            toolbox_open_group_ = 0;
            workflow().cancel_effect_draft(); set_active_tool(tool);
            return true;
        }
        return false;
    }

    if (key == VK_ESCAPE && toolbox_keyboard_focus_) {
        toolbox_keyboard_focus_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã trả focus về vùng vẽ · Tab để chọn nhóm toolbox"
            : "Canvas focus · press Tab to focus a toolbox group";
        return true;
    }
    if ((alt_pressed && key == VK_DOWN) ||
        (!alt_pressed && toolbox_keyboard_focus_ && (key == VK_RETURN || key == VK_SPACE))) {
        open_focused_group();
        return true;
    }
    return false;
}

bool NativeWindow::handle_align_panel_click(int x, int y) {
    using Metrics = AlignPanelMetrics;
    const int left = width_ - Metrics::dock_width + Metrics::content_left;
    if (x < left || x >= left + Metrics::content_width) return false;
    const auto selected = root().vector_controller().selection().selected_ids();
    const auto select_reference = [&](geometry::AlignReference reference) {
        align_reference_ = reference;
        if (reference == geometry::AlignReference::KeyObject) {
            key_object_id_ = selected.empty()
                ? std::nullopt
                : std::optional<document::ObjectId>{selected.back()};
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Căn theo đối tượng chọn cuối cùng (Key Object)"
                : "Align relative to the last selected object (Key Object)";
        } else {
            key_object_id_.reset();
            status_msg_ = reference == geometry::AlignReference::Page
                ? (language_ == UILanguage::Vietnamese ? "Đã chọn căn theo trang" : "Align reference: Page")
                : (language_ == UILanguage::Vietnamese ? "Đã chọn căn theo vùng chọn" : "Align reference: Selection bounds");
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    };

    if (y >= Metrics::reference_top && y < Metrics::reference_top + Metrics::reference_height) {
        const int button_width = (Metrics::content_width - 12) / 3;
        const int offset = x - left;
        const int column = offset / (button_width + 6);
        if (offset % (button_width + 6) >= button_width) return true;
        if (column == 0) select_reference(geometry::AlignReference::Selection);
        else if (column == 1 && selected.size() >= 2) select_reference(geometry::AlignReference::KeyObject);
        else if (column == 2) select_reference(geometry::AlignReference::Page);
        else return true;
        return true;
    }

    // Center Page is an explicit one-click action independent of the selected reference.
    if (y >= 164 && y < 185 && x >= left + Metrics::content_width - 98) {
        align_selected_objects(geometry::AlignMode::CenterPage);
        return true;
    }

    const int align_second_row = Metrics::align_top + Metrics::align_button_height + Metrics::align_row_gap;
    const bool align_first_row_hit = y >= Metrics::align_top && y < Metrics::align_top + Metrics::align_button_height;
    const bool align_second_row_hit = y >= align_second_row && y < align_second_row + Metrics::align_button_height;
    if (align_first_row_hit || align_second_row_hit) {
        const int button_width = (Metrics::content_width - 12) / 3;
        const int offset = x - left;
        const int column = std::clamp(offset / (button_width + 6), 0, 2);
        if (offset % (button_width + 6) >= button_width) return true;
        const bool second_row = align_second_row_hit;
        constexpr geometry::AlignMode modes[] = {
            geometry::AlignMode::Left,
            geometry::AlignMode::CenterHorizontal,
            geometry::AlignMode::Right,
            geometry::AlignMode::Top,
            geometry::AlignMode::CenterVertical,
            geometry::AlignMode::Bottom
        };
        const size_t index = static_cast<size_t>(second_row ? 3 + column : column);
        if (workflow().can_align_selected(modes[index], align_reference_, key_object_id_))
            align_selected_objects(modes[index]);
        return true;
    }

    const int distribute_second_row_offset = Metrics::distribute_button_height + Metrics::distribute_row_gap;
    const int horizontal_second_row = Metrics::horizontal_top + distribute_second_row_offset;
    const int vertical_second_row = Metrics::vertical_top + distribute_second_row_offset;
    const auto run_distribute = [&](const geometry::DistributeMode* modes, int top) {
        const bool second_row = y >= top + distribute_second_row_offset;
        const int column_width = (Metrics::content_width - 6) / 2;
        const int offset = x - left;
        const int column = std::clamp(offset / (column_width + 6), 0, 1);
        if (offset % (column_width + 6) >= column_width) return;
        const size_t index = static_cast<size_t>((second_row ? 2 : 0) + column);
        if (workflow().can_distribute_selected(modes[index]))
            distribute_selected_objects(modes[index]);
    };
    const bool horizontal_first_row_hit = y >= Metrics::horizontal_top && y < Metrics::horizontal_top + Metrics::distribute_button_height;
    const bool horizontal_second_row_hit = y >= horizontal_second_row && y < horizontal_second_row + Metrics::distribute_button_height;
    if (horizontal_first_row_hit || horizontal_second_row_hit) {
        constexpr geometry::DistributeMode modes[] = {
            geometry::DistributeMode::SpacingHorizontal,
            geometry::DistributeMode::CentersHorizontal,
            geometry::DistributeMode::LeftEdges,
            geometry::DistributeMode::RightEdges
        };
        run_distribute(modes, Metrics::horizontal_top);
        return true;
    }
    const bool vertical_first_row_hit = y >= Metrics::vertical_top && y < Metrics::vertical_top + Metrics::distribute_button_height;
    const bool vertical_second_row_hit = y >= vertical_second_row && y < vertical_second_row + Metrics::distribute_button_height;
    if (vertical_first_row_hit || vertical_second_row_hit) {
        constexpr geometry::DistributeMode modes[] = {
            geometry::DistributeMode::SpacingVertical,
            geometry::DistributeMode::CentersVertical,
            geometry::DistributeMode::TopEdges,
            geometry::DistributeMode::BottomEdges
        };
        run_distribute(modes, Metrics::vertical_top);
        return true;
    }

    if (y >= Metrics::gap_field_top && y < Metrics::gap_field_top + Metrics::gap_field_height && x < left + 166) {
        start_numeric_input(NumericFieldId::DistributeSpacing);
        return true;
    }
    if (y >= Metrics::apply_top && y < Metrics::apply_top + Metrics::apply_height) {
        if (active_num_field_ == NumericFieldId::DistributeSpacing) {
            commit_numeric_field();
            if (active_num_field_ == NumericFieldId::DistributeSpacing) return true;
        }
        const int column_width = (Metrics::content_width - 6) / 2;
        const int offset = x - left;
        if (offset < column_width) apply_specified_spacing(true);
        else if (offset >= column_width + 6) apply_specified_spacing(false);
        return true;
    }
    return false;
}

void NativeWindow::handle_mouse_down(int x, int y) {
    handle_mouse_down(x, y, 0);
}

void NativeWindow::handle_mouse_down(int x, int y, unsigned modifiers) {
    int w = width_;
    int h = height_;
    if (hwnd_) {
        RECT client_rc;
        if (GetClientRect(hwnd_, &client_rc)) {
            w = client_rc.right - client_rc.left;
            h = client_rc.bottom - client_rc.top;
            width_ = w;
            height_ = h;
        }
    }

    // Commit an opacity draft on blur, but keep the pointer action on the field while
    // editing. A rejected draft must not silently carry over to a different selection.
    if (active_num_field_ == NumericFieldId::ObjectOpacity) {
        const auto& selected = root().vector_controller().selection().selected_ids();
        const bool single_editable = selected.size() == 1 && workflow().can_set_selected_object_opacity();
        const bool text_mode = active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected();
        const bool on_property_bar = single_editable && y >= 66 && y <= 102 &&
            (text_mode ? (x >= 920 && x <= 1044 && x + 124 <= w - 280)
                       : (x >= 820 && x <= 944 && x + 124 <= w - 280));
        const bool on_properties_field = active_dock_tab_ == 0 && show_style_inspector_ &&
            x >= w - 280 + 12 && x <= w - 280 + 268 &&
            y >= 162 + 298 - style_inspector_scroll_ &&
            y <= 162 + 328 - style_inspector_scroll_;
        if (!on_property_bar && !on_properties_field) {
            commit_numeric_field();
            if (active_num_field_ == NumericFieldId::ObjectOpacity)
                return;
        }
    }
    if (active_num_field_ == NumericFieldId::FillOpacity ||
        active_num_field_ == NumericFieldId::StrokeOpacity) {
        if (!style_numeric_field_at(x, y)) {
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None)
                return;
        }
    }
    if (active_num_field_ == NumericFieldId::EffectSigmaX ||
        active_num_field_ == NumericFieldId::EffectSigmaY ||
        active_num_field_ == NumericFieldId::EffectOffsetX ||
        active_num_field_ == NumericFieldId::EffectOffsetY ||
        active_num_field_ == NumericFieldId::EffectOpacity) {
        if (!style_numeric_field_at(x, y)) {
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None)
                return;
        }
    }

    if (handle_modal_and_home_mouse_down(x, y))
        return;

    if (handle_workspace_chrome_mouse_down(x, y, modifiers))
        return;


    // Direct-manipulation handle for the currently focused Drop Shadow. The workflow
    // keeps the whole drag in an edit draft and commits a single appearance command.
    const auto& effect_ids = root().vector_controller().selection().selected_ids();
    const auto effect_snapshot = root().document().current_snapshot();
    if ((modifiers & MK_LBUTTON) != 0 && active_tool_ == 0 && !workflow().has_effect_draft() &&
        effect_ids.size() == 1 && effect_snapshot &&
        workflow().can_edit_selected_effect(property_effect_index_)) {
        const auto summary = selection_appearance(*effect_snapshot, effect_ids);
        if (summary.can_edit_effect(property_effect_index_)) {
            const auto* shadow = std::get_if<document::DropShadowEffect>(
                &summary.effects[property_effect_index_].parameters);
            const auto bounds = root().vector_controller().selection().bounds(*effect_snapshot);
            if (shadow && bounds) {
                const auto handle_world = geometry::Point2D{
                    bounds->center().x + shadow->dx_mm,
                    bounds->center().y + shadow->dy_mm};
                const auto area = canvas_rect(w, h);
                const auto screen = root().renderer().camera().world_to_screen(handle_world);
                const int handle_x = area.left + static_cast<int>(std::lround(screen.x));
                const int handle_y = area.top + static_cast<int>(std::lround(screen.y));
                if (is_canvas_point(x, y) && std::hypot(x - handle_x, y - handle_y) <= 10.0 &&
                    workflow().begin_selected_effect_edit_draft(property_effect_index_) &&
                    workflow().set_effect_draft_preview(true)) {
                    pointer_down_world_ = canvas_to_world(x, y);
                    drag_init_shape_x_ = shadow->dx_mm;
                    drag_init_shape_y_ = shadow->dy_mm;
                    drag_preview_delta_ = {shadow->dx_mm, shadow->dy_mm};
                    drag_start_x_ = x;
                    drag_start_y_ = y;
                    resize_handle_ = -2;
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Kéo tay nắm để đổi lệch X/Y · thả chuột lưu một bước · Esc hủy"
                        : "Drag the handle to change X/Y offset · release saves once · Esc cancels";
                    if (hwnd_) SetCapture(hwnd_);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                if (workflow().has_effect_draft()) workflow().cancel_effect_draft();
            }
        }
    }
    handle_canvas_mouse_down(x, y, modifiers);
}

void NativeWindow::handle_mouse_move(int x, int y) {
    handle_mouse_move(x, y, 0);
}

void NativeWindow::handle_mouse_move(int x, int y, unsigned modifiers) {
    last_canvas_pointer_x_ = x;
    last_canvas_pointer_y_ = y;
    bool prev_visible = tooltip_.visible;
    std::wstring prev_text = tooltip_.text;

    if (toolbox_open_group_ > 0) {
        const int group_row = toolbox_open_group_ == 1 ? 1
                            : toolbox_open_group_ == 2 ? 2 : 6;
        const int flyout_top = 112 + group_row * 42;
        const int flyout_height = toolbox_open_group_ == 3 ? 145 : 116;
        const int max_items = toolbox_open_group_ == 3 ? 4 : 3;
        if (x >= 54 && x <= 242 && y >= flyout_top + 26 && y < flyout_top + flyout_height) {
            const int item = (y - flyout_top - 26) / 29;
            if (item >= 0 && item < max_items && toolbox_flyout_index_ != item) {
                toolbox_flyout_index_ = item;
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }

    update_hover_tooltip(x, y);

    if (toolbox_open_group_ == 3) {
        const int flyout_top = 112 + 6 * 42;
        if (x >= 54 && x <= 242 && y >= flyout_top + 26 && y < flyout_top + 145) {
            const int item = (y - flyout_top - 26) / 29;
            if (item == 1 || item == 2) {
                const auto parameters = item == 1
                    ? document::EffectParameters{document::GaussianBlurEffect{1.0, 1.0}}
                    : document::EffectParameters{document::DropShadowEffect{}};
                const bool existing = workflow().selected_effect_index(parameters).has_value();
                const bool available = workflow().can_activate_selected_effect(parameters);
                tooltip_.visible = true;
                tooltip_.x = x;
                tooltip_.y = y;
                tooltip_.text = available
                    ? (language_ == UILanguage::Vietnamese
                        ? std::wstring(existing ? L"Chọn hiệu ứng hiện có để chỉnh; không tạo node mới. Properties có đầy đủ thông số."
                                                : L"Thêm mặc định một lần; sau đó chọn lại để chỉnh trong Properties.")
                        : std::wstring(L""))
                    : (language_ == UILanguage::Vietnamese
                        ? L"Chọn Path/Text không khóa có stack tương thích; bóng chỉ có một node mới mỗi đối tượng."
                        : L"Select unlocked Path/Text with a compatible stack; new shadows are unique per object.");
                if (available && language_ == UILanguage::English)
                    tooltip_.text = existing
                        ? L"Select the existing effect to edit; no new node is created. Full settings are in Properties."
                        : L"Add the default once; select it again to edit in Properties.";
            } else if (item == 3) {
                tooltip_.visible = true;
                tooltip_.x = x;
                tooltip_.y = y;
                tooltip_.text = language_ == UILanguage::Vietnamese
                    ? L"Sửa màu chuyển sắc bằng tay nắm trên đối tượng."
                    : L"Edit a path fill using gradient handles.";
            }
        }
    }

    if (open_menu_id_ >= 0) {
        const int top_item = get_menu_bar_item_at(x, y, 38);
        if (top_item >= 0 && top_item != open_menu_id_) {
            open_menu(top_item);
        } else if (top_item >= 0) {
            hovered_menu_bar_item_ = top_item;
        } else {
            const auto menu = static_cast<MenuId>(open_menu_id_);
            const int menu_height = get_menu_height(menu);
            const RECT anchor = get_menu_bar_item_rect(open_menu_id_, 38);
            const int menu_x = std::clamp(static_cast<int>(anchor.left), 4, std::max(4, width_ - 304));
            const int menu_y = std::clamp(38, 4, std::max(4, height_ - menu_height - 4));
            const bool in_menu = x >= menu_x && x < menu_x + 300 &&
                                 y >= menu_y && y < menu_y + menu_height;
            const auto* item = in_menu ? get_menu_item_at(menu, y - menu_y) : nullptr;
            const int hovered = item ? static_cast<int>(item->action) : -1;
            if (in_menu && item && item->is_heading) {
                tooltip_.visible = false;
                tooltip_.text.clear();
            } else if (in_menu && item &&
                       (item->is_disabled || !is_menu_action_enabled(item->action))) {
                tooltip_.visible = true;
                tooltip_.x = x;
                tooltip_.y = y;
                if (item->is_disabled) {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chưa có trong phiên bản này"
                        : L"Not available in this version";
                } else {
                    tooltip_.text = language_ == UILanguage::Vietnamese
                        ? L"Chọn đủ đối tượng phù hợp để bật lệnh này"
                        : L"Select enough compatible objects to enable this command";
                }
            } else if (in_menu) {
                tooltip_.visible = false;
                tooltip_.text.clear();
            }
            if (hovered != hovered_menu_item_) {
                hovered_menu_item_ = hovered;
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    } else {
        const int hovered = get_menu_bar_item_at(x, y, 38);
        if (hovered != hovered_menu_bar_item_) {
            hovered_menu_bar_item_ = hovered;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    if (prev_visible != tooltip_.visible || prev_text != tooltip_.text) {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (resize_handle_ == -2) {
        const auto draft = workflow().effect_draft();
        const auto* source_shadow = draft ? std::get_if<document::DropShadowEffect>(
            &draft->parameters) : nullptr;
        const auto current_world = canvas_to_world(x, y);
        if (!source_shadow || !draft->editing_existing) {
            workflow().cancel_effect_draft();
            resize_handle_ = -1;
            if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
            return;
        }
        auto shadow = *source_shadow;
        shadow.dx_mm = drag_init_shape_x_ + current_world.x - pointer_down_world_.x;
        shadow.dy_mm = drag_init_shape_y_ + current_world.y - pointer_down_world_.y;
        if (!workflow().set_effect_draft_parameters(shadow)) {
            workflow().cancel_effect_draft();
            resize_handle_ = -1;
            if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bản xem trước bóng không hợp lệ · tài liệu chưa đổi"
                : "Shadow preview is invalid · document unchanged";
        } else {
            drag_preview_delta_ = {shadow.dx_mm, shadow.dy_mm};
            std::ostringstream values;
            values << std::fixed << std::setprecision(1)
                   << shadow.dx_mm << ", " << shadow.dy_mm << " mm";
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Lệch bóng X, Y: " + values.str() + " · Enter lưu · Esc hủy"
                : "Shadow offset X, Y: " + values.str() + " · Enter applies · Esc cancels";
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (is_dragging_guideline_) {
        const auto world_pt = canvas_to_world(x, y);
        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            drag_guideline_pos_mm_ = world_pt.y;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? (selected_guideline_index_ >= 0 ? "Dời đường gióng ngang Y: " + ss.str() + " mm" : "Đường gióng ngang Y: " + ss.str() + " mm")
                : (selected_guideline_index_ >= 0 ? "Move horizontal guideline Y: " + ss.str() + " mm" : "Horizontal guideline Y: " + ss.str() + " mm");
        } else {
            drag_guideline_pos_mm_ = world_pt.x;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? (selected_guideline_index_ >= 0 ? "Dời đường gióng dọc X: " + ss.str() + " mm" : "Đường gióng dọc X: " + ss.str() + " mm")
                : (selected_guideline_index_ >= 0 ? "Move vertical guideline X: " + ss.str() + " mm" : "Vertical guideline X: " + ss.str() + " mm");
        }
        if (selected_guideline_index_ >= 0 && selected_guideline_index_ < static_cast<int>(guidelines_.size())) {
            guidelines_[selected_guideline_index_].position_mm = drag_guideline_pos_mm_;
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active() && !is_panning_ &&
        handle_pen_pointer_move(x, y, modifiers))
        return;

    if (active_tool_ == 11 && node_tool_ && node_tool_->active() && !is_panning_ &&
        handle_node_pointer_move(x, y, modifiers))
        return;

    if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active() && !is_panning_ &&
        handle_gradient_pointer_move(x, y, modifiers))
        return;

    if (drawing_tool_ && drawing_tool_->active() && !is_panning_) {
        if (is_canvas_point(x, y)) {
            drawing_tool_->pointer_move(canvas_to_world(x, y), root().renderer().camera(),
                window_dpi_scale(), (modifiers & MK_SHIFT) != 0,
                (modifiers & MK_CONTROL) != 0);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    handle_canvas_mouse_move(x, y, modifiers);
}

void NativeWindow::handle_mouse_up(int x, int y) {
    handle_mouse_up(x, y, 0);
}

void NativeWindow::handle_mouse_up(int x, int y, unsigned modifiers) {
    if (resize_handle_ == -2) {
        const bool moved = std::hypot(x - drag_start_x_, y - drag_start_y_) >= 3.0;
        bool applied = false;
        if (moved) {
            const auto draft = workflow().effect_draft();
            if (draft && draft->editing_existing) {
                if (const auto* current_shadow = std::get_if<document::DropShadowEffect>(
                        &draft->parameters)) {
                    auto shadow = *current_shadow;
                    const auto current_world = canvas_to_world(x, y);
                    shadow.dx_mm = drag_init_shape_x_ + current_world.x - pointer_down_world_.x;
                    shadow.dy_mm = drag_init_shape_y_ + current_world.y - pointer_down_world_.y;
                    applied = workflow().set_effect_draft_parameters(shadow) &&
                              workflow().apply_effect_draft();
                }
            }
        } else {
            workflow().cancel_effect_draft();
        }
        if (!applied && workflow().has_effect_draft()) workflow().cancel_effect_draft();
        resize_handle_ = -1;
        if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
        status_msg_ = !moved
            ? (language_ == UILanguage::Vietnamese ? "Không đổi lệch bóng" : "Shadow offset unchanged")
            : (applied
                ? (language_ == UILanguage::Vietnamese
                    ? "Đã cập nhật lệch bóng một bước · Ctrl+Z hoàn tác"
                    : "Updated shadow offset in one step · Ctrl+Z undoes")
                : (language_ == UILanguage::Vietnamese
                    ? "Không thể cập nhật bóng · tài liệu chưa đổi"
                    : "Could not update the shadow · document unchanged"));
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_dragging_guideline_) {
        is_dragging_guideline_ = false;
        if (GetCapture() == hwnd_)
            ReleaseCapture();

        const RECT area = canvas_rect(width_, height_);
        const auto world_pt = canvas_to_world(x, y);

        if (selected_guideline_index_ >= 0 && selected_guideline_index_ < static_cast<int>(guidelines_.size())) {
            const int moving_index = selected_guideline_index_;
            selected_guideline_index_ = -1;
            const bool inside = (drag_guideline_orientation_ == GuidelineOrientation::Horizontal)
                ? (y >= area.top && y <= area.bottom)
                : (x >= area.left && x <= area.right);
            if (inside) {
                guidelines_[moving_index].position_mm = drag_guideline_pos_mm_;
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã dời vị trí thước gióng" : "Moved guideline position";
            } else {
                guidelines_.erase(guidelines_.begin() + moving_index);
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã xóa thước gióng kéo ra ngoài bản vẽ" : "Deleted guideline dragged off-canvas";
            }
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            if (y >= area.top && y <= area.bottom) {
                add_guideline(GuidelineOrientation::Horizontal, world_pt.y);
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        } else {
            if (x >= area.left && x <= area.right) {
                add_guideline(GuidelineOrientation::Vertical, world_pt.x);
            } else {
                status_msg_ = (language_ == UILanguage::Vietnamese)
                    ? "Đã hủy kéo đường gióng"
                    : "Cancelled guideline";
            }
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_panning_) {
        is_panning_ = false;
        if (drawing_tool_ && drawing_tool_->active()) drawing_tool_->resume_after_pan();
        if (pen_tool_ && pen_tool_->active()) (void)pen_tool_->resume_after_pan();
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        return;
    }
    if (handle_pen_pointer_up(x, y, modifiers))
        return;
    if (handle_node_pointer_up(x, y, modifiers))
        return;
    if (handle_gradient_pointer_up(x, y, modifiers))
        return;
    if (drawing_tool_ && drawing_tool_->active())
        return;
    handle_canvas_mouse_up(x, y, modifiers);
}

double NativeWindow::window_dpi_scale() const noexcept {
    if (!hwnd_) return 1.0;
    using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
    const auto user32 = GetModuleHandleW(L"user32.dll");
    const auto get_dpi = reinterpret_cast<GetDpiForWindowFunction>(
        GetProcAddress(user32, "GetDpiForWindow"));
    return get_dpi ? static_cast<double>(get_dpi(hwnd_)) / 96.0 : 1.0;
}

LRESULT CALLBACK NativeWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_CHAR:
        if (self && self->show_settings_modal_ && self->settings_active_tab_ == 6)
            return 0;
        if (self && self->active_numeric_field() != NumericFieldId::None) {
            self->handle_numeric_char_input(static_cast<wchar_t>(wParam));
            return 0;
        }
        if (self && self->is_quick_dimension_active() && !self->is_inline_text_editing_) {
            if (wParam == VK_BACK) {
                if (!self->quick_dimension_buffer_.empty()) {
                    self->quick_dimension_buffer_.pop_back();
                    self->status_msg_ = (self->language_ == UILanguage::Vietnamese ? "Kích thước: " : "Dimensions: ") +
                                        self->quick_dimension_buffer_ +
                                        (self->language_ == UILanguage::Vietnamese ? " mm (Enter để nhận, Esc hủy)" : " mm (Enter to apply, Esc to cancel)");
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (wParam == VK_RETURN) {
                self->apply_quick_dimension();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->end_quick_dimension();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese ? "Đã hủy nhập kích thước" : "Cancelled dimension input";
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if ((wParam >= '0' && wParam <= '9') || wParam == '.' || wParam == ',' || wParam == ';' ||
                wParam == 'x' || wParam == 'X' || wParam == ' ' || wParam == '-') {
                self->quick_dimension_buffer_ += static_cast<char>(wParam);
                self->status_msg_ = (self->language_ == UILanguage::Vietnamese ? "Kích thước: " : "Dimensions: ") +
                                    self->quick_dimension_buffer_ +
                                    (self->language_ == UILanguage::Vietnamese ? " mm (Enter để nhận, Esc hủy)" : " mm (Enter to apply, Esc to cancel)");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Ký tự khác khi đang ở chế độ gõ nhanh thì kết thúc gõ nhanh và để lệnh tiếp diễn
            self->end_quick_dimension();
        }
        if (self && self->workflow().is_array_session_active() && !self->is_inline_text_editing_) {
            if (wParam == VK_BACK) {
                self->handle_array_backspace();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN) {
                self->commit_array_input();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                self->workflow().end_array_session();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if ((wParam >= '0' && wParam <= '9') || wParam == '*' || wParam == 'x' || wParam == 'X' || wParam == '/') {
                self->handle_array_char(static_cast<char>(wParam));
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (self && self->workflow().effect_draft_numeric_input_active()) {
            if (wParam == VK_BACK || wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB)
                return 0;
            if (wParam <= 0x7e && self->workflow().input_effect_draft_character(static_cast<char>(wParam))) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam <= 0x7e) return 0;
        }
        if (self && !self->is_inline_text_editing_ && wParam >= 0x20 && wParam <= 0x7e &&
            self->handle_pen_character(static_cast<char>(wParam)))
            return 0;
        if (self && !self->is_inline_text_editing_ && wParam >= 0x20 && wParam <= 0x7e &&
            self->handle_drawing_character(static_cast<char>(wParam)))
            return 0;
        if (self && self->is_inline_text_editing_) {
            if (wParam == VK_BACK) {
                if (!self->inline_text_buffer_.empty()) {
                    while (!self->inline_text_buffer_.empty()) {
                        char c = self->inline_text_buffer_.back();
                        self->inline_text_buffer_.pop_back();
                        if ((c & 0xC0) != 0x80) break;
                    }
                }
            } else if (wParam == VK_RETURN) {
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    self->inline_text_buffer_ += '\n';
                } else {
                    self->commit_inline_text_editing();
                }
            } else if (wParam == VK_ESCAPE) {
                self->cancel_inline_text_editing();
            } else if (wParam >= 32) {
                wchar_t wc = static_cast<wchar_t>(wParam);
                char utf8[8] = {0};
                WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, sizeof(utf8), nullptr, nullptr);
                self->inline_text_buffer_ += utf8;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (self && wParam >= 0x20 && wParam <= 0x7e &&
            self->handle_drawing_character(static_cast<char>(wParam)))
            return 0;
        break;
    case WM_SYSKEYDOWN:
        if (self && wParam == VK_F10) {
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (alt) {
                if (self->is_menu_open()) self->close_menu();
                else self->open_menu(static_cast<int>(MenuId::File));
            } else {
                self->set_active_tool(11);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (self && self->handle_toolbox_key(
                wParam, true, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (self && self->is_menu_open()) {
            const auto menu = static_cast<MenuId>(self->open_menu_id());
            if (wParam == VK_ESCAPE) {
                self->close_menu();
                return 0;
            }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                const auto action = get_adjacent_menu_action(
                    menu, self->hovered_menu_item_, wParam == VK_UP ? -1 : 1,
                    [self](MenuActionId candidate) { return self->is_menu_action_enabled(candidate); });
                self->hovered_menu_item_ = action ? static_cast<int>(*action) : -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                const int direction = wParam == VK_LEFT ? -1 : 1;
                const int count = static_cast<int>(MenuId::Count);
                const int next = (self->open_menu_id() + direction + count) % count;
                self->open_menu(next);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                const auto action = self->hovered_menu_item_ >= 0
                    ? std::optional(static_cast<MenuActionId>(self->hovered_menu_item_))
                    : get_adjacent_menu_action(menu, -1, 1,
                        [self](MenuActionId candidate) { return self->is_menu_action_enabled(candidate); });
                self->close_menu();
                if (action && self->is_menu_action_enabled(*action))
                    self->execute_menu_action(static_cast<int>(*action));
                return 0;
            }
        }
        if (self && self->show_settings_modal_ && self->settings_active_tab_ == 6) {
            if (wParam == VK_ESCAPE) {
                self->close_settings_modal();
                return 0;
            }
            if (wParam == VK_TAB) {
                constexpr int kFocusCount = 7;
                const int step = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
                self->settings_ai_focus_index_ =
                    (self->settings_ai_focus_index_ + step + kFocusCount) % kFocusCount;
                if (self->settings_ai_focus_index_ == 2 || self->settings_ai_focus_index_ == 3) {
                    const bool endpoint = self->settings_ai_focus_index_ == 2;
                    self->status_msg_ = self->language_ == UILanguage::Vietnamese
                        ? (endpoint
                            ? "Chép URL MCP · nhấn Enter hoặc Space"
                            : "Chép khóa MCP · nhấn Enter hoặc Space")
                        : (endpoint
                            ? "Copy MCP URL · press Enter or Space"
                            : "Copy MCP access key · press Enter or Space");
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                self->activate_ai_connections_focus();
                return 0;
            }
            return 0;
        }
        if (self && self->handle_toolbox_key(
                wParam, false, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (self && self->workflow().has_effect_draft()) {
            auto &workflow = self->workflow();
            if (wParam == VK_ESCAPE) {
                if (self->resize_handle_ == -2) {
                    self->resize_handle_ = -1;
                    if (GetCapture() == hwnd) ReleaseCapture();
                }
                workflow.cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy bản nháp/chỉnh hiệu ứng · tài liệu không đổi"
                    : "Effect draft/edit cancelled · document unchanged";
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN) {
                const auto draft = workflow.effect_draft();
                const bool edits_existing = draft && draft->editing_existing;
                const bool editing = workflow.effect_draft_numeric_input_active();
                const bool applied = editing ? workflow.commit_effect_draft_numeric_input()
                                             : workflow.apply_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? (applied ? (editing ? "Đã cập nhật thông số bản nháp · Apply để lưu"
                                      : (edits_existing
                                          ? "Đã cập nhật hiệu ứng hiện có · Ctrl+Z hoàn tác"
                                          : "Đã thêm hiệu ứng bằng một bước Undo · Ctrl+Z hoàn tác"))
                               : "Thông số không hợp lệ hoặc bản nháp đã cũ")
                    : (applied ? (editing ? "Draft value updated · Apply to keep"
                                      : (edits_existing
                                          ? "Updated existing effect · Ctrl+Z undoes"
                                          : "Applied effect in one undo step · Ctrl+Z undoes"))
                               : "Invalid value or stale effect draft");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_TAB && workflow.effect_draft_numeric_input_active()) {
                (void)workflow.focus_next_effect_draft_numeric_field();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_BACK && workflow.effect_draft_numeric_input_active()) {
                (void)workflow.backspace_effect_draft_numeric_input();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (!workflow.effect_draft_numeric_input_active() &&
                (wParam == 'V' || wParam == 'R' || wParam == 'E' || wParam == 'K' ||
                 wParam == 'C' || wParam == 'A' || wParam == 'T' || wParam == 'H')) {
                workflow.cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy bản nháp khi chuyển công cụ"
                    : "Effect draft cancelled on tool change";
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        if (self && wParam == VK_SPACE) {
            self->space_down_ = true;
            return 0;
        }
        if (self && self->handle_key_down(wParam))
            return 0;
        break;
    case WM_KEYUP:
        if (self && wParam == VK_SPACE) {
            self->space_down_ = false;
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (self) {
            bool focus_remains_in_owned_dialog = false;
            for (HWND focus = reinterpret_cast<HWND>(wParam); focus;
                 focus = GetWindow(focus, GW_OWNER)) {
                if (focus == hwnd) {
                    focus_remains_in_owned_dialog = true;
                    break;
                }
            }
            if (self->workflow().has_effect_draft() && !focus_remains_in_owned_dialog) {
                self->workflow().cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy bản nháp hiệu ứng khi rời cửa sổ"
                    : "Effect draft cancelled when the window lost focus";
            }
            if (self->active_num_field_ == NumericFieldId::PenExpression) {
                self->cancel_numeric_field();
            } else {
                self->commit_numeric_field();
            }
            self->space_down_ = false;
            if (self->drawing_tool_ && self->drawing_tool_->active()) {
                KillTimer(hwnd, 9);
                if (self->is_panning_)
                    self->drawing_tool_->resume_after_pan();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Tạm dừng đường vẽ · bản nháp được giữ lại"
                    : "Drawing paused · draft preserved";
            }
            if (self->is_panning_) {
                self->is_panning_ = false;
                if (GetCapture() == hwnd) ReleaseCapture();
            }
            if (self->pen_tool_ && self->pen_tool_->active()) {
                if ((self->pen_tool_->pointer_pending() || self->pen_close_candidate_) &&
                    GetCapture() == hwnd)
                    ReleaseCapture();
                (void)self->pen_tool_->on_focus_lost();
                self->pen_pointer_dragged_ = false;
                self->pen_close_candidate_ = false;
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Tạm dừng Pen · bản nháp được giữ lại"
                    : "Pen paused · draft preserved";
            }
        }
        break;
    case WM_SETFOCUS:
        if (self && self->drawing_tool_ && self->drawing_tool_->active()) {
            SetTimer(hwnd, 9, 33, nullptr);
            self->status_msg_ = self->language_ == UILanguage::Vietnamese
                ? "Tiếp tục đường vẽ" : "Drawing resumed";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (self && self->pen_tool_ && self->pen_tool_->active()) {
            self->status_msg_ = self->language_ == UILanguage::Vietnamese
                ? "Tiếp tục Pen · nhấp điểm kế hoặc Enter để kết thúc"
                : "Pen resumed · click the next point or press Enter to finish";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    case WM_LBUTTONDOWN:
        if (self) {
            SetFocus(hwnd);
            self->handle_mouse_down(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                    static_cast<unsigned>(wParam));
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (self) {
            SetFocus(hwnd);
            self->handle_double_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->handle_mouse_move(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                          static_cast<unsigned>(wParam));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->handle_mouse_up(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                        static_cast<unsigned>(wParam));
        return 0;
    case WM_RBUTTONDOWN:
        if (self)
            self->handle_style_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_MBUTTONDOWN:
        if (self && self->is_canvas_point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
            if (self->drawing_tool_ && self->drawing_tool_->active())
                self->drawing_tool_->suspend_for_pan();
            else if (self->pen_tool_ && self->pen_tool_->active())
                (void)self->pen_tool_->suspend_for_pan();
            else
                self->cancel_canvas_interaction();
            self->is_panning_ = true;
            self->last_pointer_x_ = GET_X_LPARAM(lParam);
            self->last_pointer_y_ = GET_Y_LPARAM(lParam);
            SetCapture(hwnd);
        }
        return 0;
    case WM_MBUTTONUP:
        if (self && self->is_panning_) {
            self->is_panning_ = false;
            if (self->drawing_tool_ && self->drawing_tool_->active())
                self->drawing_tool_->resume_after_pan();
            if (self->pen_tool_ && self->pen_tool_->active())
                (void)self->pen_tool_->resume_after_pan();
            if (GetCapture() == hwnd) ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (self) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            if (self->active_dock_tab_ == 0 && self->show_style_inspector_ &&
                point.x >= self->width_ - 280 && point.x < self->width_ &&
                point.y >= 162 && point.y < self->height_ - 26) {
                self->scroll_style_inspector(GET_WHEEL_DELTA_WPARAM(wParam));
            } else if (self->is_canvas_point(point.x, point.y)) {
                const auto area = self->canvas_rect(self->width_, self->height_);
                auto& camera = self->root().renderer().camera();
                const double factor = std::pow(1.1,
                    static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
                const double target_zoom = std::clamp(camera.zoom() * factor, 0.01, 64.0);
                camera.zoom_at(target_zoom / camera.zoom(),
                    static_cast<float>(point.x - area.left),
                    static_cast<float>(point.y - area.top));
                self->zoom_level_ = std::clamp(
                    static_cast<int>(std::lround(camera.zoom() * 100.0)), 1, 6400);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_TIMER:
        if (self && wParam == 9 && self->drawing_tool_ && self->drawing_tool_->active()) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            if (self->is_canvas_point(point.x, point.y)) {
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                self->last_canvas_pointer_x_ = point.x;
                self->last_canvas_pointer_y_ = point.y;
                self->drawing_tool_->pointer_move(self->canvas_to_world(point.x, point.y),
                    self->root().renderer().camera(), self->window_dpi_scale(), shift, control);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_SETCURSOR:
        if (self && LOWORD(lParam) == HTCLIENT) {
            const WORD cursor_id = self->is_panning_ ? 32646
                : self->active_tool_ == 3 ? 32649
                : self->active_tool_ >= 6 ? 32515 : 32512;
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cursor_id)));
            return TRUE;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) self->paint(hdc, rc.right - rc.left, rc.bottom - rc.top);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (self && w > 0 && h > 0) {
            self->width_ = w;
            self->height_ = h;
            self->shell_.set_window_size(static_cast<double>(w), static_cast<double>(h));
            const auto area = self->canvas_rect(w, h);
            self->root().renderer().camera().set_viewport_size(
                area.right - area.left, area.bottom - area.top);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (self) {
            if (self->resize_handle_ == -2) {
                self->resize_handle_ = -1;
                self->workflow().cancel_effect_draft();
                self->status_msg_ = self->language_ == UILanguage::Vietnamese
                    ? "Đã hủy kéo tay nắm bóng · tài liệu không đổi"
                    : "Shadow handle drag cancelled · document unchanged";
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (self->drawing_tool_ && self->drawing_tool_->active()) {
                self->is_panning_ = false;
                self->drawing_tool_->resume_after_pan();
            }
            if (self->pen_tool_ && self->pen_tool_->active()) {
                if (self->is_panning_) {
                    (void)self->pen_tool_->resume_after_pan();
                } else if (self->pen_tool_->pointer_pending()) {
                    (void)self->pen_tool_->on_capture_lost();
                    self->pen_pointer_dragged_ = false;
                    self->pen_close_candidate_ = false;
                    self->status_msg_ = self->language_ == UILanguage::Vietnamese
                        ? "Đã hủy thao tác kéo Pen chưa xác nhận · bản nháp được giữ lại"
                        : "Unconfirmed Pen gesture cancelled · draft preserved";
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            self->is_panning_ = false;
            self->is_dragging_guideline_ = false;
            if (self->pointer_active_) {
                self->root().vector_controller().on_escape();
                self->pointer_active_ = false;
                self->is_dragging_ = false;
                self->is_creating_shape_ = false;
                self->drag_preview_bounds_.reset();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

#endif

} // namespace pikdraw::desktop
