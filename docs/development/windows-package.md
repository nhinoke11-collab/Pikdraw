# Gói Windows

PikDraw tạo một ZIP duy nhất gồm desktop app, MCP stdio sidecar, COM local server,
runtime DLL cần thiết, resources, hướng dẫn kết nối và giấy phép. `pikdraw.exe` nằm ở gốc;
sidecar và COM server nằm trong `apps/mcp-server` và `apps/com-server`, khớp với cấu hình
MCP mẫu. COM không tự đăng ký khi giải nén; người dùng chủ động gọi `/RegServer` và
`/UnregServer` theo hướng dẫn kèm gói.

## Tạo và kiểm tra package

Chạy từ source root trên Windows:

```powershell
.\tooling\native\package.ps1 -Configuration Release
```

Lệnh dựng ba executable rồi chạy target CPack `pikdraw-package` dưới cùng native build lock, ghi ZIP vào
`build/win-dev/packages/`, rồi kiểm tra ba file thực thi, hai hướng dẫn AI, resources, giấy
phép và SHA-256 của từng executable trong ZIP so với output canonical. Lệnh từ chối chạy
khi `build/win-dev/apps/desktop/pikdraw.exe` còn mở để tránh ghi đè chương trình đang dùng.
Đóng PikDraw sau khi lưu tài liệu rồi chạy lại nếu gặp thông báo này.

ZIP là gói giải nén, không phải MSI và không tự cấp quyền agent. Có thể giải nén vào thư mục
do người dùng chọn. Sau đó bật MCP trong `Settings > AI Connections…`; cấu hình MCP client
trỏ đến `apps\mcp-server\pikdraw-mcp-server.exe`. Nếu dùng COM, mở PowerShell tại thư mục
giải nén và chạy:

```powershell
.\apps\com-server\pikdraw-com-server.exe /RegServer
```

Lệnh chỉ đăng ký COM cho user hiện tại. Khi gỡ bản này, đóng client COM rồi gọi
`apps\com-server\pikdraw-com-server.exe /UnregServer` từ đúng thư mục đã đăng ký.

## Gate nghiệm thu

ZIP/hash check chỉ chứng minh package mang đúng executable đã build và các tài liệu/resource
đã chọn. Trước khi phát hành, giải nén package vào một thư mục sạch, chạy bản đã giải nén,
bật/revoke MCP trong app, kết nối MCP client độc lập, tạo/validate/save/verify-reopen một
brief, rồi kiểm COM activation và gỡ đăng ký. Chạy bằng máy người dùng, installer hoặc ZIP
không thay thế kiểm tra trực tiếp app, quyền cùng-user, fidelity tài liệu hay độ sẵn sàng sản
xuất. `docs/roadmap/ai-agent-mcp-com-20261017.md` giữ các AC cấp tính năng và gate còn mở.
