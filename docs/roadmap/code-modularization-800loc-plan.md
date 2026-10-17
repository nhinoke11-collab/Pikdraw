# Kế hoạch tách các Siêu File về ngưỡng ≤ 800 dòng theo Nhóm Tính Năng

**Trạng thái**: Sẵn sàng thực thi (Ready for Execution).  
**Đối tượng thực thi**: Agent cấp dưới / Subagent thực hiện tuần tự từng gói công việc (`M01` $\rightarrow$ `M08`).  
**Mục tiêu**: Đưa toàn bộ các file mã nguồn C++ đang vượt ngưỡng 800 dòng về mức chuẩn **350–750 dòng/file** (nằm trọn trong 1 khung nhìn `view_file` 800 dòng của AI), nhóm chặt theo tính năng (Feature Cohesion), **đảm bảo 0 lỗi biên dịch (compile), 0 lỗi liên kết (linker), và 100% pass CTest**.

---

## 1. Quy tắc Bắt buộc cho Agent Thực thi (Đọc kỹ trước khi làm)

1. **Quy trình Harness cho mỗi Task (`M01`..`M08`)**:
   - Tạo và nhận task với đúng danh sách `--path` được liệt kê trong từng mục:
     ```powershell
     powershell -NoProfile -ExecutionPolicy Bypass -File tooling/pikdraw.ps1 task create <task-id> --title "<title>" --module <module> --goal "<goal>" --path <path1> --path <path2>
     powershell -NoProfile -ExecutionPolicy Bypass -File tooling/pikdraw.ps1 task claim <task-id> --owner codex:modularizer
     ```
2. **Không làm gãy Liên kết (Zero Linker Errors)**:
   - Khi cắt (cut) các phương thức của một class (ví dụ `DocumentWorkflow::...` hoặc `NativeWindow::...`) sang file `.cpp` mới:
     - **Giữ nguyên 100% chữ ký hàm (function signature) và thân hàm**, không sửa logic bên trong.
     - Sao chép khối `#include` ở đầu file gốc sang đầu file `.cpp` mới, bọc trong đúng `namespace` của file gốc.
     - **Xử lý hàm phụ trợ trong `namespace { ... }` (Anonymous Namespace)**:
       - Nếu một hàm/class trong `namespace { ... }` chỉ được gọi bởi các hàm ở file mới $\rightarrow$ **di chuyển (move)** nó sang `namespace { ... }` của file mới.
       - Nếu nó được gọi ở cả file cũ lẫn file mới $\rightarrow$ đưa nó vào một file header nội bộ `*_internal.hpp` (dùng `inline`) nằm cùng thư mục `src/...` để cả 2 file `.cpp` cùng `#include`. **Tuyệt đối không để hàm không dùng trong `namespace { ... }` gây cảnh báo `-Wunused-function`.**
3. **Cập nhật `CMakeLists.txt` đồng bộ**:
   - Ngay khi tạo file `.cpp` mới, phải thêm đường dẫn tương đối của file đó vào `add_library(...)` trong `CMakeLists.txt` của module trước khi chạy lệnh build.
4. **Bảo vệ các file đang có thay đổi chưa commit của task khác**:
   - Không chạm vào `apps/desktop/src/shell/native_window_selection.cpp`, `native_window_selection_paint.cpp`, `native_window_shortcuts.cpp` và `apps/desktop/tests/test_desktop_shell.cpp` cho tới khi các thay đổi đang dở trên đó được hoàn tất.

---

## 2. Chi tiết từng Gói Công Việc (`M01` $\rightarrow$ `M08`)

### Gói M01: Tách module `automation` (`automation_rpc.cpp` & `automation_service.cpp`)
- **Module**: `automation`
- **Owned paths**: `modules/automation/src`, `modules/automation/CMakeLists.txt`
- **Hiện trạng**:
  - `modules/automation/src/automation_rpc.cpp` (**1.237 dòng**)
  - `modules/automation/src/automation_service.cpp` (**1.078 dòng**)
- **Các bước thực hiện chính xác**:
  1. **Tách `automation_rpc.cpp` (1.237 dòng) thành 2 file (~600 dòng/file)**:
     - Tạo `modules/automation/src/automation_rpc_parsers.hpp` (hoặc `.cpp`): Chuyển các hàm parse JSON request trong `namespace` ở dòng `37–732` (`only_fields`, `parse_color`, `parse_target_ids`, `parse_object_id`, `parse_object_operations`, `read_revision_pair`, `parse_font` và các helper đi kèm) sang `modules/automation/src/automation_rpc_parsers.cpp` (khai báo prototype trong `automation_rpc_internal.hpp`).
     - Giữ phần session, authorization và dispatch RPC (`owns_session`, `has_scope`, `authorize`, `handle_request`, v.v. từ dòng `735–1267`) trong `modules/automation/src/automation_rpc.cpp` (~540 dòng).
  2. **Tách `automation_service.cpp` (1.078 dòng) thành 2 file (~540 dòng/file)**:
     - Tạo `modules/automation/src/automation_service_batch.cpp`: Chuyển các hàm xử lý batch, align/distribute helpers và validation phức tạp (`make_alignment_deltas`, `make_distribution_deltas`, `validate_batch`, `apply_batch`, `validate_document` ở dòng `253–497` và `666–1002`, `1056–1127`) sang `automation_service_batch.cpp` (~550 dòng).
     - Giữ các hàm core query, single/rect/object creation, undo/redo, text/font query (`capabilities`, `inspect`, `query_objects`, `create_rectangle`, `validate_rectangles`, `validate_objects`, `create_rectangles`, `create_objects`, `undo`, `redo`, `measure_text`, `query_font`) trong `automation_service.cpp` (~530 dòng).
  3. **Cập nhật `modules/automation/CMakeLists.txt`**:
     - Thêm `src/automation_rpc_parsers.cpp` và `src/automation_service_batch.cpp` vào `add_library(pikdraw_automation STATIC ...)`.
  4. **Lệnh kiểm tra bắt buộc**:
     ```powershell
     .\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_automation_service,test_pikdraw_automation_rpc -TestRegex '^test_pikdraw_automation_(service|rpc)$'
     ```

---

### Gói M02: Tách `core/rendering/src/effects/object_compositor.cpp` (1.003 dòng)
- **Module**: `rendering`
- **Owned paths**: `core/rendering/src/effects`, `core/rendering/CMakeLists.txt`
- **Các bước thực hiện chính xác**:
  1. Tạo header nội bộ `core/rendering/src/effects/object_compositor_internal.hpp` chứa các struct/hàm nội bộ dùng chung giữa phần tính bounds/surface và phần render stack (`valid_appearance`, `valid_effect_node`, `is_default_appearance`, `has_enabled_effects`, `screen_bounds`, `make_local_surface`, `encoded_to_linear`, `linear_to_encoded`, `write_target_pixel`).
  2. Tạo `core/rendering/src/effects/object_compositor_surface.cpp` (~520 dòng): Chuyển toàn bộ các hàm kiểm tra hợp lệ, tính toán hình học/bounds của hiệu ứng, khởi tạo local surface và chuyển đổi không gian màu sRGB/Linear (từ dòng `19–579` của `object_compositor.cpp`) sang file này.
  3. Giữ `compose_shadow_under_input`, `render_effect_stack` và `render_path_with_appearance` (dòng `580–1065`, ~490 dòng) trong `core/rendering/src/effects/object_compositor.cpp`.
  4. Thêm `src/effects/object_compositor_surface.cpp` vào `add_library(pikdraw_rendering STATIC ...)` trong `core/rendering/CMakeLists.txt`.
  5. **Lệnh kiểm tra bắt buộc**:
     ```powershell
     .\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_rendering_effect_compositor,test_pikdraw_rendering_gradient_compositor -TestRegex '^test_pikdraw_rendering_(effect|gradient)_compositor$'
     ```

---

### Gói M03: Tách `apps/desktop/src/workflows/document_workflow.cpp` (3.088 dòng $\rightarrow$ 4 file)
- **Module**: `desktop`
- **Owned paths**: `apps/desktop/src/workflows`, `apps/desktop/CMakeLists.txt`
- **Các bước thực hiện chính xác**:
  1. Tạo `apps/desktop/src/workflows/document_workflow_internal.hpp`:
     - Đặt các hàm helper ngắn dùng chung: `parse_hex_color`, `collect_all_descendants`, `filter_top_level_roots`, `selection_has_editable_bounds` (dòng `37–83` và `248–261` của `document_workflow.cpp`) dưới dạng `inline` trong `namespace pikdraw::desktop::detail`.
  2. Giữ lại trong `apps/desktop/src/workflows/document_workflow.cpp` (**~460 dòng**):
     - Dòng `486–901`: Constructor `DocumentWorkflow`, `add_rectangle`, `add_ellipse`, `add_vietnamese_text`, `update_text_content`, `update_text_style`, `convert_text_to_curves`, `create_table`, `select_object`, `transform_object`, `can_align_selected`, `align_selected`, `can_distribute_selected`, `distribute_selected`, `undo`, `redo`, `new_document`, `set_page_size`.
  3. Tạo `apps/desktop/src/workflows/document_workflow_clipboard_transform.cpp` (**~760 dòng**):
     - Chuyển các class command `AddClonedHierarchyCommand`, `ReplaceClonedHierarchyCommand`, `GroupObjectsCommand`, `UngroupObjectsCommand` (dòng `84–246` và `340–483`).
     - Chuyển các phương thức dòng `903–1716`: `delete_selected`, `copy_selected`, `cut_selected`, `paste`, `paste_in_place`, `paste_in_front`, `paste_in_back`, `duplicate_selected`, `duplicate_at_origin`, `is_array_session_active`, `array_session`, `start_array_session`, `apply_array_multiplier`, `apply_array_divider`, `end_array_session`, `can_group_selected`, `group_selected`, `ungroup_selected`, `is_group`, `find_parent_group`, `get_object_bounds`, `resize_object`, `move_object`, `transform_selected`, `flip_selected_horizontal`, `flip_selected_vertical`, `rotate_selected_degrees`, `rotate_selected_90_cw`, `rotate_selected_90_ccw`, `rotate_selected_180`.
  4. Tạo `apps/desktop/src/workflows/document_workflow_appearance.cpp` (**~760 dòng**):
     - Chuyển helper `valid_effect_parameters` và `parse_effect_draft_number` (dòng `277–313`).
     - Chuyển toàn bộ các phương thức Appearance & Effects ở dòng `1718–2613`: `set_selected_fill`, `set_selected_stroke`, `can_set_selected_object_opacity`, `set_selected_object_opacity`, `can_set_selected_fill_opacity`, `set_selected_fill_opacity`, `can_set_selected_stroke_opacity`, `set_selected_stroke_opacity`, `can_edit_selected_appearance`, `can_add_selected_effect`, `selected_effect_index`, `can_activate_selected_effect`, `activate_selected_effect`, toàn bộ nhóm `*_effect_draft*` và `set_selected_effect_*`.
  5. Tạo `apps/desktop/src/workflows/document_workflow_io_shaping.cpp` (**~520 dòng**):
     - Chuyển helper `is_supported_boolean_operation` và `compute_selected_boolean` (dòng `263–276` và `315–338`).
     - Chuyển các phương thức ở dòng `2615–3087`: `save_native`, `open_native`, `save_svg`, `import_svg`, `export_svg`, `can_boolean_operation_selected`, `boolean_operation_selected`, `add_bitmap_image`, `split_objects_by_line`, `trim_bitmap_with_vector`, `auto_weld_overlapping`, `snapshot_for_canvas`, `render_snapshot` (2 overloads), `sync_status_bar`.
  6. Đăng ký 3 file `.cpp` mới vào `add_library(pikdraw_desktop STATIC ...)` trong `apps/desktop/CMakeLists.txt`.
  7. **Lệnh kiểm tra bắt buộc**:
     ```powershell
     .\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_effect_draft,test_pikdraw_vertical_slice_e2e -TestRegex '^test_pikdraw_(effect_draft|vertical_slice_e2e)$'
     ```

---

### Gói M04: Tách `apps/desktop/src/shell/native_window.cpp` (3.297 dòng $\rightarrow$ 5 file)
- **Module**: `desktop`
- **Owned paths**: `apps/desktop/src/shell`, `apps/desktop/CMakeLists.txt`
- **Các bước thực hiện chính xác**:
  1. Giữ lại trong `apps/desktop/src/shell/native_window.cpp` (**~720 dòng**):
     - Dòng `27–655`: `utf8_to_wide`, `wide_to_utf8`, `initialize_default_tab`, `register_drawing_actions`, `bind_drawing_tool_to_active_document`, `active_tab`, `root`, `workflow`, `drawing_snap_*`, `switch_to_tab`, `close_tab`, `create_new_tab`, `open/close/confirm_new_doc_modal`, `set_active_page_size`, `toggle_page_orientation`, `toggle/close/select_page_preset`, `resize/move_selected_object`, `apply_fill/stroke_to_selection`, `trigger_boolean_operation`, `trigger_auto_weld`, Constructor/Destructor, `cleanup_*`, `ensure_ui_fonts`, `set/toggle_language`, `set/toggle_theme`, `canvas_background_color*`.
     - Dòng `1037–1484` và `3291–3297`: `set_active_workspace`, `set_active_dock_tab`, `zoom_in`, `zoom_out`, `fit_page_to_canvas`, `open/close/toggle_menu`, `open/close/toggle_file_menu`, `open/close/toggle_home_screen`, `set_active_home_tab`, `open_recent_document`, `create_from_template`, `trigger_export`, `export_svg_to`, `create_new_document`, `enter/exit_group_isolation`, `is_object_in_isolated_group`, `handle_double_click`, `initialize`, `run`.
  2. Tạo `apps/desktop/src/shell/native_window_settings.cpp` (**~410 dòng**):
     - Chuyển dòng `656–1035`: `open_settings_modal`, `open_ai_connections_settings`, `close_settings_modal`, `toggle_settings_modal`, `set_settings_tab`, `discard/toggle/reset/apply/activate_ai_connections_*`, `set_settings_theme_choice`, `set_settings_accent_choice`, `set_settings_unit_choice`, `set_active_tool`, `trigger_effects_contour`, `trigger_break_contour_apart`.
  3. Tạo `apps/desktop/src/shell/native_window_text_align.cpp` (**~340 dòng**):
     - Chuyển dòng `1486–1798`: `start/commit/cancel_inline_text_editing`, `cycle_font_family`, `adjust_font_size`, `toggle_font_bold`, `toggle_font_italic`, `toggle_font_underline`, `set_font_alignment`, `convert_selected_text_to_curves`, `insert_table`, `is_text_object_selected`, `align_selected_objects`, `distribute_selected_objects`, `apply_specified_spacing`, `open_inspector_properties_panel`, `open_object_properties_panel`, `open_align_distribute_panel`.
  4. Tạo `apps/desktop/src/shell/native_window_tooltips_guides.cpp` (**~530 dòng**):
     - Chuyển dòng `1799–2310`: `update_hover_tooltip`, `add_guideline`, `toggle_guidelines`, `clear_guidelines`, `guideline_at`.
  5. Tạo `apps/desktop/src/shell/native_window_numeric_input.cpp` (**~760 dòng**):
     - Chuyển dòng `2312–3290`: `start_numeric_input`, `start_effect_numeric_input`, `commit_numeric_field`, `cancel_numeric_field`, `handle_numeric_char_input`, `capture_screenshot_bmp` (bản `#ifdef _WIN32`), `handle_array_char`, `handle_array_backspace`, `commit_array_input`, `end_quick_dimension`, `apply_quick_dimension`.
  6. Đăng ký 4 file `.cpp` mới vào `apps/desktop/CMakeLists.txt`.
  7. **Lệnh kiểm tra bắt buộc**:
     ```powershell
     .\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell,test_pikdraw_drawing_tool_ui -TestRegex '^test_pikdraw_drawing_tool_ui$'
     ```

---

### Gói M05: Tách `apps/desktop/src/shell/native_window_events.cpp` (3.075 dòng $\rightarrow$ 4 file)
- **Module**: `desktop`
- **Owned paths**: `apps/desktop/src/shell`, `apps/desktop/include/pikdraw/desktop/shell/native_window.hpp`, `apps/desktop/CMakeLists.txt`
- **Các bước thực hiện chính xác**:
  1. Khai báo thêm 2 private helper methods trong `NativeWindow` (`apps/desktop/include/pikdraw/desktop/shell/native_window.hpp`):
     - `bool handle_modal_and_home_mouse_down(int x, int y);`
     - `bool handle_workspace_chrome_mouse_down(int x, int y, unsigned modifiers);`
  2. Tạo `apps/desktop/src/shell/native_window_tool_events.cpp` (**~670 dòng**):
     - Chuyển toàn bộ các phương thức điều khiển tool trên canvas ở dòng `685–1334`: `update_pen_dimension_candidate`, `handle_pen_pointer_down`, `handle_pen_pointer_move`, `pen_snap_point`, `handle_pen_pointer_up`, `handle_node_pointer_down`, `handle_node_pointer_move`, `handle_node_pointer_up`, `handle_node_property_click`, `handle_contour_property_click`, `handle_gradient_property_click`, `handle_gradient_pointer_down`, `handle_gradient_pointer_move`, `handle_gradient_pointer_up`, `handle_pen_character`, `finish_pen_session`.
  3. Tạo `apps/desktop/src/shell/native_window_modal_clicks.cpp` (**~430 dòng**):
     - Đưa phần xử lý click của New Document Modal, Settings Modal, Page Preset Dropdown và Home Screen (dòng `1391–1779` trong `handle_mouse_down`) vào hàm `bool NativeWindow::handle_modal_and_home_mouse_down(int x, int y)` (trả về `true` nếu click đã được xử lý/chặn bởi modal hoặc màn hình Home).
  4. Tạo `apps/desktop/src/shell/native_window_chrome_clicks.cpp` (**~520 dòng**):
     - Đưa phần xử lý click của Document Tab Bar, Context Tool Ribbon, Typography/Page/Object Ribbon, Grouped Toolbox & Flyouts, Right Dock Tabs, Status Bar và Ruler Guideline Drag (dòng `1780–2220` trong `handle_mouse_down`) vào hàm `bool NativeWindow::handle_workspace_chrome_mouse_down(int x, int y, unsigned modifiers)`.
  5. Giữ lại trong `apps/desktop/src/shell/native_window_events.cpp` (**~740 dòng**):
     - Dòng `26–684`: `is_menu_action_enabled`, `is_menu_action_checked`, `execute_menu_action`, `activate_toolbox_effect_item`, `handle_toolbox_key`, `handle_align_panel_click`.
     - Hàm `handle_mouse_down` gọn nhẹ (gọi `handle_modal_and_home_mouse_down`, `handle_workspace_chrome_mouse_down`, và phần click trực tiếp lên Canvas ở dòng `2221–2265`), cùng `handle_mouse_move`, `handle_mouse_up`, `window_dpi_scale`, `window_proc` (dòng `2266–3075`).
  6. Đăng ký 3 file `.cpp` mới vào `apps/desktop/CMakeLists.txt`.
  7. **Lệnh kiểm tra bắt buộc**:
     ```powershell
     .\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_pen_native_shell,test_pikdraw_drawing_tool_ui -TestRegex '^test_pikdraw_(pen_native_shell|drawing_tool_ui)$'
     ```

---

### Gói M06: Tách `native_window_paint.cpp` (2.038 dòng) & `native_window_paint_modal.cpp` (1.077 dòng)
- **Module**: `desktop`
- **Owned paths**: `apps/desktop/src/shell`, `apps/desktop/include/pikdraw/desktop/shell/native_window.hpp`, `apps/desktop/CMakeLists.txt`
- **Các bước thực hiện chính xác**:
  1. **Tách `native_window_paint.cpp` (2.038 dòng) thành 3 file**:
     - Tạo `apps/desktop/src/shell/native_window_paint_icons.hpp` và `apps/desktop/src/shell/native_window_paint_icons.cpp` (**~640 dòng**): Chuyển toàn bộ bộ vẽ icon GDI+ / GDI fallback (dòng `24–650`: `GdiplusApi`, `draw_toolbox_flyout_marker`, `draw_tool_icon_gdiplus`, `draw_tool_icon_gdi_fallback`, `draw_tool_icon`, `draw_object_context_icon`) sang `native_window_paint_icons.cpp`.
     - Tạo `apps/desktop/src/shell/native_window_paint_docks.cpp` (**~520 dòng**): Tách mục `5. Left Tool Strip` và `6. Right Inspector & Layers Dock` (dòng `1341–1840` của `NativeWindow::paint`) thành 2 hàm `NativeWindow::paint_left_toolbox(HDC mem_dc, int w, int h)` và `NativeWindow::paint_right_inspector_dock(HDC mem_dc, int w, int h)`.
     - Giữ phần còn lại của `paint_document_tab_bar` và `paint` (Canvas, Context Tool Ribbon, Topbar, Status Bar, Modals/Menus/Tooltip triggers — **~720 dòng**) trong `native_window_paint.cpp`.
  2. **Tách `native_window_paint_modal.cpp` (1.077 dòng) thành 2 file**:
     - Tạo `apps/desktop/src/shell/native_window_paint_settings_modal.cpp` (**~770 dòng**): Chuyển `NativeWindow::paint_settings_modal` (dòng `10–777`) sang file này.
     - Giữ `NativeWindow::paint_new_document_modal` (dòng `778–1077`, **~310 dòng**) trong `native_window_paint_modal.cpp`.
  3. Đăng ký `src/shell/native_window_paint_icons.cpp`, `src/shell/native_window_paint_docks.cpp`, `src/shell/native_window_paint_settings_modal.cpp` vào `apps/desktop/CMakeLists.txt`.
  4. **Lệnh kiểm tra bắt buộc**:
     ```powershell
     .\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_effect_draft,test_pikdraw_drawing_tool_ui -TestRegex '^test_pikdraw_(effect_draft|drawing_tool_ui)$'
     ```

---

### Gói M07: Tách `apps/desktop/src/shell/native_window_canvas.cpp` (1.583 dòng $\rightarrow$ 2 file)
- **Module**: `desktop`
- **Owned paths**: `apps/desktop/src/shell`, `apps/desktop/CMakeLists.txt`
- **Các bước thực hiện chính xác**:
  1. Tạo `apps/desktop/src/shell/native_window_canvas_overlays.cpp` (**~740 dòng**):
     - Chuyển các hàm vẽ overlay công cụ và thước đo (dòng `854–1583`): `paint_pen_overlay`, `paint_node_canvas_overlay`, `paint_contour_canvas_overlay`, `paint_gradient_canvas_overlay`, `paint_canvas_guidelines`, `paint_canvas_rulers` (cùng helper `screen_rect` và `draw_outline`).
  2. Giữ lại trong `apps/desktop/src/shell/native_window_canvas.cpp` (**~760 dòng**):
     - Dòng `1–853`: `canvas_rect`, `is_canvas_point`, `canvas_to_world`, `paint_document_canvas`, `paint_drawing_overlay`.
  3. Thêm `src/shell/native_window_canvas_overlays.cpp` vào `apps/desktop/CMakeLists.txt`.
  4. **Lệnh kiểm tra bắt buộc**:
     ```powershell
     .\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_pen_native_shell,test_pikdraw_drawing_tool_ui -TestRegex '^test_pikdraw_(pen_native_shell|drawing_tool_ui)$'
     ```

---

### Gói M08: Tách các file Test vượt ngưỡng 800 dòng
- **Lưu ý**: Chỉ thực hiện tách `apps/desktop/tests/test_desktop_shell.cpp` (2.429 dòng) sau khi các thay đổi local chưa commit trên file đó đã được chốt. Hai file kia (`test_drawing_tool_controller.cpp` — 951 dòng, và `test_storage_fidelity.cpp` — 849 dòng) có thể tách ngay:
  1. `core/document/tests/storage/test_storage_fidelity.cpp` (849 dòng): Tách các test case migration v2/v3/v4 appearance sang `test_storage_appearance_migration.cpp` (~400 dòng) hoặc dùng chung header helper `storage_test_helpers.hpp`, giữ mỗi file test dưới 600 dòng.
  2. `apps/desktop/tests/test_drawing_tool_controller.cpp` (951 dòng): Tách phần test snap/input grammar sang `test_drawing_tool_snap_input.cpp` (~450 dòng), giữ phần gesture/shape controller (~500 dòng).
  3. `apps/desktop/tests/test_desktop_shell.cpp` (2.429 dòng): Tách theo 4 miền kiểm thử độc lập:
     - `test_desktop_shell.cpp` (~650 dòng: window init, tabs, page presets, zoom, basic shortcuts)
     - `test_desktop_shell_align_boolean.cpp` (~600 dòng: align/distribute, group/ungroup, boolean, knife, context actions)
     - `test_desktop_shell_appearance.cpp` (~600 dòng: object/fill/stroke opacity, blend mode, effects stack & toolbox flyout)
     - `test_desktop_shell_text_settings.cpp` (~580 dòng: text editing, font controls, settings modal & AI connections)

---

## 3. Bảng tổng hợp Kết quả sau khi hoàn tất M01 $\rightarrow$ M08

| File gốc hiện tại | Số dòng cũ | Các file sau khi tách | Số dòng mới (mỗi file) |
| :--- | :---: | :--- | :---: |
| `native_window.cpp` | **3.297** | `native_window.cpp`, `native_window_settings.cpp`, `native_window_text_align.cpp`, `native_window_tooltips_guides.cpp`, `native_window_numeric_input.cpp` | **340 – 760** |
| `document_workflow.cpp` | **3.088** | `document_workflow.cpp`, `document_workflow_clipboard_transform.cpp`, `document_workflow_appearance.cpp`, `document_workflow_io_shaping.cpp` | **460 – 760** |
| `native_window_events.cpp` | **3.075** | `native_window_events.cpp`, `native_window_tool_events.cpp`, `native_window_modal_clicks.cpp`, `native_window_chrome_clicks.cpp` | **430 – 740** |
| `test_desktop_shell.cpp` | **2.429** | 4 file test theo domain (`shell`, `align_boolean`, `appearance`, `text_settings`) | **580 – 650** |
| `native_window_paint.cpp` | **2.038** | `native_window_paint.cpp`, `native_window_paint_icons.cpp`, `native_window_paint_docks.cpp` | **520 – 720** |
| `native_window_canvas.cpp` | **1.583** | `native_window_canvas.cpp`, `native_window_canvas_overlays.cpp` | **740 – 760** |
| `automation_rpc.cpp` | **1.237** | `automation_rpc.cpp`, `automation_rpc_parsers.cpp` | **540 – 680** |
| `automation_service.cpp` | **1.078** | `automation_service.cpp`, `automation_service_batch.cpp` | **530 – 550** |
| `native_window_paint_modal.cpp` | **1.077** | `native_window_paint_modal.cpp`, `native_window_paint_settings_modal.cpp` | **310 – 770** |
| `object_compositor.cpp` | **1.003** | `object_compositor.cpp`, `object_compositor_surface.cpp` | **490 – 520** |

---

### Gói M09 (Bắt buộc sau khi xong M01 $\rightarrow$ M08): Chuẩn hóa lại Tài liệu & Xóa Kế hoạch Đã Hoàn tất
Để tránh các Agent đời sau đọc nhầm bản đồ file cũ hoặc chạy lại kế hoạch đã xong, Agent thực hiện bước cuối cùng này:
1. **Cập nhật bản đồ file trong tài liệu**:
   - `apps/desktop/README.md`: Cập nhật bảng "Bản đồ tính năng" khớp với tên các file mới đã tách (`native_window_paint_icons.cpp`, `native_window_paint_docks.cpp`, `native_window_canvas_overlays.cpp`, `document_workflow_*.cpp`...).
   - `docs/development/code-organization.md`: Ghi nhận toàn bộ codebase đã đạt chuẩn $\le 800$ dòng/file.
   - `docs/project-state.json`: Cập nhật snapshot trạng thái ngắn gọn (giữ dung lượng $< 8.000$ byte).
2. **Xóa bỏ file kế hoạch đã hoàn thành**:
   - Chạy `git rm docs/roadmap/code-modularization-800loc-plan.md` để không lưu trữ kế hoạch lịch sử đã xong trong `docs/roadmap/`.
3. **Kiểm tra cổng kiến trúc**:
   - Chạy `powershell -File tooling/pikdraw.ps1 doctor` và `powershell -File tooling/native/check.ps1` đảm bảo pass 100%.

