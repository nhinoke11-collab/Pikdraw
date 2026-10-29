# WS5 — Automation MCP/COM và IPC

Owned paths: `apps/desktop/src/automation/**`, `apps/mcp-server/**`, `apps/com-server/**`,
`modules/automation/**` (chỉ khi cần API).

Đọc trước: [C10 agent automation](../../../contracts/c10-agent-automation.md),
[automation quickstart](../../development/automation-quickstart.md), [MCP server](../../../apps/mcp-server/README.md).

Test chung: `test_pikdraw_automation_rpc`, `test_pikdraw_automation_service`, `test_pikdraw_mcp_server`,
`test_pikdraw_mcp_pipe_integration`, `test_pikdraw_com_dispatch`, `test_pikdraw_com_runtime_e2e`.

---

## WS5-01 [High] [x] `document_write_busy` bỏ sót nhiều trạng thái tương tác

- Bằng chứng (đã xác minh): `apps/desktop/src/automation/native_window_automation.cpp:203` chỉ xét inline text,
  drawing path, tool 8.
- Cách sửa: thay bằng `NativeWindow::has_active_document_interaction()` (public, const) gom: pointer drag/creating shape/
  `pointer_active_`, effect draft, gradient drag, contour draft, annotation placement/note editing, array session,
  quick dimension, numeric field đang mở, import placement, native open đang chờ. Dùng chung với WS4-03 để không lệch.
  Phối hợp với WS4 (getter nằm trong header `NativeWindow`; thêm getter là thay đổi nhỏ, ghi rõ trong commit).
- Test: E2E (`NativeWindowMcpE2eAccess`) — bắt đầu drag rồi gửi `batch.apply` → `BUSY_INTERACTION`, document không đổi;
  thả chuột → lệnh lặp lại thành công.

## WS5-02 [High] [x] `history.undo/redo` không đi qua guard busy

- Bằng chứng (đã xác minh): `native_window_automation.cpp:459` chỉ guard `batch.apply` và `object.create_rectangle`.
- Cách sửa: áp guard cho mọi operation ghi: `history.undo`, `history.redo`, mọi tool write tương lai. Tốt nhất phân loại
  operation read/write trong một bảng (`is_write_operation(op)`) để tool mới mặc định là write cho tới khi khai báo read.
- Test: đang gõ inline text → `history.undo` trả `BUSY_INTERACTION`, text draft còn nguyên.

## WS5-03 [High] [x] UI không repaint và selection không đồng bộ sau mutation từ MCP

- Bằng chứng (cần xác minh): `native_window_automation.cpp:461` trả kết quả mà không `InvalidateRect`, không prune selection.
- Cách sửa: sau mọi write thành công (so revision trước/sau): prune selection khỏi ID không còn tồn tại, reset target
  NodeTool/GradientTool nếu object đổi identity, cập nhật property bar, `InvalidateRect(hwnd, nullptr, FALSE)`.
  Gom vào `NativeWindow::on_external_document_change()` (dùng chung cho COM).
- Test: chọn object, MCP xóa object → selection rỗng, không crash khi paint; flag invalidation được đặt.

## WS5-04 [High] [x] Named pipe một instance, đọc blocking không timeout (DoS cục bộ)

- Bằng chứng (cần xác minh): `automation_pipe_host.cpp:151-159,214-226` — `nMaxInstances=1`, `read_exact` đồng bộ.
- Cách sửa: dùng overlapped I/O với timeout đọc header/body (ví dụ 5 s), đóng kết nối khi quá hạn hoặc vượt kích thước;
  cho phép nhiều instance có giới hạn (ví dụ 4) để một client treo không khóa client khác. Giữ ACL same-user.
- Test: client giả mở pipe gửi 2 byte rồi treo → sau timeout client hợp lệ vẫn kết nối và gọi được.

## WS5-05 [High] [x] Bảng session `modern_server` không được dọn (giới hạn 64)

- Bằng chứng (cần xác minh): `embedded_mcp_http_host.cpp:281-299` chỉ dọn map wrapper;
  `apps/mcp-server/src/mcp_server.cpp` khoảng `:589` từ chối khi `sessions_.size() >= 64`.
- Cách sửa: thêm `McpServer::expire_idle_sessions(now, ttl)` và gọi từ host; lưu `last_seen` mỗi session; TTL cấu hình.
- Test: tạo 64 session không DELETE, giả lập thời gian trôi qua TTL → session mới được chấp nhận.

## WS5-06 [Medium] [ ] Timer 12 ms chạy suốt đời app

- Bằng chứng (cần xác minh): `automation_pipe_host.cpp:309,345-381`; worker chờ `wait_for(20ms)` lặp.
- Cách sửa: thay timer bằng `PostMessage(WM_APP+X)` khi enqueue task (UI thread drain khi nhận message); worker chờ
  condition variable không polling.
- Test: không có client → không có `WM_TIMER` của host (đếm message); latency lệnh không tăng.

## WS5-07 [Low] [ ] Đồng nhất kiểm tra giữa HTTP, stdio và COM

- Cách sửa: liệt kê tool write ở cả ba transport; mỗi transport đi qua cùng `NativeWindowAutomationRuntime::handle`
  và guard. Thêm test bảng (table-driven) gọi cùng op qua ba đường cho kết quả status giống nhau khi busy.
