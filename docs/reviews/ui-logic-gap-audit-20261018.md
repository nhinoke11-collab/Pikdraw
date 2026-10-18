# Kiểm tra logic đã có nhưng thiếu UI/UX — 18/10/2026

## Phạm vi và cách xác nhận

Kiểm tra source gốc hiện tại sau R01–R10, giữ nguyên các local edits của phiên khác.
Đối chiếu public API, implementation và test với menu, action routing, toolbox,
property bar, Properties, keyboard/pointer handlers và startup của Win32 shell.
Đối chiếu [chuẩn UI](../design/ui-ux-standard.md) và [workspace](../architecture/workspace-layout.md).
Đây là audit source; không chạy lại nghiệm thu giao diện hay suy ra UX từ test pass.
Không sửa code sản phẩm trong task này.

“Có logic” không có nghĩa tính năng đã sẵn sàng sản xuất. Phân biệt:

- **A — Chưa nối vào luồng dùng:** có implementation/controller/service nhưng shell chưa dùng.
- **B — UI một phần:** người dùng đã dùng được một phần, còn API/tham số chưa có điều khiển.
- **C — Model/prototype:** có dữ liệu hoặc thuật toán giới hạn, còn thiếu cả command/integration;
  không thể chỉ thêm nút là thành tính năng hoàn chỉnh.

Kết quả: **4 nhóm A, 5 nhóm B và 2 nhóm C**. Không tính thuật toán nội bộ không cần UI riêng.

## A — Logic chưa nối vào luồng dùng

### A1. Dimension / ghi kích thước kỹ thuật

- **Đã có:** `DimensionToolController` với các bước chọn điểm/placement/preview/commit;
  DimensionStyle; layout và formatter đơn vị, đơn vị kép, dung sai; command thêm/sửa/xóa dimension.
- **Thiếu:** không có instance/controller trong NativeWindow; không có menu/action,
  toolbox, event route, overlay preview hay Properties cho Dimension.
  Tìm `DimensionToolController` trong app chỉ thấy implementation và test controller.
- **Ảnh hưởng:** người dùng chưa thể tạo ghi kích thước từ editor dù test riêng gọi được controller.
  “Quick dimension” của shell là nhập nhanh W/H để resize/tạo hình, không phải dimension annotation.
- **Giới hạn ngoài UI:** command chuyển dimension thành PathObject qua `dimension_to_path_object`;
  hàm này chép lines/arrows nhưng chưa đưa `layout.formatted_text` thành đối tượng chữ/glyph.
  Vì vậy chưa thể coi chỉ thiếu nút hoặc đã có pipeline hiển thị số đo hoàn chỉnh.
- **Bằng chứng:** [controller](../../apps/desktop/src/tools/dimension_tool_controller.hpp),
  [commit](../../apps/desktop/src/tools/dimension_tool_controller.cpp),
  [dimension_to_path_object, dòng 168](../../core/document/src/commands/dimension_commands.cpp),
  [formatter](../../core/document/src/annotations/dimension_style.cpp),
  [test controller](../../apps/desktop/tests/test_dimension_tool_controller.cpp).

### A2. Phục hồi tài liệu và bản nháp Pen

- **Đã có:** autosave dirty tabs 60 giây, ghi/đọc checkpoint và recovery draft;
  Pen có write/stage/accept/discard recovery cùng kiểm tra provenance.
- **Thiếu:** startup không quét/danh sách phiên phục hồi; không có luồng chọn Recover/Discard,
  nhận biết bản mới hơn, xem trước hoặc mở checkpoint từ UI.
  `find_latest_checkpoint` trong shell hiện chỉ dùng để xoay bản autosave cũ.
  API recovery của Pen chưa được shell gọi.
- **Ảnh hưởng:** dữ liệu phục hồi có thể tồn tại trên đĩa nhưng người dùng chưa có đường tự phục hồi trong app.
- **Bằng chứng:** [autosave, dòng 60](../../apps/desktop/src/shell/native_window_files.cpp),
  [SaveRecoveryService](../../core/document/include/pikdraw/document/storage/save_recovery.hpp),
  [Pen recovery API, dòng 78](../../apps/desktop/src/tools/pen_tool_controller.hpp),
  [startup](../../apps/desktop/src/shell/native_window_isolation_menus.cpp).

### A3. File > Import SVG/DXF và báo cáo nhập

- **Đã có:** adapter SVG static subset, DXF ASCII 2D `dxf-entities-v1`, worker adapters,
  capability/fidelity reports và tests. Workflow có `import_svg`.
- **UI đang có:** File > Open chỉ mở native `.pikdraw`; File > Export SVG có thật;
  SVG có thể đi qua clipboard Paste.
- **Thiếu:** chọn tệp SVG/DXF từ File > Import, option đơn vị theo capability,
  tiến trình/hủy nhập và trình bày lỗi/fidelity report. Không tìm thấy caller DXF trong desktop.
- **Giới hạn integration:** `DocumentWorkflow::import_svg` hiện thêm từng object qua các command riêng,
  không dùng trang/layer/report trong kết quả; chưa phải một luồng import hoàn chỉnh có một Undo.
  Không nên chỉ nối file dialog trực tiếp vào hàm này rồi tuyên bố hoàn tất.
- **Bằng chứng:** [File menu, dòng 339](../../apps/desktop/src/shell/native_window_menus.hpp),
  [SVG workflow, dòng 109](../../apps/desktop/src/workflows/document_workflow_io_shaping.cpp),
  [SVG clipboard, dòng 389](../../apps/desktop/src/workflows/document_workflow_clipboard_transform.cpp),
  [DXF adapter](../../modules/interop/include/pikdraw/interop/adapters/dxf_importer.hpp),
  [capability matrix](../../modules/interop/src/format_capability_matrix.cpp).
- **Định dạng:** SVG và DXF liên quan; PDF/AI/CDR/EPS hiện bị từ chối, không xếp là logic đã có chờ UI.
  Không suy ra editability, metadata, round-trip hoặc khả năng sản xuất từ preview.
  Đối chiếu [tương thích vector](../architecture/vector-resource-interoperability.md),
  [C8](../../contracts/c8-import-fidelity.md) và [C9](../../contracts/c9-appearance-effects.md).

### A4. Snap tracking / điểm đã giữ / khóa tia suy luận

- **Đã có:** `SnapStatusController`: dwell acquisition, tập acquired points,
  tracking rays, polar direction và lock/unlock; có test riêng.
- **Thiếu:** NativeWindow và drawing/pen controllers không tạo hoặc gọi controller này;
  không có đường nhận kết quả inference, vẽ tia tracking, marker điểm đã giữ,
  thao tác khóa/mở khóa hoặc chỉnh dwell/max-points trong shell.
- **Phân biệt:** OSnap trực tiếp, các tùy chọn Snap và direction snapping hiện có UI;
  kết luận này chỉ áp dụng lớp inference/tracking riêng nói trên.
- **Bằng chứng:** [API, dòng 44](../../apps/desktop/src/tools/snap_status_controller.hpp),
  [implementation](../../apps/desktop/src/tools/snap_status_controller.cpp),
  [tests](../../apps/desktop/tests/test_snap_status_controller.cpp),
  [UI snap hiện có](../../apps/desktop/src/shell/native_window_canvas_snap.cpp).

## B — Đã có UI nhưng thiếu lối vào hoặc điều khiển chi tiết

### B1. Node tool: menu bị vô hiệu hóa và thiếu nhập X/Y node

- **Đã có:** controller kéo/chọn node, mode Cusp/Smooth/Symmetric, add/delete/break/join/close/reverse,
  convert segment; API `set_selected_node_position`, `move_selected_delta` và query tọa độ.
- **UI đang có:** F10 có route kích hoạt Node; action dispatcher có đăng ký;
  property bar đã vẽ và xử lý các thao tác node cơ bản.
- **Thiếu/sai nối:** mục Tools > Node Edit Tool vẫn `is_disabled=true`;
  `execute_menu_action(ToolNode)` nằm trong nhóm handler chỉ `return`.
  Không có nút Node trong toolbox hiện tại. API sửa tọa độ node chưa có caller shell;
  chưa có field X/Y node hoặc phần Properties Node chi tiết.
- **Ảnh hưởng:** tính năng khó khám phá; đường menu công khai không dẫn tới chức năng đang tồn tại.
- **Bằng chứng:** [menu, dòng 474](../../apps/desktop/src/shell/native_window_menus.hpp),
  [handler, dòng 410](../../apps/desktop/src/shell/native_window_menu_actions.cpp),
  [F10, dòng 143](../../apps/desktop/src/shell/native_window_events.cpp),
  [property click, dòng 373](../../apps/desktop/src/shell/native_window_tool_events.cpp),
  [tọa độ API, dòng 38](../../apps/desktop/src/tools/node_tool_controller.hpp).

### B2. Gradient editor chi tiết

- **Đã có:** presenter sửa/xóa stop, màu stop, opacity, offset, midpoint và geometry;
  tool có canvas handles/drag, Apply/Cancel.
- **UI đang có:** Linear/Radial, Pad/Repeat/Reflect, Reverse, Fit Bounds,
  tăng góc 45°, thêm một stop vàng cố định tại 50%, Apply/Cancel.
- **Thiếu:** danh sách/chọn stop, nhập offset, chọn màu từng stop, opacity, midpoint,
  xóa stop; nhập góc/chiều dài/tâm chính xác; Properties Gradient đầy đủ.
  Tìm các setter tương ứng chỉ thấy definition/test, không thấy shell gọi.
- **Ảnh hưởng:** có thể tạo gradient cơ bản nhưng chưa chỉnh được dải màu tùy ý qua UI.
- **Bằng chứng:** [presenter API, dòng 97](../../apps/desktop/include/pikdraw/desktop/presenters/gradient_properties.hpp),
  [UI handler, dòng 494](../../apps/desktop/src/shell/native_window_tool_events.cpp),
  [property bar](../../apps/desktop/src/shell/native_window_paint.cpp),
  [test presenter](../../apps/desktop/tests/test_gradient_tool_controller.cpp).

### B3. Contour: tham số nâng cao và Properties

- **Đã có:** direction/spacing/steps/join/miter-limit, Apply/Break/Remove/Cancel.
- **UI đang có:** menu Effects, toolbox flyout, property bar direction,
  spacing +/-0,5 mm, steps +/-, Miter/Round/Bevel, Apply và Break.
- **Thiếu:** nhập spacing/steps tùy ý bằng field; miter-limit không có caller shell;
  chưa có Properties Contour chi tiết và entry riêng gọi `ContourToolController::remove()`.
  Effects > Clear gọi effect-stack workflow, không phải caller của Contour `remove()`.
- **Bằng chứng:** [controller](../../apps/desktop/src/tools/contour_tool_controller.hpp),
  [handler, dòng 433](../../apps/desktop/src/shell/native_window_tool_events.cpp),
  [UI bar](../../apps/desktop/src/shell/native_window_paint.cpp),
  [menu routing](../../apps/desktop/src/shell/native_window_menu_actions.cpp).

### B4. Text: caret/grapheme, paragraph layout và chọn font

- **Đã có:** `hit_test_caret`, next/prev grapheme, layout wrap theo `max_width_mm`,
  `line_spacing_ratio`; workflow nhận tên font bất kỳ và cập nhật style.
- **UI đang có:** nhập text, Backspace cuối chuỗi, Shift+Enter xuống dòng,
  font family tuần tự, size/bold/italic/underline/alignment và convert curves.
- **Thiếu:** click đặt caret giữa chuỗi, chọn đoạn chữ, di chuyển caret theo grapheme;
  shell chưa gọi các API caret. Không có field text-frame width/line spacing hoặc
  font picker tìm/chọn font hệ thống; `cycle_font_family()` chỉ quay danh sách cố định.
- **Giới hạn ngoài UI:** paragraph width/spacing chưa được model TextObject lưu như
  thuộc tính authoring; Justify có enum nhưng layout chưa xử lý justify thực sự.
  Đây không phải bằng chứng một paragraph editor hoàn chỉnh đã có sẵn.
- **Bằng chứng:** [caret API](../../core/typography/include/pikdraw/typography/layout/caret_navigation.hpp),
  [layout options](../../core/typography/include/pikdraw/typography/layout/text_layout.hpp),
  [layout implementation](../../core/typography/src/layout/text_layout.cpp),
  [input cuối chuỗi, dòng 113](../../apps/desktop/src/shell/native_window_events.cpp),
  [font cycle, dòng 131](../../apps/desktop/src/shell/native_window_text_align.cpp).

### B5. Màu Spot/Grayscale và overprint

- **Đã có:** ColorValue Spot/Gray, spot name/tint/alternate RGB,
  overprint metadata; presenter so sánh/hiển thị và style patch nhận ColorValue.
  Codec/pipeline giữ màu nguồn trong phạm vi đã hỗ trợ.
- **UI đang có:** palette RGB/CMYK, dialog nhập RGB/CMYK/alpha, ICC preview.
- **Thiếu:** tạo/chỉnh spot name/tint/alternate, chế độ Gray và field overprint fill/stroke.
  Dialog source color chỉ tạo hai lựa chọn RGB/CMYK.
- **Giới hạn:** lưu metadata/preview màu không chứng nhận print separation,
  soft proof hoặc overprint simulation. Không coi các chức năng đó đã có chỉ thiếu UI.
- **Bằng chứng:** [ColorValue, dòng 52](../../core/color/include/pikdraw/color/values/color_value.hpp),
  [presenter](../../apps/desktop/src/presenters/object_style.cpp),
  [dialog, dòng 106](../../apps/desktop/src/shell/native_style_dialog.cpp).

## C — Model/prototype có một phần logic, UI hiện chưa phải editor hoàn chỉnh

### C1. Layers/Object manager và History

- Layer có name/visible/locked/root-order; engine đọc trạng thái và hierarchy.
  Undo/Redo có stack và command names.
- Tab Layers hiện chỉ in tối đa 20 tên object/ID, không dựng cây layer/group;
  không có row interaction đổi tên, chọn active layer, bật/tắt, khóa, đổi order.
- Tab History chỉ in revision/dirty/can-undo/can-redo, không có danh sách lệnh
  hoặc thao tác chọn mốc. Undo/Redo qua menu/shortcut đã có thật.
- Chưa thấy bộ command layer-management công khai hay API liệt kê history labels đầy đủ;
  vì vậy thiếu cả command/query contract, không chỉ thiếu widget.
- **Bằng chứng:** [Layer model, dòng 208](../../core/document/include/pikdraw/document/model/object.hpp),
  [Layers/History paint, dòng 529](../../apps/desktop/src/shell/native_window_paint_docks.cpp),
  [UndoHistory](../../core/document/include/pikdraw/document/history/undo_history.hpp).

### C2. Bitmap/vector clipping và nhập ảnh

- ImageObject có pixels/transform/clip_path; CPU/D2D có đường render ảnh/clip.
  Workflow có `trim_bitmap_with_vector`, được demo/tests gọi nhưng chưa có caller shell.
- Menu Bitmaps hiện đều disabled. Không có workflow chọn bitmap + vector cutter rồi
  áp crop/clip/trim qua UI.
- Logic trim hiện lấy **giao bounding box** để trích pixel, không cắt chính xác theo path.
  `add_bitmap_image` tạo procedural photo, không phải decoder nhập ảnh từ file.
  Capability matrix còn từ chối PNG/JPEG; Trace/Color Mask/Convert to Bitmap chưa được
  coi là implementation hoàn chỉnh chờ mở nút.
- **Bằng chứng:** [image model, dòng 125](../../core/document/include/pikdraw/document/model/object.hpp),
  [workflow, dòng 196 và 386](../../apps/desktop/src/workflows/document_workflow_io_shaping.cpp),
  [Bitmaps menu, dòng 454](../../apps/desktop/src/shell/native_window_menus.hpp),
  [capabilities](../../modules/interop/src/format_capability_matrix.cpp).

## Những phần không xếp vào lỗi thiếu UI

- Blur/Drop Shadow, effect-stack ordering/enable/blend/opacity đã có menu và Properties;
  Contour/Gradient cơ bản cũng đã được nối. Không gộp toàn bộ nhóm Effects thành chưa có UI.
- BVH/culling, geometry kernels, cache/readback, worker Job Object, revision/epoch guards
  và transaction writer là hạ tầng; không cần mỗi thuật toán có một nút.
- Automation/MCP/COM đã có API/adapter và Settings > AI Connections; không yêu cầu
  giao diện riêng cho từng endpoint như một công cụ vẽ.
- Raster/Production/Scene3D/plugin-sdk phần lớn vẫn planned/specification;
  PDF/AI/CDR/EPS không có adapter runtime. Không ghi chúng là “logic đã xong, thiếu UI”.

## Ưu tiên xử lý

1. Phục hồi autosave; luồng File > Import theo capability; sửa đường menu Node.
2. Properties Gradient/Contour/Node để dùng được tham số đã có.
3. Tích hợp Dimension và Snap inference sau khi hoàn thiện các mắt xích runtime còn thiếu.
4. Text editor/caret, màu Spot/Gray/overprint; Layers/History và bitmap cần bổ sung contract/core tương ứng.

Đây là thứ tự đề xuất từ audit, chưa phải plan triển khai hoặc yêu cầu sửa code.
Khi triển khai phải lập feature spec đủ menu/action/toolbox/property bar/Properties,
field scopes/states, icon, tooltip/focus/status/help và test người dùng theo chuẩn hiện có.

## Kiểm chứng và giới hạn

- Đã đọc registry, header/implementation và caller trong toàn bộ `apps/desktop/src`;
  tìm đối chiếu với `apps/desktop/tests`, core và module tests.
- Negative call-site được xác định bằng `rg` tên class/API, sau đó đọc menu/handler
  tương ứng; không suy diễn chỉ từ tên thư mục hoặc việc có một file test.
- Task chỉ tạo báo cáo này, không sửa sản phẩm, không tạo subagent, không build/test native lại.
- Bằng chứng phản ánh source tại thời điểm audit; không thay nghiệm thu runtime,
  fidelity, hiệu năng, accessibility hay khả năng sản xuất.
