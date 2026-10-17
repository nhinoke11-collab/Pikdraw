# Audit Pen Tool ngày 16/10/2026

> Ảnh chụp lịch sử trước P03–P05. Source hiện đã có Pen controller/core và numeric Pen; P07 shell hookup vẫn còn thiếu. Xem README và task board để biết trạng thái mới.

## Phạm vi và bằng chứng

Đọc source gốc tại HEAD `11512046d23fac08059ecd37382179283b4530a0` cộng working-tree edits
có sẵn; không coi HEAD là định danh đủ cho source đang dirty. Hash các input audit nằm trong
[validation](validation.json). Kiểm board bằng `tooling/pikdraw.ps1 task list`, tìm Pen/Bezier,
đọc action registration, menu, tool controller, model, numeric parser và kế hoạch cũ.
Không khởi chạy UI hoặc rebuild trong task tài liệu; kết luận dưới đây là source audit.

| Bằng chứng | Điều thấy được | Kết luận |
| --- | --- | --- |
| [Kế hoạch vẽ, phần 4](../drawing-tools-plan.md), [tasks](../drawing-tools-tasks.json) | D10 Pen và D11 Node vẫn planned; D10 phụ thuộc D09 | Đã có ý tưởng, chưa có delivery Pen |
| [Task kế hoạch cũ](../../../.agent/tasks/drawing-tools-plan-20261015.json) | Scope là thiết kế, không runtime; status review | Không phải task triển khai D10 |
| [Action IDs](../../../apps/desktop/include/pikdraw/desktop/actions/action_id.hpp) | Có kToolPen và kToolNodeEdit | ID khai báo chưa chứng minh handler |
| [Native action registration](../../../apps/desktop/src/shell/native_window.cpp) | register_drawing_actions đăng ký Line, Polyline, Snap; active tool nối Line/Polyline | Chưa có dispatch Pen |
| [Menu runtime](../../../apps/desktop/src/shell/native_window_menus.hpp) | Tools có Line/Polyline, không có Pen; Node disabled | Không chỉ thiếu tooltip/icon |
| [Toolbox runtime](../../../apps/desktop/src/shell/native_window_paint.cpp) và [header](../../../apps/desktop/include/pikdraw/desktop/shell/native_window.hpp) | Các slot Select/Rectangle/Ellipse/Pan/Text/Knife/Line/Polyline | Chưa có slot Pen được nối |
| [PathDrawingKind](../../../modules/vector/include/pikdraw/vector/tools/line/path_drawing_tool.hpp) | Enum chỉ Line, Polyline; không có controller tools/pen trong tree đã đọc | Không có Pen line/cubic interaction |
| [EditablePath](../../../core/document/include/pikdraw/document/model/editable_path.hpp) | Typed IDs, node mode, line/quadratic/cubic/arc, topology validation | Nền nguồn có thể reuse; phải kiểm invariants |
| [Path commands](../../../core/document/include/pikdraw/document/commands/path_commands.hpp) | Move/Split/Delete/Join/Close | Có primitives topology; chưa có node UI hoàn chỉnh |
| [Native format](../../../core/document/include/pikdraw/document/storage/native_format.hpp) và [codec](../../../core/document/src/storage/native_format.cpp) | Nguồn path authoring có native v3 | Có code persistence, không dùng đoạn mô tả v2 cũ để thiết kế lại codec |
| [Numeric API](../../../modules/vector/include/pikdraw/vector/precision/numeric/numeric_input.hpp) | @distance<angle, @dx;dy, #x;y; frame có góc X, chưa có origin tùy ý | Reuse parser; Y reference cần adapter, frame origin cần scoped extension |
| [Project state](../../project-state.json) | Line/Polyline candidate đã tích hợp, Pen nằm planned | Phù hợp các đường thực thi đã đọc |

## Những chỗ kế hoạch cũ cần làm rõ

1. D10 gộp core, handle, HUD, native UI và kiểm chứng trong một task; D11 tương tự.
   Chưa có các mốc giao nhỏ để người dùng mở app là thấy Pen dùng được.
2. Chưa có phiếu UI đầy đủ theo chuẩn mới ngày 16/10; kế hoạch này bổ sung placement,
   state và help, không tự thêm panel nổi theo mockup cũ.
3. Có góc từ X nhưng cần định nghĩa cụ thể góc từ Y, góc tương đối và điều kiện xung đột.
4. ADR 0006 còn phần mô tả baseline cũ “implementation has not started”; README vector cũng
   có câu D09 chưa tích hợp. Không dùng các câu lịch sử đó để phủ nhận source v3/D09 hiện có.
   Ngược lại, không dùng source v3 để tuyên bố mọi gate storage đã đạt trên executable hiện có.
5. Source validation hiện không cho Corner mang handle; Smooth/Symmetric đòi hai handle đối
   nhau, có độ dài khác zero. Model UI phải ánh xạ Corner không handle / Cusp độc lập;
   không ghi zero-length smooth chỉ vì một endpoint mới đang kéo.
6. D09 F3 hiện mở bảng Snap, trong khi semantics target mô tả F3 toggle tổng.
   Hành vi Ctrl/Shift trong D09 cũng chưa phải profile Pen đã nghiệm thu. P01 phải lập bản
   đồ và chốt migration, không âm thầm đổi shortcut chung khi nối Pen.
7. Nguồn test đã xóa theo yêu cầu trước đó. Task đang chạy `canvas-upgrade-p00a-inventory-20261016`
   chỉ kiểm kê test; không mặc định nó đã khôi phục, không tự phục hồi trong scope kế hoạch này.

## Trạng thái giao hiện tại

`Pen usable = false (source audit)`; `native UI exercised = false`; `runtime changed = false`.
Không có bằng chứng cần sửa sự cố mất icon của một Pen đã hoạt động. Hành động tiếp theo là
nhận P00, kiểm baseline và prerequisite test, sau đó P01/P02 trước code Pen.

