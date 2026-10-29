#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/cut_shape_tool_catalog.hpp"
#include "native_window_toolbox.hpp"
#include "native_window_text_properties.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_page_navigation.hpp"
#include "native_window_raster_dock_metrics.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"
#include "native_window_drop_shadow_paint.hpp"

#include <algorithm>
#include <array>
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
void show_spot_palette_picker(NativeWindow& window, bool target_stroke);
#endif

#ifdef _WIN32

bool NativeWindow::handle_workspace_chrome_mouse_down(int x, int y, unsigned modifiers) {
    int w = width_;
    int h = height_;
    if (hwnd_) {
        RECT client_rc;
        if (GetClientRect(hwnd_, &client_rc)) {
            w = logical_pixel(client_rc.right - client_rc.left);
            h = logical_pixel(client_rc.bottom - client_rc.top);
        }
    }
    (void)h;

    if (handle_snap_toolbar_click(x, y)) return true;
    if (active_tool_ == 14 && y >= 66 && y <= 102) {
        (void)handle_artboard_property_click(x, y);
        return true;
    }
    if (active_tool_ == 14 && x >= w - 280 && y >= 138) {
        (void)handle_artboard_property_click(x, y);
        return true;
    }
    if ((active_tool_ == 5 || active_tool_ == 16 || active_tool_ == 20) && active_dock_tab_ == 0 &&
        x >= w - inspector_width() && y >= 138 && handle_cut_tool_properties_click(x, y, true))
        return true;
    if (active_tool_ == 21 && active_dock_tab_ == 0 && x >= w - inspector_width() && y >= 138) {
        (void)handle_cut_tool_properties_click(x, y, true);
        return true;
    }
    if ((active_tool_ == 22 || active_tool_ == 23 || active_tool_ == 24) && active_dock_tab_ == 0 &&
        x >= w - inspector_width() && y >= 138) {
        (void)handle_cut_tool_properties_click(x, y, true);
        return true;
    }
    if (active_dock_tab_ == 0 && x >= w - 280 && y >= 138) {
        if (handle_raster_dock_interactive_mouse_down(x, y)) {
            return true;
        }
    }
    if (show_snap_properties_ && active_dock_tab_ == 0 && x >= w - 280 && y >= 138) {
        (void)handle_snap_properties_click(x, y);
        return true;
    }
    if (show_snap_to_popup_) {
        show_snap_to_popup_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    if (show_page_units_flyout_) {
        const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
        const int menu_top = AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height +
            PageUnitPropertiesMetrics::row_top_in_content + PageUnitPropertiesMetrics::row_height +
            PageUnitPropertiesMetrics::menu_gap;
        const int menu_right = panel_left + PageUnitPropertiesMetrics::menu_width;
        const int menu_bottom = menu_top + 8 * PageUnitPropertiesMetrics::item_height;
        const int row_top = AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height +
            PageUnitPropertiesMetrics::row_top_in_content;
        const bool on_unit_row = x >= panel_left && x <= menu_right &&
            y >= row_top && y < row_top + PageUnitPropertiesMetrics::row_height;
        if (x >= panel_left && x <= menu_right && y >= menu_top && y < menu_bottom) {
            constexpr MenuActionId unit_actions[] = {
                MenuActionId::LayoutUnitMillimeter, MenuActionId::LayoutUnitCentimeter,
                MenuActionId::LayoutUnitMeter, MenuActionId::LayoutUnitInch,
                MenuActionId::LayoutUnitFoot, MenuActionId::LayoutUnitYard,
                MenuActionId::LayoutUnitPoint, MenuActionId::LayoutUnitPica};
            const int index = (y - menu_top) / PageUnitPropertiesMetrics::item_height;
            show_page_units_flyout_ = false;
            execute_menu_action(static_cast<int>(unit_actions[index]));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (!on_unit_row) {
            show_page_units_flyout_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

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
                    return true;
                }
                switch_to_tab(i);
                return true;
            }
            cur_x += tab_w + 4;
        }
        if (x >= cur_x && x <= cur_x + 28) {
            open_new_doc_modal();
            return true;
        }
        return true;
    }

    // 3b. Context Tool Ribbon clicks (y: 66 to 102)
    if (y >= 66 && y <= 102) {
        if (active_tool_ == 19) {
            if (x >= 160 && x <= 286) {
                active_dock_tab_ = 0;
                properties_dock_scroll_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã mở Hướng dẫn Scissors"
                    : "Opened Scissors Properties";
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (active_tool_ == 21) {
            active_dock_tab_ = 0;
            properties_dock_scroll_ = 0;
            show_style_inspector_ = false;
            show_align_panel_ = false;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã mở Thuộc tính Smart Trim"
                : "Opened Smart Trim Properties";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (active_tool_ == 22 || active_tool_ == 23) {
            if (active_tool_ == 22 && x >= 110 && x < 222) {
                commit_shape_builder(false);
            } else if (active_tool_ == 22 && x >= 228 && x < 336) {
                commit_shape_builder(true);
            } else if (active_tool_ == 22 && x >= 342 && x < 430) {
                reset_shape_builder();
            } else if (x >= 436 && x <= 552) {
                active_dock_tab_ = 0;
                properties_dock_scroll_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? (active_tool_ == 22 ? "Đã mở Thuộc tính Shape Builder" : "Đã mở Thuộc tính Smart Fill")
                    : (active_tool_ == 22 ? "Opened Shape Builder Properties" : "Opened Smart Fill Properties");
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (active_tool_ == 24) {
            if (x >= 130 && x < 252) {
                constexpr std::array<double, 6> diameters{4.0, 8.0, 12.0, 24.0, 48.0, 96.0};
                const auto current = std::find_if(diameters.begin(), diameters.end(), [&](double value) {
                    return std::abs(value - raster_eraser_diameter_px_) < 0.01;
                });
                raster_eraser_diameter_px_ = current == diameters.end() || std::next(current) == diameters.end()
                    ? diameters.front() : *std::next(current);
                status_msg_ = (language_ == UILanguage::Vietnamese
                    ? "Đường kính tẩy ảnh: " : "Raster eraser diameter: ") +
                    std::to_string(static_cast<int>(std::lround(raster_eraser_diameter_px_))) + " px";
            } else if (x >= 260 && x < 382) {
                active_dock_tab_ = 0;
                properties_dock_scroll_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã mở Thuộc tính Tẩy ảnh" : "Opened Raster Eraser Properties";
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (active_tool_ == 20) {
            if (x >= 110 && x < 222) {
                discard_pending_cut_geometry_proposal();
                trim_objects_keep_sources_ = !trim_objects_keep_sources_;
            } else if (x >= 228 && x < 336) {
                commit_trim_objects();
            } else if (x >= 342 && x < 430) {
                reset_trim_objects_roles();
            } else if (x >= 436 && x < 548) {
                active_dock_tab_ = 0;
                properties_dock_scroll_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã mở Thuộc tính Cắt bằng đối tượng"
                    : "Opened Trim Objects Properties";
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (active_tool_ == 5) {
            if (x >= 75 && x <= 147) {
                if (active_num_field_ != NumericFieldId::None) {
                    commit_numeric_field();
                    if (active_num_field_ != NumericFieldId::None) return true;
                }
                knife_freehand_mode_ = false;
            } else if (x >= 151 && x <= 239) {
                if (active_num_field_ != NumericFieldId::None) {
                    commit_numeric_field();
                    if (active_num_field_ != NumericFieldId::None) return true;
                }
                knife_freehand_mode_ = true;
            } else if (x >= 245 && x <= 365) {
                start_numeric_input(NumericFieldId::KnifeGap);
            } else if (x >= 372 && x <= 484) {
                active_dock_tab_ = 0;
                properties_dock_scroll_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã mở Thuộc tính Knife"
                    : "Opened Knife Properties";
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (active_tool_ == 16) {
            if (x >= 112 && x <= 233) {
                start_numeric_input(NumericFieldId::VectorEraserDiameter);
            } else if (x >= 240 && x <= 350) {
                if (active_num_field_ != NumericFieldId::None) {
                    commit_numeric_field();
                    if (active_num_field_ != NumericFieldId::None) return true;
                }
                vector_eraser_round_nib_ = !vector_eraser_round_nib_;
            } else if (x >= 357 && x <= 469) {
                active_dock_tab_ = 0;
                properties_dock_scroll_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã mở Thuộc tính Tẩy vector"
                    : "Opened Vector Eraser Properties";
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (has_annotation_properties_target() && handle_annotation_property_bar_click(x, y)) return true;
        if (active_tool_ == 0 && handle_powerclip_controls(x,y,10,70,
            w - AlignPanelMetrics::dock_width - 10,false)) return true;
        if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
            if (handle_node_property_click(x, y))
                return true;
            return true;
        }
        if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
            if (handle_contour_property_click(x, y))
                return true;
            return true;
        }
        if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) {
            if (handle_gradient_property_click(x, y))
                return true;
            return true;
        }
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
                return true;
            }
            if (x >= 192 && x <= 315) {
                if (active_tool_ == 8 && pen_tool_->anchor_count() == 0) {
                    (void)pen_tool_->pointer_down(pen_last_pointer_world_);
                    (void)pen_tool_->pointer_up();
                } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_->path_point_count() == 0) {
                    (void)drawing_tool_->confirm_candidate();
                }
                start_numeric_input(NumericFieldId::PenAngle);
                return true;
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
                return true;
            }
            if (x >= 422 && x <= 490) {
                if (active_tool_ == 8) finish_pen_session(false, true);
                else {
                    drawing_tool_->cancel();
                    finish_drawing_tool(true);
                }
                return true;
            }
            if (x >= 495 && x <= 605) {
                active_dock_tab_ = 0;
                show_style_inspector_ = true;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Bảng kiểu dáng: Tùy chỉnh nét vẽ, màu sắc và thuộc tính đường vẽ"
                    : "Style panel: Stroke width, cap, join and color settings";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            return true;
        } else if (active_tool_ == 1 && selection.empty()) {
            int rx = 78;
            if (x >= rx && x < rx + 28) { set_creation_rect_corner_type(vector::RectCornerType::Round); return true; }
            if (x >= rx + 32 && x < rx + 60) { set_creation_rect_corner_type(vector::RectCornerType::Scallop); return true; }
            if (x >= rx + 64 && x < rx + 92) { set_creation_rect_corner_type(vector::RectCornerType::Chamfer); return true; }
            rx += 98;
            if (x >= rx && x <= rx + 80) {
                start_numeric_input(NumericFieldId::RectCornerRadiusTL);
                return true;
            }
            rx += 86;
            if (x >= rx && x <= rx + 24) { toggle_creation_rect_corners_locked(); return true; }
            rx += 30;
            if (x >= rx && x <= rx + 62) { toggle_creation_rect_corners_scale_relative(); return true; }
            rx += 68;
            if (x >= rx && x <= rx + 110) {
                active_dock_tab_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã mở Bảng Thuộc tính chi tiết Hình chữ nhật"
                    : "Opened Rectangle Properties inspector";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            commit_numeric_field();
            return true;
        } else if (active_tool_ == 9) {
            if (x >= 58 && x <= 164) {
                start_numeric_input(NumericFieldId::PolygonSides);
                return true;
            }
            if (x >= 172 && x <= 280) {
                active_dock_tab_ = 0;
                show_style_inspector_ = false;
                show_align_panel_ = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã mở Bảng Thuộc tính chi tiết Đa giác"
                    : "Opened Polygon Properties inspector";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            commit_numeric_field();
            return true;
        } else if (active_tool_ == 15) {
            if (handle_eyedropper_property_bar_click(x, y)) {
                return true;
            }
        } else if (active_tool_ == 10 || is_dimension_object_selected()) {
            if (handle_dimension_property_bar_click(x, y)) {
                return true;
            }
        } else if (is_text_mode) {
            // CONTEXTUAL TEXT PROPERTY BAR CLICKS (CorelDRAW / Word Reference)
            if (x >= 52 && x <= 192) {
                open_font_picker(52, 96);
                return true;
            }
            if (x >= 220 && x <= 240) { adjust_font_size(-2.0); return true; }
            if (x >= 242 && x <= 292) {
                start_numeric_input(text_numeric_field(TextPropertyField::FontSize));
                return true;
            }
            if (x >= 294 && x <= 314) { adjust_font_size(2.0); return true; }
            if (x >= 322 && x <= 350) { toggle_font_bold(); return true; }
            if (x >= 354 && x <= 382) { toggle_font_italic(); return true; }
            if (x >= 386 && x <= 414) { toggle_font_underline(); return true; }
            if (x >= 422 && x <= 462) { set_font_alignment(typography::TextAlignment::Left); return true; }
            if (x >= 466 && x <= 506) { set_font_alignment(typography::TextAlignment::Center); return true; }
            if (x >= 510 && x <= 550) {
                const auto text_view = text_properties_view(*this);
                const auto align = text_view.context ? text_view.alignment : font_alignment_;
                if (text_view.mixed_alignment) {
                    set_font_alignment(typography::TextAlignment::Right);
                } else if (align == typography::TextAlignment::Right) {
                    set_font_alignment(typography::TextAlignment::Justify);
                } else {
                    set_font_alignment(typography::TextAlignment::Right);
                }
                return true;
            }
            if (x >= 556 && x <= 616) { convert_selected_text_to_curves(); return true; }
            if (x >= 622 && x <= 692) { insert_table(table_rows_, table_cols_); return true; }
            if (x >= 698 && x <= 728) { toggle_font_strikethrough(); return true; }
            if (x >= 732 && x <= 762) { toggle_text_bullets(); return true; }
            if (x >= 766 && x <= 796) { toggle_text_numbering(); return true; }
            if (x >= 800 && x <= 826) { toggle_non_printing_characters(); return true; }
            if (x >= 830 && x <= 864) { toggle_convert_artistic_paragraph(); return true; }
            if (!selection.empty() && x >= 868 && x <= 914) {
                workflow().delete_selected();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                x >= 920 && x <= 1044 && x + 124 <= w - 280) {
                start_numeric_input(NumericFieldId::ObjectOpacity);
                return true;
            }
            commit_numeric_field();
            return true;
        } else if (selection.empty()) {
            // PAGE MODE (Direct Numeric Input + Compact Preset Combobox)
            const auto snapshot = root().document().current_snapshot();
            const double pw = snapshot->page().width_mm;
            const double ph = snapshot->page().height_mm;
            if (x >= 52 && x <= 165) {
                commit_numeric_field();
                toggle_page_preset_dropdown();
                return true;
            }
            if (x >= 175 && x <= 275) { close_page_preset_dropdown(); start_numeric_input(NumericFieldId::PageWidth); return true; } // Page W
            if (x >= 283 && x <= 383) { close_page_preset_dropdown(); start_numeric_input(NumericFieldId::PageHeight); return true; } // Page H
            if (x >= 391 && x <= 455) { close_page_preset_dropdown(); commit_numeric_field(); if (pw > ph) toggle_page_orientation(); return true; } // Dọc
            if (x >= 461 && x <= 525) { close_page_preset_dropdown(); commit_numeric_field(); if (ph > pw) toggle_page_orientation(); return true; } // Ngang
            close_page_preset_dropdown();
            commit_numeric_field();
        } else {
            size_t sidx = 0;
            document::DropShadowEffect sval;
            const bool has_shadow = selection.size() == 1 &&
                DropShadowRibbon::get_shadow(*this, sidx, sval);
            if (ribbon_drop_shadow_mode_ && has_shadow) {
                if (DropShadowRibbon::handle_click(*this, x, y, w, 280))
                    return true;
            }
            // OBJECT MODE (Direct Numeric Input)
            if (x >= 10 && x <= 95) { start_numeric_input(NumericFieldId::ObjectX); return true; } // X box
            if (x >= 100 && x <= 185) { start_numeric_input(NumericFieldId::ObjectY); return true; } // Y box
            if (x >= 190 && x <= 275) { start_numeric_input(NumericFieldId::ObjectWidth); return true; } // W box
            if (x >= 280 && x <= 335) { commit_numeric_field(); toggle_aspect_ratio_lock(); InvalidateRect(hwnd_, nullptr, FALSE); return true; } // Lock toggle
            if (x >= 340 && x <= 425) { start_numeric_input(NumericFieldId::ObjectHeight); return true; } // H box
            if (x >= 435 && x <= 525) { start_numeric_input(NumericFieldId::ObjectRotation); return true; } // Rotation angle box
            const bool is_rect_single = selection.size() == 1 && is_selected_rectangle();
            const auto exposure_ev = selection.size() == 1
                ? selected_bitmap_exposure_ev() : std::optional<double>{};
            if (exposure_ev && can_edit_selected_bitmap(true) &&
                x >= 720 && x <= 844 && x + 124 <= w - 280) {
                start_numeric_input(NumericFieldId::RasterExposureEv);
                return true;
            }
            if (!is_rect_single && selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                x >= 720 && x <= 844 && x + 124 <= w - 280) {
                start_numeric_input(NumericFieldId::ObjectOpacity);
                return true;
            }
            commit_numeric_field();

            // Group / Ungroup / Enter Group / Exit Group / Align Ribbon Buttons
            if (is_in_isolation_mode()) {
                if (x >= 580 && x <= 770) { exit_group_isolation(); return true; }
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
                        return true;
                    }
                } else if (selection.size() == 1) {
                    int rx = 535;
                    document::ObjectId rect_id;
                    vector::RectCornerSettings rect_s;
                    if (is_selected_rectangle(&rect_id, &rect_s)) {
                        if (x >= rx && x < rx + 28) { set_rect_corner_type(vector::RectCornerType::Round); return true; }
                        if (x >= rx + 32 && x < rx + 60) { set_rect_corner_type(vector::RectCornerType::Scallop); return true; }
                        if (x >= rx + 64 && x < rx + 92) { set_rect_corner_type(vector::RectCornerType::Chamfer); return true; }
                        rx += 98;
                        if (x >= rx && x <= rx + 75) {
                            start_numeric_input(NumericFieldId::RectCornerRadiusTL);
                            return true;
                        }
                        rx += 80;
                        if (x >= rx && x <= rx + 24) { toggle_rect_corners_locked(); return true; }
                        rx += 28;
                        if (x >= rx && x <= rx + 62) { toggle_rect_corners_scale_relative(); return true; }
                        rx += 68;
                        if (x >= rx && x <= rx + 80) {
                            active_dock_tab_ = 0;
                            show_style_inspector_ = false;
                            show_align_panel_ = false;
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Đã mở Bảng Thuộc tính chi tiết Bo góc"
                                : "Opened Rectangle Corner Properties";
                            InvalidateRect(hwnd_, nullptr, FALSE);
                            return true;
                        }
                        rx += 86;
                    }
                    if (rx + 128 <= w - 280 - 10) {
                        if (x >= rx && x < rx + 28) {
                            execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipHorizontal));
                            return true;
                        }
                        if (x >= rx + 32 && x < rx + 60) {
                            execute_menu_action(static_cast<int>(MenuActionId::ObjectFlipVertical));
                            return true;
                        }
                        if (x >= rx + 64 && x < rx + 92) {
                            execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CW));
                            return true;
                        }
                        if (x >= rx + 96 && x < rx + 124) {
                            execute_menu_action(static_cast<int>(MenuActionId::ObjectRotate90CCW));
                            return true;
                        }
                        rx += 130;
                    }
                    if (has_shadow && rx + 72 <= w - 280 - 10 && x >= rx && x < rx + 66) {
                        commit_numeric_field();
                        ribbon_drop_shadow_mode_ = true;
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Đang hiển thị thanh thuộc tính Bóng đổ"
                            : "Showing Drop Shadow properties";
                        InvalidateRect(hwnd_, nullptr, FALSE);
                        return true;
                    }
                    if (has_shadow && rx + 72 <= w - 280 - 10) rx += 72;
                    if (workflow().is_clipping_container(selection.front())) {
                        const auto clip_id = selection.front();
                        if (x >= rx && x < rx + 72) {
                            enter_group_isolation(clip_id);
                            return true;
                        }
                        rx += 76;
                        if (x >= rx && x < rx + 78) {
                            enter_group_isolation(clip_id);
                            return true;
                        }
                        rx += 82;
                        if (x >= rx && x < rx + 88) {
                            if (workflow().extract_powerclip_contents(clip_id)) {
                                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã trích xuất nội dung PowerClip" : "Extracted PowerClip Contents";
                                InvalidateRect(hwnd_, nullptr, FALSE);
                            }
                            return true;
                        }
                        rx += 92;
                        if (x >= rx && x < rx + 76) {
                            const auto snap = root().document().current_snapshot();
                            const auto* grp = std::get_if<document::GroupObject>(snap->objects().lookup_ref(clip_id));
                            const bool locked = grp && grp->clip ? grp->clip->lock_contents_to_frame : true;
                            workflow().set_powerclip_lock_contents(clip_id, !locked);
                            status_msg_ = (language_ == UILanguage::Vietnamese)
                                ? (!locked ? "Đã khóa vị trí nội dung vào khung PowerClip" : "Đã mở khóa nội dung khỏi khung PowerClip")
                                : (!locked ? "Locked contents to PowerClip frame" : "Unlocked contents from PowerClip frame");
                            InvalidateRect(hwnd_, nullptr, FALSE);
                            return true;
                        }
                        rx += 80;
                        if (x >= rx && x < rx + 82) {
                            workflow().fit_powerclip_contents(clip_id, document::ClipContentFitMode::CenterContents);
                            status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã căn giữa nội dung PowerClip" : "Centered PowerClip Contents";
                            InvalidateRect(hwnd_, nullptr, FALSE);
                            return true;
                        }
                        rx += 86;
                        if (x >= rx && x < rx + 78) {
                            POINT pt{rx, 74};
                            ClientToScreen(hwnd_, &pt);
                            HMENU hFit = CreatePopupMenu();
                            AppendMenuW(hFit, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipFitCenter),
                                        language_ == UILanguage::Vietnamese ? L"Căn giữa (Center Contents)" : L"Center Contents");
                            AppendMenuW(hFit, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipFitProp),
                                        language_ == UILanguage::Vietnamese ? L"Vừa khít giữ tỷ lệ (Fit Proportionally)" : L"Fit Proportionally");
                            AppendMenuW(hFit, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipFillProp),
                                        language_ == UILanguage::Vietnamese ? L"Phủ đầy khung (Fill Proportionally)" : L"Fill Proportionally");
                            AppendMenuW(hFit, MF_STRING, static_cast<UINT_PTR>(MenuActionId::ObjectPowerClipStretch),
                                        language_ == UILanguage::Vietnamese ? L"Kéo giãn vừa khung (Stretch to Fill)" : L"Stretch to Fill");
                            TrackPopupMenuEx(hFit, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hwnd_, nullptr);
                            DestroyMenu(hFit);
                            return true;
                        }
                    } else if (workflow().is_group(selection.front()) && rx + 144 <= w - 280 - 10) {
                        if (x >= rx && x < rx + 66) {
                            if (workflow().ungroup_selected()) {
                                status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã rã nhóm (Ungroup)" : "Ungrouped";
                                InvalidateRect(hwnd_, nullptr, FALSE);
                            }
                            return true;
                        }
                        if (x >= rx + 72 && x < rx + 138) {
                            enter_group_isolation(selection.front());
                            return true;
                        }
                    }
                }
            }

        }
        return true;
    }

    const bool leaving_pen_expression = active_num_field_ == NumericFieldId::PenExpression ||
                                        active_num_field_ == NumericFieldId::PenLength ||
                                        active_num_field_ == NumericFieldId::PenAngle;
    const bool invalid_polygon_setting = active_num_field_ == NumericFieldId::PolygonSides ||
                                         active_num_field_ == NumericFieldId::PolygonRotation;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (leaving_pen_expression && is_canvas_point(x, y))
            return true;
        if (invalid_polygon_setting && active_num_field_ != NumericFieldId::None &&
            is_canvas_point(x, y))
            return true;
    }

    // Space + left drag temporarily pans without changing the active tool.
    if (space_down_ && is_canvas_point(x, y)) {
        if (drawing_tool_ && drawing_tool_->active()) drawing_tool_->suspend_for_pan();
        if (pen_tool_ && pen_tool_->active()) (void)pen_tool_->suspend_for_pan();
        is_panning_ = true;
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        SetCapture(hwnd_);
        return true;
    }

    // 4. Quick Customize flyout handling
    if (show_toolbox_customize_flyout_) {
        const auto slots = visible_toolbox_slots();
        const int plus_y = 112 + static_cast<int>(slots.size()) * 42;
        const int flyout_left = 54;
        const int flyout_top = std::max(112, std::min(plus_y - 200, h - 26 - 445));
        const int flyout_w = 264;
        const int flyout_h = 425;
        const RECT flyout_rc = { flyout_left, flyout_top, flyout_left + flyout_w, flyout_top + flyout_h };

        if (x >= flyout_rc.left && x <= flyout_rc.right && y >= flyout_rc.top && y <= flyout_rc.bottom) {
            // Check close button 'X'
            if (x >= flyout_rc.right - 26 && x <= flyout_rc.right - 4 && y >= flyout_top + 4 && y <= flyout_top + 26) {
                close_toolbox_customize_flyout();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            // Check toolbox slot rows
            for (int i = 0; i < static_cast<int>(ToolboxSlotId::Count); ++i) {
                const int item_top = flyout_top + 43 + i * 26;
                if (y >= item_top && y < item_top + 25) {
                    const auto slot = static_cast<ToolboxSlotId>(i);
                    if (slot != ToolboxSlotId::Pick) {
                        toggle_toolbox_slot_visible(slot);
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            }
            // Check bottom buttons: Reset & Edit Mode
            const int bottom_y = flyout_top + 385;
            if (y >= bottom_y && y <= bottom_y + 26) {
                if (x >= flyout_left + 10 && x <= flyout_left + 122) {
                    reset_toolbox_slots_default();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                } else if (x >= flyout_left + 128 && x <= flyout_rc.right - 10) {
                    toggle_toolbox_edit_mode();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            }
            return true;
        }
        if (x < 6 || x > 46 || y < 112) {
            close_toolbox_customize_flyout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    // Grouped toolbox flyouts (Quick Shapes, Path Drawing, Appearance, Gradient, Eyedropper)
    if (toolbox_open_group_ > 0) {
        const ToolboxSlotId slot_for_group = toolbox_open_group_ == 1 ? ToolboxSlotId::QuickShapes
                                           : toolbox_open_group_ == 2 ? ToolboxSlotId::PathDrawing
                                           : toolbox_open_group_ == 3 ? ToolboxSlotId::Blur
                                           : toolbox_open_group_ == 4 ? ToolboxSlotId::Gradient
                                           : toolbox_open_group_ == 5 ? ToolboxSlotId::Eyedropper
                                           : toolbox_open_group_ == 6 ? ToolboxSlotId::Knife
                                           : ToolboxSlotId::Annotation;
        const int slot_idx = get_visible_index_for_slot(slot_for_group);
        const RECT slot_rc = get_toolbox_slot_rect(slot_idx >= 0 ? slot_idx : 0);
        const int flyout_top = slot_rc.top;
        const int flyout_left = toolbox_width() + 4;
        const int flyout_height = toolbox_open_group_ == 6
            ? 26 + static_cast<int>(kCutShapeToolCatalog.size()) * 29 + 7
            : toolbox_open_group_ == 7 ? 116
            : toolbox_open_group_ == 3 ? 174
            : (toolbox_open_group_ == 2) ? 145
            : (toolbox_open_group_ == 5) ? 116
            : (toolbox_open_group_ == 4) ? 58
            : (toolbox_open_group_ == 1) ? 145 : 116;
        const int flyout_width = (toolbox_open_group_ == 6 || toolbox_open_group_ == 7) ? 220 : (toolbox_open_group_ == 3 || toolbox_open_group_ == 5) ? 214 : 188;
        const bool inside_flyout = x >= flyout_left && x <= flyout_left + flyout_width &&
            y >= flyout_top && y < flyout_top + flyout_height;
        if (inside_flyout && y >= flyout_top + 26) {
            const int item = (y - flyout_top - 26) / 29;
            const int max_items = toolbox_open_group_ == 6 ? static_cast<int>(kCutShapeToolCatalog.size())
                : toolbox_open_group_ == 7 ? 3
                : toolbox_open_group_ == 3 ? 5
                : toolbox_open_group_ == 2 ? 4
                : toolbox_open_group_ == 5 ? 3
                : toolbox_open_group_ == 4 ? 1
                : toolbox_open_group_ == 1 ? 4 : 3;
            if (item >= 0 && item < max_items) {
                if (toolbox_open_group_ == 7) {
                    toolbox_open_group_ = 0;
                    activate_annotation_tool(item == 0 ? tools::AnnotationToolMode::DetailView
                                           : item == 1 ? tools::AnnotationToolMode::LeaderNote
                                           : tools::AnnotationToolMode::PlainNote);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                if (toolbox_open_group_ == 3) {
                    activate_toolbox_effect_item(item);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                if (toolbox_open_group_ == 4) {
                    toolbox_open_group_ = 0;
                    set_active_tool(13);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                if (toolbox_open_group_ == 5) {
                    toolbox_open_group_ = 0;
                    set_eyedropper_mode(item);
                    set_active_tool(15);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                if (toolbox_open_group_ == 6) {
                    toolbox_open_group_ = 0;
                    const auto* descriptor = cut_shape_tool_at(static_cast<size_t>(item));
                    if (!descriptor) return true;
                    if (descriptor->id == CutShapeToolId::TrimObjects) {
                        reset_trim_objects_roles();
                        trim_objects_keep_sources_ = true;
                    }
                    if (descriptor->id == CutShapeToolId::ShapeBuilder)
                        shape_builder_seed_points_.clear();
                    set_active_tool(static_cast<int>(descriptor->id));
                    if (descriptor->id == CutShapeToolId::TrimObjects) {
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Cắt bằng đối tượng · nhấp Target trước, rồi nhấp một hoặc nhiều Source"
                            : "Trim Objects · click the Target first, then one or more Sources";
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                const int tool = toolbox_open_group_ == 1
                    ? (item == 0 ? 1 : item == 1 ? 2 : item == 2 ? 9 : 18)
                    : (item == 0 ? 6 : item == 1 ? 7 : item == 2 ? 8 : 11);
                toolbox_open_group_ = 0;
                set_active_tool(tool);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        if (!inside_flyout && (x < 6 || x > toolbox_width() - 6 || y < 104)) {
            toolbox_open_group_ = 0;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    const auto visible_slots = visible_toolbox_slots();
    const int total_slots = static_cast<int>(visible_slots.size());
    const int tool_w = toolbox_width();
    const RECT rc_plus = get_toolbox_plus_rect(total_slots);
    const int toolbox_max_y = rc_plus.bottom + 8;
    const bool on_toolbox = x >= 0 && x < tool_w && y >= 102 && y < toolbox_max_y;

    if (on_toolbox) {
        toolbox_keyboard_focus_ = false;

        // Check Column Toggle header button
        const RECT rc_toggle = get_toolbox_toggle_columns_rect();
        if (x >= rc_toggle.left && x <= rc_toggle.right && y >= rc_toggle.top && y <= rc_toggle.bottom) {
            toggle_toolbox_columns();
            return true;
        }

        // Check Quick Customize [+] button
        if (x >= rc_plus.left && x <= rc_plus.right && y >= rc_plus.top && y <= rc_plus.bottom) {
            toolbox_open_group_ = 0;
            toggle_toolbox_customize_flyout();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }

        // Check Slot buttons
        for (int i = 0; i < total_slots; ++i) {
            const RECT rc_tb = get_toolbox_slot_rect(i);
            if (x >= rc_tb.left && x <= rc_tb.right && y >= rc_tb.top && y <= rc_tb.bottom) {
                const auto slot_id = visible_slots[i];
                // Delete badge click in edit mode
                if (toolbox_edit_mode_ && slot_id != ToolboxSlotId::Pick &&
                    x >= rc_tb.right - 10 && x <= rc_tb.right && y >= rc_tb.top && y <= rc_tb.top + 10) {
                    set_toolbox_slot_visible(slot_id, false);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }

                close_toolbox_customize_flyout();
                const bool on_arrow = (x >= rc_tb.right - 10 && y >= rc_tb.bottom - 10);

                switch (slot_id) {
                case ToolboxSlotId::Pick:
                    toolbox_open_group_ = 0;
                    previous_drawing_tool_ = 0;
                    set_active_tool(0);
                    break;
                case ToolboxSlotId::Node:
                    toolbox_open_group_ = 0;
                    set_active_tool(11);
                    break;
                case ToolboxSlotId::QuickShapes: {
                    const int group = 1;
                    toolbox_focus_group_ = group;
                    if (on_arrow) {
                        toolbox_open_group_ = (toolbox_open_group_ == group) ? 0 : group;
                        toolbox_flyout_index_ = quick_shapes_last_tool_ == 1 ? 0 : quick_shapes_last_tool_ == 2 ? 1 : quick_shapes_last_tool_ == 9 ? 2 : 3;
                    } else {
                        toolbox_open_group_ = 0;
                        set_active_tool(quick_shapes_last_tool_);
                    }
                    break;
                }
                case ToolboxSlotId::PathDrawing: {
                    const int group = 2;
                    toolbox_focus_group_ = group;
                    if (on_arrow) {
                        toolbox_open_group_ = (toolbox_open_group_ == group) ? 0 : group;
                        toolbox_flyout_index_ = path_drawing_last_tool_ == 6 ? 0 : path_drawing_last_tool_ == 7 ? 1 : 2;
                    } else {
                        toolbox_open_group_ = 0;
                        set_active_tool(path_drawing_last_tool_);
                    }
                    break;
                }
                case ToolboxSlotId::Pan:
                    toolbox_open_group_ = 0;
                    set_active_tool(3);
                    break;
                case ToolboxSlotId::Text:
                    toolbox_open_group_ = 0;
                    set_active_tool(4);
                    break;
                case ToolboxSlotId::Knife:
                    toolbox_focus_group_ = 6;
                    if (on_arrow) {
                        toolbox_open_group_ = (toolbox_open_group_ == 6) ? 0 : 6;
                        toolbox_flyout_index_ = 0;
                    } else {
                        toolbox_open_group_ = 0;
                        set_active_tool(5);
                    }
                    break;
                case ToolboxSlotId::Blur:
                    toolbox_focus_group_ = 3;
                    if (on_arrow) {
                        toolbox_open_group_ = (toolbox_open_group_ == 3) ? 0 : 3;
                        toolbox_flyout_index_ = 0;
                    } else {
                        toolbox_open_group_ = 0;
                        activate_toolbox_effect_item(1);
                    }
                    break;
                case ToolboxSlotId::DropShadow:
                    toolbox_open_group_ = 0;
                    activate_toolbox_effect_item(2);
                    break;
                case ToolboxSlotId::Contour:
                    toolbox_open_group_ = 0;
                    workflow().cancel_effect_draft();
                    trigger_effects_contour();
                    break;
                case ToolboxSlotId::Gradient:
                    toolbox_focus_group_ = 4;
                    if (on_arrow) {
                        toolbox_open_group_ = (toolbox_open_group_ == 4) ? 0 : 4;
                        toolbox_flyout_index_ = 0;
                    } else {
                        toolbox_open_group_ = 0;
                        set_active_tool(13);
                    }
                    break;
                case ToolboxSlotId::Eyedropper:
                    toolbox_focus_group_ = 5;
                    if (on_arrow) {
                        toolbox_open_group_ = (toolbox_open_group_ == 5) ? 0 : 5;
                        toolbox_flyout_index_ = eyedropper_mode_;
                    } else {
                        toolbox_open_group_ = 0;
                        set_active_tool(15);
                    }
                    break;
                case ToolboxSlotId::Dimension:
                    toolbox_open_group_ = 0;
                    set_active_tool(10);
                    break;
                case ToolboxSlotId::Artboard:
                    toolbox_open_group_ = 0;
                    set_active_tool(14);
                    break;
                case ToolboxSlotId::Annotation: {
                    const int group = 7;
                    toolbox_focus_group_ = group;
                    if (on_arrow) {
                        toolbox_open_group_ = (toolbox_open_group_ == group) ? 0 : group;
                        toolbox_flyout_index_ = 0;
                    } else {
                        toolbox_open_group_ = 0;
                        activate_annotation_tool(tools::AnnotationToolMode::DetailView);
                    }
                    break;
                }
                default:
                    break;
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
    }

    // 5. Right Dock Tabs & Shaping Docker (x: w - 280 to w)
    if (x >= w - 280 && x <= w) {
        if (y >= 102 && y <= 132) {
            int tab_w = 280 / 3;
            int tab_idx = (x - (w - 280)) / tab_w;
            if (tab_idx >= 0 && tab_idx < 3) {
                set_active_dock_tab(tab_idx);
                return true;
            }
        }
        const int inspector_content_y = 138;
        const int dock_y = (active_dock_tab_ == 0) ? (y + properties_dock_scroll_) : y;
        const int color_balance_histogram_top = raster_dock_metrics::kInspectorContentTop +
            raster_dock_metrics::kHistogramTopOffset;
        if (active_dock_tab_ == 0 && active_tool_ == 0 &&
            !show_style_inspector_ && !show_align_panel_ &&
            x >= w - 266 && x < w - 14 &&
            ((dock_y >= color_balance_histogram_top + 20 && dock_y < color_balance_histogram_top + 38) ||
             (dock_y >= 382 && dock_y < 400))) {
            const auto& selected =
                root().vector_controller().selection().selected_ids();
            const auto snapshot = root().document().current_snapshot();
            const auto* object = selected.size() == 1 && snapshot
                ? snapshot->objects().lookup_ref(selected.front()) : nullptr;
            if (object && std::holds_alternative<document::ImageObject>(*object)) {
                const size_t channel = static_cast<size_t>((x - (w - 266)) / 63);
                if (channel < raster_histogram_channels_visible_.size()) {
                    raster_histogram_channels_visible_[channel] =
                        !raster_histogram_channels_visible_[channel];
                    constexpr const char* names_vi[]{"R", "G", "B", "A"};
                    constexpr const char* names_en[]{"R", "G", "B", "A"};
                    const auto& name = language_ == UILanguage::Vietnamese
                        ? names_vi[channel] : names_en[channel];
                    status_msg_ = std::string(language_ == UILanguage::Vietnamese
                        ? "Histogram kênh " : "Histogram channel ") + name +
                        (raster_histogram_channels_visible_[channel]
                            ? (language_ == UILanguage::Vietnamese ? " bật" : " shown")
                            : (language_ == UILanguage::Vietnamese ? " tắt" : " hidden"));
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
            }
        }
        const int color_balance_panel_left = w - raster_dock_metrics::kInspectorDockWidth +
            raster_dock_metrics::kInspectorPanelInset;
        const int color_balance_panel_right = color_balance_panel_left +
            raster_dock_metrics::kInspectorPanelWidth;
        const int adjustment_tabs_top = color_balance_histogram_top +
            raster_dock_metrics::kRasterAdjustmentTabsTopOffset;
        const int adjustment_tabs_bottom = adjustment_tabs_top +
            raster_dock_metrics::kRasterAdjustmentTabsHeight;
        const int light_fields_top = color_balance_histogram_top +
            raster_dock_metrics::kLightFieldsTopOffset;
        const int light_fields_bottom = light_fields_top +
            raster_dock_metrics::kLightFieldsHeight;
        const int color_fields_top = color_balance_histogram_top +
            raster_dock_metrics::kColorFieldsTopOffset;
        const int color_fields_bottom = color_fields_top +
            raster_dock_metrics::kColorFieldsHeight;
        const int levels_channel_top = color_balance_histogram_top +
            raster_dock_metrics::kLevelsChannelButtonsTopOffset;
        const int levels_fields_top = color_balance_histogram_top +
            raster_dock_metrics::kLevelsFieldsTopOffset;
        const int levels_fields_bottom = levels_fields_top +
            raster_dock_metrics::kLevelsFieldsHeight;
        const int curves_fields_top = levels_fields_top;
        const int curves_fields_bottom = levels_fields_bottom;
        const int raster_advanced_actions_top = color_balance_histogram_top +
            raster_dock_metrics::kRasterAdvancedActionsTopOffset;
        const int raster_advanced_actions_bottom = raster_advanced_actions_top +
            2 * raster_dock_metrics::kRasterAdvancedActionHeight +
            raster_dock_metrics::kRasterAdvancedActionGap;
        const int white_balance_fields_top = color_balance_histogram_top +
            raster_dock_metrics::kWhiteBalanceFieldsTopOffset;
        const int white_balance_fields_bottom = white_balance_fields_top +
            raster_dock_metrics::kWhiteBalanceFieldHeight;
        const int color_balance_ranges_top = color_balance_histogram_top +
            raster_dock_metrics::kColorBalanceRangesTopOffset;
        const int color_balance_fields_top = color_balance_histogram_top +
            raster_dock_metrics::kColorBalanceFieldsTopOffset;
        const int color_balance_preserve_top = color_balance_histogram_top +
            raster_dock_metrics::kColorBalancePreserveTopOffset;
        const int max_bottom = (std::max)({
            light_fields_bottom,
            color_balance_preserve_top + raster_dock_metrics::kColorBalancePreserveHeight,
            color_fields_bottom,
            levels_fields_bottom,
            white_balance_fields_bottom,
            curves_fields_bottom,
            raster_advanced_actions_bottom
        });
        if (active_dock_tab_ == 0 && active_tool_ == 0 &&
            !show_style_inspector_ && !show_align_panel_ &&
            x >= color_balance_panel_left && x < color_balance_panel_right &&
            dock_y >= adjustment_tabs_top && dock_y < max_bottom) {
            const auto& selected =
                root().vector_controller().selection().selected_ids();
            const auto snapshot = root().document().current_snapshot();
            const auto* object = selected.size() == 1 && snapshot
                ? snapshot->objects().lookup_ref(selected.front()) : nullptr;
            if (object && std::holds_alternative<document::ImageObject>(*object)) {
                // Commit the active field before changing the adjustment panel.
                if (active_num_field_ != NumericFieldId::None) {
                    commit_numeric_field();
                    if (active_num_field_ != NumericFieldId::None)
                        return true;
                }
                if (!can_edit_selected_bitmap(true)) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Cân bằng màu cần một ảnh RGB sRGB có nguồn được giữ lại"
                        : "Color Balance requires one RGB sRGB image with a preserved source";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }

                if (dock_y >= adjustment_tabs_top && dock_y < adjustment_tabs_bottom) {
                    raster_adjustment_inspector_tab_ = static_cast<uint8_t>(
                        raster_dock_metrics::raster_adjustment_tab_from_x(
                            x, color_balance_panel_left));
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }

                if (raster_adjustment_inspector_tab_ == 1) {
                    if (dock_y >= light_fields_top && dock_y < light_fields_bottom) {
                        const int row = (dock_y - light_fields_top) /
                            raster_dock_metrics::kLightFieldRowSpacing;
                        const int column = (x - color_balance_panel_left) /
                            (raster_dock_metrics::kInspectorPanelWidth / 2);
                        constexpr NumericFieldId fields[]{
                            NumericFieldId::RasterExposureEv,
                            NumericFieldId::RasterLightContrast,
                            NumericFieldId::RasterLightHighlights,
                            NumericFieldId::RasterLightShadows,
                            NumericFieldId::RasterLightWhites,
                            NumericFieldId::RasterLightBlacks};
                        const int index = row * 2 + std::clamp(column, 0, 1);
                        if (row >= 0 && row < 3 && index >= 0 && index < 6) {
                            start_numeric_input(fields[index]);
                            return true;
                        }
                    }
                    return true;
                }

                if (raster_adjustment_inspector_tab_ == 2) {
                    if (dock_y >= color_fields_top && dock_y < color_fields_bottom) {
                        const int row = (dock_y - color_fields_top) /
                            raster_dock_metrics::kColorFieldRowSpacing;
                        const int column = (x - color_balance_panel_left) /
                            (raster_dock_metrics::kInspectorPanelWidth / 2);
                        constexpr NumericFieldId fields[]{
                            NumericFieldId::RasterHslHue,
                            NumericFieldId::RasterHslSaturation,
                            NumericFieldId::RasterHslLightness,
                            NumericFieldId::RasterVibranceAmount};
                        const int index = row * 2 + std::clamp(column, 0, 1);
                        if (row >= 0 && row < 2 && index >= 0 && index < 4) {
                            start_numeric_input(fields[index]);
                            return true;
                        }
                    }
                    if (dock_y >= white_balance_fields_top &&
                        dock_y < white_balance_fields_bottom) {
                        const int column = (x - color_balance_panel_left) /
                            (raster_dock_metrics::kInspectorPanelWidth / 2);
                        constexpr NumericFieldId fields[]{
                            NumericFieldId::RasterWhiteBalanceTemperature,
                            NumericFieldId::RasterWhiteBalanceTint};
                        start_numeric_input(fields[std::clamp(column, 0, 1)]);
                        return true;
                    }
                    return true;
                }

                if (raster_adjustment_inspector_tab_ == 3) {
                    if (dock_y >= levels_channel_top &&
                        dock_y < levels_channel_top +
                            raster_dock_metrics::kLevelsChannelButtonsHeight) {
                        const int button_width =
                            (raster_dock_metrics::kInspectorPanelWidth - 12) / 4;
                        const int channel = std::clamp(
                            (x - color_balance_panel_left) / (button_width + 4),
                            0, 3);
                        constexpr document::RasterToneChannel channels[]{
                            document::RasterToneChannel::Composite,
                            document::RasterToneChannel::Red,
                            document::RasterToneChannel::Green,
                            document::RasterToneChannel::Blue};
                        if (set_selected_bitmap_levels_channel(channels[channel])) {
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Đang xem kênh Levels; các kênh khác được giữ nguyên"
                                : "Viewing Levels channel; other channels are preserved";
                            InvalidateRect(hwnd_, nullptr, FALSE);
                        }
                        return true;
                    }
                    if (dock_y >= levels_fields_top && dock_y < levels_fields_bottom) {
                        const int row = (dock_y - levels_fields_top) /
                            raster_dock_metrics::kLevelsFieldRowSpacing;
                        const int column = (x - color_balance_panel_left) /
                            (raster_dock_metrics::kInspectorPanelWidth / 2);
                        constexpr NumericFieldId fields[]{
                            NumericFieldId::RasterLevelsBlackIn,
                            NumericFieldId::RasterLevelsWhiteIn,
                            NumericFieldId::RasterLevelsGamma,
                            NumericFieldId::RasterLevelsBlackOut,
                            NumericFieldId::RasterLevelsWhiteOut};
                        const int index = row * 2 + std::clamp(column, 0, 1);
                        if (row >= 0 && row < 3 && index >= 0 && index < 5) {
                            start_numeric_input(fields[index]);
                            return true;
                        }
                    }
                    return true;
                }

                if (raster_adjustment_inspector_tab_ == 5) {
                    if (dock_y >= levels_channel_top &&
                        dock_y < levels_channel_top +
                            raster_dock_metrics::kLevelsChannelButtonsHeight) {
                        const int button_width =
                            (raster_dock_metrics::kInspectorPanelWidth - 12) / 4;
                        const int channel = std::clamp(
                            (x - color_balance_panel_left) / (button_width + 4),
                            0, 3);
                        constexpr document::RasterToneChannel channels[]{
                            document::RasterToneChannel::Composite,
                            document::RasterToneChannel::Red,
                            document::RasterToneChannel::Green,
                            document::RasterToneChannel::Blue};
                        if (set_selected_bitmap_curves_channel(channels[channel])) {
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Đang xem kênh Curves; các kênh khác được giữ nguyên"
                                : "Viewing Curves channel; other channels are preserved";
                            InvalidateRect(hwnd_, nullptr, FALSE);
                        }
                        return true;
                    }
                    if (dock_y >= curves_fields_top && dock_y < curves_fields_bottom) {
                        const int row = (dock_y - curves_fields_top) /
                            raster_dock_metrics::kLevelsFieldRowSpacing;
                        const int column = (x - color_balance_panel_left) /
                            (raster_dock_metrics::kInspectorPanelWidth / 2);
                        constexpr NumericFieldId fields[]{
                            NumericFieldId::RasterCurvePoint0,
                            NumericFieldId::RasterCurvePoint1,
                            NumericFieldId::RasterCurvePoint2,
                            NumericFieldId::RasterCurvePoint3,
                            NumericFieldId::RasterCurvePoint4};
                        const int index = row * 2 + std::clamp(column, 0, 1);
                        if (row >= 0 && row < 3 && index >= 0 && index < 5) {
                            start_numeric_input(fields[index]);
                            return true;
                        }
                    }
                    return true;
                }

                if (raster_adjustment_inspector_tab_ == 6) {
                    const int action_height =
                        raster_dock_metrics::kRasterAdvancedActionHeight;
                    const int action_stride = action_height +
                        raster_dock_metrics::kRasterAdvancedActionGap;
                    if (dock_y >= raster_advanced_actions_top &&
                        dock_y < raster_advanced_actions_top + action_height) {
                        edit_selected_bitmap_selective_hsl();
                        return true;
                    }
                    if (dock_y >= raster_advanced_actions_top + action_stride &&
                        dock_y < raster_advanced_actions_top + action_stride +
                            action_height) {
                        edit_selected_bitmap_channel_mixer();
                        return true;
                    }
                    return true;
                }

                if (raster_adjustment_inspector_tab_ == 4) {
                    const int range_width =
                        raster_dock_metrics::kInspectorPanelWidth / 3;
                    if (dock_y >= color_balance_ranges_top &&
                        dock_y < color_balance_ranges_top +
                            raster_dock_metrics::kColorBalanceRangeHeight) {
                        const int range = std::clamp(
                            (x - color_balance_panel_left) / range_width, 0, 2);
                        raster_color_balance_active_range_ =
                            static_cast<document::RasterTonalRange>(range);
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Đang chỉnh Cân bằng màu: "
                            : "Editing Color Balance: ";
                        constexpr const char* range_names_vi[]{
                            "Vùng tối", "Trung gian", "Vùng sáng"};
                        constexpr const char* range_names_en[]{
                            "Shadows", "Midtones", "Highlights"};
                        status_msg_ += language_ == UILanguage::Vietnamese
                            ? range_names_vi[range] : range_names_en[range];
                        InvalidateRect(hwnd_, nullptr, FALSE);
                        return true;
                    }
                    if (dock_y >= color_balance_fields_top &&
                        dock_y < color_balance_fields_top +
                            raster_dock_metrics::kColorBalanceFieldsHeight) {
                        const int row = (dock_y - color_balance_fields_top) /
                            raster_dock_metrics::kColorBalanceFieldRowSpacing;
                        constexpr NumericFieldId fields[]{
                            NumericFieldId::RasterColorBalanceRedCyan,
                            NumericFieldId::RasterColorBalanceGreenMagenta,
                            NumericFieldId::RasterColorBalanceBlueYellow};
                        if (row >= 0 && row < 3) {
                            start_numeric_input(fields[row]);
                            return true;
                        }
                    }
                    if (dock_y >= color_balance_preserve_top &&
                        dock_y < color_balance_preserve_top +
                            raster_dock_metrics::kColorBalancePreserveHeight) {
                        if (toggle_selected_bitmap_color_balance_preserve_luminosity()) {
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Đã đổi Bảo toàn độ sáng · Ctrl+Z hoàn tác"
                                : "Changed Preserve Luminosity · Ctrl+Z undoes";
                        } else {
                            status_msg_ = language_ == UILanguage::Vietnamese
                                ? "Không thể cập nhật Cân bằng màu"
                                : "Could not update Color Balance";
                        }
                        InvalidateRect(hwnd_, nullptr, FALSE);
                        return true;
                    }
                }
                return true;
            }
        }
        if (active_dock_tab_ == 0 && (active_tool_ == 10 || is_dimension_object_selected()) &&
            !show_style_inspector_ && !show_align_panel_ && y >= inspector_content_y) {
            if (handle_dimension_dock_click(x, y)) {
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        if (active_dock_tab_ == 0 && has_annotation_properties_target() &&
            handle_annotation_properties_panel_click(x, y)) {
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }

        // Transform controls & Rectangle corner controls in Inspector Dock (Objects tab)
        if (active_dock_tab_ == 0 && !show_style_inspector_ && !show_align_panel_ &&
            active_tool_ != 12 && active_tool_ != 13 && active_tool_ != 11 &&
            !has_text_properties_target(*this)) {
            const auto& sel = root().vector_controller().selection().selected_ids();
            const int content_y = 138 - properties_dock_scroll_;
            const int insp_x = w - 280;
            const int panel_left = insp_x + 14;
            const int panel_width = (w - 14) - panel_left;
            const int half_w = (panel_width - 6) / 2;

            if (!sel.empty()) {
                const int tf_y = content_y + 50;
                // Row 1: X & Y
                if (y >= tf_y && y <= tf_y + 26) {
                    if (x < panel_left + half_w) start_numeric_input(NumericFieldId::ObjectX);
                    else start_numeric_input(NumericFieldId::ObjectY);
                    return true;
                }
                // Row 2: W & H
                if (y >= tf_y + 30 && y <= tf_y + 56) {
                    if (x < panel_left + half_w) start_numeric_input(NumericFieldId::ObjectWidth);
                    else start_numeric_input(NumericFieldId::ObjectHeight);
                    return true;
                }
                // Row 3: Rotation & Aspect Ratio Lock
                if (y >= tf_y + 60 && y <= tf_y + 86) {
                    if (x < panel_left + half_w) {
                        start_numeric_input(NumericFieldId::ObjectRotation);
                    } else if (sel.size() == 1 &&
                               x >= panel_left + half_w + 6 &&
                               x <= panel_left + panel_width &&
                               selected_bitmap_exposure_ev() &&
                               can_edit_selected_bitmap(true)) {
                        start_numeric_input(NumericFieldId::RasterExposureEv);
                    } else {
                        aspect_ratio_locked_ = !aspect_ratio_locked_;
                        status_msg_ = aspect_ratio_locked_
                            ? (language_ == UILanguage::Vietnamese ? "Đã khóa tỉ lệ kích thước (W/H)" : "Aspect ratio locked")
                            : (language_ == UILanguage::Vietnamese ? "Đã mở khóa tỉ lệ kích thước (W/H)" : "Aspect ratio unlocked");
                        InvalidateRect(hwnd_, nullptr, FALSE);
                    }
                    return true;
                }
            }

            const bool is_rect_single = sel.size() == 1 && is_selected_rectangle();
            const bool is_rect_tool = sel.empty() && active_tool_ == 1;
            if (is_rect_single || is_rect_tool) {
                const int corner_y = content_y + (is_rect_tool ? 48 : 155);
                // 3 Mode buttons
                if (y >= corner_y && y <= corner_y + 26) {
                    const int triple_w = (panel_width - 12) / 3;
                    const int idx = std::clamp((x - panel_left) / (triple_w + 6), 0, 2);
                    constexpr vector::RectCornerType ctypes[] = {
                        vector::RectCornerType::Round, vector::RectCornerType::Scallop, vector::RectCornerType::Chamfer
                    };
                    if (is_rect_tool) set_creation_rect_corner_type(ctypes[idx]);
                    else set_rect_corner_type(ctypes[idx]);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                // 4 Corner Inputs
                const int half_w = (panel_width - 6) / 2;
                if (y >= corner_y + 32 && y <= corner_y + 58) {
                    if (x < panel_left + half_w) start_numeric_input(NumericFieldId::RectCornerRadiusTL);
                    else start_numeric_input(NumericFieldId::RectCornerRadiusTR);
                    return true;
                }
                if (y >= corner_y + 64 && y <= corner_y + 90) {
                    if (x < panel_left + half_w) start_numeric_input(NumericFieldId::RectCornerRadiusBL);
                    else start_numeric_input(NumericFieldId::RectCornerRadiusBR);
                    return true;
                }
                // Toggles Row: Lock & Scale
                if (y >= corner_y + 98 && y <= corner_y + 124) {
                    if (x < panel_left + half_w) {
                        if (is_rect_tool) toggle_creation_rect_corners_locked();
                        else toggle_rect_corners_locked();
                    } else {
                        if (is_rect_tool) toggle_creation_rect_corners_scale_relative();
                        else toggle_rect_corners_scale_relative();
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                // Action: Convert to curves (Ctrl+Q)
                if (is_rect_single && y >= corner_y + 132 && y <= corner_y + 158) {
                    execute_menu_action(static_cast<int>(MenuActionId::ObjectConvertToCurves));
                    return true;
                }
            }
        }
        if (active_dock_tab_ == 0 && !show_style_inspector_ && !show_align_panel_ &&
            root().vector_controller().selection().selected_ids().empty() &&
            active_tool_ != 9 && !(active_tool_ == 11 && node_tool_ && node_tool_->active()) &&
            !(active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) &&
            !(active_tool_ == 12 && contour_tool_ && contour_tool_->active()) &&
            !has_text_properties_target(*this)) {
            const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
            const int row_top = AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height +
                PageUnitPropertiesMetrics::row_top_in_content;
            if (x >= panel_left && x <= panel_left + AlignPanelMetrics::content_width &&
                y >= row_top && y < row_top + PageUnitPropertiesMetrics::row_height) {
                show_page_units_flyout_ = !show_page_units_flyout_;
                if (show_page_units_flyout_)
                    page_units_flyout_focus_index_ = std::clamp(static_cast<int>(display_length_unit_), 0, 7);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        if (active_dock_tab_ == 0 && active_tool_ == 11 &&
            dock_y >= inspector_content_y + 24) {
            (void)handle_node_property_click(x, y);
            return true;
        }
        if (active_dock_tab_ == 0 && active_tool_ == 9 && dock_y >= inspector_content_y + 24) {
            if (x >= w - 148 && x <= w - 14 && dock_y >= 172 && dock_y <= 206) {
                start_numeric_input(NumericFieldId::PolygonSides);
                return true;
            }
            if (x >= w - 148 && x <= w - 14 && dock_y >= 216 && dock_y <= 250) {
                start_numeric_input(NumericFieldId::PolygonRotation);
                return true;
            }
            return true;
        }
        if (active_dock_tab_ == 0 && has_text_properties_target(*this) &&
            dock_y >= inspector_content_y) {
            const auto view = text_properties_view(*this);
            const int local_y = dock_y - inspector_content_y;
            if (local_y >= TextPropertiesLayout::font_button_top &&
                local_y < TextPropertiesLayout::font_button_top + TextPropertiesLayout::font_button_height) {
                const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
                const int row_width = AlignPanelMetrics::content_width;
                const int style_row_width = 4 * TextPropertiesLayout::style_button_width + 3 * TextPropertiesLayout::font_row_gap;
                const int family_width = std::max(72, row_width - TextPropertiesLayout::font_size_width -
                    style_row_width - 2 * TextPropertiesLayout::font_row_gap);
                const int size_left = panel_left + family_width + TextPropertiesLayout::font_row_gap;
                const int style_left = size_left + TextPropertiesLayout::font_size_width + TextPropertiesLayout::font_row_gap;
                if (x >= size_left) {
                    if (!view.editable) status_msg_ = language_ == UILanguage::Vietnamese ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính" : "Text is locked or hidden · properties cannot be edited";
                    else if (x < style_left) {
                        start_numeric_input(text_numeric_field(TextPropertyField::FontSize));
                    } else {
                        const int index = std::clamp((x - style_left) / (TextPropertiesLayout::style_button_width + TextPropertiesLayout::font_row_gap), 0, 3);
                        font_size_pt_ = view.size_pt; font_bold_ = view.bold; font_italic_ = view.italic;
                        font_underline_ = view.underline; font_strikethrough_ = view.strikethrough; font_alignment_ = view.alignment;
                        if (index == 0) toggle_font_bold();
                        else if (index == 1) toggle_font_italic();
                        else if (index == 2) toggle_font_underline();
                        else toggle_font_strikethrough();
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE); return true;
                }
                if (!view.editable) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                        : "Text is locked or hidden · properties cannot be edited";
                } else {
                    open_font_picker(panel_left - 440, y + 16);
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= TextPropertiesLayout::alignment_buttons_top &&
                local_y < TextPropertiesLayout::alignment_buttons_top + TextPropertiesLayout::alignment_buttons_height) {
                if (view.editable) {
                    const int quad_width = std::max(1, (AlignPanelMetrics::content_width - 18) / 4);
                    const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
                    const int index = std::clamp((x - panel_left) / (quad_width + 6), 0, 3);
                    font_size_pt_ = view.size_pt;
                    font_bold_ = view.bold;
                    font_italic_ = view.italic;
                    font_underline_ = view.underline;
                    font_strikethrough_ = view.strikethrough;
                    font_alignment_ = view.alignment;
                    if (index == 0) set_font_alignment(typography::TextAlignment::Left);
                    else if (index == 1) set_font_alignment(typography::TextAlignment::Center);
                    else if (index == 2) set_font_alignment(typography::TextAlignment::Right);
                    else set_font_alignment(typography::TextAlignment::Justify);
                } else status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                        : "Text is locked or hidden · properties cannot be edited";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= TextPropertiesLayout::frame_field_top &&
                local_y < TextPropertiesLayout::frame_field_top + TextPropertiesLayout::field_height) {
                if (view.editable) start_numeric_input(text_numeric_field(TextPropertyField::FrameWidth));
                else status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                    : "Text is locked or hidden · properties cannot be edited";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= TextPropertiesLayout::spacing_field_top &&
                local_y < TextPropertiesLayout::spacing_field_top + TextPropertiesLayout::field_height) {
                if (view.editable) start_numeric_input(text_numeric_field(TextPropertyField::LineSpacing));
                else status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                    : "Text is locked or hidden · properties cannot be edited";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= TextPropertiesLayout::image_search_top &&
                local_y < TextPropertiesLayout::image_search_top + TextPropertiesLayout::image_search_height) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Tìm font bằng ảnh sẽ được phát triển sau"
                    : "Font search from an image is planned for a later update";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= 0) return true;
        }
        if (active_dock_tab_ == 0 && y >= inspector_content_y && y < inspector_content_y + 24) {
            const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
            const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
            const int column = std::clamp((x - panel_left) / (button_width + 6), 0, 2);
            show_style_inspector_ = column == 1;
            show_align_panel_ = column == 2;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (active_dock_tab_ == 0 && show_align_panel_ && handle_align_panel_click(x, y))
            return true;
        if (active_dock_tab_ == 0 && show_style_inspector_) {
            const int style_x = x - (w - 280);
            const int style_y = y - kStylePanelTop + style_inspector_scroll_;
            if (style_y >= 504 && style_y < 532 && style_x >= 12 && style_x < 268) {
                show_spot_palette_picker(*this, palette_stroke_);
                return true;
            }
            if (handle_style_click(x, y))
                return true;
            if (y >= inspector_content_y + 24) return true;
        }
        if (active_dock_tab_ == 1) {
            (void)handle_layers_dock_mouse_down(x, y);
            if (handle_layers_dock_click(x, y))
                return true;
        }
    }

    // Page navigation row sits immediately above the status bar.
    if (y >= height_ - page_navigation::kBottomChromeHeight &&
        y < height_ - page_navigation::kStatusBarHeight) {
        const bool artboard_mode = artboard_overview_enabled();
        const auto& pages = root().document().pages();
        const auto snapshot = root().document().current_snapshot();
        const auto& boards = snapshot->active_page().artboards;
        const size_t item_count = artboard_mode ? boards.size() : pages.size();
        size_t active_index = 0;
        if (artboard_mode) {
            const auto active_it = std::find_if(boards.begin(), boards.end(), [&](const auto& board) {
                return board.id == active_tab().active_artboard_id;
            });
            if (active_it != boards.end())
                active_index = static_cast<size_t>(std::distance(boards.begin(), active_it));
        } else {
            const auto active_it = std::find_if(pages.begin(), pages.end(), [&](const auto& page) {
                return page.id == root().document().active_page_id();
            });
            if (active_it != pages.end())
                active_index = static_cast<size_t>(std::distance(pages.begin(), active_it));
        }
        const auto nav = page_navigation::make_layout(w, item_count, active_index);
        const auto activate_item = [this, artboard_mode, &pages, &boards](size_t index) {
            if (artboard_mode) {
                if (index >= boards.size()) return;
                (void)set_active_artboard(boards[index].id);
                status_msg_ = (language_ == UILanguage::Vietnamese ? "Artboard đang hoạt động · " : "Active Artboard · ") +
                    boards[index].name;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            if (index >= pages.size()) return;
            cancel_canvas_interaction();
            if (!root().document().set_active_page(pages[index].id)) return;
            const auto active_snapshot = root().document().current_snapshot();
            const auto* page = active_snapshot ? active_snapshot->find_page(active_snapshot->active_page_id()) : nullptr;
            if (!page) return;
            active_tab().active_artboard_id = page->artboards.empty()
                ? document::kInvalidArtboardId : page->artboards.front().id;
            sync_display_unit_from_active_page();
            active_tab().page_width_mm = page->layout.width_mm;
            active_tab().page_height_mm = page->layout.height_mm;
            root().vector_controller().selection().clear();
            fit_page_to_canvas();
            status_msg_ = (language_ == UILanguage::Vietnamese ? "Đã chuyển đến " : "Opened ") +
                page->name;
            InvalidateRect(hwnd_, nullptr, FALSE);
        };
        if (nav.first.contains(x)) { activate_item(0); return true; }
        if (nav.previous.contains(x)) {
            if (active_index > 0) activate_item(active_index - 1);
            return true;
        }
        if (nav.next.contains(x)) {
            if (active_index + 1 < item_count) activate_item(active_index + 1);
            return true;
        }
        if (nav.last.contains(x)) { activate_item(item_count == 0 ? 0 : item_count - 1); return true; }
        if (nav.add.contains(x)) {
            execute_menu_action(static_cast<int>(artboard_mode
                ? MenuActionId::LayoutArtboardAdd : MenuActionId::LayoutPageAdd));
            return true;
        }
        if (nav.remove.contains(x)) {
            execute_menu_action(static_cast<int>(artboard_mode
                ? MenuActionId::LayoutArtboardDelete : MenuActionId::LayoutPageDelete));
            return true;
        }
        for (size_t i = nav.first_visible; i < nav.first_visible + nav.visible_count; ++i) {
            if (nav.tab(i).contains(x)) { activate_item(i); return true; }
        }
        return true;
    }

    // Page and zoom controls in the status bar.
    if (y >= height_ - page_navigation::kStatusBarHeight) {
        if (x >= w - 210 && x <= w - 145) { fit_page_to_canvas(); return true; }
        if (x >= w - 120 && x <= w - 95) { zoom_out(); return true; }
        if (x >= w - 90 && x <= w - 50) {
            auto& camera = root().renderer().camera();
            const auto area = canvas_rect(width_, height_);
            camera.set_viewport_size(area.right - area.left, area.bottom - area.top);
            camera.set_zoom(1.0);
            zoom_level_ = 100;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (x >= w - 45 && x <= w - 20) { zoom_in(); return true; }
    }

    const auto area = canvas_rect(width_, height_);
    if (is_in_isolation_mode() && y >= area.top && y <= area.top + 34 && x >= area.left && x <= area.right) {
        if (x >= area.right - 150) {
            exit_group_isolation();
            return true;
        }
        if (x >= area.left + 8 && x <= area.left + 140) {
            exit_all_isolation();
            return true;
        }
        return true;
    }

    // Ruler guideline dragging: Top ruler creates horizontal guideline, Left ruler creates vertical guideline
    if (x >= area.left && x <= area.right && y >= 102 && y <= area.top) {
        selected_guideline_index_ = -1;
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
        return true;
    }

    if (x >= toolbox_width() && x <= area.left && y >= area.top && y <= area.bottom) {
        selected_guideline_index_ = -1;
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
        return true;
    }

    if (handle_drawing_panel_click(x, y))
        return true;
    if (handle_contour_property_click(x, y))
        return true;
    if (handle_gradient_property_click(x, y))
        return true;
    if (handle_style_click(x, y))
        return true;
    if (handle_node_property_click(x, y))
        return true;
    if (handle_node_pointer_down(x, y, modifiers))
        return true;
    if (handle_pen_pointer_down(x, y, modifiers))
        return true;
    if (handle_gradient_pointer_down(x, y, modifiers))
        return true;
    if (handle_drawing_click(x, y, modifiers))
        return true;
    return false;
}

bool NativeWindow::handle_cut_tool_properties_click(int x, int y, bool in_properties_dock) {
    if (active_tool_ != 5 && active_tool_ != 16 && active_tool_ != 20 && active_tool_ != 21 &&
        active_tool_ != 22 && active_tool_ != 23 && active_tool_ != 24) return false;
    int w = width_;
    if (hwnd_) {
        RECT client{};
        if (GetClientRect(hwnd_, &client)) w = logical_pixel(client.right - client.left);
    }
    if (!in_properties_dock) return false;
    const int content_y = 138 - properties_dock_scroll_;
    const int left = w - inspector_width() + 14;
    const int right = w - 14;
    if (active_tool_ == 21 || active_tool_ == 23) return x >= left && x < right;
    if (active_tool_ == 22) {
        if (x < left || x >= right) return false;
        if (y >= content_y + 76 && y < content_y + 106) {
            commit_shape_builder(false);
        } else if (y >= content_y + 110 && y < content_y + 140) {
            commit_shape_builder(true);
        } else if (y >= content_y + 144 && y < content_y + 174) {
            reset_shape_builder();
        } else {
            return false;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (active_tool_ == 24) {
        const int choice_left = left;
        const int gap = 6;
        const int button_width = std::max(1, (right - choice_left - gap * 2) / 3);
        const int row_one = content_y + 142;
        const int row_two = content_y + 178;
        const bool in_row = (y >= row_one && y < row_one + 30) ||
                            (y >= row_two && y < row_two + 30);
        if (!in_row || x < choice_left || x >= right) return false;
        const int row = y >= row_two ? 1 : 0;
        const int column = std::clamp((x - choice_left) / (button_width + gap), 0, 2);
        constexpr std::array<double, 6> diameters{4.0, 8.0, 12.0, 24.0, 48.0, 96.0};
        raster_eraser_diameter_px_ = diameters[static_cast<size_t>(row * 3 + column)];
        status_msg_ = (language_ == UILanguage::Vietnamese
            ? "Đường kính tẩy ảnh: " : "Raster eraser diameter: ") +
            std::to_string(static_cast<int>(std::lround(raster_eraser_diameter_px_))) + " px";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (active_tool_ == 20) {
        if (y >= content_y + 84 && y < content_y + 112 && x >= left && x < right) {
            trim_objects_keep_sources_ = !trim_objects_keep_sources_;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (y >= content_y + 118 && y < content_y + 146 && x >= left && x < right) {
            commit_trim_objects();
            return true;
        }
        if (y >= content_y + 152 && y < content_y + 180 && x >= left && x < right) {
            reset_trim_objects_roles();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        return false;
    }
    if (y >= content_y + 32 && y < content_y + 62 && active_tool_ == 5 &&
        x >= left && x < right) {
        if (active_num_field_ != NumericFieldId::None) {
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None) return true;
        }
        knife_freehand_mode_ = x >= (left + right) / 2;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (knife_freehand_mode_ ? "Knife tự do · nhả chuột để cắt" : "Knife đường thẳng · nhả chuột để cắt")
            : (knife_freehand_mode_ ? "Freehand Knife · release to cut" : "Straight Knife · release to cut");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const int numeric_y = content_y + (active_tool_ == 5 ? 68 : 32);
    if (y >= numeric_y && y < numeric_y + 30 && x >= left && x < right) {
        start_numeric_input(active_tool_ == 5
            ? NumericFieldId::KnifeGap : NumericFieldId::VectorEraserDiameter);
        return true;
    }
    if (active_tool_ == 16 && y >= content_y + 70 && y < content_y + 98 &&
        x >= left && x < right) {
        if (active_num_field_ != NumericFieldId::None) {
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None) return true;
        }
        vector_eraser_round_nib_ = !vector_eraser_round_nib_;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (vector_eraser_round_nib_ ? "Đầu tẩy tròn" : "Đầu tẩy vuông")
            : (vector_eraser_round_nib_ ? "Round eraser nib" : "Square eraser nib");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    return false;
}
#endif

} // namespace pikdraw::desktop
