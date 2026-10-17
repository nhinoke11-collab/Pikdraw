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
        const char *labels_en[3][3] = {
            {"Rectangle", "Ellipse", "Polygon"},
            {"Line", "Polyline", "Pen"},
            {"Appearance Properties", "Gaussian Blur", "Drop Shadow"}};
        const char *labels_vi[3][3] = {
            {"Hình chữ nhật", "Hình elip", "Đa giác"},
            {"Đoạn thẳng", "Đường gấp khúc", "Pen"},
            {"Thuộc tính diện mạo", "Gaussian Blur", "Drop Shadow"}};
        const int group_index = std::clamp(toolbox_focus_group_ - 1, 0, 2);
        const int item_index = std::clamp(toolbox_flyout_index_, 0, 2);
        if (toolbox_focus_group_ == 3 && item_index > 0) {
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
            toolbox_flyout_index_ = (toolbox_flyout_index_ + (key == VK_UP ? 2 : 1)) % 3;
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

void NativeWindow::update_pen_dimension_candidate() {
    geometry::Point2D base_world{0.0, 0.0};
    bool has_base = false;
    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
        if (pen_tool_->anchor_count() > 0 && !pen_tool_->draft().nodes.empty()) {
            base_world = pen_tool_->world_point_from_source(pen_tool_->draft().nodes.back().anchor);
            has_base = true;
        }
    } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active()) {
        const auto state = drawing_tool_->visual_state();
        if (!state.points.empty()) {
            base_world = state.points.back();
            has_base = true;
        }
    }
    if (!has_base) return;

    // Get length value
    std::string len_str = (active_num_field_ == NumericFieldId::PenLength) ? num_input_buffer_ : pen_numeric_length_;
    std::replace(len_str.begin(), len_str.end(), ',', '.');
    double length_mm = 0.0;
    bool has_length = false;
    if (!len_str.empty()) {
        try {
            size_t p = 0;
            double v = std::stod(len_str, &p);
            if (p == len_str.size() && std::isfinite(v) && v > 0.0) {
                length_mm = v;
                has_length = true;
            }
        } catch (...) {}
    }

    // Get angle value
    std::string ang_str = (active_num_field_ == NumericFieldId::PenAngle) ? num_input_buffer_ : pen_numeric_angle_;
    std::replace(ang_str.begin(), ang_str.end(), ',', '.');
    double angle_deg = 0.0;
    bool has_angle = false;
    if (!ang_str.empty()) {
        try {
            size_t p = 0;
            double v = std::stod(ang_str, &p);
            if (p == ang_str.size() && std::isfinite(v)) {
                angle_deg = v;
                has_angle = true;
            }
        } catch (...) {}
    }

    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
    geometry::Point2D target_world = pen_last_pointer_world_;

    if (has_length && has_angle) {
        // Direct reference to horizontal X-axis: dx = L*cos(theta), dy = L*sin(theta)
        const double rad = angle_deg * kDegToRad;
        target_world = {
            base_world.x + length_mm * std::cos(rad),
            base_world.y + length_mm * std::sin(rad)
        };
    } else if (has_length) {
        const auto delta = pen_last_pointer_world_ - base_world;
        const double dist = std::hypot(delta.x, delta.y);
        geometry::Point2D dir{1.0, 0.0};
        if (dist > 1e-6) {
            // AutoCAD polar tracking snap (15-degree steps)
            double raw_deg = std::atan2(delta.y, delta.x) * kRadToDeg;
            if (raw_deg < 0.0) raw_deg += 360.0;
            double snap_deg = std::round(raw_deg / 15.0) * 15.0;
            if (snap_deg >= 360.0) snap_deg -= 360.0;
            double diff = std::abs(raw_deg - snap_deg);
            if (diff > 180.0) diff = 360.0 - diff;
            if (diff <= 3.5) {
                const double rad = snap_deg * kDegToRad;
                dir = {std::cos(rad), std::sin(rad)};
            } else {
                dir = {delta.x / dist, delta.y / dist};
            }
        }
        target_world = {
            base_world.x + length_mm * dir.x,
            base_world.y + length_mm * dir.y
        };
    } else if (has_angle) {
        const auto delta = pen_last_pointer_world_ - base_world;
        double dist = std::hypot(delta.x, delta.y);
        if (dist < 1.0) dist = 20.0;
        const double rad = angle_deg * kDegToRad;
        target_world = {
            base_world.x + dist * std::cos(rad),
            base_world.y + dist * std::sin(rad)
        };
    }

    if (active_tool_ == 8 && pen_tool_) {
        (void)pen_tool_->pointer_move(target_world, false);
    } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_) {
        drawing_tool_->pointer_move(target_world, root().renderer().camera(),
            window_dpi_scale(), false, false);
    }
}

bool NativeWindow::handle_pen_pointer_down(int x, int y, unsigned modifiers) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() ||
        !is_canvas_point(x, y))
        return false;

    pen_pointer_start_x_ = x;
    pen_pointer_start_y_ = y;
    pen_pointer_dragged_ = false;
    pen_last_pointer_world_ = pen_snap_point(canvas_to_world(x, y), modifiers);
    pen_close_candidate_ = false;

    const auto& draft = pen_tool_->draft();
    if (!pen_tool_->resuming() && draft.nodes.size() >= 2) {
        const auto area = canvas_rect(width_, height_);
        const auto first_world = pen_tool_->world_point_from_source(draft.nodes.front().anchor);
        const auto first_screen = root().renderer().camera().world_to_screen(first_world);
        const double scale = window_dpi_scale();
        const double dx = static_cast<double>(x - area.left) - first_screen.x;
        const double dy = static_cast<double>(y - area.top) - first_screen.y;
        if (std::hypot(dx, dy) <= 8.0 * scale) {
            pen_close_candidate_ = true;
            if (hwnd_) SetCapture(hwnd_);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhấp điểm đầu để đóng đường · Enter để kết thúc mở"
                : "Click the first point to close · Enter finishes open";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }

    pen_numeric_buffer_.clear();
    const auto result = pen_tool_->pointer_down(pen_last_pointer_world_);
    if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
        result.code == vector::tools::pen::PenDrawingResultCode::Stale) {
        status_msg_ = result.message;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    (void)modifiers;
    if (hwnd_) SetCapture(hwnd_);
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Kéo để tạo tay nắm cong · Alt tách tay nắm · nhả để đặt điểm"
        : "Drag for curve handles · Alt separates handles · release to place";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_pen_pointer_move(int x, int y, unsigned modifiers) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() || is_panning_)
        return false;
    const bool gesture = pen_close_candidate_ || pen_tool_->pointer_pending();
    if (!gesture && !is_canvas_point(x, y))
        return false;

    const double scale = window_dpi_scale();
    const double dx = static_cast<double>(x - pen_pointer_start_x_);
    const double dy = static_cast<double>(y - pen_pointer_start_y_);
    if (gesture && !pen_pointer_dragged_ && std::hypot(dx, dy) >= 3.0 * scale)
        pen_pointer_dragged_ = true;
    const auto raw_world = canvas_to_world(x, y);
    pen_last_pointer_world_ = gesture ? raw_world :
        pen_numeric_buffer_.empty() ? pen_snap_point(raw_world, modifiers) : raw_world;

    if (pen_close_candidate_) {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const tools::PenGestureModifiers pen_modifiers{
        (modifiers & MK_SHIFT) != 0,
        (modifiers & MK_CONTROL) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0};
    vector::tools::pen::PenDrawingResult result;
    if (pen_tool_->pointer_pending()) {
        result = pen_tool_->pointer_move(pen_last_pointer_world_, pen_pointer_dragged_,
                                         pen_modifiers);
    } else if (active_num_field_ == NumericFieldId::PenLength ||
               active_num_field_ == NumericFieldId::PenAngle) {
        update_pen_dimension_candidate();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    } else if (!pen_numeric_buffer_.empty()) {
        result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_);
    } else {
        result = pen_tool_->pointer_move(pen_last_pointer_world_, false,
                                         pen_modifiers);
    }
    if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
        result.code == vector::tools::pen::PenDrawingResultCode::Stale ||
        result.code == vector::tools::pen::PenDrawingResultCode::BudgetExceeded) {
        status_msg_ = result.message;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

geometry::Point2D NativeWindow::pen_snap_point(geometry::Point2D raw_world,
                                               unsigned modifiers) {
    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
        const auto& draft = pen_tool_->draft();
        if (!draft.nodes.empty()) {
            const auto base_world = pen_tool_->world_point_from_source(draft.nodes.back().anchor);
            const auto delta = raw_world - base_world;
            const double dist = std::hypot(delta.x, delta.y);
            if (dist > 0.5) {
                constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
                constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
                double raw_deg = std::atan2(delta.y, delta.x) * kRadToDeg;
                if (raw_deg < 0.0) raw_deg += 360.0;
                double snap_deg = std::round(raw_deg / 15.0) * 15.0;
                if (snap_deg >= 360.0) snap_deg -= 360.0;
                double diff = std::abs(raw_deg - snap_deg);
                if (diff > 180.0) diff = 360.0 - diff;
                if (diff <= 3.5 || (modifiers & MK_SHIFT) != 0) {
                    const double rad = snap_deg * kDegToRad;
                    return geometry::Point2D{
                        base_world.x + dist * std::cos(rad),
                        base_world.y + dist * std::sin(rad)
                    };
                }
            }
        }
        return raw_world;
    }
    if (!drawing_tool_ || !drawing_tool_->active()) return raw_world;
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool control = (modifiers & MK_CONTROL) != 0;
    drawing_tool_->pointer_move(raw_world, root().renderer().camera(),
        window_dpi_scale(), shift, control);
    const auto state = drawing_tool_->visual_state();
    return state.active && state.candidate ? *state.candidate : raw_world;
}

bool NativeWindow::handle_pen_pointer_up(int x, int y, unsigned modifiers) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() ||
        (!pen_close_candidate_ && !pen_tool_->pointer_pending()))
        return false;

    const double scale = window_dpi_scale();
    const double dx = static_cast<double>(x - pen_pointer_start_x_);
    const double dy = static_cast<double>(y - pen_pointer_start_y_);
    if (!pen_pointer_dragged_ && std::hypot(dx, dy) >= 3.0 * scale)
        pen_pointer_dragged_ = true;
    pen_last_pointer_world_ = canvas_to_world(x, y);
    const bool close_candidate = pen_close_candidate_;
    pen_close_candidate_ = false;

    if (close_candidate) {
        if (GetCapture() == hwnd_) ReleaseCapture();
        const auto area = canvas_rect(width_, height_);
        const auto& draft = pen_tool_->draft();
        bool on_start = false;
        if (!draft.nodes.empty()) {
            const auto first_world = pen_tool_->world_point_from_source(draft.nodes.front().anchor);
            const auto first_screen = root().renderer().camera().world_to_screen(first_world);
            const double sx = static_cast<double>(x - area.left) - first_screen.x;
            const double sy = static_cast<double>(y - area.top) - first_screen.y;
            on_start = std::hypot(sx, sy) <= 8.0 * scale;
        }
        if (!pen_pointer_dragged_ && on_start)
            return finish_pen_session(true);
        pen_pointer_dragged_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đường chưa đóng · nhấp đúng điểm đầu hoặc nhấn Enter"
            : "Path remains open · click the first point or press Enter";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const tools::PenGestureModifiers pen_modifiers{
        (modifiers & MK_SHIFT) != 0,
        (modifiers & MK_CONTROL) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0};
    (void)pen_tool_->pointer_move(pen_last_pointer_world_, pen_pointer_dragged_, pen_modifiers);
    const auto result = pen_tool_->pointer_up();
    if (GetCapture() == hwnd_) ReleaseCapture();
    pen_pointer_dragged_ = false;
    if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
        result.code == vector::tools::pen::PenDrawingResultCode::Stale ||
        result.code == vector::tools::pen::PenDrawingResultCode::BudgetExceeded) {
        status_msg_ = result.message;
    } else if (!pen_numeric_buffer_.empty()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã đặt điểm · gõ @L<góc hoặc #X;Y · Enter kết thúc"
            : "Point placed · type @L<angle or #X;Y · Enter finishes";
    } else {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhấp điểm kế · kéo tạo cong · Enter kết thúc · Esc kết thúc / Shift+Esc hủy"
            : "Click next point · drag for curve · Enter finishes · Esc finishes / Shift+Esc cancels";
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_node_pointer_down(int x, int y, unsigned modifiers) {
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active() || is_panning_)
        return false;
    if (!is_canvas_point(x, y))
        return false;
    const auto world_pt = canvas_to_world(x, y);
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const double zoom = root().renderer().camera().zoom();
    const double tol_mm = (zoom > 1e-4) ? (8.0 / zoom) : 5.0;
    const bool handled = node_tool_->pointer_down(world_pt, shift, tol_mm);
    if (handled) {
        if (hwnd_) SetCapture(hwnd_);
        const size_t count = node_tool_->selected_node_count();
        if (count > 0) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đang chọn " + std::to_string(count) + " node · Kéo để di chuyển hoặc kéo tiếp tuyến Bézier"
                : "Selected " + std::to_string(count) + " node(s) · Drag to move or adjust Bézier handles";
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return handled;
}

bool NativeWindow::handle_node_pointer_move(int x, int y, unsigned modifiers) {
    (void)modifiers;
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active() || is_panning_)
        return false;
    if (GetCapture() != hwnd_)
        return false;
    const auto world_pt = canvas_to_world(x, y);
    const bool handled = node_tool_->pointer_move(world_pt);
    if (handled && hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return handled;
}

bool NativeWindow::handle_node_pointer_up(int x, int y, unsigned modifiers) {
    (void)modifiers;
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active())
        return false;
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    const auto world_pt = canvas_to_world(x, y);
    const bool handled = node_tool_->pointer_up(world_pt);
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return handled;
}

bool NativeWindow::handle_node_property_click(int x, int y) {
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active())
        return false;
    const int topbar_h = 32;
    const int tabbar_h = 32;
    const int content_top = topbar_h + tabbar_h + 40;
    if (y < topbar_h + tabbar_h || y > content_top)
        return false;

    if (x >= 90 && x < 165) {
        if (node_tool_->add_node_at_midpoint()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã thêm điểm node tại trung điểm đoạn" : "Added node at midpoint";
        }
    } else if (x >= 170 && x < 245) {
        if (node_tool_->delete_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xóa các điểm node được chọn" : "Deleted selected nodes";
        }
    } else if (x >= 250 && x < 325) {
        if (node_tool_->break_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã tách đường tại node" : "Broken path at node";
        }
    } else if (x >= 330 && x < 405) {
        if (node_tool_->join_selected(true)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã nối 2 node đầu mút" : "Joined 2 end nodes";
        }
    } else if (x >= 410 && x < 485) {
        if (node_tool_->close_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đóng kín đường cong" : "Closed subpath";
        }
    } else if (x >= 490 && x < 570) {
        if (node_tool_->set_selected_mode(document::PathNodeMode::Cusp)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi sang chế độ Node Cusp (Góc nhọn)" : "Changed node to Cusp mode";
        }
    } else if (x >= 575 && x < 655) {
        if (node_tool_->set_selected_mode(document::PathNodeMode::Smooth)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi sang chế độ Node Smooth (Mượt trơn)" : "Changed node to Smooth mode";
        }
    } else if (x >= 660 && x < 740) {
        if (node_tool_->set_selected_mode(document::PathNodeMode::Symmetric)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi sang chế độ Node Symmetric (Đối xứng)" : "Changed node to Symmetric mode";
        }
    } else if (x >= 745 && x < 830) {
        if (node_tool_->convert_selected_segment(document::PathSegmentKind::Line)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Chuyển đoạn cong thành đoạn thẳng" : "Converted segment to Line";
        }
    } else if (x >= 835 && x < 920) {
        if (node_tool_->convert_selected_segment(document::PathSegmentKind::CubicBezier)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Chuyển đoạn thẳng thành đường cong Bézier" : "Converted segment to Cubic Bézier";
        }
    } else if (x >= 925 && x < 1010) {
        if (node_tool_->reverse_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đảo chiều thứ tự đường vẽ" : "Reversed subpath direction";
        }
    } else {
        return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_contour_property_click(int x, int y) {
    if (active_tool_ != 12 || !contour_tool_ || !contour_tool_->active())
        return false;
    const int topbar_h = 32;
    const int tabbar_h = 32;
    const int content_top = topbar_h + tabbar_h + 40;
    if (y < topbar_h + tabbar_h || y > content_top)
        return false;

    if (x >= 80 && x < 145) {
        contour_tool_->set_direction(geometry::ContourDirection::Outside);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi hướng: Viền ngoài (Outside)" : "Direction: Outside";
    } else if (x >= 150 && x < 215) {
        contour_tool_->set_direction(geometry::ContourDirection::Inside);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi hướng: Viền trong (Inside)" : "Direction: Inside";
    } else if (x >= 220 && x < 295) {
        contour_tool_->set_direction(geometry::ContourDirection::ToCenter);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi hướng: Viền vào tâm (To Center)" : "Direction: To Center";
    } else if (x >= 305 && x < 330) {
        contour_tool_->set_spacing_mm(std::max(0.1, contour_tool_->spacing_mm() - 0.5));
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Khoảng cách viền: " + std::to_string(contour_tool_->spacing_mm()) + " mm"
            : "Contour spacing: " + std::to_string(contour_tool_->spacing_mm()) + " mm";
    } else if (x >= 410 && x < 435) {
        contour_tool_->set_spacing_mm(contour_tool_->spacing_mm() + 0.5);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Khoảng cách viền: " + std::to_string(contour_tool_->spacing_mm()) + " mm"
            : "Contour spacing: " + std::to_string(contour_tool_->spacing_mm()) + " mm";
    } else if (x >= 445 && x < 470) {
        contour_tool_->set_steps(std::max(1, contour_tool_->steps() - 1));
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Số bước viền: " + std::to_string(contour_tool_->steps())
            : "Contour steps: " + std::to_string(contour_tool_->steps());
    } else if (x >= 540 && x < 565) {
        contour_tool_->set_steps(contour_tool_->steps() + 1);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Số bước viền: " + std::to_string(contour_tool_->steps())
            : "Contour steps: " + std::to_string(contour_tool_->steps());
    } else if (x >= 575 && x < 630) {
        contour_tool_->set_join_type(geometry::JoinType::Miter);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Góc nối: Nhọn (Miter)" : "Corner join: Miter";
    } else if (x >= 635 && x < 690) {
        contour_tool_->set_join_type(geometry::JoinType::Round);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Góc nối: Tròn (Round)" : "Corner join: Round";
    } else if (x >= 695 && x < 750) {
        contour_tool_->set_join_type(geometry::JoinType::Bevel);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Góc nối: Vát (Bevel)" : "Corner join: Bevel";
    } else if (x >= 760 && x < 825) {
        if (contour_tool_->apply()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã áp dụng viền Contour vào đối tượng" : "Applied contour to object";
        }
    } else if (x >= 830 && x < 940) {
        trigger_break_contour_apart();
    } else {
        return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_gradient_property_click(int x, int y) {
    if (active_tool_ != 13 || !gradient_tool_ || !gradient_tool_->active())
        return false;
    const int topbar_h = 32;
    const int tabbar_h = 32;
    const int content_top = topbar_h + tabbar_h + 40;
    if (y < topbar_h + tabbar_h || y > content_top)
        return false;

    if (x >= 80 && x < 145) {
        gradient_tool_->presenter().set_kind(document::GradientKind::Linear);
        gradient_tool_->tool().set_kind(document::GradientKind::Linear);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu: Tuyến tính (Linear)" : "Kind: Linear";
    } else if (x >= 150 && x < 215) {
        gradient_tool_->presenter().set_kind(document::GradientKind::Radial);
        gradient_tool_->tool().set_kind(document::GradientKind::Radial);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu: Tỏa tròn (Radial)" : "Kind: Radial";
    } else if (x >= 225 && x < 275) {
        gradient_tool_->presenter().set_spread(document::GradientSpread::Pad);
        gradient_tool_->tool().set_spread(document::GradientSpread::Pad);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Tràn viền: Chặn (Pad)" : "Spread: Pad";
    } else if (x >= 280 && x < 345) {
        gradient_tool_->presenter().set_spread(document::GradientSpread::Repeat);
        gradient_tool_->tool().set_spread(document::GradientSpread::Repeat);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Tràn viền: Lặp lại (Repeat)" : "Spread: Repeat";
    } else if (x >= 350 && x < 420) {
        gradient_tool_->presenter().set_spread(document::GradientSpread::Reflect);
        gradient_tool_->tool().set_spread(document::GradientSpread::Reflect);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Tràn viền: Đối xứng (Reflect)" : "Spread: Reflect";
    } else if (x >= 430 && x < 500) {
        gradient_tool_->presenter().reverse_stops();
        gradient_tool_->tool().reverse_stops();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đảo chiều dải màu" : "Reversed gradient stops";
    } else if (x >= 505 && x < 575) {
        const auto bounds = root().vector_controller().selection().bounds(*root().document().current_snapshot()).value_or(geometry::Rect2D{0, 0, 100, 100});
        gradient_tool_->presenter().fit_to_bounds(bounds);
        gradient_tool_->tool().fit_to_object_bounds(bounds);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã khớp dải màu theo khung đối tượng" : "Fit gradient to bounds";
    } else if (x >= 585 && x < 665) {
        auto proj = gradient_tool_->presenter().geometry_projection();
        proj.angle_degrees = std::fmod(proj.angle_degrees + 45.0, 360.0);
        gradient_tool_->presenter().set_angle_degrees(proj.angle_degrees);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Góc quay: " + std::to_string(static_cast<int>(proj.angle_degrees)) + "°"
            : "Angle: " + std::to_string(static_cast<int>(proj.angle_degrees)) + "°";
    } else if (x >= 675 && x < 735) {
        gradient_tool_->presenter().add_stop(0.5, color::ColorValue{color::RGBColor{255, 200, 50}});
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã thêm điểm dừng màu ở vị trí 50%" : "Added color stop at 50%";
    } else if (x >= 745 && x < 815) {
        if (gradient_tool_->apply()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã áp dụng chuyển sắc vào đối tượng" : "Applied gradient fill";
        }
    } else if (x >= 820 && x < 880) {
        gradient_tool_->cancel();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy thay đổi chuyển sắc" : "Cancelled gradient edits";
    } else {
        return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_pen_character(char value) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() ||
        active_num_field_ != NumericFieldId::None ||
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetKeyState(VK_MENU) & 0x8000) != 0)
        return false;

    const bool numeric_start = (value >= '0' && value <= '9') || value == '-' ||
        value == '+' || value == '@' || value == '#' || value == '.';
    const bool expression_char = numeric_start ||
        (!pen_numeric_buffer_.empty() &&
         ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
          value == '<' || value == '>' || value == ';' || value == '/' ||
          value == '*' || value == '(' || value == ')' || value == ' ' || value == ','));
    if (!expression_char) return false;
    if (pen_numeric_buffer_.size() >= 256) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Biểu thức Pen vượt giới hạn 256 ký tự"
            : "Pen expression exceeds the 256-character limit";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    pen_numeric_buffer_.push_back(value);
    const auto result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_);
    status_msg_ = result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput
        ? result.message
        : language_ == UILanguage::Vietnamese
            ? "Xem trước số · Enter đặt điểm · Enter lần nữa kết thúc"
            : "Numeric preview · Enter places point · Enter again finishes";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::finish_pen_session(bool close_path, bool cancel) {
    if (!pen_tool_ || !pen_tool_->active()) return false;
    if (active_num_field_ == NumericFieldId::PenExpression ||
        active_num_field_ == NumericFieldId::PenLength ||
        active_num_field_ == NumericFieldId::PenAngle) {
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
    }
    pen_numeric_buffer_.clear();
    pen_numeric_length_.clear();
    pen_numeric_angle_.clear();
    if (cancel) {
        pen_tool_->cancel();
        workflow().cancel_effect_draft(); set_active_tool(0);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã hủy bản nháp Pen · tài liệu không thay đổi"
            : "Pen draft cancelled · document unchanged";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const auto result = close_path ? pen_tool_->close() : pen_tool_->finish_open();
    if (!result.committed() || !result.object_id) {
        status_msg_ = result.message.empty()
            ? (language_ == UILanguage::Vietnamese ? "Không thể kết thúc đường Pen" : "Could not finish the Pen path")
            : result.message;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const auto snapshot = root().document().current_snapshot();
    if (snapshot)
        root().vector_controller().set_selection({*result.object_id}, *snapshot);
    workflow().cancel_effect_draft(); set_active_tool(0);
    status_msg_ = close_path
        ? (language_ == UILanguage::Vietnamese ? "Đã đóng đường Pen" : "Pen path closed")
        : (language_ == UILanguage::Vietnamese ? "Đã hoàn tất đường Pen" : "Pen path finished");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
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

    // 0. Handle clicks inside or dismissing the New Document Modal
    if (show_new_doc_modal_) {
        int modal_w = 680;
        int modal_h = 510;
        int modal_x = (w - modal_w) / 2;
        int modal_y = (h - modal_h) / 2;

        if (x < modal_x || x > modal_x + modal_w || y < modal_y || y > modal_y + modal_h) {
            close_new_doc_modal();
            return;
        }

        // Close button [X]
        if (x >= modal_x + modal_w - 38 && x <= modal_x + modal_w - 12 &&
            y >= modal_y + 9 && y <= modal_y + 35) {
            close_new_doc_modal();
            return;
        }

        int cx = modal_x + 24;
        int cy = modal_y + 58;

        // Presets: 6 cards (y: cy + 24 to cy + 120)
        struct PresetInfo { double w; double h; };
        PresetInfo presets[] = {
            { 210.0, 297.0 }, { 297.0, 420.0 }, { 420.0, 594.0 },
            { 2000.0, 1000.0 }, { 3000.0, 1000.0 }, { 1080.0, 1080.0 }
        };
        for (int i = 0; i < 6; ++i) {
            int px = cx + (i % 3) * 212;
            int py = cy + 24 + (i / 3) * 48;
            if (x >= px && x <= px + 204 && y >= py && y <= py + 42) {
                new_doc_w_ = presets[i].w;
                new_doc_h_ = presets[i].h;
                new_doc_landscape_ = (new_doc_w_ > new_doc_h_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }

        int fy_form = cy + 142;
        // Width input box
        RECT rc_w_box = { cx, fy_form + 22, cx + 290, fy_form + 54 };
        if (x >= rc_w_box.left && x <= rc_w_box.right && y >= rc_w_box.top && y <= rc_w_box.bottom) {
            start_numeric_input(NumericFieldId::ModalWidth);
            return;
        }

        // Height input box
        RECT rc_h_box = { cx + 320, fy_form + 22, cx + 610, fy_form + 54 };
        if (x >= rc_h_box.left && x <= rc_h_box.right && y >= rc_h_box.top && y <= rc_h_box.bottom) {
            start_numeric_input(NumericFieldId::ModalHeight);
            return;
        }

        if (active_num_field_ == NumericFieldId::ModalWidth || active_num_field_ == NumericFieldId::ModalHeight) {
            commit_numeric_field();
        }

        // Orientation & Color Mode (oy: fy_form + 64, y: oy + 22 to oy + 54)
        int oy = fy_form + 64;
        if (y >= oy + 22 && y <= oy + 54) {
            // Portrait [ ▯ Dọc ]
            if (x >= cx && x <= cx + 140) {
                new_doc_landscape_ = false;
                if (new_doc_w_ > new_doc_h_) std::swap(new_doc_w_, new_doc_h_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            // Landscape [ ▭ Ngang ]
            if (x >= cx + 150 && x <= cx + 300) {
                new_doc_landscape_ = true;
                if (new_doc_h_ > new_doc_w_) std::swap(new_doc_w_, new_doc_h_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            // Color Mode: CMYK
            if (x >= cx + 320 && x <= cx + 460) {
                new_doc_color_mode_ = 0;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            // Color Mode: RGB
            if (x >= cx + 470 && x <= cx + 610) {
                new_doc_color_mode_ = 1;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }

        // DPI (ry: oy + 64, y: ry + 22 to ry + 52)
        int ry = oy + 64;
        if (y >= ry + 22 && y <= ry + 52) {
            int dpis[] = { 300, 150, 72 };
            for (int i = 0; i < 3; ++i) {
                int dx = cx + i * 205;
                if (x >= dx && x <= dx + 195) {
                    new_doc_dpi_ = dpis[i];
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
            }
        }

        // Footer buttons (fy_btn: modal_y + modal_h - 48)
        int fy_btn = modal_y + modal_h - 48;
        if (y >= fy_btn + 8 && y <= fy_btn + 40) {
            // Cancel [ Hủy bỏ ]
            if (x >= modal_x + modal_w - 280 && x <= modal_x + modal_w - 150) {
                close_new_doc_modal();
                return;
            }
            // Create [ Tạo tài liệu ]
            if (x >= modal_x + modal_w - 140 && x <= modal_x + modal_w - 15) {
                confirm_new_document();
                return;
            }
        }
        return;
    }

    // 1. Handle clicks inside or dismissing the Settings Modal (Corel Options Dialog)
    if (show_settings_modal_) {
        int modal_w = 720;
        int modal_h = 500;
        int modal_x = (w - modal_w) / 2;
        int modal_y = (h - modal_h) / 2;

        if (x < modal_x || x > modal_x + modal_w || y < modal_y || y > modal_y + modal_h) {
            close_settings_modal();
            return;
        }

        // Close button [X]
        if (x >= modal_x + modal_w - 38 && x <= modal_x + modal_w - 12 &&
            y >= modal_y + 9 && y <= modal_y + 35) {
            close_settings_modal();
            return;
        }

        // Left sidebar tabs (7 categories)
        if (x >= modal_x + 10 && x <= modal_x + 185) {
            for (int i = 0; i < 7; ++i) {
                int ty = modal_y + 54 + i * 36;
                if (y >= ty && y <= ty + 32) {
                    set_settings_tab(i);
                    return;
                }
            }
        }

        // Tab 0: Chung & Ngôn ngữ (General & Language)
        if (settings_active_tab_ == 0) {
            // UI Language cards (y: modal_y + 85 to modal_y + 115)
            if (y >= modal_y + 85 && y <= modal_y + 115) {
                if (x >= modal_x + 205 && x <= modal_x + 345) { set_language(UILanguage::Vietnamese); return; }
                if (x >= modal_x + 355 && x <= modal_x + 495) { set_language(UILanguage::English); return; }
            }
            // Quick Theme Mode cards inside General tab (y: modal_y + 155 to modal_y + 185)
            if (y >= modal_y + 155 && y <= modal_y + 185) {
                if (x >= modal_x + 205 && x <= modal_x + 345) { set_settings_theme_choice(0); return; }
                if (x >= modal_x + 355 && x <= modal_x + 495) { set_settings_theme_choice(1); return; }
            }
        }

        // Tab 1: Giao diện & Hiển thị (Display & Theme)
        if (settings_active_tab_ == 1) {
            // Theme Mode cards
            if (y >= modal_y + 95 && y <= modal_y + 175) {
                int cx0 = modal_x + 205;
                if (x >= cx0 && x <= cx0 + 155) { set_settings_theme_choice(0); return; }
                if (x >= cx0 + 165 && x <= cx0 + 320) { set_settings_theme_choice(1); return; }
                if (x >= cx0 + 330 && x <= cx0 + 485) { set_settings_theme_choice(2); return; }
            }
            // Accent Color options
            if (y >= modal_y + 225 && y <= modal_y + 255) {
                for (int i = 0; i < 4; ++i) {
                    int ax = modal_x + 205 + i * 90;
                    if (x >= ax && x <= ax + 85) { set_settings_accent_choice(i); return; }
                }
            }
        }

        // Tab 2: Đơn vị & Bắt điểm (Units & Snapping)
        if (settings_active_tab_ == 2 && y >= modal_y + 95 && y <= modal_y + 130) {
            for (int i = 0; i < 4; ++i) {
                int ux = modal_x + 205 + i * 115;
                if (x >= ux && x <= ux + 105) { set_settings_unit_choice(i); return; }
            }
        }
        if (settings_active_tab_ == 3 && x >= modal_x + 205 && x <= modal_x + 695 &&
            y >= modal_y + 338 && y <= modal_y + 370) {
            show_color_profiles();
            return;
        }

        // Tab 4: Phím tắt & Thao tác (Shortcuts profile chooser)
        if (settings_active_tab_ == 4 && y >= modal_y + 80 && y <= modal_y + 115) {
            for (int i = 0; i < 3; ++i) {
                int px = modal_x + 205 + i * 160;
                if (x >= px && x <= px + 150) {
                    shortcut_profile_ = i;
                    if (pen_tool_ && pen_tool_->active()) {
                        auto preferences = tools::PenGesturePreferences{};
                        preferences.profile = i == 0
                            ? tools::PenShortcutProfile::Corel
                            : i == 1 ? tools::PenShortcutProfile::Illustrator
                                     : tools::PenShortcutProfile::PikDraw;
                        (void)pen_tool_->set_gesture_preferences(preferences);
                    }
                    status_msg_ = (language_ == UILanguage::Vietnamese) ?
                        "Đã chuyển hồ sơ phím tắt" : "Switched shortcut profile";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
            }
        }

        if (settings_active_tab_ == 6 && x >= modal_x + 205 && x <= modal_x + 695) {
            if (y >= modal_y + 92 && y <= modal_y + 150) {
                settings_ai_focus_index_ = 0;
                toggle_ai_connections_read_choice();
                return;
            }
            if (y >= modal_y + 160 && y <= modal_y + 226) {
                settings_ai_focus_index_ = 1;
                toggle_ai_connections_edit_choice();
                return;
            }
            if (x >= modal_x + 205 + 410 && y >= modal_y + 304 && y <= modal_y + 324) {
                settings_ai_focus_index_ = 2;
                activate_ai_connections_focus();
                return;
            }
            if (x >= modal_x + 205 + 410 && y >= modal_y + 325 && y <= modal_y + 345) {
                settings_ai_focus_index_ = 3;
                activate_ai_connections_focus();
                return;
            }
        }

        // Footer buttons
        if (y >= modal_y + modal_h - 44 && y <= modal_y + modal_h - 10) {
            // Reset to defaults
            if (x >= modal_x + 16 && x <= modal_x + 160) {
                if (settings_active_tab_ == 6) {
                    settings_ai_focus_index_ = 4;
                    reset_ai_connections_choices();
                    return;
                }
                set_language(UILanguage::English);
                set_theme(ThemeMode::Light);
                settings_accent_choice_ = 0;
                settings_unit_choice_ = 0;
                status_msg_ = (language_ == UILanguage::Vietnamese) ?
                    "Đã khôi phục cấu hình mặc định (Light Mode Silver Tech)" :
                    "Reset preferences to defaults (Light Mode Silver Tech)";
                InvalidateRect(hwnd_, NULL, FALSE);
                return;
            }
            // Cancel
            if (x >= modal_x + modal_w - 170 && x <= modal_x + modal_w - 95) {
                settings_ai_focus_index_ = 5;
                close_settings_modal();
                return;
            }
            // OK / Apply
            if (x >= modal_x + modal_w - 85 && x <= modal_x + modal_w - 15) {
                if (settings_active_tab_ == 6) {
                    settings_ai_focus_index_ = 6;
                    (void)apply_ai_connections_choices();
                    return;
                }
                close_settings_modal();
                return;
            }
        }
        return;
    }

    if (open_menu_id_ >= 0) {
        const auto menu = static_cast<MenuId>(open_menu_id_);
        const int menu_height = get_menu_height(menu);
        constexpr int menu_width = 300;
        const RECT anchor = get_menu_bar_item_rect(open_menu_id_, 38);
        const int menu_x = std::clamp(static_cast<int>(anchor.left), 4, std::max(4, w - menu_width - 4));
        const int menu_y = std::clamp(38, 4, std::max(4, h - menu_height - 4));
        if (x >= menu_x && x < menu_x + menu_width &&
            y >= menu_y && y < menu_y + menu_height) {
            const auto action = get_menu_action_at(menu, y - menu_y);
            close_menu();
            if (action && is_menu_action_enabled(*action)) execute_menu_action(static_cast<int>(*action));
            return;
        }
        const int top_item = get_menu_bar_item_at(x, y, 38);
        if (top_item >= 0) {
            if (top_item == open_menu_id_) close_menu();
            else open_menu(top_item);
            return;
        }
        close_menu();
    } else {
        const int top_item = get_menu_bar_item_at(x, y, 38);
        if (top_item >= 0) {
            toggle_menu(top_item);
            return;
        }
    }

    // Page preset dropdown list click handling (CorelDRAW / Illustrator Style)
    if (show_page_preset_dropdown_) {
        int drop_x = 52;
        int drop_y = 38 + 28 + 36; // 102
        int drop_w = kPagePresetDropdownWidth; // 240
        int drop_h = page_preset_dropdown_height();
        if (x >= drop_x && x <= drop_x + drop_w && y >= drop_y && y <= drop_y + drop_h) {
            int row_y = drop_y + kPagePresetPadding;
            for (size_t i = 0; i < kPagePresetEntries.size(); ++i) {
                if (y >= row_y && y < row_y + kPagePresetRowHeight) {
                    select_page_preset(static_cast<int>(i));
                    return;
                }
                row_y += kPagePresetRowHeight;
            }
            return;
        }
        close_page_preset_dropdown();
        if (x >= 52 && x <= 165 && y >= 38 + 28 && y <= 38 + 28 + 36) {
            return;
        }
    }

    // 2. Handle clicks on Home Screen (Trang Home)
    if (show_home_screen_) {
        // Back to Canvas button at top-right
        if (x >= w - 240 && x <= w - 40 && y >= 55 && y <= 88) {
            close_home_screen();
            return;
        }

        // Home Navigation Tabs (y: 110 to 142)
        if (y >= 110 && y <= 142) {
            int tx = 180;
            if (x >= tx && x < tx + 140) { set_active_home_tab(0); return; }
            if (x >= tx + 145 && x < tx + 295) { set_active_home_tab(1); return; }
            if (x >= tx + 300 && x < tx + 480) { set_active_home_tab(2); return; }
            if (x >= tx + 485 && x < tx + 665) { set_active_home_tab(3); return; }
        }

        // Tab 0: Bắt đầu nhanh cards (y: 160 to 280)
        if (active_home_tab_ == 0 && y >= 160 && y <= 280) {
            if (x >= 180 && x <= 420) { close_home_screen(); return; } // Tạo mới
            if (x >= 440 && x <= 680) { set_active_home_tab(2); return; } // Từ mẫu
            if (x >= 700 && x <= 940) { close_home_screen(); return; } // Mở tệp
            if (x >= 960 && x <= 1200) {
                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đang đồng bộ Cloud..." : "Connecting to Cloud...";
                InvalidateRect(hwnd_, NULL, FALSE);
                return;
            }
        }

        // Tab 1: Tệp gần đây rows (y: 160 + i * 55)
        if (active_home_tab_ == 1 && y >= 160) {
            for (int i = 0; i < 5; ++i) {
                int ry = 160 + i * 55;
                if (y >= ry && y <= ry + 46 && x >= 180 && x <= w - 180) {
                    open_recent_document(i);
                    return;
                }
            }
        }

        // Tab 2: Mẫu thi công cards
        if (active_home_tab_ == 2) {
            if (y >= 160 && y <= 260) {
                if (x >= 180 && x <= 520) { create_from_template(0); return; }
                if (x >= 540 && x <= 880) { create_from_template(1); return; }
                if (x >= 900 && x <= 1240) { create_from_template(2); return; }
            }
            if (y >= 280 && y <= 380) {
                if (x >= 180 && x <= 520) { create_from_template(3); return; }
                if (x >= 540 && x <= 880) { create_from_template(4); return; }
                if (x >= 900 && x <= 1240) { create_from_template(5); return; }
            }
        }
    }

    // Document commands are exposed through the named File and Edit menus.

    // 3a. Document Tab Bar clicks (y: 38 to 66)
    if (y >= 38 && y <= 66) {
        int cur_x = 10;
        int tab_w = 210;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            int tab_left = cur_x;
            int tab_right = cur_x + tab_w;
            if (x >= tab_left && x <= tab_right) {
                if (x >= tab_right - 24 && x <= tab_right - 4) {
                    close_tab(i);
                    return;
                }
                switch_to_tab(i);
                return;
            }
            cur_x += tab_w + 4;
        }
        if (x >= cur_x && x <= cur_x + 28) {
            open_new_doc_modal();
            return;
        }
        return;
    }

    // 3b. Context Tool Ribbon clicks (y: 66 to 102)
    if (y >= 66 && y <= 102) {
        const auto& selection = root().vector_controller().selection().selected_ids();
        bool is_text_mode = (active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected());

        if ((active_tool_ == 8 && pen_tool_ && pen_tool_->active()) ||
            ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active())) {
            if (x >= 52 && x <= 185) {
                if (active_tool_ == 8 && pen_tool_->anchor_count() == 0) {
                    (void)pen_tool_->pointer_down(pen_last_pointer_world_);
                    (void)pen_tool_->pointer_up();
                } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_->path_point_count() == 0) {
                    (void)drawing_tool_->confirm_candidate();
                }
                start_numeric_input(NumericFieldId::PenLength);
                return;
            }
            if (x >= 192 && x <= 315) {
                if (active_tool_ == 8 && pen_tool_->anchor_count() == 0) {
                    (void)pen_tool_->pointer_down(pen_last_pointer_world_);
                    (void)pen_tool_->pointer_up();
                } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_->path_point_count() == 0) {
                    (void)drawing_tool_->confirm_candidate();
                }
                start_numeric_input(NumericFieldId::PenAngle);
                return;
            }
            if (x >= 325 && x <= 415) {
                if (active_num_field_ == NumericFieldId::PenLength ||
                    active_num_field_ == NumericFieldId::PenAngle ||
                    active_num_field_ == NumericFieldId::PenExpression) {
                    commit_numeric_field();
                }
                if (active_tool_ == 8) finish_pen_session(false);
                else {
                    const auto result = drawing_tool_->enter();
                    if (!drawing_tool_->active()) finish_drawing_tool(true);
                }
                return;
            }
            if (x >= 422 && x <= 492) {
                if (active_tool_ == 8) finish_pen_session(false, true);
                else {
                    drawing_tool_->cancel();
                    finish_drawing_tool(true);
                }
                return;
            }
            return;
        } else if (active_tool_ == 9) {
            if (x >= 58 && x <= 164) {
                start_numeric_input(NumericFieldId::PolygonSides);
                return;
            }
            commit_numeric_field();
            return;
        } else if (is_text_mode) {
            // TYPOGRAPHY RIBBON CLICKS (Corel / Word Reference)
            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                x >= 920 && x <= 1044 && x + 124 <= w - 280) {
                start_numeric_input(NumericFieldId::ObjectOpacity);
                return;
            }
            if (x >= 52 && x <= 192) { cycle_font_family(); return; }
            if (x >= 220 && x <= 240) { adjust_font_size(-2.0); return; }
            if (x >= 294 && x <= 314) { adjust_font_size(2.0); return; }
            if (x >= 322 && x <= 350) { toggle_font_bold(); return; }
            if (x >= 354 && x <= 382) { toggle_font_italic(); return; }
            if (x >= 386 && x <= 414) { toggle_font_underline(); return; }
            if (x >= 422 && x <= 462) { set_font_alignment(typography::TextAlignment::Left); return; }
            if (x >= 466 && x <= 506) { set_font_alignment(typography::TextAlignment::Center); return; }
            if (x >= 510 && x <= 550) { set_font_alignment(typography::TextAlignment::Right); return; }
            if (x >= 556 && x <= 616) { convert_selected_text_to_curves(); return; }
            if (x >= 622 && x <= 692) { insert_table(table_rows_, table_cols_); return; }
            if (!selection.empty() && x >= 698 && x <= 748) {
                workflow().delete_selected();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            int color_start_x = (!selection.empty()) ? 754 : 698;
            if (x >= color_start_x && x <= color_start_x + 36) { apply_fill_to_selection("#00f2c3"); return; }
            if (x >= color_start_x + 40 && x <= color_start_x + 76) { apply_fill_to_selection("#0ea5e9"); return; }
            if (x >= color_start_x + 80 && x <= color_start_x + 116) { apply_fill_to_selection("#eab308"); return; }
            if (x >= color_start_x + 120 && x <= color_start_x + 156) { apply_fill_to_selection("#ef4444"); return; }
            return;
        } else if (selection.empty()) {
            // PAGE MODE (Direct Numeric Input + Compact Preset Combobox)
            const auto snapshot = root().document().current_snapshot();
            const double pw = snapshot->page().width_mm;
            const double ph = snapshot->page().height_mm;
            if (x >= 52 && x <= 165) {
                commit_numeric_field();
                toggle_page_preset_dropdown();
                return;
            }
            if (x >= 175 && x <= 275) { close_page_preset_dropdown(); start_numeric_input(NumericFieldId::PageWidth); return; } // Page W
            if (x >= 283 && x <= 383) { close_page_preset_dropdown(); start_numeric_input(NumericFieldId::PageHeight); return; } // Page H
            if (x >= 391 && x <= 455) { close_page_preset_dropdown(); commit_numeric_field(); if (pw > ph) toggle_page_orientation(); return; } // Dọc
            if (x >= 461 && x <= 525) { close_page_preset_dropdown(); commit_numeric_field(); if (ph > pw) toggle_page_orientation(); return; } // Ngang
            close_page_preset_dropdown();
            commit_numeric_field();
        } else {
            // OBJECT MODE (Direct Numeric Input)
            if (x >= 10 && x <= 95) { start_numeric_input(NumericFieldId::ObjectX); return; } // X box
            if (x >= 100 && x <= 185) { start_numeric_input(NumericFieldId::ObjectY); return; } // Y box
            if (x >= 190 && x <= 285) { start_numeric_input(NumericFieldId::ObjectWidth); return; } // W box
            if (x >= 290 && x <= 360) { commit_numeric_field(); toggle_aspect_ratio_lock(); InvalidateRect(hwnd_, nullptr, FALSE); return; } // Lock toggle
            if (x >= 365 && x <= 460) { start_numeric_input(NumericFieldId::ObjectHeight); return; } // H box
            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                x >= 820 && x <= 944 && x + 124 <= w - 280) {
                start_numeric_input(NumericFieldId::ObjectOpacity);
                return;
            }
            commit_numeric_field();

            // Group / Ungroup / Enter Group / Exit Group / Align Ribbon Buttons
            if (is_in_isolation_mode()) {
                if (x >= 580 && x <= 670) { exit_group_isolation(); return; }
            } else {
                if (selection.size() >= 2) {
                    const auto buttons = get_multi_object_context_buttons(workflow(), workflow().can_group_selected(), w - 280);
                    for (const auto& button : buttons) {
                        if (x < button.left || x >= button.left + button.width) continue;
                        switch (button.action) {
                        case ObjectContextAction::AlignLeft: align_selected_objects(geometry::AlignMode::Left); break;
                        case ObjectContextAction::AlignCenterHorizontal: align_selected_objects(geometry::AlignMode::CenterHorizontal); break;
                        case ObjectContextAction::AlignRight: align_selected_objects(geometry::AlignMode::Right); break;
                        case ObjectContextAction::AlignTop: align_selected_objects(geometry::AlignMode::Top); break;
                        case ObjectContextAction::AlignCenterVertical: align_selected_objects(geometry::AlignMode::CenterVertical); break;
                        case ObjectContextAction::AlignBottom: align_selected_objects(geometry::AlignMode::Bottom); break;
                        case ObjectContextAction::CenterPage: align_selected_objects(geometry::AlignMode::CenterPage); break;
                        case ObjectContextAction::DistributeHorizontal:
                            distribute_selected_objects(geometry::DistributeMode::SpacingHorizontal); break;
                        case ObjectContextAction::DistributeVertical:
                            distribute_selected_objects(geometry::DistributeMode::SpacingVertical); break;
                        case ObjectContextAction::Group: {
                            const auto gid = workflow().group_selected();
                            if (gid != document::kInvalidObjectId) {
                                status_msg_ = language_ == UILanguage::Vietnamese
                                    ? "Đã nhóm các đối tượng" : "Grouped selected objects";
                            }
                            InvalidateRect(hwnd_, nullptr, FALSE);
                            break;
                        }
                        case ObjectContextAction::FlipHorizontal: execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipHorizontal)); break;
                        case ObjectContextAction::FlipVertical: execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipVertical)); break;
                        case ObjectContextAction::Rotate90CW: execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CW)); break;
                        case ObjectContextAction::Rotate90CCW: execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CCW)); break;
                        case ObjectContextAction::BooleanUnion: trigger_boolean_operation(geometry::BooleanOp::Union); break;
                        case ObjectContextAction::BooleanDifference: trigger_boolean_operation(geometry::BooleanOp::Difference); break;
                        case ObjectContextAction::BooleanIntersection: trigger_boolean_operation(geometry::BooleanOp::Intersection); break;
                        case ObjectContextAction::BooleanExclude: trigger_boolean_operation(geometry::BooleanOp::Xor); break;
                        case ObjectContextAction::BooleanReverseDifference: trigger_boolean_operation(geometry::BooleanOp::BackMinusFront); break;
                        }
                        return;
                    }
                } else if (selection.size() == 1) {
                    int rx = 580;
                    if (x >= rx && x < rx + 28) {
                        execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipHorizontal));
                        return;
                    }
                    if (x >= rx + 32 && x < rx + 60) {
                        execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipVertical));
                        return;
                    }
                    if (x >= rx + 64 && x < rx + 92) {
                        execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CW));
                        return;
                    }
                    if (x >= rx + 96 && x < rx + 124) {
                        execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CCW));
                        return;
                    }
                    rx += 130;
                    if (workflow().is_group(selection.front())) {
                        if (x >= rx && x < rx + 66) {
                            if (workflow().ungroup_selected()) {
                                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                                InvalidateRect(hwnd_, nullptr, FALSE);
                            }
                            return;
                        }
                        if (x >= rx + 72 && x < rx + 138) {
                            enter_group_isolation(selection.front());
                            return;
                        }
                    }
                }
            }

            int color_start_x = (selection.size() == 1 && workflow().is_group(selection.front())) ? 920
                              : (selection.size() == 1) ? 780
                              : (selection.size() >= 2) ? 1160
                              : is_in_isolation_mode() ? 676 : 580;
            if (x >= color_start_x && x <= color_start_x + 36) { apply_fill_to_selection("#00f2c3"); return; }
            if (x >= color_start_x + 40 && x <= color_start_x + 76) { apply_fill_to_selection("#0ea5e9"); return; }
            if (x >= color_start_x + 80 && x <= color_start_x + 116) { apply_fill_to_selection("#eab308"); return; }
        }
        return;
    }

    const bool leaving_pen_expression = active_num_field_ == NumericFieldId::PenExpression ||
                                        active_num_field_ == NumericFieldId::PenLength ||
                                        active_num_field_ == NumericFieldId::PenAngle;
    const bool invalid_polygon_setting = active_num_field_ == NumericFieldId::PolygonSides ||
                                         active_num_field_ == NumericFieldId::PolygonRotation;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (leaving_pen_expression && is_canvas_point(x, y))
            return;
        if (invalid_polygon_setting && active_num_field_ != NumericFieldId::None &&
            is_canvas_point(x, y))
            return;
    }

    // Space + left drag temporarily pans without changing the active tool.
    if (space_down_ && is_canvas_point(x, y)) {
        if (drawing_tool_ && drawing_tool_->active()) drawing_tool_->suspend_for_pan();
        if (pen_tool_ && pen_tool_->active()) (void)pen_tool_->suspend_for_pan();
        is_panning_ = true;
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        SetCapture(hwnd_);
        return;
    }

    // 4. Grouped toolbox and its compact flyouts.
    const bool on_toolbox = x >= 6 && x <= 46 && y >= 112 && y < 406;
    if (toolbox_open_group_ > 0) {
        const int group_row = toolbox_open_group_ == 1 ? 1
                            : toolbox_open_group_ == 2 ? 2 : 6;
        const int flyout_top = 112 + group_row * 42;
        const bool inside_flyout = x >= 54 && x <= 242 &&
            y >= flyout_top && y < flyout_top + 116;
        if (inside_flyout && y >= flyout_top + 26) {
            const int item = (y - flyout_top - 26) / 29;
            if (item >= 0 && item < 3) {
                if (toolbox_open_group_ == 3) {
                    activate_toolbox_effect_item(item);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                const int tool = toolbox_open_group_ == 1
                    ? (item == 0 ? 1 : item == 1 ? 2 : 9)
                    : (item == 0 ? 6 : item == 1 ? 7 : 8);
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft(); set_active_tool(tool);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        if (!inside_flyout && !on_toolbox) {
            toolbox_open_group_ = 0;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    if (on_toolbox) {
        toolbox_keyboard_focus_ = false;
        const int row = (y - 112) / 42;
        if (row >= 0 && row < 7) {
            if (row == 1 || row == 2 || row == 6) {
                const int group = row == 1 ? 1 : row == 2 ? 2 : 3;
                toolbox_focus_group_ = group;
                if (x >= 37) {
                    if (toolbox_open_group_ == group) {
                        toolbox_open_group_ = 0;
                    } else {
                        toolbox_open_group_ = group;
                        if (group == 3) {
                            toolbox_flyout_index_ = 0;
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Appearance · ↑/↓ chọn · Enter mở · Esc đóng"
                                : "Appearance · ↑/↓ select · Enter open · Esc close";
                        } else {
                            const int active = group == 1 ? quick_shapes_last_tool_ : path_drawing_last_tool_;
                            toolbox_flyout_index_ = group == 1
                                ? (active == 1 ? 0 : active == 2 ? 1 : 2)
                                : (active == 6 ? 0 : active == 7 ? 1 : 2);
                        }
                    }
                } else if (group == 3) {
                    toolbox_open_group_ = 0;
                    execute_menu_action(static_cast<int>(MenuActionId::ObjectProperties));
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                } else {
                    toolbox_open_group_ = 0;
                    const int active_in_group = group == 1
                        ? ((active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9)
                               ? active_tool_ : quick_shapes_last_tool_)
                        : ((active_tool_ >= 6 && active_tool_ <= 8)
                               ? active_tool_ : path_drawing_last_tool_);
                    workflow().cancel_effect_draft(); set_active_tool(active_in_group);
                }
            } else if (row < 6) {
                static constexpr int separate_tools[] = {0, 0, 0, 3, 4, 5};
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft(); set_active_tool(separate_tools[row]);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // 5. Right Dock Tabs & Shaping Docker (x: w - 280 to w)
    if (x >= w - 280 && x <= w) {
        if (y >= 102 && y <= 132) {
            int tab_w = 280 / 3;
            int tab_idx = (x - (w - 280)) / tab_w;
            if (tab_idx >= 0 && tab_idx < 3) {
                set_active_dock_tab(tab_idx);
                return;
            }
        }
        const int inspector_content_y = 138;
        if (active_dock_tab_ == 0 && active_tool_ == 9 && y >= inspector_content_y + 24) {
            if (x >= w - 148 && x <= w - 14 && y >= 172 && y <= 206) {
                start_numeric_input(NumericFieldId::PolygonSides);
                return;
            }
            if (x >= w - 148 && x <= w - 14 && y >= 216 && y <= 250) {
                start_numeric_input(NumericFieldId::PolygonRotation);
                return;
            }
            return;
        }
        if (active_dock_tab_ == 0 && y >= inspector_content_y && y < inspector_content_y + 24) {
            const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
            const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
            const int column = std::clamp((x - panel_left) / (button_width + 6), 0, 2);
            show_style_inspector_ = column == 1;
            show_align_panel_ = column == 2;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (active_dock_tab_ == 0 && show_align_panel_ && handle_align_panel_click(x, y))
            return;
        if (active_dock_tab_ == 0 && show_style_inspector_) {
            if (handle_style_click(x, y))
                return;
            if (y >= inspector_content_y + 24) return;
        }
    }

    // 6. Page and zoom controls in the status bar.
    if (y >= height_ - 26) {
        if (x >= w - 210 && x <= w - 145) { fit_page_to_canvas(); return; }
        if (x >= w - 120 && x <= w - 95) { zoom_out(); return; }
        if (x >= w - 90 && x <= w - 50) {
            auto& camera = root().renderer().camera();
            const auto area = canvas_rect(width_, height_);
            camera.set_viewport_size(area.right - area.left, area.bottom - area.top);
            camera.set_zoom(1.0);
            zoom_level_ = 100;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (x >= w - 45 && x <= w - 20) { zoom_in(); return; }
    }

    const auto area = canvas_rect(width_, height_);
    if (is_in_isolation_mode() && y >= area.top && y <= area.top + 34 && x >= area.left && x <= area.right) {
        if (x >= area.right - 150) {
            exit_group_isolation();
            return;
        }
        if (x >= area.left + 8 && x <= area.left + 140) {
            exit_all_isolation();
            return;
        }
        return;
    }

    // Ruler guideline dragging: Top ruler creates horizontal guideline, Left ruler creates vertical guideline
    if (x >= area.left && x <= area.right && y >= 102 && y <= area.top) {
        is_dragging_guideline_ = true;
        drag_guideline_orientation_ = GuidelineOrientation::Horizontal;
        show_guidelines_ = true;
        const auto world_pt = canvas_to_world(x, y);
        drag_guideline_pos_mm_ = world_pt.y;
        SetCapture(hwnd_);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đang kéo đường gióng ngang từ thước trên..."
            : "Dragging horizontal guideline from top ruler...";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (x >= 52 && x <= area.left && y >= area.top && y <= area.bottom) {
        is_dragging_guideline_ = true;
        drag_guideline_orientation_ = GuidelineOrientation::Vertical;
        show_guidelines_ = true;
        const auto world_pt = canvas_to_world(x, y);
        drag_guideline_pos_mm_ = world_pt.x;
        SetCapture(hwnd_);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đang kéo đường gióng dọc từ thước trái..."
            : "Dragging vertical guideline from left ruler...";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (handle_drawing_panel_click(x, y))
        return;
    if (handle_style_click(x, y))
        return;
    if (handle_node_property_click(x, y))
        return;
    if (handle_contour_property_click(x, y))
        return;
    if (handle_gradient_property_click(x, y))
        return;
    if (handle_node_pointer_down(x, y, modifiers))
        return;
    if (handle_pen_pointer_down(x, y, modifiers))
        return;
    if (handle_drawing_click(x, y, modifiers))
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
        if (x >= 54 && x <= 242 && y >= flyout_top + 26 && y < flyout_top + 116) {
            const int item = (y - flyout_top - 26) / 29;
            if (item >= 0 && item < 3 && toolbox_flyout_index_ != item) {
                toolbox_flyout_index_ = item;
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }

    update_hover_tooltip(x, y);

    if (toolbox_open_group_ == 3) {
        const int flyout_top = 112 + 6 * 42;
        if (x >= 54 && x <= 242 && y >= flyout_top + 26 && y < flyout_top + 116) {
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
