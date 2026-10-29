# WS2 — Toàn vẹn document, Undo và validation

Owned paths: `apps/desktop/src/tools/node_tool_controller.{hpp,cpp}`, `modules/vector/src/tools/node/**`,
`core/document/include/pikdraw/document/snapshots/**`, `core/document/src/{commands,snapshots}/**`,
deserializer trong `core/document/src/storage/` (sau WS1-01). Các call site trong `apps/desktop/src/shell` chỉ sửa
đúng dòng gọi đã nêu; không refactor file shell khác (thuộc WS3/WS4).

Contract cần đọc: [C2 Document](../../../contracts/c2-document.md), [C1 Geometry](../../../contracts/c1-geometry.md).

Nguyên tắc chung: mọi ghi document qua command/transaction và một logical writer; **không ghi document trong
đường paint, hit-test hay activate tool**; một thao tác người dùng = một Undo unit.

---

## WS2-01 [Critical] [x] `NodeToolController::activate` ghi document trong `WM_PAINT` và kẹt `Ctrl+Z`

- Bằng chứng (đã xác minh):
  - `apps/desktop/src/tools/node_tool_controller.cpp:14-20` và `:32-38` thực thi `SetAuthoringSourceCommand`.
  - `apps/desktop/src/shell/native_window_selection_paint.cpp:253` gọi `node_tool_->activate(rect_id)` trong paint overlay.
  - `apps/desktop/src/shell/native_window_pointer_dispatch.cpp:197` gọi khi mouse down ở Pick tool.
- Tái hiện: tạo PathObject không có `authoring_source` (ví dụ import SVG rectangle hoặc fixture), chọn bằng Pick,
  quan sát revision tăng và Redo bị xóa; `Ctrl+Z` rồi repaint → command bị thực thi lại.
- Cách sửa:
  1. `activate()` chỉ đặt target và dựng `EditablePath` tạm trong bộ nhớ (`editable_path_from_path2d`) cho overlay/hit-test;
     không gọi `execute_command`.
  2. Chuyển việc gắn authoring source vào lúc commit thao tác node đầu tiên: command commit của NodeTool được bọc
     `CompositeCommand{SetAuthoringSource, <edit>}` để một gesture = một Undo.
  3. Bỏ hẳn lời gọi `activate` trong `paint_selection_overlay`; paint chỉ đọc state. Nếu overlay cần handle góc, đọc từ
     state đã chuẩn bị ở pointer/selection change.
  4. Nhánh "không có target thì lấy path đầu tiên trong document" (`:25-44`) cũng không được ghi document; cân nhắc bỏ
     auto-pick vì gây chọn ngẫu nhiên.
- Test: mở rộng `test_pikdraw_node_tool_controller` — (a) activate không đổi `revision()`/history size,
  (b) kéo node đầu tiên tạo đúng 1 Undo step và Undo trả path gốc không có authoring source,
  (c) paint lặp lại sau Undo không tăng revision. Thêm case shell trong `test_pikdraw_desktop_shell` nếu cần.
- Nghiệm thu: không còn `execute_command` trong `activate`; `grep "node_tool_->activate"` không còn trong file paint.

## WS2-02 [High] [x] Contour tool tự convert text thành curves khi mới chọn tool

- Bằng chứng (cần xác minh dòng): `apps/desktop/src/shell/native_window_settings.cpp` khoảng `:695-701`,
  `set_active_tool(12)` gọi `workflow().convert_text_to_curves(target)`.
- Cách sửa: không mutate khi kích hoạt tool. Khi người dùng apply contour trên TextObject, tạo
  `CompositeCommand{ConvertTextToCurves, ApplyContour}`; hoặc hiển thị status hint "Contour cần curves — Ctrl+Q" theo
  [chuẩn UI](../../design/ui-ux-standard.md). Ghi lựa chọn và lý do trong báo cáo.
- Test: `test_pikdraw_contour_tool_controller` — chọn tool trên text không đổi revision; apply tạo 1 Undo step.

## WS2-03 [High] [x] Gom thao tác nhiều phần tử thành một Undo unit

- Bằng chứng (cần xác minh dòng):
  - `modules/vector/src/tools/node/node_tool.cpp` `delete_selected()`, `break_selected()`, `set_selected_mode()`,
    `convert_selected_segment()` gọi `execute_command` trong vòng lặp.
  - `apps/desktop/src/shell/native_window_shortcuts_settings.cpp` `reorder_selected_objects`, `nudge_selected_objects`.
  - `apps/desktop/src/shell/native_window_menu_actions.cpp` `ObjectOrderReverse/Lock/Unlock/Hide`.
- Cách sửa:
  1. Dùng `TransactionWriter`/`CompositeCommand` có sẵn (`core/document/src/commands/transaction_writer.cpp`).
  2. `delete_selected`: sắp xếp node theo (subpath, index) giảm dần hoặc tham chiếu theo stable node ID để tránh lệch index.
  3. Nudge nhiều object: một transform command cho cả selection (kiểm có API batch trong `DocumentWorkflow`; nếu chưa,
     thêm `transform_objects(ids, dx, dy)` tạo một composite).
- Test: `test_pikdraw_node_tool_controller`, `test_pikdraw_desktop_actions` — N phần tử → history tăng đúng 1;
  xóa 3 node liền nhau trong cùng subpath cho đúng kết quả hình học; Undo khôi phục toàn bộ.

## WS2-04 [Critical] [x] Incremental validation bị tắt vĩnh viễn sau import

- Bằng chứng (đã xác minh): `core/document/include/pikdraw/document/snapshots/document_snapshot.hpp:320-321`
  trả `nullopt` khi `page.import_fidelity_reports` hoặc `old.import_fidelity_reports` không rỗng.
- Hệ quả: mọi edit sau import chạy full O(N) validation; `changes_since()` luôn `requires_full_query`.
- Cách sửa:
  1. Đọc lý do điều kiện (report tham chiếu object IDs?). Nếu report chỉ cần kiểm khi report đổi hoặc object được
     tham chiếu bị xóa/đổi identity: thay bằng `reports_unchanged(page, old)` (so sánh shared pointer/revision hoặc
     equality) và kiểm các touched IDs có nằm trong report không.
  2. Giữ full validation khi report thêm/xóa/sửa.
- Test: thêm regression vào test snapshot/document (target chứa `document_snapshot` tests, ví dụ
  `test_pikdraw_document_writer_safety` hoặc test core document hiện có): page có report, sửa 1 object không thuộc report
  → đi đường incremental (expose qua hook test hoặc đo `changes_since` không yêu cầu full query); xóa object được report
  tham chiếu → vẫn bị từ chối/validate đúng.
- Nghiệm thu: benchmark W1 commit p95 trên page có import không tệ hơn page không import (ghi số trong roadmap hiệu năng).

## WS2-05 [Medium] [ ] `validate_object_source` chạy hai lần mỗi lần ghi object

- Bằng chứng (cần xác minh): `persistent_map.hpp:142` (`set`) và `document_snapshot.hpp:416,420` (`validate`).
- Cách sửa: giữ một điểm kiểm duy nhất. Ưu tiên giữ trong `validate()` (biên transaction) và bỏ trong `set()`,
  nếu mọi đường tạo snapshot đều đi qua `validate()`; nếu có đường không qua `validate()` (deserialize?), thêm cờ
  `set_unchecked` nội bộ chỉ cho đường đã validate sau đó.
- Test: regression nhập path có `authoring_source` không khớp vẫn bị từ chối ở commit và ở load `.pr`.

## WS2-06 [Medium] [ ] Các trường hợp rơi về full rebuild/validation quá rộng

- Bằng chứng (cần xác minh): `document_snapshot.hpp` — đổi số object, đổi `layer.object_ids` (z-order), sửa
  `GroupObject` (`same_object_authored_ids` trả false), thêm/xóa node trong EditablePath đều về full validation;
  `scene_hierarchy.hpp` tương tự cho topology.
- Cách sửa (từng bước, đo trước/sau):
  1. Add/remove object: validate object mới + kiểm ID không trùng bằng tập ID của snapshot trước (O(K log N)).
  2. Z-order trong cùng layer: tập ID không đổi → không cần kiểm uniqueness lại.
  3. EditablePath đổi node: chỉ validate path đó.
- Test: mở rộng test hierarchy/snapshot; benchmark W2 commit p95 (`benchmark_pikdraw_*`) trước/sau.
- Nghiệm thu: không làm mất kiểm tra uniqueness/reachability; regression ID trùng và group vòng vẫn bị bắt.

## WS2-07 [Medium] [ ] Deserializer dựng lại `PersistentObjectMap` ba lần

- Bằng chứng (cần xác minh, sau WS1-01): `native_format.cpp` khoảng `:1843`, `:1950-1967`, `:2018-2029` —
  chèn object rồi lặp `page.objects.all()` copy theo giá trị để gắn metadata/appearance và layer metadata.
- Cách sửa: gắn source metadata, appearance và layer metadata vào `decoded_objects` trước khi chèn map một lần;
  dùng move, không copy `ObjectVariant`.
- Test: `test_pikdraw_document_storage` + fidelity; đo thời gian load fixture lớn trước/sau.

## WS2-08 [Low] [ ] Ghi/đọc pixel RGBA8 và preview PDF từng byte

- Bằng chứng (cần xác minh): `native_format.cpp` khoảng `:985`, `:1087-1088`, `:1553-1554`, `:1644-1645`;
  `byte_codec.hpp:83-85,175-180`.
- Cách sửa: thêm `ByteWriter::bytes_le_u32_span` / `ByteReader::read_u32_array` dùng `memcpy` trên little-endian
  (static_assert hoặc nhánh byte-swap cho big-endian). Không đổi wire format.
- Test: round-trip pixel table + fixture legacy v8–v25 cho ra byte giống hệt (so CRC).
