# Kế hoạch: sửa lỗi uốn đoạn path bị "trả lại" và nâng cấp nhận biết đường đang sửa (Node tool)

Ngày lập: 2026-10-22 · Phạm vi: Node tool (id 11) trên hình kín vẽ bằng Polyline/Pen.

## 1. Hiện tượng

1. Dùng Node tool kéo trực tiếp một cạnh của hình kín (tam giác vẽ bằng Poly/Pen) để uốn cong. Thả chuột thì cạnh quay lại thẳng như cũ, nhưng hai node đầu cạnh vẫn hiện là "đang chọn" (ảnh người dùng: hai ô vuông xanh, tay đòn nhỏ hai phía node trái, cạnh vẫn thẳng).
2. Khi đang ở Node tool không có dấu hiệu rõ ràng đường/đoạn nào đang được chọn và sửa được; chỉ có ô vuông node 9 px.

## 2. Nguyên nhân (đã đối chiếu mã, cần xác nhận bằng test tái hiện ở Phase 0)

| # | Nguyên nhân | Vị trí | Mức chắc chắn |
|---|---|---|---|
| RC1 | `BendSegmentCommand` chỉ ghi **một** tay đòn mỗi đầu đoạn và chỉ đổi mode khi node là `Corner/AutoSmooth`. Node `Smooth/Symmetric` giữ nguyên mode nhưng tay đòn không còn đối xứng/thẳng hàng, nên `EditablePath::validation_error()` trả "Smooth handles must be opposing and collinear" và lệnh bị từ chối. Pen tool tạo node `Symmetric` (`pen_tool_controller.cpp:113,150,422`), khớp với ảnh (node trái có hai tay đòn đối diện). | [node_commands.cpp#L361-L374](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/core/document/src/commands/node_commands.cpp#L361-L374), [editable_path.hpp#L114-L132](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/core/document/include/pikdraw/document/model/editable_path.hpp#L114-L132) | Cao (hình Pen). Test hiện tại chỉ phủ node `Corner` (`make_test_quad`). |
| RC2 | Lỗi bị nuốt: `pointer_up` gọi `(void)execute_command(...)`, rồi vẫn đặt `selected_nodes_` và UI báo "Đã cập nhật (N node)" kể cả khi lệnh thất bại. Người dùng thấy hình "tự trả lại" mà không có lý do. | [node_tool.cpp#L646-L666](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/modules/vector/src/tools/node/node_tool.cpp#L646-L666), [native_window_tool_events.cpp#L522-L549](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/apps/desktop/src/shell/native_window_tool_events.cpp#L522-L549) | Cao |
| RC3 | `hit_test_segment` coi mọi đoạn là đường thẳng nối hai anchor và trả đoạn đầu tiên trong tolerance. Đoạn đã cong bị bắt sai/trượt; trong hình kín gần đỉnh có thể bắt nhầm cạnh kề. `click_t` cũng tính trên dây cung. | [node_tool.cpp#L318-L346](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/modules/vector/src/tools/node/node_tool.cpp#L318-L346) | Cao |
| RC4 | Đoạn `Quadratic`/`Arc` khi uốn được giả định là thẳng (`c1,c2` = 1/3, 2/3 dây cung) nên hình nhảy khi bắt đầu kéo; `AutoSmooth` không được "vật chất hóa" tay đòn trước khi ghi đè nên cạnh kề đổi hình. | [node_tool.cpp#L460-L468](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/modules/vector/src/tools/node/node_tool.cpp#L460-L468) | Trung bình |
| RC5 | Preview không phản ánh kết quả: lúc uốn chỉ vẽ một đường cong overlay đè lên, cạnh cũ và vùng tô vẫn là hình gốc. Preview Move node/handle vẽ cả viền một màu, bỏ qua `Quadratic/Arc/AutoSmooth`, nên preview khác kết quả cuối. Hai vòng vẽ lặp mã. | [native_window_canvas_overlays.cpp#L414-L572](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/apps/desktop/src/shell/native_window_canvas_overlays.cpp#L414-L572) | Cao |
| RC6 | Không có khái niệm "đường đang chọn" trong overlay: `selected_segment_` là private và không có getter; không có hover đoạn; không vẽ outline path đang sửa. | [node_tool.hpp#L113-L121](file:///c:/Users/CK%20ART%20STAGE/Downloads/PikDraw/modules/vector/include/pikdraw/vector/tools/node/node_tool.hpp#L113-L121) | Cao |

## 3. Kế hoạch triển khai

### Phase 0 – Tái hiện (chặn mọi sửa đổi khác)
- Mở rộng `modules/vector/tests/tools/node/test_node_tool.cpp` và `core/document/tests/commands/test_node_commands.cpp` (không tạo file test rời):
  - Tam giác kín node `Corner` (kiểu Poly) và tam giác kín node `Symmetric` có tay đòn nhỏ (kiểu Pen).
  - Uốn lần lượt từng đoạn, gồm đoạn đóng kín (node cuối → node 0); assert lệnh thành công, `kind == CubicBezier`, các đoạn khác không đổi, `validation_error()` rỗng, undo một bước.
  - Hiện tại case Symmetric dự kiến FAIL → xác nhận RC1.
- Ghi log tạm kết quả `CommandResult` để xác nhận trên app thật, xóa sau khi xong.

### Phase 1 – Sửa lõi lệnh uốn (RC1, RC4)
- `BendSegmentCommand::apply_edit`:
  - Node đầu/cuối là `Smooth/Symmetric` → chuyển `Cusp` (chỉ một phía tay đòn đổi; giữ tay đòn phía kia như cũ). Không bao giờ để lệnh sinh trạng thái vi phạm validation.
  - Node `AutoSmooth` → ghi cả hai tay đòn lấy từ `editable_path_handles_for` rồi mới ghi đè phía uốn, mode `Cusp`, để cạnh kề không đổi hình.
- `NodeTool::pointer_down` (segment): điểm khởi đầu `c1,c2` lấy theo đường cong thật: Line → 1/3-2/3; Cubic → tay đòn hiện có; Quadratic → nâng bậc đúng; Arc → xấp xỉ cubic từ `EllipticArc` (không đổi hình lúc bắt đầu kéo).
- Giữ ID ổn định, một lệnh một undo, mapping `Preserved` như hiện nay.

### Phase 2 – Không nuốt lỗi (RC2)
- `NodeTool::pointer_up` trả cấu trúc `NodeEditResult { committed, message }` (lưu thêm `last_result()`); chỉ cập nhật `selected_nodes_` khi `committed`.
- `handle_node_pointer_up` hiển thị `message` khi thất bại (VN/EN), bỏ câu "Đã cập nhật" khi không có thay đổi. Áp dụng cho MoveNode, MoveHandle, BendSegment, RoundCorner.

### Phase 3 – Hit-test đoạn chính xác (RC3)
- Dùng `segment_curve()` (đã có ở `path_command_support.hpp`) và truy vấn điểm gần nhất trên Line/Quad/Cubic/Arc; dung sai tính theo pixel (8 px / zoom như hiện nay).
- Duyệt toàn bộ subpath, chọn đoạn **gần nhất**; ưu tiên node > tay đòn > đoạn; `click_t` lấy theo tham số đường cong.
- Thêm test: đoạn đã cong vẫn chọn được tại điểm giữa; đỉnh hình kín chọn đúng cạnh theo hướng con trỏ.

### Phase 4 – Nhận biết đường đang chọn và sửa (RC5, RC6)
Chi tiết spec UI (theo `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md`; dùng bề mặt hiện có, không thêm menu/toolbar mới):

| Mục | Quyết định |
|---|---|
| Đường menu / action ID | Không thêm. Dùng Node tool hiện có (toolbox hàng Path Drawing, id 11). Không áp dụng cho action mới. |
| Toolbox / Property bar | Giữ nút hiện có (Cusp/Smooth/Symmetric, Line/Curve ở property bar Node). Thêm nhãn trạng thái ngắn "Đoạn: Thẳng / Cong" khi có đoạn đang chọn. |
| Properties dock | Không đổi (X/Y node, mode). |
| Icon SVG | Không áp dụng (không thêm nút). |
| Outline đối tượng đang sửa | Vẽ toàn bộ viền path đang sửa 1.5 px màu nhấn (cyan `#00F2C3` theme tối, `#0EA5E9` theme sáng); đối tượng khác không vẽ. Đối tượng khóa/ẩn/không sửa được: viền xám nét đứt + status giải thích. |
| Đoạn hover | Nét dày 2.5 px màu nhấn + tooltip "Đoạn · kéo để uốn · nhấp đúp để thêm node". |
| Đoạn đang chọn | Nét 3 px màu hổ phách `#F59E0B` (khác màu nhấn) để phân biệt với outline; sau khi uốn xong đoạn đó vẫn được giữ chọn (thay vì chọn hai node). |
| Node | Chưa chọn: nền trắng viền tối; đã chọn: nền màu nhấn; hover: phóng to 1.3×; điểm đầu/cuối subpath hở dùng hình thoi. Không chỉ dựa vào màu: kích thước/hình/độ dày cũng khác. |
| Tay đòn | Chỉ hiện cho node đã chọn (như hiện nay) và cho hai đầu đoạn đang chọn; ghi chú độ dài tối thiểu hiển thị 6 px khi tay đòn quá ngắn (như Pen tay đòn nhỏ). |
| Preview thời gian thực | Uốn/Move/Handle: vẽ đường preview đúng loại đoạn bằng **một hàm dùng chung** (Line/Quad/Cubic/Arc) và vẽ cạnh gốc dạng "ghost" 40% opacity. Mục tiêu nâng cao: `DocumentWorkflow::preview_path_edit` tạo snapshot ứng viên như `preview_text_edit` để vùng tô cập nhật trực tiếp; bị từ chối khi revision/epoch cũ. |
| Label ngắn / status hint | VN: "Đang uốn đoạn · thả chuột để áp dụng · Esc hủy"; thất bại: lý do cụ thể từ `NodeEditResult`. EN tương ứng. |
| Hướng dẫn dùng | Cập nhật mục Node tool trong tài liệu người dùng (VN): chọn path, hover đoạn, kéo uốn, màu sắc ý nghĩa. |
| Contrast / accessibility | Tương phản ≥ 3:1 trên nền canvas ở cả hai theme; không dùng màu đơn lẻ làm tín hiệu duy nhất. |

Cấu trúc mã: tách phần node overlay khỏi `native_window_canvas_overlays.cpp` (1278 dòng) sang `native_window_node_overlay.cpp` mới, có hàm `draw_editable_segment(...)` dùng chung thay hai vòng lặp lặp mã. Thêm getter `NodeTool::selected_segment()`, `hover_segment()`, `hover_node()`; cập nhật hover trong `handle_node_pointer_move`.

### Phase 5 – Kiểm chứng
- `tooling/native/build.ps1 -Test -TestTarget <target node/vector> -TestRegex "node"` cho test lõi; `-TestTarget test_pikdraw_desktop_shell -TestRegex "^test_pikdraw_desktop_shell$"` cho shell.
- Test shell mới (mở rộng test hiện có): mô phỏng mouse down/move/up trên cạnh tam giác Pen (Symmetric) và Poly (Corner); sau thả chuột cạnh còn cong, status không báo "đã cập nhật" khi thất bại, đoạn vẫn được chọn.
- Build `tooling/native/build.ps1` (app chính) và kiểm tra thời gian `build/win-dev/apps/desktop/pikdraw.exe`.
- Kiểm tra thủ công theo Acceptance bên dưới; dọn ảnh/log/test tạm.

## 4. Ma trận tương thích tài nguyên vector

| Định dạng | Liên quan | Ghi chú |
|---|---|---|
| SVG, DXF | Có | Path nhập vào qua `editable_path_from_path2d`; có thể chứa Quad/Arc → Phase 1 phải uốn được và không làm mất nghĩa; DXF bulge → Arc. |
| PDF, AI, EPS | Có | Chỉ chạm qua mô hình trung lập sau khi adapter chuẩn hóa; không đổi parser. Cubic là chủ yếu. |
| CDR | Chưa áp dụng | Chưa có adapter ổn định; hành vi sẽ đi qua cùng mô hình khi có. |

Tách kết quả: xem/render (không đổi), editability (Phase 1–3), metadata nguồn (giữ ID và `contour_spec`), export round-trip (cubic xuất SVG/PDF bình thường, kiểm ở test export hiện có), khả năng sản xuất (không đổi).

## 5. Tiêu chí chấp nhận

1. Uốn bất kỳ cạnh nào (kể cả cạnh đóng) của hình kín Poly (Corner) và Pen (Symmetric/Smooth) giữ kết quả sau khi thả chuột; Ctrl+Z hoàn tác đúng một bước.
2. Khi không thể áp dụng, hình không đổi và status nêu lý do; không báo "Đã cập nhật".
3. Đoạn đã cong vẫn chọn/uốn được; không bắt nhầm cạnh kề.
4. Vào Node tool thấy rõ path đang sửa (outline), đoạn hover, đoạn đang chọn, node chọn/chưa chọn; hoạt động ở theme sáng và tối.
5. Preview khi kéo trùng kết quả cuối (cùng loại đoạn).
6. Test lõi và test shell pass; file mới và file sửa < 2000 dòng.

## 6. Thứ tự và rủi ro

- Thứ tự: Phase 0 → 1 → 2 → 3 → 4 → 5. Phase 1–2 sửa dứt điểm lỗi "trả lại" và có thể giao riêng.
- Rủi ro: đổi mode node sang `Cusp` thay đổi cách tay đòn đối xứng được mô tả trong Properties (kiểm tra `sync` mode trên dock); preview snapshot cần tránh tốn hiệu năng trên path lớn (giới hạn tần suất như live text scale).
