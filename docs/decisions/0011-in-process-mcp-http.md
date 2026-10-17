# ADR 0011 — MCP Streamable HTTP trong tiến trình desktop

Ngày: 17/10/2026. Trạng thái: **Implementation in progress; release acceptance open**.
Task: `mcp-in-app-streamable-http-20261017`. ADR này thay lựa chọn hoãn HTTP trong ADR 0010
cho đường kết nối MCP chính thức trong desktop.

## Bối cảnh

Người dùng cần agent kết nối trực tiếp với app và gọi công cụ thiết kế/kiểm chứng mà không
chụp màn hình hay điều khiển chuột. MCP stdio sidecar hiện chuyển tiếp qua named pipe. Đường
này hữu ích cho client cũ và COM nhưng không phải MCP endpoint do app desktop host.

## Quyết định

1. `apps/desktop` host MCP Streamable HTTP trong process sau khi user bật `AI Connections`.
   Endpoint chỉ bind `127.0.0.1` trên cổng ngẫu nhiên và app sinh bearer key 256-bit mới cho
   mỗi lần bật. Tắt quyền, lỗi start hoặc đóng app dừng listener và xóa key trong host.
2. HTTP adapter dùng chung `pikdraw_mcp_server_lib` và các tool/schema/session với sidecar.
   `McpServer::AppCaller` được inject bởi desktop; direct HTTP route gọi `AutomationPipeHost::invoke`
   để marshal sang UI-owner handler. Request từ HTTP không tự gửi qua process khác hoặc tự ghi
   document; writer, permission, revision, epoch, validation và undo vẫn thuộc service hiện có.
3. HTTP phục vụ MCP `2026-07-28` stateless với version/capabilities trong từng request và
   `Mcp-Method`/`Mcp-Name` khớp body. Giữ handshake + transport session `2025-11-25` để client
   tương thích còn dùng được. Legacy GET event stream không hỗ trợ; endpoint trả 405.
4. Named pipe tiếp tục là transport nội bộ cùng-user cho stdio sidecar và COM local server.
   MCP endpoint mới không quảng bá pipe name hay manifest nội bộ. Không mở LAN, remote HTTP
   hoặc DCOM.
5. Revoke đặt cancellation flag, dừng listener và loại queued request trước khi UI writer xử lý
   lệnh tiếp theo. Request đã vào UI handler hoàn tất trước khi UI thread xử lý thao tác Apply.

## Bảo vệ và giới hạn

HTTP parser giới hạn header 16 KiB, body 1 MiB, 24 client đồng thời và timeout socket. Host chỉ
nhận Host loopback khớp port, từ chối Origin ngoài loopback và so bearer constant-time. Legacy
session giới hạn 32 và hết hạn sau 30 phút không hoạt động. App queue giới hạn 32 request; write
vẫn cần session handle có scope, idempotency key, revision và epoch phù hợp.

Bearer key hiển thị trong Settings nên người dùng có thể cấu hình MCP client. Không ghi key vào
file hoặc log. GDI Settings hiện render chuỗi URL/key; thao tác copy thuận tiện và kiểm tra UI
thực tế là một phần acceptance riêng, chưa chứng minh bằng source test.

## Lựa chọn đã cân nhắc

| Lựa chọn | Kết quả |
| --- | --- |
| Chỉ giữ stdio sidecar | Không thỏa yêu cầu endpoint trực tiếp trong desktop; giữ làm fallback cho client legacy |
| MCP server riêng qua named pipe | Thêm process và IPC trên đường MCP chính; không còn là host endpoint trong app |
| HTTP bind mọi interface | Tăng bề mặt mạng, không cần cho agent chạy trên cùng máy; loại bỏ |
| Embedded HTTP trên loopback + bearer | Chọn; lifecycle gắn với Settings grant/revoke và dùng chung semantic server |
| Chỉ triển khai protocol 2025 sessionful | Không bao quát protocol hiện hành 2026 stateless; giữ handshake làm compatibility |

## Kiểm chứng và điều kiện nhận

Transport test phải chứng minh loopback bind, bearer/Origin/Host policy, cả modern stateless và
legacy session flow, tool catalog, direct dispatch về UI-owner thread, scope-limited document
create, notification/GET behavior, revoke cancellation và listener shutdown. Sau đó cần build
canonical app, live MCP client smoke trên app chuẩn, thao tác Settings grant/revoke, package và
installed-client acceptance. Chỉ test fixture hoặc build pass không xác nhận trực quan UI,
containment toàn hệ thống, chất lượng thiết kế, định dạng ngoài native hay khả năng sản xuất.

Đối chiếu protocol theo [MCP specification 2026-07-28](https://modelcontextprotocol.io/specification/2026-07-28)
và [thông báo thay đổi transport](https://blog.modelcontextprotocol.io/posts/2026-07-28/). ADR 0010
vẫn ghi quyết định spike lịch sử; điều khoản hoãn HTTP được thay bởi ADR này.
