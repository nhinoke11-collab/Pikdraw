#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_text_properties.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "native_window_drop_shadow_paint.hpp"

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
            if (x >= 422 && x <= 492) {
                if (active_tool_ == 8) finish_pen_session(false, true);
                else {
                    drawing_tool_->cancel();
                    finish_drawing_tool(true);
                }
                return true;
            }
            return true;
        } else if (active_tool_ == 9) {
            if (x >= 58 && x <= 164) {
                start_numeric_input(NumericFieldId::PolygonSides);
                return true;
            }
            commit_numeric_field();
            return true;
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
            if (x >= 510 && x <= 550) { set_font_alignment(typography::TextAlignment::Right); return true; }
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
            if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                x >= 720 && x <= 844 && x + 124 <= w - 280) {
                start_numeric_input(NumericFieldId::ObjectOpacity);
                return true;
            }
            commit_numeric_field();

            // Group / Ungroup / Enter Group / Exit Group / Align Ribbon Buttons
            if (is_in_isolation_mode()) {
                if (x >= 580 && x <= 670) { exit_group_isolation(); return true; }
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
                    int rx = 540;
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
                    if (has_shadow && x >= rx && x < rx + 66) {
                        commit_numeric_field();
                        ribbon_drop_shadow_mode_ = true;
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Đang hiển thị thanh thuộc tính Bóng đổ"
                            : "Showing Drop Shadow properties";
                        InvalidateRect(hwnd_, nullptr, FALSE);
                        return true;
                    }
                    if (has_shadow) rx += 72;
                    if (workflow().is_group(selection.front())) {
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

    // 4. Grouped toolbox and individual tools.
    const bool on_toolbox = x >= 6 && x <= 46 && y >= 112 && y < 532;
    if (toolbox_open_group_ > 0) {
        const int group_row = toolbox_open_group_ == 1 ? 1
                            : toolbox_open_group_ == 2 ? 2 : 6;
        const int flyout_top = 112 + group_row * 42;
        const int flyout_height = toolbox_open_group_ == 3 ? 174
            : toolbox_open_group_ == 2 ? 145 : 116;
        const bool inside_flyout = x >= 54 && x <= 242 &&
            y >= flyout_top && y < flyout_top + flyout_height;
        if (inside_flyout && y >= flyout_top + 26) {
            const int item = (y - flyout_top - 26) / 29;
            const int max_items = toolbox_open_group_ == 3 ? 5
                : toolbox_open_group_ == 2 ? 4 : 3;
            if (item >= 0 && item < max_items) {
                if (toolbox_open_group_ == 3) {
                    activate_toolbox_effect_item(item);
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                const int tool = toolbox_open_group_ == 1
                    ? (item == 0 ? 1 : item == 1 ? 2 : 9)
                    : (item == 0 ? 6 : item == 1 ? 7 : item == 2 ? 8 : 11);
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft(); set_active_tool(tool);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
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
        if (row >= 0 && row < 10) {
            if (row == 1 || row == 2) {
                const int group = row;
                toolbox_focus_group_ = group;
                if (x >= 37) {
                    if (toolbox_open_group_ == group) {
                        toolbox_open_group_ = 0;
                    } else {
                        toolbox_open_group_ = group;
                        const int active = group == 1 ? quick_shapes_last_tool_ : path_drawing_last_tool_;
                        toolbox_flyout_index_ = group == 1
                            ? (active == 1 ? 0 : active == 2 ? 1 : 2)
                            : (active == 6 ? 0 : active == 7 ? 1 : active == 8 ? 2 : 3);
                    }
                } else {
                    toolbox_open_group_ = 0;
                    const int active_in_group = group == 1
                        ? ((active_tool_ == 1 || active_tool_ == 2 || active_tool_ == 9)
                               ? active_tool_ : quick_shapes_last_tool_)
                        : ((active_tool_ >= 6 && active_tool_ <= 8)
                               || active_tool_ == 11
                               ? active_tool_ : path_drawing_last_tool_);
                    workflow().cancel_effect_draft(); set_active_tool(active_in_group);
                }
            } else if (row == 0) {
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft(); set_active_tool(0);
            } else if (row == 3) {
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft(); set_active_tool(3);
            } else if (row == 4) {
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft(); set_active_tool(4);
            } else if (row == 5) {
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft(); set_active_tool(5);
            } else if (row == 6) {
                toolbox_open_group_ = 0;
                activate_toolbox_effect_item(1);
            } else if (row == 7) {
                toolbox_open_group_ = 0;
                activate_toolbox_effect_item(2);
            } else if (row == 8) {
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft();
                trigger_effects_contour();
            } else if (row == 9) {
                toolbox_open_group_ = 0;
                workflow().cancel_effect_draft();
                set_active_tool(13);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
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
        if (active_dock_tab_ == 0 && active_tool_ == 11 &&
            y >= inspector_content_y + 24) {
            (void)handle_node_property_click(x, y);
            return true;
        }
        if (active_dock_tab_ == 0 && active_tool_ == 9 && y >= inspector_content_y + 24) {
            if (x >= w - 148 && x <= w - 14 && y >= 172 && y <= 206) {
                start_numeric_input(NumericFieldId::PolygonSides);
                return true;
            }
            if (x >= w - 148 && x <= w - 14 && y >= 216 && y <= 250) {
                start_numeric_input(NumericFieldId::PolygonRotation);
                return true;
            }
            return true;
        }
        if (active_dock_tab_ == 0 && has_text_properties_target(*this) &&
            y >= inspector_content_y) {
            const auto view = text_properties_view(*this);
            const int local_y = y - inspector_content_y;
            if (local_y >= TextPropertiesLayout::font_button_top &&
                local_y < TextPropertiesLayout::font_button_top + TextPropertiesLayout::font_button_height) {
                const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
                const int row_width = AlignPanelMetrics::content_width;
                const int family_width = std::max(72, row_width - TextPropertiesLayout::font_size_width -
                    3 * TextPropertiesLayout::style_button_width - 4 * TextPropertiesLayout::font_row_gap);
                const int size_left = panel_left + family_width + TextPropertiesLayout::font_row_gap;
                const int style_left = size_left + TextPropertiesLayout::font_size_width + TextPropertiesLayout::font_row_gap;
                if (x >= size_left) {
                    if (!view.editable) status_msg_ = language_ == UILanguage::Vietnamese ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính" : "Text is locked or hidden · properties cannot be edited";
                    else if (x < style_left) {
                        if (!view.mixed_font) start_numeric_input(text_numeric_field(TextPropertyField::FontSize));
                        else status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu chữ đang hỗn hợp · chọn một đối tượng hoặc thống nhất trước" : "Font settings are mixed · select one object or unify them first";
                    } else if (!view.mixed_font) {
                        const int index = std::clamp((x - style_left) / (TextPropertiesLayout::style_button_width + TextPropertiesLayout::font_row_gap), 0, 2);
                        font_size_pt_ = view.size_pt; font_bold_ = view.bold; font_italic_ = view.italic;
                        font_underline_ = view.underline; font_alignment_ = view.alignment;
                        if (index == 0) toggle_font_bold(); else if (index == 1) toggle_font_italic(); else toggle_font_underline();
                    } else status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu chữ đang hỗn hợp · chọn một đối tượng hoặc thống nhất trước" : "Font settings are mixed · select one object or unify them first";
                    InvalidateRect(hwnd_, nullptr, FALSE); return true;
                }
                if (!view.editable) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                        : "Text is locked or hidden · properties cannot be edited";
                } else if (view.mixed_font) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Kiểu chữ đang hỗn hợp · chọn một đối tượng hoặc thống nhất trước"
                        : "Font settings are mixed · select one object or unify them first";
                } else {
                    open_font_picker(panel_left - 440, y + 16);
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= TextPropertiesLayout::size_field_top &&
                local_y < TextPropertiesLayout::size_field_top + TextPropertiesLayout::field_height) {
                if (view.editable && !view.mixed_font)
                    start_numeric_input(text_numeric_field(TextPropertyField::FontSize));
                else status_msg_ = view.editable
                    ? language_ == UILanguage::Vietnamese
                        ? "Kiểu chữ hỗn hợp · chọn một đối tượng hoặc thống nhất trước"
                        : "Font settings are mixed · select one object or unify them first"
                    : language_ == UILanguage::Vietnamese
                        ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                        : "Text is locked or hidden · properties cannot be edited";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= TextPropertiesLayout::style_buttons_top &&
                local_y < TextPropertiesLayout::style_buttons_top + TextPropertiesLayout::style_buttons_height) {
                if (view.editable && !view.mixed_font) {
                    const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
                    const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
                    const int index = std::clamp((x - panel_left) / (button_width + 6), 0, 2);
                    font_size_pt_ = view.size_pt;
                    font_bold_ = view.bold;
                    font_italic_ = view.italic;
                    font_underline_ = view.underline;
                    font_alignment_ = view.alignment;
                    if (index == 0) toggle_font_bold();
                    else if (index == 1) toggle_font_italic();
                    else toggle_font_underline();
                } else status_msg_ = view.editable
                    ? language_ == UILanguage::Vietnamese
                        ? "Kiểu chữ hỗn hợp · chọn một đối tượng hoặc thống nhất trước"
                        : "Font settings are mixed · select one object or unify them first"
                    : language_ == UILanguage::Vietnamese
                        ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                        : "Text is locked or hidden · properties cannot be edited";
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
            if (local_y >= TextPropertiesLayout::alignment_buttons_top &&
                local_y < TextPropertiesLayout::alignment_buttons_top + TextPropertiesLayout::alignment_buttons_height) {
                if (view.editable && !view.mixed_font) {
                    const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
                    const int panel_left = w - AlignPanelMetrics::dock_width + AlignPanelMetrics::content_left;
                    const int index = std::clamp((x - panel_left) / (button_width + 6), 0, 2);
                    font_size_pt_ = view.size_pt;
                    font_bold_ = view.bold;
                    font_italic_ = view.italic;
                    font_underline_ = view.underline;
                    font_alignment_ = view.alignment;
                    set_font_alignment(index == 0 ? typography::TextAlignment::Left
                        : index == 1 ? typography::TextAlignment::Center
                                      : typography::TextAlignment::Right);
                } else status_msg_ = view.editable
                    ? language_ == UILanguage::Vietnamese
                        ? "Kiểu chữ hỗn hợp · chọn một đối tượng hoặc thống nhất trước"
                        : "Font settings are mixed · select one object or unify them first"
                    : language_ == UILanguage::Vietnamese
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
            if (handle_layers_dock_click(x, y))
                return true;
        }
    }

    // 6. Page and zoom controls in the status bar.
    if (y >= height_ - 26) {
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
        return true;
    }

    if (handle_drawing_panel_click(x, y))
        return true;
    if (handle_style_click(x, y))
        return true;
    if (handle_node_property_click(x, y))
        return true;
    if (handle_contour_property_click(x, y))
        return true;
    if (handle_gradient_property_click(x, y))
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
#endif

} // namespace pikdraw::desktop
