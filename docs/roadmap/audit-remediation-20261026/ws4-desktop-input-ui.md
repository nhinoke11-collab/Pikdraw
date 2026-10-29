# WS4 — Input, phím tắt và UI desktop

Owned paths: `apps/desktop/src/shell/native_window_events.cpp`, `native_window_shortcuts.cpp`,
`native_window_shortcuts_settings.cpp`, `native_window_settings.cpp` (trừ dòng WS2-02), `native_window_measurement.cpp`,
`native_window_recovery.cpp` (phần paint modal), `native_style_dialog*.{cpp,hpp}`, `shell_geometry.cpp`,
`apps/desktop/src/actions/**`, `apps/desktop/src/tools/gradient_tool_controller.cpp` (preview),
`apps/desktop/src/workflows/document_workflow_effect_draft.cpp` (preview).

Đọc trước: [UI/UX standard](../../design/ui-ux-standard.md), [workspace layout](../../architecture/workspace-layout.md),
[C7 Actions/UI](../../../contracts/c7-actions-ui.md).

Mỗi thay đổi UI ghi rõ trong báo cáo: action ID, shortcut, label vi/en, tooltip, status hint; mục không áp dụng ghi
"không áp dụng".

---

## WS4-01 [High] [x] Pointer coalescer bị bỏ qua trên toàn canvas

- Bằng chứng (đã xác minh): `native_window_events.cpp:599` `if (is_dragging || is_canvas)` flush coalescer, gọi
  `handle_mouse_move` ngay và `UpdateWindow(hwnd)` (`:603`) mỗi `WM_MOUSEMOVE`.
- Cách sửa:
  1. Mọi move trên canvas (hover lẫn drag) đi qua `queue_coalesced_mouse_move()`; giữ sample mới nhất.
  2. Flush đồng bộ trước `WM_LBUTTONUP/RBUTTONUP`, `WM_KEYDOWN`, mất capture, và trước commit gesture (hiện coalescer đã
     flush trước message khác — xác nhận).
  3. Bỏ `UpdateWindow` trong `WM_MOUSEMOVE`; dùng `InvalidateRect` + một frame pending. Cân nhắc thay timer 16 ms bằng
     xử lý ở đầu vòng message khi không còn `WM_MOUSEMOVE` trong queue (`PeekMessage` với `PM_NOREMOVE`) để không bị
     giới hạn 60 Hz trên màn 120–240 Hz.
  4. Pen/drawing cần mọi điểm (freehand)? Nếu có tool cần lịch sử điểm, dùng `GetMouseMovePointsEx` lấy điểm bị gộp
     thay vì xử lý từng message.
- Test: `test_pikdraw_pointer_move_coalescer` + case shell: 100 move giả lập trong một lượt → ≤ 1 lần xử lý/paint;
  mouse-up nhận vị trí cuối; drag kết thúc đúng tọa độ.
- Nghiệm thu: ghi rõ chưa đo input-to-present.

## WS4-02 [High] [x] Phím `1` (Zoom 100%) gọi `zoom_in()`

- Bằng chứng (đã xác minh): `native_window_shortcuts_settings.cpp:187-188`.
- Cách sửa: tạo `NativeWindow::zoom_to_actual_size()` (hoặc dùng hàm có sẵn ở `native_window_shortcuts.cpp` nhánh `'1'`
  khoảng `:763-770`) và gọi từ action. Xóa nhánh trùng trong `handle_key_down` sau khi action đảm nhận.
- Test: `test_pikdraw_desktop_actions` — dispatch shortcut "1" → zoom 100%.

## WS4-03 [High] [x] `dispatch_canvas_shortcuts` thiếu guard trạng thái nhập liệu

- Bằng chứng (đã xác minh): `native_window_shortcuts_settings.cpp:208` chỉ kiểm inline text và numeric field.
- Cách sửa: thêm hàm `NativeWindow::is_text_input_context()` gom: inline text, numeric field, annotation note editing,
  quick dimension, array session, effect-draft numeric input, dock text editing (`is_dock_text_editing()`), font picker,
  settings field đang focus. Dùng hàm này ở `dispatch_canvas_shortcuts`, `handle_key_down` và automation (WS5-01) để
  một nguồn sự thật duy nhất.
- Test: gõ "VREAF1" khi đang sửa annotation note → text nhận đủ ký tự, tool/zoom không đổi.

## WS4-04 [High] [x] Phím căn chỉnh `R`/`E` bị phím tool cướp

- Bằng chứng (cần xác minh): `native_window_shortcuts_settings.cpp:179-182` (R/E tool) chạy trước nhánh align
  `native_window_shortcuts.cpp:725-739` khi selection ≥ 2.
- Cách sửa: theo profile CorelDRAW, alignment một phím (L/R/T/B/E/C/P) ưu tiên khi selection ≥ 2 và tool là Pick; đưa
  logic này vào `ActionDispatcher` có điều kiện enable (context predicate) thay vì nhánh rơi xuống `handle_key_down`.
  Profile Illustrator giữ R/E cho tool. Ghi bảng xung đột trong C7.
- Test: 2 object chọn, profile Corel, phím R → align right, tool vẫn Pick; 1 object → R đổi sang Rectangle.

## WS4-05 [Medium] [ ] Hai `ActionDispatcher` và nhánh `handle_key_down` song song

- Bằng chứng (cần xác minh): `NativeWindow::action_dispatcher_` + `CompositionRoot::window().action_dispatcher()` +
  `handle_key_down` ~600 dòng (`native_window_shortcuts.cpp:205-817`).
- Cách sửa: một registry action duy nhất; mỗi action có `id`, shortcut theo profile, `enabled(context)`, handler.
  Di chuyển dần các nhánh hardcode (Ctrl+]/[, Rotate90…) vào registry; `handle_key_down` chỉ còn điều hướng tool-local
  (arrow trong text, Esc hủy gesture). Làm theo từng nhóm, mỗi nhóm một commit có test.
- Test: bảng shortcut mỗi profile không có trùng; `Ctrl+]` theo profile Corel ra đúng action đã khai báo.

## WS4-06 [Medium] [ ] Tùy chỉnh phím tắt không lưu và xóa xung đột âm thầm

- Bằng chứng (cần xác minh): `g_shortcuts_state` static (`native_window_shortcuts_settings.cpp:11`);
  `action_dispatcher.cpp:53-59` clear shortcut trùng không báo.
- Cách sửa: lưu profile + override vào settings người dùng (cùng nơi lưu settings hiện có, JSON có version); khi gán
  trùng, hiện xác nhận "Phím này đang dùng cho X — thay thế?" trong Settings > Phím tắt.
- Test: gán, khởi tạo lại window (đọc settings) → còn; gán trùng → không tự xóa khi chưa xác nhận.

## WS4-07 [High] [ ] Kéo Gradient/Effect draft dựng snapshot mới mỗi mouse move

- Bằng chứng (cần xác minh): `native_window_canvas.cpp:89-92`, `gradient_tool_controller.cpp:182-187,315-384`,
  `document_workflow_effect_draft.cpp:129-173,329-360`.
- Cách sửa: sau WS4-01 (coalescing) số lần rebuild đã giảm; tiếp theo:
  1. Gradient: vẽ preview của riêng object đang sửa (render object vào layer tạm, ghép trên cache canvas không gồm object đó)
     thay vì toàn viewport. Cần API renderer "render single object to bitmap" (phối hợp WS3).
  2. Effect draft: cùng cơ chế; giới hạn tần suất theo frame.
- Test: drag 60 move → số full-scene render ≤ số frame; commit cuối đúng giá trị.

## WS4-08 [Medium] [ ] Dialog con không scale DPI

- Bằng chứng (cần xác minh): `native_style_dialog.cpp:24-28,477-490` pixel cứng, `CreateFontW(-12)`;
  `native_window_measurement.cpp:85-116` trộn dialog units và pixel.
- Cách sửa: dùng `GetDpiForWindow` + `MulDiv(value, dpi, 96)` cho mọi kích thước/font; xử lý `WM_DPICHANGED` trong
  dialog; measurement dialog đặt control bằng dialog units hoặc tính lại theo DPI.
- Test: `test_pikdraw_style_dialog` với DPI giả lập 144/192 (dùng cơ chế synthetic DPI đã có) — kích thước client ∝ DPI.

## WS4-09 [Low] [x] Recovery modal tạo/xóa font mỗi frame

- Bằng chứng (cần xác minh): `native_window_recovery.cpp:346-354,467-469`.
- Cách sửa: dùng font UI cache (`ensure_ui_fonts`).
- Test: paint modal 10 lần không tăng GDI object count (`GetGuiResources`).

## WS4-10 [Medium] [ ] Chuỗi UI cứng, thiếu message catalog

- Bằng chứng (cần xác minh): ternary `language_ == UILanguage::Vietnamese ? ... : ...` rải khắp shell;
  `native_window.cpp:579,612` "Bản vẽ " cứng; `native_window_paint_docks.cpp:419` `L"[Khóa]"` ở tiếng Anh.
- Cách sửa: tạo catalog `apps/desktop/src/localization/strings.{hpp,cpp}` (key enum → vi/en), hoặc dùng `locales/`
  nếu đã có định dạng. Bước 1 sửa các chuỗi sai ngôn ngữ đã nêu; bước 2 di chuyển dần theo file khi chạm vào, không
  big-bang.
- Test: chuyển English → tên tab mới "Drawing N", nhãn khóa tiếng Anh.

## WS4-11 [Medium] [ ] Import/compare measurement chạy đồng bộ trên UI thread

- Bằng chứng (cần xác minh): `native_window_measurement.cpp:264-475`, file tới 64 MiB.
- Cách sửa: dùng mẫu job nền như test-sheet (`kTestSheetWorkerMessage`): snapshot đầu vào bất biến, cancel, progress,
  kiểm tab/epoch trước khi áp kết quả.
- Test: `test_pikdraw_production_measurement` + case UI: import lớn không chặn message loop (đo bằng message ping).

## WS4-12 [Medium] [ ] Color picker CMYK dùng công thức device thay vì ICC

- Bằng chứng (cần xác minh): `native_style_dialog.cpp:218,677,713` gọi `cmyk_to_rgb`/`rgb_to_cmyk` của
  `native_style_dialog_math.hpp`.
- Cách sửa: khi có `ProfileColorTransform` cho profile CMYK hiện hành, dùng nó cho mặt phẳng picker và các trường
  RGB/HSB/Lab; chỉ fallback công thức device khi không có profile, kèm nhãn "không quản lý màu". Giá trị nguồn CMYK
  lưu nguyên, không round-trip qua RGB.
- Test: `test_pikdraw_style_dialog` — nhập CMYK, preview RGB khớp transform ICC trong tolerance; giá trị CMYK lưu không đổi.

## WS4-13 [Low] [ ] `ShellGeometry` không được dùng, dock phải cố định 280 px

- Bằng chứng (cần xác minh): `shell_geometry.cpp:22-47` không có call site; `native_window_paint_docks.cpp:464-466`.
- Cách sửa: hoặc nối `ShellGeometry` vào layout (splitter kéo 200–520 px, thu gọn dock, lưu settings) theo
  workspace-layout, hoặc xóa code chết nếu thiết kế quyết định dock cố định. Ghi quyết định vào workspace-layout.
- Test: nếu triển khai — kéo splitter cập nhật `canvas_rect`, hit-test inspector đúng sau resize.

## WS4-14 [Low] [ ] Accessibility tối thiểu

- Bằng chứng (cần xác minh): không có `IAccessible`/UIA provider; Tab chỉ đổi giữa W/H.
- Cách sửa (chỉ thiết kế trong đợt này): viết spec UIA provider cho toolbox, menu, property bar, dock (tên, role,
  state) trong ui-ux-standard; triển khai theo pha sau. Không áp dụng cho canvas content ngoài tên object chọn.
