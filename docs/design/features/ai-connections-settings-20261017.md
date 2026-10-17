# Phiếu UI: AI Connections trong Settings

Task: `ai-agent-settings-ui-runtime-20261017` · Module: desktop · Runtime: C++20/Win32/GDI.
Quyết định theo [chuẩn UX/UI](../ui-ux-standard.md) và [bố cục workspace](../../architecture/workspace-layout.md).

## Mục tiêu và vị trí

Người dùng chủ động bật MCP cho đúng tiến trình PikDraw đang mở, chọn mức quyền đọc hoặc sửa/lưu, lấy URL/access key cho client, thấy trạng thái endpoint và thu hồi kết nối. MCP tắt mặc định; quyền này chỉ sống trong phiên app hiện tại. Desktop host Streamable HTTP trực tiếp trong process và bind loopback trên cổng ngẫu nhiên. `--enable-mcp` và `--enable-mcp-edit` tiếp tục là lựa chọn khởi chạy cho kiểm thử.

| Bề mặt | Quyết định |
| --- | --- |
| Menu chính | `Settings > AI Connections…`; dùng chung trang Settings đang có, không thêm menu cấp cao. |
| Action ID | `action.settings.ai_connections`, mở/focus tab AI Connections; trạng thái lấy từ runtime gateway. |
| Help | `Help > AI Automation…` (`action.help.ai_automation`) mở cùng trang hướng dẫn/kết nối. |
| Toolbox / toolbar | Không áp dụng: đây là quyền truy cập app, không phải thao tác canvas. |
| Property bar / Properties | Không áp dụng: không sửa trang hoặc selection. |
| Dialog | Tái dùng Preferences hiện tại; thêm tab thứ bảy bên dưới các mục đang có. Không mở dialog nổi mới. |
| Endpoint/key controls | Hai nút `Copy URL` / `Chép URL` và `Copy key` / `Chép khóa`, nằm cùng hàng với từng giá trị trong tab. Action IDs ổn định: `action.settings.ai_connections.copy_endpoint` và `action.settings.ai_connections.copy_access_key`. Chỉ bật khi endpoint đang lắng nghe; không lưu credential vào document/preferences. |
| Icon | Không áp dụng: menu action và nút copy có nhãn chữ; không thêm icon dùng một lần. |
| Tooltip/focus help | Focus qua Tab/Shift+Tab; status hint nêu thao tác hiện được chọn và `Enter`/`Space` để copy. Tooltip nổi không áp dụng vì GDI shell chưa có tooltip control cho các field trong trang này. |
| Usage help | Trang này ghi cách cấu hình client MCP; Settings giải thích loopback, bearer header, phạm vi quyền và thao tác copy. |

## Trạng thái và trường

Các checkbox là lựa chọn nháp. `Apply` cập nhật listener ngay; `Cancel`, đóng bằng X hoặc click ra ngoài khôi phục lựa chọn nháp theo quyền runtime đang chạy. `Reset` tại tab này đưa nháp về tắt. Đổi quyền khi đang bật sẽ thay endpoint cùng user/process ID và làm phiên MCP cũ mất hiệu lực.

| Field / label EN + VI | Kiểu, miền, mặc định | Phạm vi và lúc áp dụng |
| --- | --- | --- |
| MCP access / Cho phép MCP | Checkbox; on/off; mặc định off. | Quyền app theo phiên; `Apply` bật listener cùng user, `Cancel` bỏ nháp. |
| Allow document editing and native files / Cho phép sửa tài liệu và tệp native | Checkbox; on/off; mặc định off, disable khi MCP access off. | Khi bật bao gồm `document.create`, `document.edit`, `file.read`, `file.write`; luôn bao hàm quyền đọc. Chỉ áp dụng sau `Apply`. |
| Endpoint status / Trạng thái endpoint | Read-only: Off, Listening · Read only, Listening · Editing, Starting/Error. | Phản ánh listener HTTP loopback thực; không khẳng định agent client đã kết nối khi chỉ có listener. |
| MCP URL / URL MCP | Read-only: `http://127.0.0.1:<ephemeral-port>/mcp`. | Tạo theo phiên runtime; dùng transport Streamable HTTP. URL cũ hết hiệu lực khi revoke hoặc app đóng. |
| Access key / Khóa truy cập | Read-only bearer token ngẫu nhiên 256-bit, chỉ hiện khi listener active. | `Authorization: Bearer <access key>`; token không persist và rotate khi listener khởi động lại. |
| Copy URL / Chép URL | Button; enabled khi listener active. | Chép đúng endpoint đang chạy dưới dạng Unicode text; không đổi document revision hoặc quyền. |
| Copy key / Chép khóa | Button; enabled khi listener active. | Chép bearer key hiện hành; status hint xác nhận thao tác nhưng không lặp lại secret. |
| Local-only access / Chỉ truy cập trên máy này | Read-only explanatory text. | HTTP chỉ bind loopback; xác thực bearer, kiểm tra Host/Origin. Named pipe cùng-user chỉ là compatibility cho sidecar/COM; không mở LAN/DCOM. |

Không có thư mục grant riêng ở UI: phiên bản hiện tại giới hạn native files vào `%LOCALAPPDATA%\PikDraw\automation\files`. Không có nút mở/tải file tùy ý. Session và receipt cũ bị xóa khi revoke; mọi commit đã hoàn tất vẫn nằm trong lịch sử tài liệu.

## Ngữ cảnh, hướng dẫn và lỗi

| Trạng thái | Hành vi |
| --- | --- |
| Không có tài liệu / đang có selection | Trang quyền hoạt động như nhau; document không bị dirty và không có lệnh Undo. |
| Listener off | Hiện `Off`; client không liệt kê instance này. |
| Listener chỉ đọc | Client có thể query, đo chữ, font và validation; lệnh tạo/sửa/lưu bị từ chối theo scope. |
| Listener sửa | MCP cho phép tạo tab tài liệu AI, batch edit và native file grant; không ghi đè tab đang mở. |
| Error | Listener không active; hiện lý do ngắn trong trang, checkbox phản ánh quyền thực và có thể Apply để thử lại. |
| Revoke | `Apply` với cả checkbox tắt dừng HTTP loopback, xóa access key, đóng session transport và hủy write đang chờ trong app queue; request đang chạy trên UI thread kết thúc trước khi xử lý click Apply. |

Label English mặc định; tiếng Việt lấy theo `UILanguage` và dùng ID API ổn định, không dịch protocol names. Status hint trên status line hiện có: `MCP off`, `MCP listening · Read only`, `MCP listening · Editing`, `Copy MCP URL · press Enter or Space`, `Copy MCP access key · press Enter or Space`, hoặc `Could not start MCP: <reason>`. Không thêm status panel riêng; chỉ cập nhật hint trong Preferences hiện tại. Không có shortcut riêng; action tìm được qua Settings menu và Help. Tab AI Connections hỗ trợ Tab/Shift+Tab theo thứ tự read, edit, copy URL, copy key, Reset, Cancel, Apply; Enter/Space kích hoạt, Esc bỏ nháp, focus ring luôn nhìn thấy. Các tab Preferences cũ giữ nguyên input model hiện tại.

## Nghiệm thu Given / When / Then

1. Given app mới với MCP off, when mở `Settings > AI Connections…`, then status là Off, cả quyền đều tắt và document revision không đổi.
2. Given trang đang Off, when bật MCP read-only và Apply, then Settings hiển thị URL loopback cùng bearer key mới; MCP Streamable HTTP discovery/query thành công, còn `document.create`/`batch.apply` bị từ chối. Named pipe/manifest cùng-user tiếp tục cho sidecar/COM.
3. Given read-only listener, when bật quyền sửa/tệp và Apply, then MCP HTTP endpoint chỉ công bố scopes được cấp; client tạo tab riêng, sửa batch và save/reopen native được.
4. Given có listener, when chọn revoke và Apply, then endpoint/key dừng, request queued bị hủy, session cũ không được dùng lại, pipe/manifest compatibility bị gỡ, document đang mở giữ nguyên revision và commit lịch sử.
5. Given đang sửa checkbox nhưng chưa Apply, when Cancel hoặc đóng Preferences, then quyền runtime không đổi và checkbox trở lại trạng thái đã áp dụng.
6. Given endpoint không khởi động được, when Apply, then quyền không báo active giả; trang hiện lỗi và cho thử lại.
7. Given HTTP endpoint đang bật, when client dùng MCP `2026-07-28` stateless rồi client tương thích dùng `2025-11-25` handshake, then cả hai đi tới cùng tool catalog; modern request không có transport session, legacy session dùng DELETE để disconnect.
8. Given listener đang bật, when người dùng dùng Tab rồi Enter/Space tại Copy URL và Copy key, then clipboard nhận endpoint/key hiện hành, status xác nhận thao tác mà không lộ key, document không đổi.
9. Given listener đang bật trong NativeWindow, when client MCP gọi discovery, tools/list, connect, create, batch validate/apply, query, document validate, save và verify-reopen, then document canonical được tạo/sửa/lưu với stable IDs và giới hạn scope; khi người dùng revoke bằng Settings keyboard, endpoint không nhận request cũ nữa.

Kiểm tra hỗ trợ: focused CTest, Settings state/action test, headless direct MCP E2E qua NativeWindow thật, build app canonical và thao tác native trên cửa sổ 1366×768 cùng 1920×1080, scaling có sẵn. Test E2E dùng clipboard snapshot và khôi phục clipboard text-only; nếu clipboard có format khác, chỉ phần copy UI được skip. Test state không chứng minh visual clipping hoặc mọi DPI; visual/narrow-window/accessibility review vẫn là gate riêng.
