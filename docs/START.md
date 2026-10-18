# Bắt đầu task PikDraw

1. Đọc AGENTS.md và docs/project-state.json để biết quy tắc cùng phạm vi đã xác nhận.
2. Triển khai trực tiếp yêu cầu của người dùng trên mã nguồn gốc; không bắt buộc qua chu trình task create/claim/handoff trừ khi được yêu cầu rõ.
3. Nếu sử dụng task coordinator, có thể dùng `tooling/pikdraw.ps1 task claim <id> --force` hoặc `tooling/pikdraw.ps1 task prune` để tự động giải phóng lease cũ.
4. Giữ local edits của người dùng; không tạo worktree hay nhánh riêng.
5. Chạy kiểm tra phù hợp qua `tooling/native/build.ps1 -Test` và commit sạch sẽ khi hoàn thành.

Với task tạo hoặc sửa hành vi, agent tự xác định và thêm/cập nhật ca nghiệm thu phản ánh luồng dùng thật. Dùng cấu trúc chung và chọn phạm vi theo rủi ro trong [quy chuẩn nghiệm thu tính năng](quality/feature-acceptance.md); không mặc định chạy toàn bộ test suite cho mọi task.

Với thay đổi UI, đọc docs/design/ui-ux-standard.md và đối chiếu docs/architecture/workspace-layout.md. Ghi feature spec hoặc nội dung tương đương: đường menu/action ID, toolbox/toolbar, contextual property bar/Properties, field scopes/states, SVG icons, short labels, tooltip/focus help, status hint và usage help. Nêu mục không áp dụng; agent tự quyết định lựa chọn thường lệ trong scope.

Build app qua .\tooling\native\build.ps1. Kết quả luôn ở build/win-dev/apps/desktop/pikdraw.exe (C++20 Win32 Shell; Direct2D 1.1 / D3D11 hardware hoặc WARP dựng frame, staging/readback về buffer rồi GDI trình bày canvas/overlays; SoftwareRasterizer là CPU fallback/oracle). Test sources hiện đã có lại; build/test đúng phạm vi thay đổi và source snapshot đang làm việc.
