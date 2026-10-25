# Agent thao tác PikDraw nhanh và chính xác

Áp dụng cho Codex/Claude hoặc client MCP khác. API hiện tại được mô tả trong
[MCP server](../../apps/mcp-server/README.md); schema trả bởi server đang chạy là nguồn quyết định.
Không tải toàn bộ docs hoặc toàn bộ scene vào context để thực hiện một sửa đổi nhỏ.

## Luồng dùng API hiện có

1. Người dùng bật Automation trong Settings và cấp scope cần thiết. Connect session đúng tab;
   lấy capabilities/inspect, giữ document ID, epoch và revision. Cache schema trong session.
2. Nếu cần tìm object: objects.query với limit nhỏ, tối đa 100. Trang đầu trả next_cursor; trang tiếp
   theo gửi cursor đó cùng epoch/revision từ trang đầu. Offset vẫn hỗ trợ client cũ. Với danh sách,
   gửi fields như id/type/name/bounds để tránh tải path nodes; bỏ fields để giữ response đầy đủ cũ.
   Object đã biết ID thì dùng objects.get. Filter và objects.changes có trong schema MCP hiện tại.
3. Với chữ mới: font.query/text.measure cho font/ngôn ngữ/kích thước sẽ dùng; cache theo inputs/font state.
   Dùng units thật, giá trị số, stable IDs, client_ref; không suy tọa độ từ screenshot nếu API có geometry.
4. Lập tối đa 256 operations thành batch, validate rồi apply trên cùng expected_epoch/revision;
   dùng request_id/idempotency_key. Một batch thành một transaction và một bước Undo.
5. Đọc receipt/created IDs và kiểm phần đã sửa. Document.validate dùng khi workflow cần bounds,
   safe margin/text layout; không chạy toàn-scene validation sau từng object nếu task không cần.
6. file.save dùng basename .pr theo grant; thiếu suffix được thêm .pr. verify_reopen khi task cần
   xác minh persistence. Kết quả lưu file không tự chứng minh pixel fidelity hoặc CNC.

BUSY_INTERACTION: chờ interaction kết thúc rồi đọc revision; không ép commit qua draft của người dùng.
SESSION_EXPIRED/stale: reconnect/inspect và tính lại intent trên state mới. Timeout write: dùng request
lookup/idempotency để biết kết quả; không retry bằng key mới gây thêm object trùng.
Read-only task chỉ cần read grant. Không yêu cầu quyền write nếu người dùng chỉ xem/đo.

## Giới hạn hiện tại

file_name chỉ là basename ASCII an toàn tối đa 100 ký tự, không slash/colon/.. hay Windows device name.
File grant nằm trong %LOCALAPPDATA%/PikDraw/automation/files; không coi filename là quyền đọc path tùy ý.
Suffix duy nhất .pr; file sai magic/schema vẫn bị từ chối.

objects.query hiện duyệt trực tiếp các ID theo layer trên snapshot, không tạo toàn bộ ObjectSummary
trước khi lấy một trang. Cursor version v2 giữ vị trí layer/object và filter set, chỉ dùng cùng
epoch/revision; offset/cursor v1 vẫn tương thích. Fields projection có id/type/name/layer/bounds;
filters và `objects.changes` bounded revision delta hiện có. Delta trả tối đa 4.096 stable IDs;
response cần full query có danh sách `changed_ids` rỗng. Delta yêu cầu full query sau Undo/Redo,
thay đổi cấu trúc hoặc khi revision cũ hơn cửa sổ 128 bước. Delta subscription, job API và giảm lặp
response vẫn còn thiếu.
Client phải đọc tool schema của version đang chạy, không đoán tên tool/field từ roadmap.

## Tiêu chí ít token nhưng đủ bằng chứng

Receipt/revision và kiểm tra IDs/bounds/text cụ thể là bằng chứng thao tác semantic.
Ảnh chỉ dùng khi cần kiểm visual appearance. Không trả binary/base64 cả file thiết kế trong hội thoại;
trả đường dẫn/resource handle đúng quyền. Chỉ expand diagnostics ảnh hưởng kết quả.
Đo bytes/token/tool calls và correctness trên cùng task trước–sau; không bỏ validation/quyền/Undo để
giảm số calls. Khi viết source, đọc START → module AGENTS/README → contract liên quan, không nạp mọi roadmap.
