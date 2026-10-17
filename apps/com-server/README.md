# Tự động hóa PikDraw qua COM

PikDraw có Windows COM local server bổ sung cho MCP. COM gọi cùng semantic tools,
named pipe đã xác thực, session scopes và command writer của ứng dụng; COM không tạo
đường ghi tài liệu riêng. App phải bật `Settings > AI Connections…` trước khi agent
thấy instance. Mặc định app vẫn tắt gateway.

## Đăng ký cho user hiện tại

Build target `pikdraw-com-server`, rồi chạy executable với `/RegServer`. Lệnh chỉ ghi
`HKCU\Software\Classes`, không cần quyền administrator. Gọi `/UnregServer` để gỡ.
Việc đăng ký chỉ tạo COM class `PikDraw.Automation` và `PikDraw.Automation.1` dạng local
server; không tạo AppID, in-process DLL hay DCOM endpoint. COM khởi chạy executable với
`-Embedding`. Nếu chuyển executable sang nơi khác, gỡ đăng ký cũ rồi đăng ký từ path mới.

```powershell
& "C:\path\to\pikdraw-com-server.exe" /RegServer
$pikdraw = New-Object -ComObject PikDraw.Automation
$pikdraw.GetTools()
$pikdraw.GetInstances()
$pikdraw.CallTool("pikdraw.connect", '{"instance_id":"12345","scopes":["document.read"]}')
```

## IDispatch

Interface chỉ hỗ trợ late binding, không đăng ký type library. Các method và DISPID ổn
định:

| Method | DISPID | Tham số | Kết quả BSTR JSON |
| --- | ---: | --- | --- |
| `GetInstances()` | 1 | Không | Structured result của `pikdraw.instances` |
| `GetTools()` | 2 | Không | Mảng MCP tool definitions đang được build này hỗ trợ |
| `CallTool(name, argumentsJson)` | 3 | Tên tool và JSON object UTF-16 | MCP tool result có `structuredContent`, `content`, `isError` |

Tên tool phải có trong `GetTools()`; script/eval, lệnh shell và thao tác tùy ý không được
expose. `argumentsJson` tối đa 1 MiB. Bắt đầu với `pikdraw.instances`, rồi `pikdraw.connect`
với scope cần dùng; các tool tiếp theo dùng `session_handle` trả về. Gọi
`pikdraw.disconnect` trước khi kết thúc phiên để thu hồi session ứng dụng. Lỗi COM như
tham số sai dùng HRESULT; lỗi nghiệp vụ giữ domain code trong JSON result, giống MCP.

COM local server giới hạn access DACL vào SID của user đã đăng ký và chỉ bật quyền local;
named pipe tiếp tục kiểm tra user và scopes ở mỗi call. STA clients được COM marshal sang
server MTA. Không có callback sự kiện trong lúc commit.

## Kiểm chứng

`test_pikdraw_com_dispatch` kiểm tra late-binding, schema/tools và lỗi tham số trực tiếp
trên đối tượng COM mà không ghi Registry. `test_pikdraw_com_client` là client độc lập gọi
activation từ STA rồi MTA để smoke-test marshaling khi đăng ký class tạm thời. Không chạy
`/RegServer` tự động trong CTest vì đăng ký là thay đổi machine user profile; script nghiệm
thu phải gỡ bằng `/UnregServer` trong `finally`.

COM chỉ expose operations đang có trong `GetTools()`. Gọi thành công không mở rộng fidelity,
editability, kiểm tra thẩm mỹ hay production/CNC acceptance của định dạng hoặc artwork.
