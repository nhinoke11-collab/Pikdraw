#include "native_window_powerclip_ui.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_document_color.hpp"
#include "native_window_text_properties.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_length_input.hpp"
#include "pikdraw/document/commands/page_commands.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include "pikdraw/desktop/shell/native_window_shortcuts_settings.hpp"
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
namespace {
std::string color_conversion_summary_text(
    const std::optional<color::ColorConversionSummary>& report, bool vietnamese) {
    if (!report)
        return vietnamese ? "không có màu process cần chuyển" : "no process colors needed conversion";
    std::string text;
    if (report->icc_managed > 0) text = "ICC " + std::to_string(report->icc_managed);
    if (report->approximate > 0) {
        if (!text.empty()) text += " · ";
        text += (vietnamese ? "gần đúng " : "fallback ") + std::to_string(report->approximate);
    }
    if (report->preset_policy > 0) {
        if (!text.empty()) text += " · ";
        text += (vietnamese ? "chính sách đen " : "black policy ") +
            std::to_string(report->preset_policy);
    }
    if (report->tac_limited > 0) {
        if (!text.empty()) text += " · ";
        text += "TAC " + std::to_string(report->tac_limited);
    }
    if (text.empty())
        text = vietnamese ? "không có màu process cần chuyển" : "no process colors needed conversion";
    return text;
}
} // namespace

#ifdef _WIN32

bool NativeWindow::is_menu_action_enabled(MenuActionId action) const {
    if(const auto id=powerclip_ui::action_for(action); !id.empty())
        return root().window().action_dispatcher().is_action_enabled(id);
    const auto& selected = root().vector_controller().selection().selected_ids();
    switch (action) {
    case MenuActionId::ColorConvertActivePageProfile:
        return root().window().action_dispatcher().is_action_enabled(
            actions::ActionId::kColorConvertProfile);
    case MenuActionId::ColorAssignSelectionProfile:
        return root().window().action_dispatcher().is_action_enabled(
            actions::ActionId::kColorAssignSelectionProfile);
    case MenuActionId::ColorConvertSelectionProfile:
        return root().window().action_dispatcher().is_action_enabled(
            actions::ActionId::kColorConvertSelectionProfile);
    case MenuActionId::ColorConvertDocumentProfile:
        return root().window().action_dispatcher().is_action_enabled(
            actions::ActionId::kColorConvertDocumentProfile);
    case MenuActionId::ObjectGroup:
        return workflow().can_group_selected();
    case MenuActionId::ObjectUngroup:
        return selected.size() == 1 && workflow().is_group(selected.front());
    case MenuActionId::ObjectPowerClipMake:
        return workflow().can_make_clipping_mask_selected();
    case MenuActionId::ObjectPowerClipRelease:
    case MenuActionId::ObjectPowerClipFitCenter:
    case MenuActionId::ObjectPowerClipFitProp:
    case MenuActionId::ObjectPowerClipFillProp:
        return selected.size() == 1 && workflow().is_clipping_container(selected.front());
    case MenuActionId::ObjectPowerClipPlaceInside:
        return !selected.empty();
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
    case MenuActionId::ObjectOrderToFront:
    case MenuActionId::ObjectOrderToBack:
    case MenuActionId::ObjectOrderForwardOne:
    case MenuActionId::ObjectOrderBackwardOne:
    case MenuActionId::ObjectLock:
    case MenuActionId::ObjectUnlock:
    case MenuActionId::ObjectHide:
        return !selected.empty();
    case MenuActionId::ObjectOrderReverse:
        return selected.size() >= 2;
    case MenuActionId::ObjectPowerClipToggleLock:
        return selected.size() == 1 && workflow().is_clipping_container(selected.front());
    case MenuActionId::ObjectIsolationEnter:
        return selected.size() == 1 && (workflow().is_group(selected.front()) || workflow().is_clipping_container(selected.front()));
    case MenuActionId::ObjectIsolationExit:
        return is_in_isolation_mode();
    case MenuActionId::EffectsGaussianBlur:
        return workflow().can_activate_selected_effect(document::GaussianBlurEffect{1.0, 1.0});
    case MenuActionId::EffectsContour:
        return !selected.empty();
    case MenuActionId::EffectsBreakContourApart:
        return (contour_tool_ && contour_tool_->active()) || !selected.empty();
    case MenuActionId::EffectsDropShadow:
        return workflow().can_activate_selected_effect(document::DropShadowEffect{});
    case MenuActionId::EffectsClear:
        return workflow().can_clear_selected_effects();
    case MenuActionId::LayoutPagePrevious:
    case MenuActionId::LayoutPageNext:
    case MenuActionId::LayoutPageDelete: {
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) return false;
        const auto& pages = snapshot->pages();
        const auto found = std::find_if(pages.begin(), pages.end(), [&](const auto& page) {
            return page.id == snapshot->active_page_id();
        });
        if (found == pages.end()) return false;
        const auto index = static_cast<size_t>(std::distance(pages.begin(), found));
        if (action == MenuActionId::LayoutPagePrevious) return index > 0;
        if (action == MenuActionId::LayoutPageNext) return index + 1 < pages.size();
        return pages.size() > 1;
    }
    case MenuActionId::LayoutUnitMillimeter:
    case MenuActionId::LayoutUnitCentimeter:
    case MenuActionId::LayoutUnitMeter:
    case MenuActionId::LayoutUnitInch:
    case MenuActionId::LayoutUnitFoot:
    case MenuActionId::LayoutUnitYard:
    case MenuActionId::LayoutUnitPoint:
    case MenuActionId::LayoutUnitPica:
        return !root().document().is_content_locked();
    case MenuActionId::TextCaseSentence:
    case MenuActionId::TextCaseLower:
    case MenuActionId::TextCaseUpper:
    case MenuActionId::TextCaseTitle:
    case MenuActionId::TextCaseToggle: {
        if (is_inline_text_editing_ && text_editor_) return true;
        if (selected.empty()) return false;
        const auto snapshot = root().document().current_snapshot();
        return snapshot && std::all_of(selected.begin(), selected.end(), [&](document::ObjectId id) {
            const auto object = snapshot->objects().get(id);
            return object && std::holds_alternative<document::TextObject>(*object) &&
                !std::get<document::TextObject>(*object).locked;
        });
    }
    case MenuActionId::TextConvertArtisticParagraph:
        if (is_inline_text_editing_ && text_editor_) return true;
        if (selected.size() != 1) return false;
        if (const auto snapshot = root().document().current_snapshot()) {
            const auto object = snapshot->objects().get(selected.front());
            return object && std::holds_alternative<document::TextObject>(*object) &&
                !std::get<document::TextObject>(*object).locked;
        }
        return false;
    case MenuActionId::TextFitToPath: {
        if (selected.size() != 2) return false;
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) return false;
        bool has_text = false;
        bool has_path = false;
        for (const auto id : selected) {
            const auto object = snapshot->objects().get(id);
            if (!object) return false;
            if (const auto* text = std::get_if<document::TextObject>(&*object)) {
                if (text->locked) return false;
                has_text = true;
            } else if (const auto* path = std::get_if<document::PathObject>(&*object)) {
                if (path->locked) return false;
                has_path = true;
            }
        }
        return has_text && has_path;
    }
    case MenuActionId::TextStraighten:
    case MenuActionId::TextReversePath: {
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) return false;
        return std::any_of(selected.begin(), selected.end(), [&](document::ObjectId id) {
            const auto object = snapshot->objects().get(id);
            return object && std::holds_alternative<document::TextObject>(*object) &&
                !std::get<document::TextObject>(*object).locked &&
                std::get<document::TextObject>(*object).is_text_on_path();
        });
    }
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
    case MenuActionId::TextBold: return font_bold_;
    case MenuActionId::TextItalic: return font_italic_;
    case MenuActionId::TextUnderline: return font_underline_;
    case MenuActionId::TextStrikethrough: return font_strikethrough_;
    case MenuActionId::TextAlignLeft: return font_alignment_ == typography::TextAlignment::Left;
    case MenuActionId::TextAlignCenter: return font_alignment_ == typography::TextAlignment::Center;
    case MenuActionId::TextAlignRight: return font_alignment_ == typography::TextAlignment::Right;
    case MenuActionId::TextAlignJustify: return font_alignment_ == typography::TextAlignment::Justify;
    case MenuActionId::TextNonPrinting: return show_non_printing_characters_;
    case MenuActionId::ViewProofColors: return proof_colors_enabled_;
    case MenuActionId::ViewGamutWarning: return gamut_warning_enabled_;
    case MenuActionId::ViewOverprintPreview: return overprint_preview_enabled_;
    case MenuActionId::LayoutUnitMillimeter:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Millimeter;
    case MenuActionId::LayoutUnitCentimeter:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Centimeter;
    case MenuActionId::LayoutUnitMeter:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Meter;
    case MenuActionId::LayoutUnitInch:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Inch;
    case MenuActionId::LayoutUnitFoot:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Foot;
    case MenuActionId::LayoutUnitYard:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Yard;
    case MenuActionId::LayoutUnitPoint:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Point;
    case MenuActionId::LayoutUnitPica:
        return root().document().page().display_length_unit == geometry::units::LengthUnit::Pica;
    default: return false;
    }
}

void NativeWindow::execute_menu_action(int action_id) {
    if (action_id == document_color_ui::kModeMenu || action_id == document_color_ui::kDpiMenu)
        action_id = document_color_ui::choose_setting(hwnd_, *root().document().current_snapshot(),
            language_ == UILanguage::Vietnamese, action_id == document_color_ui::kDpiMenu,
            get_menu_bar_item_rect(static_cast<int>(MenuId::Edit), 38).left);
    if (*document_color_ui::action_id(action_id)) {
        (void)root().window().action_dispatcher().dispatch(document_color_ui::action_id(action_id));
        return;
    }
    const auto action = static_cast<MenuActionId>(action_id);
    if (!is_menu_action_enabled(action)) return;
    if(const auto id=powerclip_ui::action_for(action); !id.empty()) {
        root().window().action_dispatcher().dispatch(id); return;
    }
    const auto& selected_items = root().vector_controller().selection().selected_ids();
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
            if (std::holds_alternative<document::DropShadowEffect>(parameters)) ribbon_drop_shadow_mode_ = true;
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
    const auto finish_page_action = [this](std::string action_name) {
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) return;
        const auto* page = snapshot->find_page(snapshot->active_page_id());
        if (!page) return;
        sync_display_unit_from_active_page();
        active_tab().page_width_mm = page->layout.width_mm;
        active_tab().page_height_mm = page->layout.height_mm;
        root().vector_controller().selection().clear();
        fit_page_to_canvas();
        const auto page_it = std::find_if(snapshot->pages().begin(), snapshot->pages().end(),
            [&](const auto& candidate) { return candidate.id == page->id; });
        const size_t page_number = static_cast<size_t>(std::distance(snapshot->pages().begin(), page_it)) + 1;
        std::ostringstream hint;
        if (language_ == UILanguage::Vietnamese)
            hint << action_name << " · Trang " << page_number << '/' << snapshot->page_count()
                 << " · " << format_mm_display_utf8(page->layout.width_mm, display_length_unit_)
                 << " × " << format_mm_display_utf8(page->layout.height_mm, display_length_unit_);
        else
            hint << action_name << " · Page " << page_number << '/' << snapshot->page_count()
                 << " · " << format_mm_display_utf8(page->layout.width_mm, display_length_unit_)
                 << " × " << format_mm_display_utf8(page->layout.height_mm, display_length_unit_);
        status_msg_ = hint.str();
        update_color_preview_options();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    switch (action) {
    case MenuActionId::ObjectPowerClipEditFrame:
    case MenuActionId::ObjectPowerClipExtract:
    case MenuActionId::ObjectPowerClipCopyFrom:
    case MenuActionId::ObjectPowerClipEmpty:
    case MenuActionId::ObjectPowerClipRemoveFrame:
    case MenuActionId::ObjectPowerClipAppearance:
        return; // Dispatched through the shared semantic action above.

    case MenuActionId::FileNew: open_new_doc_modal(); return;
    case MenuActionId::FileOpen: open_document_dialog(); return;
    case MenuActionId::FileImport: {
        const std::string id(actions::ActionId::kFileImport);
        auto& dispatcher = root().window().action_dispatcher();
        const auto* action = dispatcher.get_action(id);
        if (!action || !action->handler) {
            dispatcher.register_action({id, "Nhập tệp...", "Import...", "Ctrl+I",
                [this] { open_import_dialog(); }, nullptr});
        }
        (void)dispatcher.dispatch(id);
        return;
    }
    case MenuActionId::FileRecoverAutosave: {
        const std::string id(actions::ActionId::kFileRecoverAutosave);
        auto& dispatcher = root().window().action_dispatcher();
        if (!dispatcher.get_action(id)) {
            dispatcher.register_action({id, "Phục hồi bản tự lưu", "Recover Autosave", "",
                [this] {
                    scan_recovery_checkpoints(true);
                    if (recovery_candidates_.empty()) show_recovery_modal_ = true;
                }, nullptr});
        }
        (void)dispatcher.dispatch(id);
        return;
    }
    case MenuActionId::FileSave: save_document_dialog(); return;
    case MenuActionId::FileSaveAs: save_document_dialog(true); return;
    case MenuActionId::FileExport: {
        const std::string id(actions::ActionId::kFileExport);
        auto& dispatcher = root().window().action_dispatcher();
        const auto* action = dispatcher.get_action(id);
        if (!action || !action->handler) {
            dispatcher.register_action({id, "Xuất tệp...", "Export...", "Ctrl+E",
                [this] { trigger_export(); }, nullptr});
        }
        (void)dispatcher.dispatch(id);
        return;
    }
    case MenuActionId::FileExportRaster: {
        const std::string id(actions::ActionId::kFileExportRaster);
        auto& dispatcher = root().window().action_dispatcher();
        const auto* action = dispatcher.get_action(id);
        if (!action || !action->handler) {
            dispatcher.register_action({id, "Xuất Raster…", "Export Raster…", "",
                [this] { export_raster_dialog(); }, nullptr});
        }
        (void)dispatcher.dispatch(id);
        return;
    }
    case MenuActionId::FileToggleGuidelines: toggle_guidelines(); return;
    case MenuActionId::FileClearGuidelines: clear_guidelines(); return;
    case MenuActionId::FileExit: DestroyWindow(hwnd_); return;
    case MenuActionId::EditUndo:
        workflow().undo();
        root().vector_controller().selection().clear();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hoàn tác" : "Undo complete";
        update_color_preview_options();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectProperties:
        open_object_properties_panel();
        return;
    case MenuActionId::EditRedo:
        workflow().redo();
        root().vector_controller().selection().clear();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã làm lại" : "Redo complete";
        update_color_preview_options();
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
        if (delete_selected_guideline()) {
            return;
        }
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
    case MenuActionId::EditSelectSameFillColor:
        if (workflow().select_same(SelectSameCriterion::FillColor)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chọn các đối tượng cùng màu tô" : "Selected objects with same fill color";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditSelectSameStrokeColor:
        if (workflow().select_same(SelectSameCriterion::StrokeColor)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chọn các đối tượng cùng màu viền" : "Selected objects with same stroke color";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditSelectSameFillAndStroke:
        if (workflow().select_same(SelectSameCriterion::FillAndStroke)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chọn các đối tượng cùng màu tô & viền" : "Selected objects with same fill & stroke";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditSelectSameAnyPaintColor:
        if (workflow().select_same(SelectSameCriterion::AnyPaintColor)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã chọn đối tượng có màu tô hoặc viền trùng khớp"
                : "Selected objects with a matching fill or stroke color";
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn một đối tượng có màu tô hoặc viền trước"
                : "Select an object with a fill or stroke color first";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditSelectSameColorSpace:
        if (workflow().select_same(SelectSameCriterion::ColorSpace)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chọn các đối tượng cùng hệ màu" : "Selected objects with same color space";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditSelectAllCmyk:
        if (workflow().select_same(SelectSameCriterion::CmykOnly)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chọn tất cả đối tượng CMYK trên trang" : "Selected all CMYK objects on page";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::EditSelectAllRgb:
        if (workflow().select_same(SelectSameCriterion::RgbOnly)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chọn tất cả đối tượng RGB trên trang" : "Selected all RGB objects on page";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ColorConvertSelectionToCmyk:
        if (workflow().convert_selected_objects_color_mode(
                color::DocumentColorMode::Cmyk, effective_color_preview_owner())) {
            const bool vi = language_ == UILanguage::Vietnamese;
            status_msg_ = (vi ? "Đã chuyển các đối tượng chọn sang CMYK · " : "Converted selected objects to CMYK · ") +
                color_conversion_summary_text(workflow().last_color_conversion_summary(), vi) +
                (vi ? " · Ctrl+Z hoàn tác" : " · Ctrl+Z to undo");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể chuyển màu vùng chọn" : "Could not convert selection colors";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ColorConvertSelectionToRgb:
        if (workflow().convert_selected_objects_color_mode(
                color::DocumentColorMode::Rgb, effective_color_preview_owner())) {
            const bool vi = language_ == UILanguage::Vietnamese;
            status_msg_ = (vi ? "Đã chuyển các đối tượng chọn sang RGB · " : "Converted selected objects to RGB · ") +
                color_conversion_summary_text(workflow().last_color_conversion_summary(), vi) +
                (vi ? " · Ctrl+Z hoàn tác" : " · Ctrl+Z to undo");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể chuyển màu vùng chọn" : "Could not convert selection colors";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ColorAssignSelectionProfile:
        (void)root().window().action_dispatcher().dispatch(
            actions::ActionId::kColorAssignSelectionProfile);
        return;
    case MenuActionId::ColorConvertSelectionProfile:
        (void)root().window().action_dispatcher().dispatch(
            actions::ActionId::kColorConvertSelectionProfile);
        return;
    case MenuActionId::ColorConvertActivePageToCmyk:
        if (workflow().convert_active_page_color_mode(
                color::DocumentColorMode::Cmyk, effective_color_preview_owner())) {
            update_color_preview_options();
            const bool vi = language_ == UILanguage::Vietnamese;
            status_msg_ = (vi ? "Đã chuyển trang hiện tại sang CMYK · " : "Converted active page to CMYK · ") +
                color_conversion_summary_text(workflow().last_color_conversion_summary(), vi) +
                (vi ? " · Ctrl+Z hoàn tác" : " · Ctrl+Z to undo");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể chuyển màu trang hiện tại" : "Could not convert active page colors";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ColorConvertActivePageToRgb:
        if (workflow().convert_active_page_color_mode(
                color::DocumentColorMode::Rgb, effective_color_preview_owner())) {
            update_color_preview_options();
            const bool vi = language_ == UILanguage::Vietnamese;
            status_msg_ = (vi ? "Đã chuyển trang hiện tại sang RGB · " : "Converted active page to RGB · ") +
                color_conversion_summary_text(workflow().last_color_conversion_summary(), vi) +
                (vi ? " · Ctrl+Z hoàn tác" : " · Ctrl+Z to undo");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể chuyển màu trang hiện tại" : "Could not convert active page colors";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ColorConvertActivePageProfile:
        (void)root().window().action_dispatcher().dispatch(actions::ActionId::kColorConvertProfile);
        return;
    case MenuActionId::ColorConvertDocumentProfile:
        (void)root().window().action_dispatcher().dispatch(
            actions::ActionId::kColorConvertDocumentProfile);
        return;
    case MenuActionId::ColorConvertDocumentToCmyk:
        if (workflow().convert_document_color_mode(color::DocumentColorMode::Cmyk, color_preview_)) {
            update_color_preview_options();
            const bool vi = language_ == UILanguage::Vietnamese;
            status_msg_ = (vi ? "Đã chuyển toàn bộ tài liệu sang CMYK · " : "Converted document to CMYK · ") +
                color_conversion_summary_text(workflow().last_color_conversion_summary(), vi) +
                (vi ? " · Ctrl+Z hoàn tác" : " · Ctrl+Z to undo");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể chuyển màu toàn tài liệu" : "Could not convert document colors";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ColorConvertDocumentToRgb:
        if (workflow().convert_document_color_mode(color::DocumentColorMode::Rgb, color_preview_)) {
            update_color_preview_options();
            const bool vi = language_ == UILanguage::Vietnamese;
            status_msg_ = (vi ? "Đã chuyển toàn bộ tài liệu sang RGB · " : "Converted document to RGB · ") +
                color_conversion_summary_text(workflow().last_color_conversion_summary(), vi) +
                (vi ? " · Ctrl+Z hoàn tác" : " · Ctrl+Z to undo");
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể chuyển màu toàn tài liệu" : "Could not convert document colors";
        }
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
    case MenuActionId::ViewProofColors: {
        proof_colors_enabled_ = !proof_colors_enabled_;
        update_color_preview_options();
        const auto* preview = effective_color_preview();
        if (proof_colors_enabled_ && (!preview || !preview->has_cmyk_transform())) {
            proof_colors_enabled_ = false;
            update_color_preview_options();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Proof preview cần profile CMYK ICC khả dụng; hiện tại màu vẫn dùng preview gần đúng."
                : "Proof preview needs an available CMYK ICC profile; colors currently use approximate preview.";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (proof_colors_enabled_ ? "Proof Colors: BẬT · ICC preview trên máy" : "Proof Colors: TẮT")
            : (proof_colors_enabled_ ? "Proof Colors: ON · machine ICC preview" : "Proof Colors: OFF");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ViewGamutWarning: {
        gamut_warning_enabled_ = !gamut_warning_enabled_;
        update_color_preview_options();
        const auto* preview = effective_color_preview();
        if (gamut_warning_enabled_ && (!preview || !preview->has_cmyk_transform())) {
            gamut_warning_enabled_ = false;
            update_color_preview_options();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Cảnh báo sai khác RGB cần profile CMYK ICC khả dụng; chưa có kết quả đo."
                : "RGB difference warning needs an available CMYK ICC profile; no measurement is available.";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (gamut_warning_enabled_ ? "Cảnh báo sai khác RGB sau proof: BẬT (gần đúng, không phải Delta E)" : "Cảnh báo sai khác RGB: TẮT")
            : (gamut_warning_enabled_ ? "Proof RGB difference warning: ON (approximate, not Delta E)" : "Proof RGB difference warning: OFF");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ViewOverprintPreview:
        overprint_preview_enabled_ = false;
        update_color_preview_options();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Overprint Preview chưa khả dụng: renderer chưa ghép các kênh mực; dữ liệu overprint vẫn được giữ."
            : "Overprint Preview is unavailable: the renderer does not composite ink separations; overprint data is preserved.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::LayoutOrientationLandscape:
        if (active_tab().page_width_mm < active_tab().page_height_mm) toggle_page_orientation();
        return;
    case MenuActionId::LayoutOrientationPortrait:
        if (active_tab().page_width_mm > active_tab().page_height_mm) toggle_page_orientation();
        return;
    case MenuActionId::LayoutPagePrevious:
    case MenuActionId::LayoutPageNext: {
        const auto snapshot = root().document().current_snapshot();
        if (!snapshot) return;
        const auto& pages = snapshot->pages();
        const auto found = std::find_if(pages.begin(), pages.end(), [&](const auto& page) {
            return page.id == snapshot->active_page_id();
        });
        if (found == pages.end()) return;
        const auto index = static_cast<size_t>(std::distance(pages.begin(), found));
        if (action == MenuActionId::LayoutPagePrevious && index > 0) {
            if (active_num_field_ != NumericFieldId::None) cancel_numeric_field();
            (void)root().document().set_active_page(pages[index - 1].id);
        } else if (action == MenuActionId::LayoutPageNext && index + 1 < pages.size()) {
            if (active_num_field_ != NumericFieldId::None) cancel_numeric_field();
            (void)root().document().set_active_page(pages[index + 1].id);
        } else return;
        finish_page_action(language_ == UILanguage::Vietnamese ? "Đã chuyển trang" : "Page changed");
        return;
    }
    case MenuActionId::LayoutPageAdd: {
        if (active_num_field_ != NumericFieldId::None) cancel_numeric_field();
        auto& doc = root().document();
        const auto name = "Page " + std::to_string(doc.pages().size() + 1);
        auto page_layout = document::Page::make_default();
        page_layout.display_length_unit = doc.page().display_length_unit;
        page_layout.display_length_unit_explicit = doc.page().display_length_unit_explicit;
        const auto result = doc.execute_command(std::make_unique<document::AddPageCommand>(
            document::make_document_page(name, std::move(page_layout))));
        if (!result.success) status_msg_ = result.error_message;
        else finish_page_action(language_ == UILanguage::Vietnamese ? "Đã thêm trang" : "Page added");
        return;
    }
    case MenuActionId::LayoutPageDuplicate: {
        if (active_num_field_ != NumericFieldId::None) cancel_numeric_field();
        auto& doc = root().document();
        const auto result = doc.execute_command(std::make_unique<document::DuplicatePageCommand>(
            doc.active_page_id()));
        if (!result.success) status_msg_ = result.error_message;
        else finish_page_action(language_ == UILanguage::Vietnamese ? "Đã nhân đôi trang" : "Page duplicated");
        return;
    }
    case MenuActionId::LayoutPageDelete: {
        if (active_num_field_ != NumericFieldId::None) cancel_numeric_field();
        auto& doc = root().document();
        const auto result = doc.execute_command(std::make_unique<document::RemovePageCommand>(
            doc.active_page_id()));
        if (!result.success) status_msg_ = result.error_message;
        else finish_page_action(language_ == UILanguage::Vietnamese ? "Đã xóa trang · Ctrl+Z để hoàn tác"
                                                                    : "Page deleted · Ctrl+Z to undo");
        return;
    }
    case MenuActionId::LayoutPageA4: select_page_preset(0); return;
    case MenuActionId::LayoutPageA3: select_page_preset(1); return;
    case MenuActionId::LayoutPageBanner: select_page_preset(6); return;
    case MenuActionId::LayoutUnitMillimeter:
    case MenuActionId::LayoutUnitCentimeter:
    case MenuActionId::LayoutUnitMeter:
    case MenuActionId::LayoutUnitInch:
    case MenuActionId::LayoutUnitFoot:
    case MenuActionId::LayoutUnitYard:
    case MenuActionId::LayoutUnitPoint:
    case MenuActionId::LayoutUnitPica: {
        const auto id = action == MenuActionId::LayoutUnitMillimeter ? actions::ActionId::kPageUnitsMillimeter
            : action == MenuActionId::LayoutUnitCentimeter ? actions::ActionId::kPageUnitsCentimeter
            : action == MenuActionId::LayoutUnitMeter ? actions::ActionId::kPageUnitsMeter
            : action == MenuActionId::LayoutUnitInch ? actions::ActionId::kPageUnitsInch
            : action == MenuActionId::LayoutUnitFoot ? actions::ActionId::kPageUnitsFoot
            : action == MenuActionId::LayoutUnitYard ? actions::ActionId::kPageUnitsYard
            : action == MenuActionId::LayoutUnitPoint ? actions::ActionId::kPageUnitsPoint
            : actions::ActionId::kPageUnitsPica;
        (void)root().window().action_dispatcher().dispatch(id);
        return;
    }
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
    case MenuActionId::ObjectPowerClipPlaceInside:
        begin_powerclip_target_pick(); return;
    case MenuActionId::ObjectPowerClipMake: {
        auto gid = workflow().make_clipping_mask_selected(true);
        if (gid != document::kInvalidObjectId) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã tạo PowerClip / Mặt nạ cắt (Ctrl+7)"
                                                              : "Created PowerClip / Clipping Mask (Ctrl+7)";
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Chọn ít nhất 2 đối tượng để tạo PowerClip"
                                                              : "Select at least 2 objects to make Clipping Mask";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ObjectPowerClipRelease: {
        if (workflow().release_clipping_mask_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy mặt nạ cắt (Release)"
                                                              : "Released Clipping Mask";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ObjectPowerClipFitCenter:
        if (selected_items.size() == 1) workflow().fit_powerclip_contents(selected_items.front(), document::ClipContentFitMode::CenterContents);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectPowerClipFitProp:
        if (selected_items.size() == 1) workflow().fit_powerclip_contents(selected_items.front(), document::ClipContentFitMode::FitProportionally);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectPowerClipFillProp:
        if (selected_items.size() == 1) workflow().fit_powerclip_contents(selected_items.front(), document::ClipContentFitMode::FillProportionally);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectPowerClipStretch:
        if (selected_items.size() == 1) workflow().fit_powerclip_contents(selected_items.front(), document::ClipContentFitMode::StretchToFill);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectPowerClipSelectContents:
        if (selected_items.size() == 1) select_powerclip_contents(selected_items.front());
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
    case MenuActionId::ObjectOrderToFront:
        reorder_selected_objects(*this, document::ReorderObjectInLayerCommand::Step::BringToFront);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chuyển lên trên cùng trang (To Front)" : "Brought to Front";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectOrderToBack:
        reorder_selected_objects(*this, document::ReorderObjectInLayerCommand::Step::SendToBack);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chuyển xuống dưới cùng trang (To Back)" : "Sent to Back";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectOrderForwardOne:
        reorder_selected_objects(*this, document::ReorderObjectInLayerCommand::Step::BringForward);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chuyển lên một lớp (Forward One)" : "Brought Forward";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectOrderBackwardOne:
        reorder_selected_objects(*this, document::ReorderObjectInLayerCommand::Step::SendBackward);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã chuyển xuống một lớp (Backward One)" : "Sent Backward";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case MenuActionId::ObjectOrderReverse: {
        const auto sel = root().vector_controller().selection().selected_ids();
        if (sel.size() >= 2) {
            auto reversed_sel = sel;
            std::reverse(reversed_sel.begin(), reversed_sel.end());
            for (auto id : reversed_sel) {
                root().document().execute_command(std::make_unique<document::ReorderObjectInLayerCommand>(
                    id, document::ReorderObjectInLayerCommand::Step::BringToFront));
            }
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đảo ngược thứ tự các đối tượng" : "Reversed object order";
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    case MenuActionId::ObjectLock: {
        const auto sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            root().document().execute_command(std::make_unique<document::SetObjectLockedCommand>(id, true));
        }
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã khóa đối tượng" : "Locked object(s)";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ObjectUnlock: {
        const auto sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            root().document().execute_command(std::make_unique<document::SetObjectLockedCommand>(id, false));
        }
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã mở khóa đối tượng" : "Unlocked object(s)";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ObjectHide: {
        const auto sel = root().vector_controller().selection().selected_ids();
        for (auto id : sel) {
            root().document().execute_command(std::make_unique<document::SetObjectVisibilityCommand>(id, false));
        }
        root().vector_controller().selection().clear();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã ẩn đối tượng" : "Hidden object(s)";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ObjectIsolationEnter: {
        const auto sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1) {
            enter_group_isolation(sel.front());
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    case MenuActionId::ObjectIsolationExit: {
        exit_group_isolation();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::ObjectPowerClipToggleLock: {
        const auto sel = root().vector_controller().selection().selected_ids();
        if (sel.size() == 1) {
            const auto snap = root().document().current_snapshot();
            if (auto obj = snap->objects().get(sel.front())) {
                if (const auto* grp = std::get_if<document::GroupObject>(&(*obj))) {
                    if (grp->clip.has_value()) {
                        workflow().set_powerclip_lock_contents(sel.front(), !grp->clip->lock_contents_to_frame);
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                }
            }
        }
        return;
    }
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
    case MenuActionId::EffectsBreakContourApart: {
        trigger_break_contour_apart();
        return;
    }
    case MenuActionId::EffectsClear: {
        ribbon_drop_shadow_mode_ = false;
        const bool cleared = workflow().clear_selected_effects();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (cleared ? "Đã xóa toàn bộ node hiệu ứng · Opacity và Blend được giữ · Ctrl+Z hoàn tác"
                       : "Không thể xóa hiệu ứng cho vùng chọn hiện tại")
            : (cleared ? "Cleared all effect nodes · Opacity and Blend were preserved · Ctrl+Z undoes"
                       : "Could not clear effects for the current selection");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case MenuActionId::TextEdit: {
        workflow().cancel_effect_draft();
        const auto selected = root().vector_controller().selection().selected_ids();
        if (selected.size() == 1) {
            const auto object = root().document().current_snapshot()->objects().get(selected.front());
            if (object && std::holds_alternative<document::TextObject>(*object)) {
                start_inline_text_editing(std::get<document::TextObject>(*object).position, selected.front());
                return;
            }
        }
        set_active_tool(4);
        return;
    }
    case MenuActionId::TextFont: open_font_picker(200, 100); return;
    case MenuActionId::TextBold: toggle_font_bold(); return;
    case MenuActionId::TextItalic: toggle_font_italic(); return;
    case MenuActionId::TextUnderline: toggle_font_underline(); return;
    case MenuActionId::TextStrikethrough: toggle_font_strikethrough(); return;
    case MenuActionId::TextAlignLeft: set_font_alignment(typography::TextAlignment::Left); return;
    case MenuActionId::TextAlignCenter: set_font_alignment(typography::TextAlignment::Center); return;
    case MenuActionId::TextAlignRight: set_font_alignment(typography::TextAlignment::Right); return;
    case MenuActionId::TextAlignJustify: set_font_alignment(typography::TextAlignment::Justify); return;
    case MenuActionId::TextChangeCase: cycle_change_case(); return;
    case MenuActionId::TextCaseSentence: apply_change_case(typography::ChangeCaseMode::SentenceCase); return;
    case MenuActionId::TextCaseLower: apply_change_case(typography::ChangeCaseMode::Lowercase); return;
    case MenuActionId::TextCaseUpper: apply_change_case(typography::ChangeCaseMode::Uppercase); return;
    case MenuActionId::TextCaseTitle: apply_change_case(typography::ChangeCaseMode::TitleCase); return;
    case MenuActionId::TextCaseToggle: apply_change_case(typography::ChangeCaseMode::ToggleCase); return;
    case MenuActionId::TextBullets: toggle_text_bullets(); return;
    case MenuActionId::TextNumbering: toggle_text_numbering(); return;
    case MenuActionId::TextNonPrinting: toggle_non_printing_characters(); return;
    case MenuActionId::TextConvertArtisticParagraph: toggle_convert_artistic_paragraph(); return;
    case MenuActionId::TextDocker: open_inspector_properties_panel(); return;
    case MenuActionId::TextConvertToCurves: convert_selected_text_to_curves(); return;
    case MenuActionId::TextLinkFrames: link_selected_text_frames(); return;
    case MenuActionId::TextUnlinkFrames: unlink_selected_text_frames(); return;
    case MenuActionId::TextCombine: combine_selected_text_objects(); return;
    case MenuActionId::TextSplitLines: split_selected_text_lines(); return;
    case MenuActionId::TextFitToPath: fit_text_to_path(); return;
    case MenuActionId::TextStraighten: straighten_text_from_path(); return;
    case MenuActionId::TextReversePath: reverse_text_on_path(); return;
    case MenuActionId::ToolPick: workflow().cancel_effect_draft(); set_active_tool(0); return;
    case MenuActionId::ToolNode: workflow().cancel_effect_draft(); set_active_tool(11); return;
    case MenuActionId::ToolRectangle: workflow().cancel_effect_draft(); set_active_tool(1); return;
    case MenuActionId::ToolEllipse: workflow().cancel_effect_draft(); set_active_tool(2); return;
    case MenuActionId::ToolPolygon: workflow().cancel_effect_draft(); set_active_tool(9); return;
    case MenuActionId::ToolText: workflow().cancel_effect_draft(); set_active_tool(4); return;
    case MenuActionId::ToolKnife: workflow().cancel_effect_draft(); set_active_tool(5); return;
    case MenuActionId::ToolLine: workflow().cancel_effect_draft(); set_active_tool(6); return;
    case MenuActionId::ToolPolyline: workflow().cancel_effect_draft(); set_active_tool(7); return;
    case MenuActionId::ToolPen: workflow().cancel_effect_draft(); set_active_tool(8); return;
    case MenuActionId::ToolGradient: workflow().cancel_effect_draft(); set_active_tool(13); return;
    case MenuActionId::ToolCustomizeToolbox: toggle_toolbox_customize_flyout(); return;
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
        trigger_effects_contour();
        return;
    }
    if (item == 4) {
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
        const char *labels_en[3][5] = {
            {"Rectangle", "Ellipse", "Polygon", "", ""},
            {"Line", "Polyline", "Pen", "Node", ""},
            {"Appearance Properties", "Gaussian Blur", "Drop Shadow", "Contour", "Gradient"}};
        const char *labels_vi[3][5] = {
            {"Hình chữ nhật", "Hình elip", "Đa giác", "", ""},
            {"Đoạn thẳng", "Đường gấp khúc", "Pen", "Node", ""},
            {"Thuộc tính diện mạo", "Gaussian Blur", "Drop Shadow", "Viền Contour", "Chuyển sắc"}};
        const int group_index = std::clamp(toolbox_focus_group_ - 1, 0, 2);
        const int max_items = toolbox_focus_group_ == 3 ? 5
            : toolbox_focus_group_ == 2 ? 4 : 3;
        const int item_index = std::clamp(toolbox_flyout_index_, 0, max_items - 1);
        if (toolbox_focus_group_ == 3 && item_index == 4) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Sửa màu chuyển sắc bằng tay nắm trên đối tượng · Enter chọn · Esc đóng"
                : "Edit a path fill using gradient handles · Enter choose · Esc closes";
        } else if (toolbox_focus_group_ == 3 && item_index == 3) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Tạo viền Contour quanh đối tượng · Enter chọn · Esc đóng"
                : "Create contour outline around object · Enter choose · Esc closes";
        } else if (toolbox_focus_group_ == 3 && (item_index == 1 || item_index == 2)) {
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
        } else if (toolbox_focus_group_ == 2 && item_index == 3) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Node · nhấp chọn, kéo để di chuyển · tọa độ X/Y trong Properties"
                : "Node · click to select, drag to move · edit X/Y in Properties";
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
                : (last == 6 ? 0 : last == 7 ? 1 : last == 8 ? 2 : 3);
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
            const int max_items = toolbox_open_group_ == 3 ? 5
                : toolbox_open_group_ == 2 ? 4 : 3;
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
                : (index == 0 ? 6 : index == 1 ? 7 : index == 2 ? 8 : 11);
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
            geometry::AlignMode::Left, geometry::AlignMode::CenterHorizontal, geometry::AlignMode::Right,
            geometry::AlignMode::Top, geometry::AlignMode::CenterVertical, geometry::AlignMode::Bottom
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
            geometry::DistributeMode::SpacingHorizontal, geometry::DistributeMode::CentersHorizontal,
            geometry::DistributeMode::LeftEdges, geometry::DistributeMode::RightEdges
        };
        run_distribute(modes, Metrics::horizontal_top);
        return true;
    }
    const bool vertical_first_row_hit = y >= Metrics::vertical_top && y < Metrics::vertical_top + Metrics::distribute_button_height;
    const bool vertical_second_row_hit = y >= vertical_second_row && y < vertical_second_row + Metrics::distribute_button_height;
    if (vertical_first_row_hit || vertical_second_row_hit) {
        constexpr geometry::DistributeMode modes[] = {
            geometry::DistributeMode::SpacingVertical, geometry::DistributeMode::CentersVertical,
            geometry::DistributeMode::TopEdges, geometry::DistributeMode::BottomEdges
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
#endif
} // namespace pikdraw::desktop
