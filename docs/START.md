# Bắt đầu task PikDraw

1. Đọc AGENTS.md và docs/project-state.json để biết quy tắc cùng phạm vi đã xác nhận.
2. Chạy .\tooling\pikdraw.ps1 task list. Nếu chưa có task phù hợp, tạo task với owned paths rõ ràng rồi claim trước khi sửa.
3. Lấy context bằng .\tooling\pikdraw.ps1 context --task-id TASK. Chỉ đọc README, AGENTS và contract của module liên quan.
4. Giữ local edits. Chỉ sửa paths đã claim; không tạo worktree hay nhánh riêng.
5. Chạy kiểm tra phù hợp và bàn giao qua tooling/pikdraw.ps1 handoff. Ghi rõ giới hạn và những gì chưa chạy.

Với task tạo hoặc sửa hành vi, agent tự xác định và thêm/cập nhật ca nghiệm thu phản ánh luồng dùng thật. Dùng cấu trúc chung và chọn phạm vi theo rủi ro trong [quy chuẩn nghiệm thu tính năng](quality/feature-acceptance.md); không mặc định chạy toàn bộ test suite cho mọi task.

Với thay đổi UI, đọc docs/design/ui-ux-standard.md và đối chiếu docs/architecture/workspace-layout.md. Ghi feature spec hoặc nội dung tương đương: đường menu/action ID, toolbox/toolbar, contextual property bar/Properties, field scopes/states, SVG icons, short labels, tooltip/focus help, status hint và usage help. Nêu mục không áp dụng; agent tự quyết định lựa chọn thường lệ trong scope.

Build app qua .\tooling\native\build.ps1. Kết quả luôn ở build/win-dev/apps/desktop/pikdraw.exe. Test sources hiện đã có lại; build/test đúng phạm vi thay đổi và source snapshot đang làm việc.
