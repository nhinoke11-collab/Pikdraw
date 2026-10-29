#include "native_window_powerclip_ui.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_toolbox.hpp"
#include "native_window_text_properties.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_page_navigation.hpp"
#include "native_window_length_input.hpp"
#include "native_canvas_frame.hpp"
#ifdef _WIN32
#include "../clipboard/native_clipboard.hpp"
#endif
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include <limits>
#include "../tools/gradient_tool_controller.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pikdraw::desktop {

void NativeWindow::update_hover_tooltip(int mouse_x, int mouse_y) {
    bool is_vi = (language_ == UILanguage::Vietnamese);
    int w = width_;
    int h = height_;
    int topbar_h = 38, tabbar_h = 28, ribbon_h = 36;
    int status_h = page_navigation::kBottomChromeHeight, tool_w = toolbox_width(), inspect_w = inspector_width();
    int content_top = topbar_h + tabbar_h + ribbon_h;
    int insp_x = w - inspect_w;
    const auto unit_suffix = display_length_unit_suffix_wide(display_length_unit_);

    std::wstring tip_text;

    if (show_settings_modal_) {
        const int modal_x = (w - 720) / 2;
        const int modal_y = (h - 500) / 2;
        if (settings_active_tab_ == 3 &&
            mouse_x >= modal_x + 533 && mouse_x <= modal_x + 695 &&
            mouse_y >= modal_y + 56 && mouse_y <= modal_y + 82) {
            tip_text = is_vi ? L"Quản lý hồ sơ ICC đã cài · không tự gán profile cho tài liệu"
                             : L"Manage installed ICC profiles · does not assign one to the document";
        } else if (settings_active_tab_ == 3 &&
                   mouse_y >= modal_y + 274 && mouse_y <= modal_y + 302) {
            const int relative_x = mouse_x - modal_x;
            if (relative_x >= 205 && relative_x <= 287) {
                tip_text = is_vi ? L"Kiểm tra TAC trên mọi trang · chỉ đọc cho tới khi bấm Sửa"
                                 : L"Check TAC across every page · read-only until Fix is pressed";
            } else if (relative_x >= 291 && relative_x <= 373) {
                tip_text = is_vi ? L"Kiểm tra TAC trên trang hiện tại · chỉ đọc cho tới khi bấm Sửa"
                                 : L"Check TAC on the active page · read-only until Fix is pressed";
            } else if (relative_x >= 377 && relative_x <= 459) {
                tip_text = is_vi ? L"Kiểm tra TAC trên vùng chọn hiện tại · chọn đối tượng trước"
                                 : L"Check TAC on the current selection · select objects first";
            } else if (relative_x >= 467 && relative_x <= 543) {
                tip_text = is_vi ? L"Ngưỡng TAC thủ công 100–400% · Enter áp dụng · Esc hủy · chưa gắn profile"
                                 : L"Manual TAC limit 100–400% · Enter applies · Esc cancels · not profile-bound";
            } else if (relative_x >= 563 && relative_x <= 629) {
                tip_text = is_vi ? L"Kiểm tra chỉ đọc màu process CMYK; spot và raster được báo riêng"
                                 : L"Read-only check of process CMYK; spot and raster are reported separately";
            } else if (relative_x >= 635 && relative_x <= 695) {
                tip_text = is_vi ? L"Sửa màu process CMYK vượt ngưỡng trong một bước Undo · ngoại hình có thể đổi"
                                 : L"Fix over-limit process CMYK in one Undo step · appearance may change";
            }
        }
    }

    // 1. Main menu groups (y: 5 to 34)
    else if (mouse_y >= 6 && mouse_y <= 32) {
        const int menu_index = get_menu_bar_item_at(mouse_x, mouse_y, 38);
        if (menu_index >= 0) {
            const auto& entry = kMenuBarEntries[static_cast<size_t>(menu_index)];
            tip_text = is_vi ? std::wstring(entry.label_vi) + L" · menu"
                             : std::wstring(entry.label_en) + L" menu";
        } else if (mouse_x >= 10 && mouse_x <= 36) {
            tip_text = is_vi ? L"PikDraw Pro · Phần mềm thiết kế đồ họa vector chuyên nghiệp"
                             : L"PikDraw Pro · Professional Vector & Manufacturing Editor";
        }
    }

    // 2. Tab Bar (y: topbar_h to topbar_h + tabbar_h)
    else if (mouse_y >= topbar_h && mouse_y < topbar_h + tabbar_h) {
        int cur_x = 10;
        int tab_w = 210;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (mouse_x >= cur_x && mouse_x <= cur_x + tab_w) {
                if (mouse_x >= cur_x + tab_w - 24) {
                    tip_text = is_vi ? L"Đóng tab tài liệu này (Ctrl+W)" : L"Close document tab (Ctrl+W)";
                } else {
                    tip_text = (is_vi ? L"Tab tài liệu: " : L"Document Tab: ") + utf8_to_wide(tabs_[i].title);
                }
                break;
            }
            cur_x += tab_w + 4;
        }
        if (tip_text.empty() && mouse_x >= cur_x && mouse_x <= cur_x + 28) {
            tip_text = is_vi ? L"Tạo tài liệu mới dạng Tab (Ctrl+N)" : L"New Document Tab (Ctrl+N)";
        }
    }

    // 3. Ribbon (y: topbar_h + tabbar_h to content_top)
    else if (mouse_y >= topbar_h + tabbar_h && mouse_y < content_top) {
        const auto& selection = root().vector_controller().selection().selected_ids();
        bool is_text_mode = (active_tool_ == 4 || is_inline_text_editing_ || is_text_object_selected());

        if (active_tool_ == 12 && contour_tool_ && contour_tool_->active()) {
            if (mouse_x >= 75 && mouse_x < 135)
                tip_text = is_vi ? L"Viền vào tâm (To Center) — Tạo các lớp viền thu hẹp dần về tâm đối tượng."
                                 : L"Contour To Center — Create concentric offset contours inward toward object center.";
            else if (mouse_x >= 138 && mouse_x < 198)
                tip_text = is_vi ? L"Viền bên trong (Inside) — Tạo các lớp viền song song nằm bên trong đường biên."
                                 : L"Inside Contour — Create parallel offset contours inside the boundary.";
            else if (mouse_x >= 201 && mouse_x < 261)
                tip_text = is_vi ? L"Viền bên ngoài (Outside) — Tạo các lớp viền song song mở rộng ra ngoài đường biên."
                                 : L"Outside Contour — Create parallel offset contours expanding outside the boundary.";
            else if (mouse_x >= 270 && mouse_x <= 352)
                tip_text = is_vi ? L"Số bước viền (Steps) — Số lượng đường viền song song sẽ tạo (1–99 bước)."
                                 : L"Contour Steps — Number of concentric offset contours to generate (1–99).";
            else if (mouse_x >= 360 && mouse_x <= 462)
                tip_text = is_vi ? L"Khoảng cách viền (Spacing) — Khoảng cách giữa các lớp viền (" + unit_suffix + L")."
                                 : L"Contour Spacing — Offset distance between adjacent contour steps (" + unit_suffix + L").";
            else if (mouse_x >= 470 && mouse_x <= 580)
                tip_text = is_vi ? L"Bảng Thuộc tính Contour (⚙) — Mở đầy đủ thông số góc nối, sai số và khối nút Áp dụng/Rã viền."
                                 : L"Contour Properties (⚙) — Open full corner join, tolerance, and Apply/Break actions.";
        } else if (active_tool_ == 11 && node_tool_ && node_tool_->active()) {
            if (mouse_x >= 90 && mouse_x < 160)
                tip_text = is_vi ? L"Thêm điểm Node (+●) — Tạo một điểm node mới tại trung điểm của đoạn đang chọn."
                                 : L"Add Node (+●) — Insert a new node at the midpoint of the selected segment.";
            else if (mouse_x >= 165 && mouse_x < 235)
                tip_text = is_vi ? L"Xóa điểm Node (-●) — Xóa bỏ các node đang chọn (Phím Delete)."
                                 : L"Delete Node (-●) — Remove selected nodes from path (Delete key).";
            else if (mouse_x >= 245 && mouse_x < 315)
                tip_text = is_vi ? L"Chế độ Node Cusp (⋀) — Điểm góc nhọn, hai tay nắm điều hướng độc lập."
                                 : L"Cusp Node Mode (⋀) — Sharp corner node with independent control handles.";
            else if (mouse_x >= 320 && mouse_x < 390)
                tip_text = is_vi ? L"Chế độ Node Smooth (∿) — Điểm uốn trơn mượt, hai tay nắm thẳng hàng."
                                 : L"Smooth Node Mode (∿) — Smooth curve node with collinear handles.";
            else if (mouse_x >= 395 && mouse_x < 465)
                tip_text = is_vi ? L"Chế độ Node Symmetric (⎚) — Điểm đối xứng, hai tay nắm bằng nhau và thẳng hàng."
                                 : L"Symmetric Node Mode (⎚) — Symmetric curve node with equal collinear handles.";
            else if (mouse_x >= 475 && mouse_x <= 580)
                tip_text = is_vi ? L"Bảng Thuộc tính Node (⚙) — Mở đầy đủ tọa độ, chuyển đoạn thẳng/cong và thao tác Tách/Nối."
                                 : L"Node Properties (⚙) — Open full coordinates, segment types, and Break/Join operations.";
        } else if (active_tool_ == 8 || active_tool_ == 6 || active_tool_ == 7) {
            if (mouse_x >= 52 && mouse_x <= 185)
                tip_text = is_vi ? L"Độ dài nét vẽ (L) — Nhập chiều dài nét vẽ · Enter áp dụng · Tab chuyển sang góc."
                                 : L"Stroke Length (L) — Enter segment length · Enter applies · Tab switches to angle.";
            else if (mouse_x >= 190 && mouse_x <= 315)
                tip_text = is_vi ? L"Góc nghiêng (° theo trục X) — Nhập góc hướng vẽ · Enter áp dụng."
                                 : L"Segment Angle (° from X axis) — Enter direction angle · Enter applies.";
            else if (mouse_x >= 325 && mouse_x <= 415)
                tip_text = is_vi ? L"Hoàn tất vẽ (Finish · Enter) — Kết thúc đường vẽ và lưu vào tài liệu."
                                 : L"Finish Drawing (Enter) — Complete current path and commit to document.";
            else if (mouse_x >= 420 && mouse_x <= 490)
                tip_text = is_vi ? L"Hủy thao tác vẽ (Cancel · Esc) — Bỏ bản nháp đang vẽ dở."
                                 : L"Cancel Drawing (Esc) — Discard current unfinished path.";
            else if (mouse_x >= 495 && mouse_x <= 605)
                tip_text = is_vi ? L"Bảng Kiểu dáng (⚙) — Tùy chỉnh nét vẽ, đầu mút (Cap), góc nối (Join) và màu sắc."
                                 : L"Style Properties (⚙) — Customize stroke width, cap, join, and color.";
        } else if (active_tool_ == 13) {
            if (mouse_x >= 80 && mouse_x < 145)
                tip_text = is_vi ? L"Chuyển sắc Tuyến tính (Linear) — Chuyển màu thẳng theo một trục từ điểm đầu đến điểm cuối."
                                 : L"Linear Gradient — Color transitions linearly along a vector axis.";
            else if (mouse_x >= 150 && mouse_x < 215)
                tip_text = is_vi ? L"Chuyển sắc Tỏa tròn (Radial) — Chuyển màu tròn lan tỏa từ tâm ra ngoài."
                                 : L"Radial Gradient — Color transitions outward in a circular radius from center.";
            else if (mouse_x >= 220 && mouse_x < 295)
                tip_text = is_vi ? L"Đảo chiều dải màu (Reverse Stops) — Đảo ngược thứ tự các màu trong dải gradient."
                                 : L"Reverse Gradient Stops — Invert the sequence of color stops.";
            else if (mouse_x >= 305 && mouse_x <= 415)
                tip_text = is_vi ? L"Bảng Thuộc tính Chuyển sắc (⚙) — Tùy chỉnh danh sách điểm màu, độ mờ, góc xoay và khớp khung."
                                 : L"Gradient Properties (⚙) — Customize color stops, opacity, angle, and bounds.";
        } else if (active_tool_ == 9) {
            if (mouse_x >= 58 && mouse_x <= 164)
                tip_text = is_vi
                    ? L"Số cạnh đa giác đều (Sides) — Nhập số nguyên 3–256 · Enter áp dụng."
                    : L"Regular polygon sides — Enter an integer from 3 to 256 · Enter applies.";
            else if (mouse_x >= 172 && mouse_x <= 280)
                tip_text = is_vi ? L"Bảng Thuộc tính Đa giác (⚙) — Mở cài đặt nâng cao góc quay và bán kính trong dock."
                                 : L"Polygon Properties (⚙) — Open advanced rotation and radius settings in dock.";
        } else if (active_tool_ == 1 && selection.empty()) {
            int rx = 78;
            if (mouse_x >= rx && mouse_x < rx + 28) {
                tip_text = is_vi ? L"Bo góc tròn lồi (Round Fillet) — Làm tròn các góc hình chữ nhật theo cung lồi."
                                 : L"Round corner (convex) — Round corners using outward arc fillet.";
            } else if (mouse_x >= rx + 32 && mouse_x < rx + 60) {
                tip_text = is_vi ? L"Bo góc tròn lõm (Scallop) — Làm lõm các góc hình chữ nhật vào trong."
                                 : L"Scalloped corner (concave) — Inward circular cut corners.";
            } else if (mouse_x >= rx + 64 && mouse_x < rx + 92) {
                tip_text = is_vi ? L"Vát góc phẳng (Chamfer) — Vát cạnh thẳng chéo 45 độ tại các đỉnh góc."
                                 : L"Chamfered corner (bevel) — 45-degree flat beveled corners.";
            } else {
                rx += 98;
                if (mouse_x >= rx && mouse_x <= rx + 80) {
                    tip_text = is_vi ? L"Bán kính bo góc (" + unit_suffix + L") — Nhập số từ bàn phím · Enter áp dụng · Tab chuyển ô."
                                     : L"Corner Radius (" + unit_suffix + L") — Type value · Enter applies · Tab cycles fields.";
                } else if (mouse_x >= rx + 86 && mouse_x <= rx + 110) {
                    tip_text = is_vi ? (creation_rect_corners_locked_ ? L"Khóa 4 góc đồng bộ (Đang Bật) — Thay đổi 1 góc sẽ áp dụng cho cả 4 góc."
                                                                      : L"Khóa 4 góc đồng bộ (Đang Tắt) — Cho phép chỉnh độc lập từng góc trên bảng Properties.")
                                     : (creation_rect_corners_locked_ ? L"Corners Locked (Active) — Editing one corner changes all 4 corners."
                                                                      : L"Corners Unlocked — Edit each corner independently in Properties dock.");
                } else if (mouse_x >= rx + 116 && mouse_x <= rx + 178) {
                    tip_text = is_vi ? (creation_rect_corners_scale_relative_
                                            ? L"Tỷ lệ bo góc theo kích thước (Đang Bật) — Bán kính co giãn khi phóng to/thu nhỏ hình."
                                            : L"Tỷ lệ bo góc theo kích thước (Đang Tắt) — Bán kính cố định theo số đo vật lý.")
                                     : (creation_rect_corners_scale_relative_
                                            ? L"Relative corner scaling (Active) — Radius scales with shape dimensions."
                                            : L"Relative corner scaling (Disabled) — Fixed physical radius value.");
                } else if (mouse_x >= rx + 184 && mouse_x <= rx + 294) {
                    tip_text = is_vi ? L"Bảng Thuộc tính Chữ nhật (⚙) — Mở form chỉnh chi tiết độc lập 4 góc TL, TR, BL, BR."
                                     : L"Rectangle Properties (⚙) — Open detailed form for individual TL, TR, BL, BR corners.";
                }
            }
        } else if (is_text_mode) {
            if (mouse_x >= 52 && mouse_x <= 192) {
                tip_text = is_vi ? L"Chọn phông chữ (Font Picker · Win32 WYSIWYG)" : L"Choose font (Font Picker)";
            } else if (mouse_x >= 220 && mouse_x <= 240) {
                tip_text = is_vi ? L"Giảm cỡ chữ (-2 pt)" : L"Decrease Font Size (-2 pt)";
            } else if (mouse_x >= 242 && mouse_x <= 292) {
                tip_text = is_vi ? L"Cỡ chữ (Font Size · pt) · nhấp để nhập số" : L"Font Size (pt) · click to edit";
            } else if (mouse_x >= 294 && mouse_x <= 314) {
                tip_text = is_vi ? L"Tăng cỡ chữ (+2 pt)" : L"Increase Font Size (+2 pt)";
            } else if (mouse_x >= 322 && mouse_x <= 350) {
                tip_text = is_vi ? L"In đậm chữ (Bold · Ctrl+B)" : L"Bold text (Ctrl+B)";
            } else if (mouse_x >= 354 && mouse_x <= 382) {
                tip_text = is_vi ? L"In nghiêng chữ (Italic · Ctrl+I)" : L"Italic text (Ctrl+Shift+I)";
            } else if (mouse_x >= 386 && mouse_x <= 414) {
                tip_text = is_vi ? L"Gạch chân chữ (Underline · Ctrl+U)" : L"Underline text (Ctrl+U)";
            } else if (mouse_x >= 422 && mouse_x <= 462) {
                tip_text = is_vi ? L"Căn lề trái dòng chữ (Align Left)" : L"Align text Left";
            } else if (mouse_x >= 466 && mouse_x <= 506) {
                tip_text = is_vi ? L"Căn giữa dòng chữ (Align Center)" : L"Align text Center";
            } else if (mouse_x >= 510 && mouse_x <= 550) {
                tip_text = is_vi ? L"Căn phải dòng chữ (Align Right)" : L"Align text Right";
            } else if (mouse_x >= 556 && mouse_x <= 616) {
                tip_text = is_vi ? L"Chuyển văn bản thành đường cong vector (Convert to Curves · Ctrl+Q)"
                                 : L"Convert Text to Curves / Vector Outlines (Ctrl+Q)";
            } else if (mouse_x >= 622 && mouse_x <= 692) {
                tip_text = is_vi ? L"Tạo bảng văn bản 3×3 (Table Tool - CorelDRAW Style)"
                                 : L"Insert 3×3 Text Table (CorelDRAW Style)";
            } else if (mouse_x >= 698 && mouse_x <= 728) {
                tip_text = is_vi ? L"Gạch ngang thân chữ (Strikethrough)" : L"Strikethrough text";
            } else if (mouse_x >= 732 && mouse_x <= 762) {
                tip_text = is_vi ? L"Danh sách dấu chấm (Bullets · •=)" : L"Bulleted list (Bullets · •=)";
            } else if (mouse_x >= 766 && mouse_x <= 796) {
                tip_text = is_vi ? L"Danh sách số thứ tự (Numbering · 1.)" : L"Numbered list (Numbering · 1.)";
            } else if (mouse_x >= 800 && mouse_x <= 826) {
                tip_text = is_vi ? L"Bật/Tắt ký tự không in (Show Non-Printing Characters · ¶)" : L"Show/Hide Non-Printing Characters (¶)";
            } else if (mouse_x >= 830 && mouse_x <= 864) {
                tip_text = is_vi ? L"Chuyển đổi qua lại giữa Văn bản Tự do và Khung (Artistic / Paragraph · Ctrl+F8)" : L"Convert between Artistic and Paragraph text (Ctrl+F8)";
            } else if (!selection.empty() && mouse_x >= 868 && mouse_x <= 914) {
                tip_text = is_vi ? L"Xóa văn bản đang chọn (Delete)" : L"Delete selected text (Delete)";
            } else {
                if (selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                    mouse_x >= 920 && mouse_x <= 1044 && 920 + 124 <= w - inspect_w) {
                    tip_text = is_vi
                        ? L"Độ mờ toàn đối tượng 0–100% · nhập số · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                        : L"Object opacity 0–100% · type a value · Enter applies · Esc cancels · Ctrl+Z undoes";
                }
            }
        } else if (selection.empty()) {
            if (mouse_x >= 52 && mouse_x <= 165) {
                tip_text = is_vi ? L"Danh sách khổ giấy chuẩn (A4, A3, A2, Pano, Standee, Card...)" : L"Standard Page Presets List (A4, A3, A2, Pano, Standee, Card...)";
            } else if (mouse_x >= 175 && mouse_x <= 275) {
                tip_text = is_vi ? L"Chiều rộng khổ giấy (W, " + unit_suffix + L") · Nhập trực tiếp số từ bàn phím" : L"Page width (W, " + unit_suffix + L") · Type directly from keyboard";
            } else if (mouse_x >= 283 && mouse_x <= 383) {
                tip_text = is_vi ? L"Chiều cao khổ giấy (H, " + unit_suffix + L") · Nhập trực tiếp số từ bàn phím" : L"Page height (H, " + unit_suffix + L") · Type directly from keyboard";
            } else if (mouse_x >= 391 && mouse_x <= 455) {
                tip_text = is_vi ? L"Chuyển trang in sang hướng Dọc (Portrait)" : L"Page Orientation: Portrait";
            } else if (mouse_x >= 461 && mouse_x <= 525) {
                tip_text = is_vi ? L"Chuyển trang in sang hướng Ngang (Landscape)" : L"Page Orientation: Landscape";
            } else if (mouse_x >= 533 && mouse_x <= 665) {
                tip_text = is_vi ? L"Hệ màu in ấn CMYK · Độ phân giải 300 DPI" : L"CMYK Print Color Profile · 300 DPI High-Res";
            }
        } else {
            // Object Selection Mode
            const bool is_rect_single = selection.size() == 1 && is_selected_rectangle();
            if (!is_rect_single && selection.size() == 1 && workflow().can_set_selected_object_opacity() &&
                mouse_x >= 720 && mouse_x <= 844 && 844 <= w - inspect_w) {
                tip_text = is_vi
                    ? L"Độ mờ toàn đối tượng 0–100% · nhập số · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                    : L"Object opacity 0–100% · type a value · Enter applies · Esc cancels · Ctrl+Z undoes";
            } else if (mouse_x >= 10 && mouse_x <= 95) {
                tip_text = is_vi ? L"Nhập vị trí X của đối tượng (" + unit_suffix + L")" : L"Enter the object's X position (" + unit_suffix + L")";
            } else if (mouse_x >= 100 && mouse_x <= 185) {
                tip_text = is_vi ? L"Nhập vị trí Y của đối tượng (" + unit_suffix + L")" : L"Enter the object's Y position (" + unit_suffix + L")";
            } else if (mouse_x >= 190 && mouse_x <= 275) {
                tip_text = is_vi ? L"Nhập chiều rộng của đối tượng (" + unit_suffix + L")" : L"Enter the object's width (" + unit_suffix + L")";
            } else if (mouse_x >= 280 && mouse_x <= 335) {
                tip_text = is_vi ? L"Khóa hoặc mở tỷ lệ W:H" : L"Lock or unlock the width-to-height ratio";
            } else if (mouse_x >= 340 && mouse_x <= 425) {
                tip_text = is_vi ? L"Nhập chiều cao của đối tượng (" + unit_suffix + L")" : L"Enter the object's height (" + unit_suffix + L")";
            } else if (mouse_x >= 435 && mouse_x <= 525) {
                tip_text = is_vi ? L"Nhập góc xoay của đối tượng (độ °) · Enter để áp dụng · Esc để hủy" : L"Enter object rotation angle (degrees °) · Enter to apply · Esc to cancel";
            } else if (is_in_isolation_mode()) {
                if (mouse_x >= 580 && mouse_x <= 670) {
                    tip_text = is_vi ? L"Thoát chế độ cô lập nhóm (Phím Esc)" : L"Exit group isolation mode (Esc)";
                }
            } else {
                if (selection.size() >= 2) {
                    const auto buttons = get_multi_object_context_buttons(workflow(), workflow().can_group_selected(), width_ - inspect_w);
                    for (const auto& button : buttons) {
                        if (mouse_x < button.left || mouse_x >= button.left + button.width) continue;
                        switch (button.action) {
                        case ObjectContextAction::AlignLeft:
                            tip_text = is_vi ? L"Căn cạnh trái của các đối tượng theo khung chọn" : L"Align selected objects by their left edges"; break;
                        case ObjectContextAction::AlignCenterHorizontal:
                            tip_text = is_vi ? L"Căn tâm theo phương ngang" : L"Align object centers horizontally"; break;
                        case ObjectContextAction::AlignRight:
                            tip_text = is_vi ? L"Căn cạnh phải của các đối tượng theo khung chọn" : L"Align selected objects by their right edges"; break;
                        case ObjectContextAction::AlignTop:
                            tip_text = is_vi ? L"Căn cạnh trên của các đối tượng theo khung chọn" : L"Align selected objects by their top edges"; break;
                        case ObjectContextAction::AlignCenterVertical:
                            tip_text = is_vi ? L"Căn tâm theo phương dọc" : L"Align object centers vertically"; break;
                        case ObjectContextAction::AlignBottom:
                            tip_text = is_vi ? L"Căn cạnh dưới của các đối tượng theo khung chọn" : L"Align selected objects by their bottom edges"; break;
                        case ObjectContextAction::CenterPage:
                            tip_text = is_vi ? L"Căn tâm vùng chọn vào tâm trang (P)" : L"Center the selection on the page (P)"; break;
                        case ObjectContextAction::DistributeHorizontal:
                            tip_text = is_vi ? L"Phân bổ khoảng hở ngang đều nhau · cần ít nhất 3 đối tượng" : L"Distribute equal horizontal gaps · requires at least 3 objects"; break;
                        case ObjectContextAction::DistributeVertical:
                            tip_text = is_vi ? L"Phân bổ khoảng hở dọc đều nhau · cần ít nhất 3 đối tượng" : L"Distribute equal vertical gaps · requires at least 3 objects"; break;
                        case ObjectContextAction::Group:
                            tip_text = is_vi ? L"Nhóm các đối tượng đã chọn (Ctrl+G)" : L"Group selected objects (Ctrl+G)"; break;
                        case ObjectContextAction::FlipHorizontal:
                            tip_text = is_vi ? L"Lật ngang đối tượng qua tâm (Ctrl+Shift+H)" : L"Flip selection horizontally (Ctrl+Shift+H)"; break;
                        case ObjectContextAction::FlipVertical:
                            tip_text = is_vi ? L"Lật dọc đối tượng qua tâm (Ctrl+Shift+V)" : L"Flip selection vertically (Ctrl+Shift+V)"; break;
                        case ObjectContextAction::Rotate90CW:
                            tip_text = is_vi ? L"Xoay 90° thuận chiều kim đồng hồ (Ctrl+])" : L"Rotate 90° clockwise (Ctrl+])"; break;
                        case ObjectContextAction::Rotate90CCW:
                            tip_text = is_vi ? L"Xoay 90° ngược chiều kim đồng hồ (Ctrl+[)" : L"Rotate 90° counter-clockwise (Ctrl+[)"; break;
                        case ObjectContextAction::BooleanUnion:
                            tip_text = is_vi ? L"Hàn hai path đã chọn thành một kết quả" : L"Weld the two selected paths into one result"; break;
                        case ObjectContextAction::BooleanDifference:
                            tip_text = is_vi ? L"Trừ path chọn thứ hai khỏi path chọn thứ nhất" : L"Subtract the second selected path from the first"; break;
                        case ObjectContextAction::BooleanIntersection:
                            tip_text = is_vi ? L"Chỉ giữ phần giao của hai path đã chọn" : L"Keep only the intersection of the selected paths"; break;
                        case ObjectContextAction::BooleanExclude:
                            tip_text = is_vi ? L"Loại bỏ phần giao; giữ phần không chồng nhau" : L"Exclude the overlap and keep non-overlapping areas"; break;
                        case ObjectContextAction::BooleanReverseDifference:
                            tip_text = is_vi ? L"Trừ path chọn thứ nhất khỏi path chọn thứ hai" : L"Subtract the first selected path from the second"; break;
                        }
                        break;
                    }
                } else if (selection.size() == 1) {
                    int rx = 535;
                    const bool is_rect = is_selected_rectangle();
                    if (is_rect) {
                        if (mouse_x >= rx && mouse_x < rx + 28) {
                            tip_text = is_vi ? L"Bo góc tròn lồi (Round Corner)" : L"Round corner (convex)";
                        } else if (mouse_x >= rx + 32 && mouse_x < rx + 60) {
                            tip_text = is_vi ? L"Bo góc tròn lõm (Scalloped Corner)" : L"Scalloped corner (concave)";
                        } else if (mouse_x >= rx + 64 && mouse_x < rx + 92) {
                            tip_text = is_vi ? L"Vát góc phẳng (Chamfered Corner)" : L"Chamfered corner (bevel)";
                        } else if (mouse_x >= rx + 98 && mouse_x <= rx + 98 + 75) {
                            tip_text = is_vi ? L"Bán kính bo góc (" + unit_suffix + L") — Nhập số từ bàn phím · Enter áp dụng · Tab chuyển ô."
                                             : L"Corner Radius (" + unit_suffix + L") — Type value · Enter applies · Tab cycles fields.";
                        } else if (mouse_x >= rx + 98 + 80 && mouse_x <= rx + 98 + 104) {
                            tip_text = is_vi ? (rect_corners_locked_ ? L"Khóa 4 góc đồng bộ (Đang Bật) — Thay đổi 1 góc sẽ áp dụng cho cả 4 góc."
                                                                    : L"Khóa 4 góc đồng bộ (Đang Tắt) — Cho phép chỉnh độc lập từng góc trên bảng Properties.")
                                             : (rect_corners_locked_ ? L"Corners Locked (Active) — Editing one corner changes all 4 corners."
                                                                    : L"Corners Unlocked — Edit each corner independently in Properties dock.");
                        } else if (mouse_x >= rx + 98 + 108 && mouse_x <= rx + 98 + 170) {
                            tip_text = is_vi ? (rect_corners_scale_relative_
                                                    ? L"Tỷ lệ bo góc theo kích thước (Đang Bật) — Bán kính co giãn khi phóng to/thu nhỏ hình."
                                                    : L"Tỷ lệ bo góc theo kích thước (Đang Tắt) — Bán kính cố định theo số đo vật lý.")
                                             : (rect_corners_scale_relative_
                                                    ? L"Relative corner scaling (Active) — Radius scales with shape dimensions."
                                                    : L"Relative corner scaling (Disabled) — Fixed physical radius value.");
                        } else if (mouse_x >= rx + 98 + 176 && mouse_x <= rx + 98 + 256) {
                            tip_text = is_vi ? L"Bảng Thuộc tính Bo góc (⚙) — Mở form chỉnh chi tiết độc lập 4 góc TL, TR, BL, BR trong dock."
                                             : L"Corner Properties (⚙) — Open detailed form for individual TL, TR, BL, BR corners in dock.";
                        }
                        rx += 98 + 80 + 28 + 68 + 86;
                    }
                    if (tip_text.empty()) {
                        if (mouse_x >= rx && mouse_x < rx + 28) {
                            tip_text = is_vi ? L"Lật ngang đối tượng qua tâm (Ctrl+Shift+H)" : L"Flip selection horizontally (Ctrl+Shift+H)";
                        } else if (mouse_x >= rx + 32 && mouse_x < rx + 60) {
                            tip_text = is_vi ? L"Lật dọc đối tượng qua tâm (Ctrl+Shift+V)" : L"Flip selection vertically (Ctrl+Shift+V)";
                        } else if (mouse_x >= rx + 64 && mouse_x < rx + 92) {
                            tip_text = is_vi ? L"Xoay 90° thuận chiều kim đồng hồ (Ctrl+])" : L"Rotate 90° clockwise (Ctrl+])";
                        } else if (mouse_x >= rx + 96 && mouse_x < rx + 124) {
                            tip_text = is_vi ? L"Xoay 90° ngược chiều kim đồng hồ (Ctrl+[)" : L"Rotate 90° counter-clockwise (Ctrl+[)";
                        } else {
                            rx += 130;
                            if (workflow().is_group(selection.front())) {
                                if (mouse_x >= rx && mouse_x < rx + 66) {
                                    tip_text = is_vi ? L"Rã nhóm đối tượng (Ungroup · Ctrl+Shift+G)" : L"Ungroup selected group (Ctrl+Shift+G)";
                                } else if (mouse_x >= rx + 72 && mouse_x < rx + 138) {
                                    tip_text = is_vi ? L"Chui vào trong nhóm để chỉnh sửa hình con (Enter hoặc Nhấp đúp)"
                                                     : L"Enter group isolation mode to edit children (Enter or Double Click)";
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 4. Tool groups and their flyouts.
    else if (toolbox_open_group_ > 0 && mouse_x >= 54 && mouse_x <= 242) {
        const int group_row = toolbox_open_group_ == 1 ? get_visible_row_for_slot(ToolboxSlotId::QuickShapes)
                            : toolbox_open_group_ == 2 ? get_visible_row_for_slot(ToolboxSlotId::PathDrawing)
                            : toolbox_open_group_ == 3 ? get_visible_row_for_slot(ToolboxSlotId::Blur)
                            : toolbox_open_group_ == 4 ? get_visible_row_for_slot(ToolboxSlotId::Gradient)
                            : toolbox_open_group_ == 5 ? get_visible_row_for_slot(ToolboxSlotId::Eyedropper)
                            : get_visible_row_for_slot(ToolboxSlotId::Knife);
        const int flyout_top = content_top + 10 + (group_row >= 0 ? group_row : 1) * 42;
        const int item = (mouse_y - flyout_top - 26) / 29;
        const int max_items = toolbox_open_group_ == 6 ? 3
            : toolbox_open_group_ == 3 ? 5
            : toolbox_open_group_ == 2 ? 4 : 3;
        if (mouse_y >= flyout_top + 26 && item >= 0 && item < max_items) {
            if (toolbox_open_group_ == 6) {
                if (item == 0) tip_text = is_vi ? L"Dao cắt (Knife) · Kéo đường qua hình đóng để cắt rời" : L"Knife · Drag across shapes to slice";
                else if (item == 1) tip_text = is_vi ? L"Tẩy vector (Eraser) · Kéo quét đầu tẩy theo mm để trừ vùng" : L"Vector Eraser · Sweep nib to erase vector areas";
                else tip_text = is_vi
                    ? L"Scissors · Nhấp node hoặc đoạn path để tách; Ctrl+Z hoàn tác"
                    : L"Scissors · Click a path node or segment to split; Ctrl+Z undoes";
            } else if (toolbox_open_group_ == 1) {
                if (item == 0)
                    tip_text = is_vi ? L"Hình chữ nhật · Shift vuông · Ctrl từ tâm"
                                     : L"Rectangle · Shift for square · Ctrl from center";
                else if (item == 1)
                    tip_text = is_vi ? L"Hình elip · Shift hình tròn · Ctrl từ tâm"
                                     : L"Ellipse · Shift for circle · Ctrl from center";
                else
                    tip_text = is_vi ? L"Đa giác đều · đặt số cạnh trên thanh thuộc tính hoặc Properties"
                                     : L"Regular polygon · set sides in the property bar or Properties";
            } else if (toolbox_open_group_ == 3) {
                if (item == 0) {
                    tip_text = is_vi
                        ? L"Mở Properties > Style để chỉnh Blend, Opacity và stack hiệu ứng"
                        : L"Open Properties > Style for Blend, Opacity and the effect stack";
                } else if (item == 1) {
                    tip_text = workflow().can_add_selected_effect()
                        ? (is_vi ? L"Thêm Gaussian Blur cuối stack · Path/Text không khóa · Ctrl+Z hoàn tác"
                                 : L"Append Gaussian Blur · unlocked Path/Text only · Ctrl+Z undoes")
                        : (is_vi ? L"Đang tắt: chọn Path/Text không khóa và còn chỗ trong stack"
                                 : L"Disabled: select unlocked Path/Text with available stack capacity");
                } else if (item == 2) {
                    tip_text = workflow().can_add_selected_effect()
                        ? (is_vi ? L"Thêm Drop Shadow cuối stack · Path/Text không khóa · Ctrl+Z hoàn tác"
                                 : L"Append Drop Shadow · unlocked Path/Text only · Ctrl+Z undoes")
                        : (is_vi ? L"Đang tắt: chọn Path/Text không khóa và còn chỗ trong stack"
                                 : L"Disabled: select unlocked Path/Text with available stack capacity");
                } else if (item == 3) {
                    tip_text = is_vi
                        ? L"Công cụ Contour · Tạo viền bao quanh đối tượng · Ctrl+F9"
                        : L"Contour Tool · Create offset outline around object · Ctrl+F9";
                } else {
                    tip_text = is_vi
                        ? L"Công cụ chuyển sắc tương tác (Gradient)"
                        : L"Interactive Gradient Tool";
                }
            } else if (item == 3) {
                tip_text = is_vi
                    ? L"Node · nhấp chọn, kéo để di chuyển · tọa độ X/Y và mode trong Properties"
                    : L"Node · click to select, drag to move · X/Y and mode are in Properties";
            } else if (item == 0) {
                tip_text = is_vi ? L"Đoạn thẳng · L" : L"Line · L";
            } else if (item == 1) {
                tip_text = is_vi ? L"Đường gấp khúc · Shift+L" : L"Polyline · Shift+L";
            } else {
                tip_text = is_vi ? L"Pen · nhấp/kéo điểm · Enter kết thúc · Esc kết thúc"
                                 : L"Pen · click/drag points · Enter finishes · Esc finishes";
            }
        }
    }
    else if (mouse_x >= 0 && mouse_x <= tool_w && mouse_y >= content_top && mouse_y < h - status_h) {
        const RECT rc_toggle = get_toolbox_toggle_columns_rect();
        if (mouse_x >= rc_toggle.left && mouse_x <= rc_toggle.right &&
            mouse_y >= rc_toggle.top && mouse_y <= rc_toggle.bottom) {
            tip_text = (toolbox_columns_ == 2)
                ? (is_vi ? L"Thu gọn 1 cột (Collapse to 1 Column)" : L"Collapse to 1 Column")
                : (is_vi ? L"Mở rộng 2 cột (Expand to 2 Columns)" : L"Expand to 2 Columns");
        } else {
            const auto slots = visible_toolbox_slots();
            const int total_slots = static_cast<int>(slots.size());
            const RECT rc_plus = get_toolbox_plus_rect(total_slots);
            if (mouse_x >= rc_plus.left && mouse_x <= rc_plus.right &&
                mouse_y >= rc_plus.top && mouse_y <= rc_plus.bottom) {
                tip_text = is_vi ? L"Tùy chỉnh Hộp công cụ (+) · Thêm hoặc xóa bớt icon công cụ"
                                 : L"Quick Customize Toolbox (+) · Add or remove tool icons";
            } else {
                for (int i = 0; i < total_slots; ++i) {
                    const RECT rc_tb = get_toolbox_slot_rect(i);
                    if (mouse_x >= rc_tb.left && mouse_x <= rc_tb.right &&
                        mouse_y >= rc_tb.top && mouse_y <= rc_tb.bottom) {
                        const auto slot = slots[i];
                        if (slot == ToolboxSlotId::Pick) {
                            tip_text = is_vi ? L"Công cụ chọn & biến đổi đối tượng (Pick Tool) · Phím V hoặc Space"
                                             : L"Pick & Select / Transform Tool · Shortcut: V or Space";
                        } else if (slot == ToolboxSlotId::Node) {
                            tip_text = is_vi ? L"Công cụ chỉnh sửa điểm (Node Edit Tool) · Phím F10 · Kéo node hoặc tay nắm Bézier"
                                             : L"Node Edit Tool · Shortcut: F10 · Drag nodes and Bézier handles";
                        } else if (slot == ToolboxSlotId::QuickShapes) {
                            tip_text = is_vi ? L"Hình cơ bản · Hình chữ nhật, elip, đa giác · Mũi tên góc mở nhóm"
                                             : L"Quick Shapes · Rectangle, Ellipse, Polygon · Corner arrow opens group";
                        } else if (slot == ToolboxSlotId::PathDrawing) {
                            tip_text = is_vi ? L"Đường vẽ · Line, Polyline, Pen, Node · Mũi tên góc mở nhóm"
                                             : L"Path Drawing · Line, Polyline, Pen, Node · Corner arrow opens group";
                        } else if (slot == ToolboxSlotId::Pan) {
                            tip_text = is_vi ? L"Kéo trượt vùng làm việc (Pan Hand Tool) · Phím H hoặc Giữ Chuột giữa"
                                             : L"Pan Viewport Hand Tool · Shortcut: H or Middle Drag";
                        } else if (slot == ToolboxSlotId::Text) {
                            tip_text = is_vi ? L"Soạn thảo văn bản & Bảng (Text Tool) · Phím F8"
                                             : L"Text Typography & Table Tool · Shortcut: F8";
                        } else if (slot == ToolboxSlotId::Knife) {
                            tip_text = is_vi ? L"Dao cắt xén đối tượng vector (Knife Tool) · Phím K"
                                             : L"Knife / Slice Tool · Shortcut: K";
                        } else if (slot == ToolboxSlotId::Blur) {
                            tip_text = is_vi ? L"Hiệu ứng làm mờ (Gaussian Blur) · Mũi tên góc mở diện mạo"
                                             : L"Gaussian Blur Effect · Corner arrow opens appearance";
                        } else if (slot == ToolboxSlotId::DropShadow) {
                            tip_text = is_vi ? L"Hiệu ứng đổ bóng (Drop Shadow) · Kéo bóng trực tiếp hoặc chỉnh sửa"
                                             : L"Drop Shadow Effect · Interactive shadow on selection";
                        } else if (slot == ToolboxSlotId::Contour) {
                            tip_text = is_vi ? L"Công cụ viền Contour · Tạo viền bao quanh đối tượng · Phím Ctrl+F9"
                                             : L"Contour Tool · Create offset outline around object · Shortcut: Ctrl+F9";
                        } else if (slot == ToolboxSlotId::Gradient) {
                            tip_text = is_vi ? L"Công cụ chuyển sắc tương tác (Gradient Tool) · Phím G"
                                             : L"Interactive Gradient Tool · Shortcut: G";
                        } else if (slot == ToolboxSlotId::Eyedropper) {
                            tip_text = is_vi ? L"Công cụ hút màu & sao chép kiểu (Eyedropper Tool) · Phím I · Mũi tên góc chọn chế độ"
                                             : L"Color Eyedropper & Sampler Tool · Shortcut: I · Corner arrow opens mode flyout";
                        } else if (slot == ToolboxSlotId::Dimension) {
                            tip_text = is_vi ? L"Công cụ đo kích thước (CAD Dimension Tool) · Phím D"
                                             : L"CAD Dimension Tool · Shortcut: D";
                        } else if (slot == ToolboxSlotId::Artboard) {
                            tip_text = is_vi ? L"Artboard Tool · tạo, di chuyển và đổi cỡ khung xuất riêng · Shift+O"
                                             : L"Artboard Tool · create, move and resize an export frame · Shift+O";
                        }
                        break;
                    }
                }
            }
        }
    }

    // 5. Right Inspector & Docker Panel (x: insp_x to w, y: content_top to h - status_h)
    else if (mouse_x >= insp_x && mouse_x <= w && mouse_y >= content_top && mouse_y < h - status_h) {
        int tab_w = inspect_w / 3;
        if (mouse_y >= content_top && mouse_y <= content_top + 28) {
            int tidx = (mouse_x - insp_x) / tab_w;
            if (tidx == 0) tip_text = is_vi ? L"Bảng thuộc tính đối tượng" : L"Object Properties";
            else if (tidx == 1) tip_text = is_vi ? L"Danh sách các lớp & Đối tượng (Layers)" : L"Layers & Objects Hierarchy";
            else if (tidx == 2) tip_text = is_vi ? L"Lịch sử các thao tác hoàn tác / làm lại (History)" : L"Command History & Undo/Redo Log";
        } else if (active_dock_tab_ == 0 && !has_text_properties_target(*this) &&
                   mouse_y >= AlignPanelMetrics::section_tabs_top &&
                   mouse_y < AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height) {
            const int panel_left = insp_x + AlignPanelMetrics::content_left;
            const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
            const int index = (mouse_x - panel_left) / (button_width + 6);
            if (index == 0) tip_text = is_vi ? L"Xem thông tin đối tượng đang chọn" : L"Show selected object properties";
            else if (index == 1) tip_text = is_vi ? L"Mở thuộc tính kiểu dáng" : L"Open style properties";
            else if (index == 2) tip_text = is_vi ? L"Mở bảng Align & Distribute (Ctrl+Shift+A)" : L"Open Align & Distribute (Ctrl+Shift+A)";
        } else if (active_dock_tab_ == 0 && !show_style_inspector_ && !show_align_panel_ &&
                   root().vector_controller().selection().selected_ids().empty() &&
                   active_tool_ != 9 && !(active_tool_ == 11 && node_tool_ && node_tool_->active()) &&
                   !(active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) &&
                   !(active_tool_ == 12 && contour_tool_ && contour_tool_->active()) &&
                   !has_text_properties_target(*this) &&
                   mouse_y >= AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height +
                       PageUnitPropertiesMetrics::row_top_in_content &&
                   mouse_y < AlignPanelMetrics::section_tabs_top + AlignPanelMetrics::section_tabs_height +
                       PageUnitPropertiesMetrics::row_top_in_content + PageUnitPropertiesMetrics::row_height) {
            tip_text = is_vi
                ? L"Đổi đơn vị hiển thị của trang · ↑/↓ chọn · Enter áp dụng · Esc đóng · Ctrl+Z hoàn tác"
                : L"Change the page display unit · ↑/↓ choose · Enter applies · Esc closes · Ctrl+Z undoes";
        } else if (active_dock_tab_ == 0 && has_text_properties_target(*this)) {
            const int local_y = mouse_y - (content_top + 36);
            if (local_y >= TextPropertiesLayout::font_button_top &&
                local_y < TextPropertiesLayout::font_button_top + TextPropertiesLayout::font_button_height) {
                const int panel_left = insp_x + AlignPanelMetrics::content_left;
                const int family_width = std::max(72, AlignPanelMetrics::content_width - TextPropertiesLayout::font_size_width -
                    3 * TextPropertiesLayout::style_button_width - 4 * TextPropertiesLayout::font_row_gap);
                const int size_left = panel_left + family_width + TextPropertiesLayout::font_row_gap;
                const int style_left = size_left + TextPropertiesLayout::font_size_width + TextPropertiesLayout::font_row_gap;
                if (mouse_x < size_left) tip_text = is_vi
                    ? L"Chọn họ phông Windows · nhóm Sans, Serif, Script và UTM/SVN nếu đã cài"
                    : L"Choose an installed Windows font · browse Sans, Serif, Script, and UTM/SVN when installed";
                else if (mouse_x < style_left) tip_text = is_vi
                    ? L"Cỡ chữ theo điểm · 1–2880 pt · Enter áp dụng · Esc hủy"
                    : L"Font size in points · 1–2880 pt · Enter applies · Esc cancels";
                else tip_text = is_vi
                    ? L"Bật/tắt đậm, nghiêng hoặc gạch chân · Ctrl+B/I/U khi đang soạn"
                    : L"Toggle bold, italic, or underline · Ctrl+B/I/U while editing";
            }
            else if (local_y >= TextPropertiesLayout::alignment_buttons_top &&
                     local_y < TextPropertiesLayout::alignment_buttons_top + TextPropertiesLayout::alignment_buttons_height)
                tip_text = is_vi ? L"Căn trái, giữa hoặc phải cho đoạn chữ"
                                 : L"Align the text paragraph left, center, or right";
            else if (local_y >= TextPropertiesLayout::frame_field_top &&
                     local_y < TextPropertiesLayout::frame_field_top + TextPropertiesLayout::field_height)
                tip_text = is_vi ? L"Độ rộng khung chữ theo " + unit_suffix + L" · 0 giữ kiểu chữ tự do · Enter áp dụng · Esc hủy"
                                 : L"Text frame width in " + unit_suffix + L" · 0 keeps point text · Enter applies · Esc cancels";
            else if (local_y >= TextPropertiesLayout::spacing_field_top &&
                     local_y < TextPropertiesLayout::spacing_field_top + TextPropertiesLayout::field_height)
                tip_text = is_vi ? L"Giãn dòng 25–1000% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                                 : L"Line spacing 25–1000% · Enter applies · Esc cancels · Ctrl+Z undoes";
            else if (local_y >= TextPropertiesLayout::image_search_top &&
                     local_y < TextPropertiesLayout::image_search_top + TextPropertiesLayout::image_search_height)
                tip_text = is_vi ? L"Tìm họ phông gần giống từ ảnh · thuật toán sẽ được bổ sung sau"
                                 : L"Find similar font families from an image · matching is planned for a later update";
        } else if (active_dock_tab_ == 0 && active_tool_ == 9) {
            if (mouse_y >= 172 && mouse_y <= 206)
                tip_text = is_vi ? L"Số cạnh của đa giác tạo tiếp theo · số nguyên 3–256"
                                 : L"Side count for the next polygon · whole number 3–256";
            else if (mouse_y >= 216 && mouse_y <= 250)
                tip_text = is_vi ? L"Góc của đỉnh đầu tiên · độ, −36000 đến 36000"
                                 : L"First vertex angle · degrees, −36000 to 36000";
        } else if (active_dock_tab_ == 0 && show_style_inspector_ &&
                   mouse_y >= 162 + 298 - style_inspector_scroll_ &&
                   mouse_y <= 162 + 328 - style_inspector_scroll_ &&
                   mouse_x >= insp_x + 12 && mouse_x <= insp_x + 268) {
            tip_text = is_vi
                ? L"Độ mờ đối tượng Path/Text · 0–100% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                : L"Path/Text object opacity · 0–100% · Enter applies · Esc cancels · Ctrl+Z undoes";
        } else if (active_dock_tab_ == 0 && show_style_inspector_) {
            const int panel_x = mouse_x - insp_x;
            const int panel_y = mouse_y - 162 + style_inspector_scroll_;
            if (panel_y >= kAppearanceBlendTop + 26 &&
                panel_y < kAppearanceBlendTop + 80) {
                tip_text = is_vi
                    ? L"Chọn Blend mode của toàn object; đây là compositing, không phải công cụ Blend hình học"
                    : L"Choose whole-object compositing; this is not Illustrator's geometric Blend tool";
            } else if (panel_y >= kAppearanceEffectsTop + 50 &&
                       panel_y < kAppearanceEffectsTop + 78) {
                tip_text = is_vi
                    ? L"Thêm một node cuối stack · Path/Text không khóa · Ctrl+Z hoàn tác"
                    : L"Append an effect node to the stack · unlocked Path/Text only · Ctrl+Z undoes";
            } else if (panel_y >= kAppearanceEffectListTop &&
                       panel_x >= 12 && panel_x < 268) {
                const auto snapshot = root().document().current_snapshot();
                const auto summary = selection_appearance(
                    *snapshot, root().vector_controller().selection().selected_ids());
                if (summary.effect_stack_compatible) {
                    int node_top = kAppearanceEffectListTop;
                    for (size_t index = 0; index < summary.effects.size(); ++index) {
                        const auto &effect = summary.effects[index];
                        const bool shadow = std::holds_alternative<document::DropShadowEffect>(effect.parameters);
                        const int node_height = shadow ? 176 : 118;
                        if (panel_y >= node_top && panel_y < node_top + node_height) {
                            if (panel_y < node_top + 45) {
                                tip_text = panel_x < 96
                                    ? (is_vi ? L"Bật/tắt node; Mixed sẽ bật tất cả đối tượng đã chọn"
                                             : L"Enable or disable this node; Mixed enables every selected object")
                                    : panel_x < 150
                                        ? (is_vi ? L"Đưa node lên trước · đổi thứ tự làm đổi kết quả"
                                                 : L"Move node earlier · order changes the result")
                                        : panel_x < 210
                                            ? (is_vi ? L"Đưa node xuống sau · đổi thứ tự làm đổi kết quả"
                                                     : L"Move node later · order changes the result")
                                            : (is_vi ? L"Xóa node khỏi toàn bộ vùng chọn · Ctrl+Z khôi phục"
                                                     : L"Remove this node from the complete selection · Ctrl+Z restores it");
                            } else if (shadow && panel_y < node_top + 136 && panel_x >= 96) {
                                tip_text = is_vi
                                    ? L"Màu nguồn của bóng · đổi màu có thể chuyển hệ màu, thông số khác giữ nguyên"
                                    : L"Shadow source color · changing model may convert color; other parameters stay intact";
                            } else if (shadow && panel_y >= node_top + 140) {
                                tip_text = is_vi
                                    ? L"Độ mờ riêng của bóng · 0–100% · không thay Object Opacity"
                                    : L"Shadow opacity · 0–100% · separate from Object Opacity";
                            } else if (shadow && panel_y >= node_top + 78) {
                                tip_text = is_vi
                                    ? L"Sigma X/Y theo " + unit_suffix + L" cục bộ · giá trị lớn có thể vượt ngân sách xem trước"
                                    : L"Local " + unit_suffix + L" Sigma X/Y · large values may exceed preview budgets";
                            } else if (shadow) {
                                tip_text = is_vi
                                    ? L"Độ lệch X/Y theo trục cục bộ " + unit_suffix + L"; +Y hướng lên"
                                    : L"Local " + unit_suffix + L" X/Y offset; positive Y points up";
                            } else {
                                tip_text = is_vi
                                    ? L"Gaussian sigma X/Y theo " + unit_suffix + L" cục bộ · không lưu pixel hoặc zoom"
                                    : L"Gaussian local " + unit_suffix + L" Sigma X/Y · pixel radius and zoom are not stored";
                            }
                            break;
                        }
                        node_top += node_height + 10;
                    }
                }
            }
        } else if (active_dock_tab_ == 0 && show_align_panel_) {
            if (mouse_y >= 164 && mouse_y < 185) {
                tip_text = is_vi
                    ? L"Căn theo vùng chọn, đối tượng chọn cuối hoặc trang; Giữa trang căn toàn bộ vùng chọn vào trang"
                    : L"Align to selection bounds, last selected object, or page; Center Page moves the selection as a group";
            } else if (mouse_y >= AlignPanelMetrics::reference_top &&
                       mouse_y < AlignPanelMetrics::reference_top + AlignPanelMetrics::reference_height) {
                tip_text = is_vi ? L"Chọn mốc cho sáu lệnh căn chỉnh" : L"Choose the reference for the six Align commands";
            } else if (mouse_y >= AlignPanelMetrics::align_top && mouse_y < AlignPanelMetrics::align_top + 58) {
                tip_text = is_vi ? L"Căn cạnh hoặc tâm đối tượng theo mốc đang chọn" : L"Align object edges or centers to the selected reference";
            } else if ((mouse_y >= AlignPanelMetrics::horizontal_top && mouse_y < AlignPanelMetrics::horizontal_top + 56) ||
                       (mouse_y >= AlignPanelMetrics::vertical_top && mouse_y < AlignPanelMetrics::vertical_top + 56)) {
                tip_text = is_vi
                    ? L"Phân bổ khoảng hở, tâm hoặc cạnh theo trục; khoảng hở tự động cần ít nhất 3 đối tượng"
                    : L"Distribute gaps, centers, or edges along an axis; automatic equal gaps need at least 3 objects";
            } else if (mouse_y >= AlignPanelMetrics::gap_field_top &&
                       mouse_y < AlignPanelMetrics::gap_field_top + AlignPanelMetrics::gap_field_height) {
                tip_text = is_vi ? L"Nhấp để nhập khoảng hở cố định (" + unit_suffix + L"); Enter xác nhận, Esc hủy" : L"Enter a fixed gap (" + unit_suffix + L"); Enter applies the value, Esc cancels";
            } else if (mouse_y >= AlignPanelMetrics::apply_top &&
                       mouse_y < AlignPanelMetrics::apply_top + AlignPanelMetrics::apply_height) {
                tip_text = is_vi ? L"Áp dụng khoảng hở nhập ở trên theo chiều ngang hoặc dọc" : L"Apply the entered gap horizontally or vertically";
            }
        }
    }

    // 6. Page navigation row.
    else if (mouse_y >= h - page_navigation::kBottomChromeHeight &&
             mouse_y < h - page_navigation::kStatusBarHeight) {
        const auto snapshot = root().document().current_snapshot();
        if (snapshot && !snapshot->pages().empty()) {
            const auto active_it = std::find_if(snapshot->pages().begin(), snapshot->pages().end(),
                [&](const auto& page) { return page.id == snapshot->active_page_id(); });
            const size_t active_index = active_it == snapshot->pages().end() ? 0
                : static_cast<size_t>(std::distance(snapshot->pages().begin(), active_it));
            const auto nav = page_navigation::make_layout(w, snapshot->pages().size(), active_index);
            if (nav.first.contains(mouse_x))
                tip_text = is_vi ? L"Đến trang đầu" : L"Go to first page";
            else if (nav.previous.contains(mouse_x))
                tip_text = is_vi ? L"Trang trước · Alt+PageUp" : L"Previous page · Alt+PageUp";
            else if (nav.next.contains(mouse_x))
                tip_text = is_vi ? L"Trang sau · Alt+PageDown" : L"Next page · Alt+PageDown";
            else if (nav.last.contains(mouse_x))
                tip_text = is_vi ? L"Đến trang cuối" : L"Go to last page";
            else if (nav.add.contains(mouse_x))
                tip_text = is_vi ? L"Thêm trang · Ctrl+Z để hoàn tác" : L"Add page · Ctrl+Z to undo";
            else if (nav.remove.contains(mouse_x))
                tip_text = is_vi ? L"Xóa trang hiện tại · Ctrl+Z để hoàn tác" : L"Delete current page · Ctrl+Z to undo";
            else {
                for (size_t i = nav.first_visible; i < nav.first_visible + nav.visible_count; ++i) {
                    if (!nav.tab(i).contains(mouse_x)) continue;
                    tip_text = is_vi ? L"Mở " : L"Open ";
                    tip_text += utf8_to_wide(snapshot->pages()[i].name);
                    break;
                }
            }
        }
    }
    // 7. Bottom Status Bar.
    else if (mouse_y >= h - page_navigation::kStatusBarHeight && mouse_y <= h) {
        const int color_badge_left = std::max(16, w - 385);
        if (mouse_x >= color_badge_left && mouse_x <= w - 245) {
            tip_text = is_vi ? L"Hệ màu tài liệu và trạng thái mô phỏng màu" : L"Document color mode and proof status";
        } else if (mouse_x >= w - 210 && mouse_x <= w - 145) {
            tip_text = is_vi ? L"Thu phóng vừa vặn toàn bộ trang in trong khung nhìn (Fit Page)" : L"Zoom to fit entire page in view";
        } else if (mouse_x >= w - 120 && mouse_x <= w - 95) {
            tip_text = is_vi ? L"Thu nhỏ góc nhìn vùng vẽ (-)" : L"Zoom Out (-)";
        } else if (mouse_x >= w - 45 && mouse_x <= w - 20) {
            tip_text = is_vi ? L"Phóng to góc nhìn vùng vẽ (+)" : L"Zoom In (+)";
        }
    }

    // 8. Canvas Rulers & Origin
    const RECT c_area = canvas_rect(w, h);
    if (mouse_x >= 52 && mouse_x < c_area.left && mouse_y >= 102 && mouse_y < c_area.top) {
        tip_text = is_vi ? L"Gốc tọa độ thước (0,0) · Phím Ctrl+; để Ẩn/Hiện đường gióng"
                         : L"Ruler Origin (0,0) · Shortcut: Ctrl+; to toggle guidelines";
    } else if (mouse_x >= c_area.left && mouse_x <= c_area.right && mouse_y >= 102 && mouse_y < c_area.top) {
        tip_text = is_vi ? L"Thước đo ngang · Nhấp và kéo chuột xuống để tạo đường gióng ngang"
                         : L"Horizontal Ruler · Drag down to place horizontal guideline";
    } else if (mouse_x >= 52 && mouse_x < c_area.left && mouse_y >= c_area.top && mouse_y <= c_area.bottom) {
        tip_text = is_vi ? L"Thước đo dọc · Nhấp và kéo chuột sang phải để tạo đường gióng dọc"
                         : L"Vertical Ruler · Drag right to place vertical guideline";
    }

    // Update state
    if(active_tool_==0&&powerclip_properties_target()) {
        const bool vertical=!show_style_inspector_&&!show_align_panel_&&active_dock_tab_==0&&mouse_x>=w-280;
        for(const auto& box:powerclip_ui::layout(vertical?w-266:10,vertical?278:70,
            vertical?w-14:w-280-10,vertical,is_in_isolation_mode()))
            if(mouse_x>=box.left&&mouse_x<box.right&&mouse_y>=box.top&&mouse_y<box.bottom)
                tip_text=utf8_to_wide(std::string(powerclip_ui::controls[box.control].help));
    }
    std::wstring prev_text = tooltip_.text;

    if (!tip_text.empty()) {
        tooltip_.visible = true;
        tooltip_.x = mouse_x;
        tooltip_.y = mouse_y;
        tooltip_.text = tip_text;
    } else {
        tooltip_.visible = false;
        tooltip_.text.clear();
    }

    // Also reflect into status bar for immediate feedback
    if (tooltip_.visible && !tooltip_.text.empty() && prev_text != tooltip_.text) {
        status_msg_ = wide_to_utf8(tooltip_.text);
    }
}

void NativeWindow::add_guideline(GuidelineOrientation orient, double pos_mm) {
    if (next_guideline_id_ == 0) return;
    const auto id = next_guideline_id_;
    next_guideline_id_ = id == std::numeric_limits<uint64_t>::max() ? 0 : id + 1;
    guidelines_.push_back({orient, pos_mm, false, id});
    const auto position = format_mm_display_utf8(pos_mm, display_length_unit_, 5);
    if (orient == GuidelineOrientation::Horizontal) {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thêm đường gióng ngang: Y = " + position
            : "Added horizontal guideline: Y = " + position;
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã thêm đường gióng dọc: X = " + position
            : "Added vertical guideline: X = " + position;
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::toggle_guidelines() noexcept {
    show_guidelines_ = !show_guidelines_;
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? (show_guidelines_ ? "Đã bật hiển thị đường gióng (Ctrl+;)" : "Đã ẩn đường gióng (Ctrl+;)")
        : (show_guidelines_ ? "Showing guidelines (Ctrl+;)" : "Guidelines hidden (Ctrl+;)");
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::clear_guidelines() noexcept {
    selected_guideline_index_ = -1;
    size_t count = guidelines_.size();
    guidelines_.clear();
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã xóa toàn bộ " + std::to_string(count) + " đường gióng"
        : "Cleared all " + std::to_string(count) + " guidelines";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::delete_selected_guideline() noexcept {
    if (selected_guideline_index_ >= 0 &&
        selected_guideline_index_ < static_cast<int>(guidelines_.size())) {
        guidelines_.erase(guidelines_.begin() + selected_guideline_index_);
        selected_guideline_index_ = -1;
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã xóa đường gióng được chọn"
            : "Deleted selected guideline";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return true;
    }
    return false;
}

int NativeWindow::guideline_at(int x, int y) const {
    if (!show_guidelines_ || guidelines_.empty()) return -1;
    const RECT area = canvas_rect(width_, height_);
    if (x < area.left || x > area.right || y < area.top || y > area.bottom)
        return -1;
    const auto& camera = root().renderer().camera();
    for (int i = static_cast<int>(guidelines_.size()) - 1; i >= 0; --i) {
        const auto& gl = guidelines_[i];
        if (gl.orientation == GuidelineOrientation::Horizontal) {
            const auto screen_pos = camera.world_to_screen({0.0, gl.position_mm});
            const int screen_y = area.top + static_cast<int>(std::round(screen_pos.y));
            if (std::abs(y - screen_y) <= 5) {
                return i;
            }
        } else {
            const auto screen_pos = camera.world_to_screen({gl.position_mm, 0.0});
            const int screen_x = area.left + static_cast<int>(std::round(screen_pos.x));
            if (std::abs(x - screen_x) <= 5) {
                return i;
            }
        }
    }
    return -1;
}
} // namespace pikdraw::desktop
