# Bắt đầu phát triển PikDraw (Quy trình tinh gọn)

1. Đọc `AGENTS.md` để nắm quy tắc kiến trúc và quy chuẩn phát triển (tối đa 2000 dòng/file).
2. Triển khai trực tiếp yêu cầu của người dùng trên mã nguồn gốc (`C:/Users/Administrator/Downloads/PikDraw`). Không qua chu trình harness hành chính rườm rà (task create/claim/handoff/lease).
3. Giữ nguyên local edits của người dùng; không tự ý reset, stash hoặc xóa code dở dang. Không tạo worktree hay nhánh task riêng.
4. Kiểm tra sức khỏe môi trường qua `.\tooling\pikdraw.ps1 doctor`.
5. Biên dịch và kiểm thử qua `.\tooling\native\build.ps1` (hoặc `.\tooling\pikdraw.ps1 test -TestTarget <target_name>`), commit rõ ràng khi hoàn thành.

Với task tạo hoặc sửa hành vi, agent tự xác định và thêm/cập nhật ca nghiệm thu phản ánh luồng dùng thật. Dùng cấu trúc chung và chọn phạm vi theo rủi ro trong [quy chuẩn nghiệm thu tính năng](quality/feature-acceptance.md); không mặc định chạy toàn bộ test suite cho mọi task.

Với thay đổi UI, đọc docs/design/ui-ux-standard.md và đối chiếu docs/architecture/workspace-layout.md. Ghi feature spec hoặc nội dung tương đương: đường menu/action ID, toolbox/toolbar, contextual property bar/Properties, field scopes/states, SVG icons, short labels, tooltip/focus help, status hint và usage help. Nêu mục không áp dụng; agent tự quyết định lựa chọn thường lệ trong scope.

Build app qua .\tooling\native\build.ps1. Kết quả luôn ở build/win-dev/apps/desktop/pikdraw.exe (C++20 Win32 Shell; Direct2D 1.1 / D3D11 hardware hoặc WARP dựng frame, staging/readback về buffer rồi GDI trình bày canvas/overlays; SoftwareRasterizer là CPU fallback/oracle). Test sources hiện đã có lại; build/test đúng phạm vi thay đổi và source snapshot đang làm việc.
