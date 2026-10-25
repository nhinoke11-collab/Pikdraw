# Kết nối AI agent qua MCP

PikDraw host MCP Streamable HTTP ngay trong tiến trình desktop khi người dùng bật quyền trong
`Settings > AI Connections…`. Agent gọi công cụ semantic trực tiếp trên document runtime qua
UI-owner queue và command writer; luồng này không khởi chạy MCP subprocess, không dùng
screenshot, tọa độ màn hình, chuột hoặc bàn phím. `pikdraw-mcp-server.exe` vẫn có cho MCP client
chỉ hỗ trợ stdio.

## Bật trong PikDraw

- `Settings > AI Connections…` bật MCP chỉ đọc hoặc quyền sửa/lưu ngay trong app; mặc định tắt.
  `Apply` cập nhật listener, `Cancel` bỏ thay đổi nháp. Khi bật, Settings hiển thị URL loopback
  và bearer access key cho tiến trình app đó. Để revoke, bỏ chọn `Allow MCP access` rồi nhấn
  `Apply`; HTTP listener dừng, key hết hiệu lực và request đang chờ bị hủy.
- `Help > AI Automation…` mở hướng dẫn và trạng thái kết nối.
- `pikdraw.exe --enable-mcp` bật kết nối chỉ đọc khi khởi chạy.
- `pikdraw.exe --enable-mcp-edit` bật các quyền instance `document.create`, `document.edit`,
  `file.read` và `file.write`; client vẫn phải yêu cầu từng scope cần dùng khi connect.
- MCP bind `127.0.0.1` trên cổng ngẫu nhiên, không nghe trên mạng LAN. Access key mới được tạo
  mỗi lần bật và mất hiệu lực khi revoke hoặc đóng app; không ghi key vào file cấu hình.
- Quyền trong UI chỉ có hiệu lực ở phiên app hiện tại. Named pipe/manifest cùng-user vẫn được
  giữ làm compatibility transport cho COM và sidecar stdio.

## Cấu hình MCP Streamable HTTP trực tiếp

Trong client MCP, thêm server từ Settings bằng transport **Streamable HTTP**, URL đang hiển thị
ở đó và header `Authorization: Bearer <access key>`. Cả URL lẫn key thay đổi sau khi tắt/bật
hoặc khởi động lại PikDraw. Không chia sẻ key; chỉ người đang có key có thể gọi capability được
app cấp.

Endpoint hỗ trợ MCP `2026-07-28` stateless (`server/discover`, version/capabilities trên từng
request, `Mcp-Method` và `Mcp-Name` cho tool call) cùng handshake compatibility `2025-11-25`.
Các session nghiệp vụ vẫn dùng `session_handle` tường minh do `pikdraw.connect` trả về; không
dựa vào transport session để chọn tài liệu.

## Compatibility cho MCP stdio

Nếu client chỉ hỗ trợ stdio, cấu hình executable sidecar này (đổi đường dẫn theo nơi cài):

```json
{
  "mcpServers": {
    "pikdraw": {
      "command": "C:\\Program Files\\PikDraw\\apps\\mcp-server\\pikdraw-mcp-server.exe",
      "args": []
    }
  }
}
```

Sidecar dùng MCP stdio và chuyển tiếp semantic request qua named pipe cùng-user đã xác thực.
Stdio chỉ chứa JSON-RPC line frames; log chẩn đoán không ghi lên stdout.

## Luồng dùng

1. Gọi `pikdraw.instances` để liệt kê tiến trình đã bật cổng.
2. Gọi `pikdraw.connect` với `instance_id`; quyền mặc định là `document.read`.
3. Đọc `pikdraw.capabilities` và `pikdraw.document.inspect` trước khi sửa.
4. `pikdraw.connect` tạo session theo các scope client yêu cầu và scope app đã cấp.
   `pikdraw.document.create` cũng phải nhận `session_handle` đã được cấp `document.create`;
   bật edit ở app không tự cấp quyền cho mọi session. Dùng `pikdraw.objects.query` để đọc tối đa
   100 object mỗi trang. Có thể lọc theo `object_id`, `layer_index`, `type` hoặc `region_mm`
   trước khi phân trang; cursor `v2` được ràng buộc với trang và bộ lọc. Ưu tiên gửi
   `next_cursor` cùng `epoch` và `revision` từ trang đầu; `offset` và cursor `v1` vẫn được
   giữ cho client cũ. Nếu tài liệu đổi, bắt đầu lại từ trang đầu.
   Khi đã có revision trước, `pikdraw.objects.changes` trả tối đa 4.096 IDs đã sửa nếu history còn
   liên tục và chỉ có thay đổi object tương thích. Nếu `requires_full_query` là true do revision
   nằm ngoài 128 bước giữ lại, vượt số ID, thay đổi cấu trúc, hoặc Undo/Redo, `changed_ids` rỗng;
   đồng bộ lại bằng `objects.query`. Dùng `objects.get` với epoch/revision mới để lấy nội dung từng ID.
   Dùng `pikdraw.objects.get` để lấy một object cụ thể theo stable ID, gồm object lồng trong group.
5. Dùng `pikdraw.font.query` và `pikdraw.text.measure` để chọn mặt chữ có thật, xem fallback,
   glyph tiếng Việt và metric trước khi tạo nội dung. `pikdraw.document.validate` kiểm tra
   bounds trang, safe margin được yêu cầu và text layout; group bounds trả `not_assessed`.
6. Khi session được cấp `document.create`, `pikdraw.document.create` mở một tab mới theo
   `page_width_mm/page_height_mm` hoặc `page_width/page_height` kèm `length_unit` vật lý
   (`mm/cm/m/in/ft/yd/pt/pc`). Kích thước trả về vẫn là mm; thao tác không thay tài liệu đang mở,
   và cần connect lại để nhận session cho tab mới. Khi được cấp
   `document.edit`, `pikdraw.batch.validate` kiểm tra trước tối đa 256 operation gồm rectangle,
   ellipse, Unicode text, line và polyline có editable nodes/segments. `pikdraw.batch.apply`
   nhận cùng epoch/revision và áp dụng toàn batch trong một transaction Undo. Có thể transform
   path/text/image theo stable object IDs với move, scale và rotation quanh pivot mm; đổi fill,
   stroke và stroke width của path, hoặc fill của text; group/ungroup các object top-level cùng
   layer; align theo reference ID; distribute ngang/dọc cho ba object trở lên; duplicate path/text
   có offset; delete top-level object theo IDs. Không cần selection. Mỗi object tạo hoặc group dùng
   `client_ref` tùy chọn để ánh xạ ID; write vẫn cần request/idempotency key.
   `pikdraw.history.undo` và `pikdraw.history.redo` hoàn tác/tái áp dụng một transaction nếu
   epoch/revision còn khớp; mỗi lần gọi có receipt và retry an toàn.
   `pikdraw.object.create_rectangle` là lối tắt cho một hình và cũng cần `request_id`,
   `idempotency_key`, `expected_epoch`, `expected_revision` cùng kích thước. Giữ nguyên bộ
   `x_mm/y_mm/width_mm/height_mm`; có thể dùng `x/y/width/height` kèm `length_unit` là
   `mm`, `cm`, `m`, `in`, `ft`, `yd`, `pt` hoặc `pc`. Batch create rectangle/ellipse, text
   position và line/polyline points nhận cùng dạng typed. Batch transform/pivot, duplicate offset
   và stroke width cũng nhận số kèm `length_unit`; mọi giá trị chuẩn hóa về mm trước khi ghi.
   `px` bị từ chối nếu request không mang DPI basis. Không trộn trường `*_mm` với trường typed
   trong cùng operation.
   Cùng key và payload trả lại cùng receipt; payload khác trả `IDEMPOTENCY_CONFLICT`.
   Tối đa 128 receipt cho mỗi session trong RAM; disconnect hoặc khởi động lại app làm
   session hết hạn.
7. Nếu call write timeout, không tạo key mới ngay. Dùng `pikdraw.request.get` với key cũ,
   đọc lại tài liệu rồi mới quyết định thao tác tiếp.
8. Khi cấp `file.write`, `pikdraw.file.save` ghi `.pr` vào
   `%LOCALAPPDATA%\PikDraw\automation\files` bằng save nguyên tử và không ghi đè file đã có.
   Gọi `pikdraw.file.verify_reopen` để nạp lại native snapshot, kiểm page, revision, object
   IDs và kiểu object mà không thay tài liệu đang active. Khi cấp `file.read`, gọi
   `pikdraw.file.open` để mở một file trong thư mục grant thành tab mới; tool không thay tab
   đang mở. Connect lại tới document ID trả về trước khi sửa tab mới. Gọi `pikdraw.disconnect`
   khi xong.

MCP HTTP dùng bearer token ngẫu nhiên 256-bit theo phiên app, chỉ bind loopback, kiểm tra Host
và Origin, giới hạn header 16 KiB/body 1 MiB, tối đa 24 kết nối và 32 session legacy; session
legacy nhàn rỗi hết hạn sau 30 phút. Hàng đợi app giới hạn 32 lệnh. `Mcp-Method`/`Mcp-Name`
phải trùng body trong protocol hiện hành. Revoke đóng listener và hủy write còn nằm trong queue.

Compatibility named pipe chỉ nhận kết nối cùng user Windows, xác minh token SID và giới hạn frame
1 MiB. MCP sidecar và COM đi qua pipe này. Session gắn với document ID và epoch; mọi write kiểm
tra scope, epoch, revision và chạy trên UI-owner writer.

## Phạm vi hiện thực

Path transform, duplicate, delete, object style, group/ungroup, align/distribute cùng
batch validate/apply theo revision đi qua command writer; path node/segment IDs được trả
trong object query/get. Undo/redo guarded theo epoch/revision và retry có receipt. Batch hiện
chưa có node editing. Group chỉ nhận top-level object cùng layer; align/distribute chưa nhận
group. Style chỉ hỗ trợ fill/stroke/width cho path và fill cho text. File I/O chỉ lưu/nạp kiểm
chứng file native trong thư mục cố định của user;
`document.validate` chưa kiểm group bounds, constraints tổng quát, production profile hoặc
thẩm mỹ. Windows COM local server là adapter bổ sung cho cùng tools và scopes; xem
[hướng dẫn COM](../com-server/README.md). Chưa có arbitrary path grant, overwrite,
mở/replace tài liệu đang active, import/export qua MCP, installer hoặc CNC. Tool thành công xác nhận
operation cụ thể của PikDraw; không xác nhận thẩm mỹ, round-trip định dạng ngoài native hay
sản xuất.
