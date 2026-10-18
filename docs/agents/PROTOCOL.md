# Giao thức phát triển tinh gọn PikDraw (Streamlined Workflow)

## 1. Nguyên tắc làm việc trực tiếp (Direct Primary Source)

- **Làm việc trực tiếp trong source gốc**: `C:/Users/Administrator/Downloads/PikDraw`. Không tạo worktree, clone hay nhánh task riêng.
- **Xóa bỏ chu trình harness hành chính**: Toàn bộ hệ thống quản lý task rườm rà (task create/claim/handoff/lease/snapshots) trước đây đã bị loại bỏ. Agent không cần và không được yêu cầu thủ tục task harness.
- **Ưu tiên giải quyết trực tiếp yêu cầu người dùng**: Tiếp nhận yêu cầu, phân tích mã nguồn và triển khai giải pháp kỹ thuật ngay lập tức.
- **Giữ nguyên local edits của người dùng**: Tuyệt đối không tự ý reset, stash, checkout hoàn tác hoặc xóa code đang viết dở của người dùng khi chưa có xác nhận rõ ràng.

## 2. Biên dịch và kiểm thử (Native Build & Test)

- **Lệnh điều phối trung tâm**: Sử dụng `tooling/pikdraw.ps1` hoặc các script con trong `tooling/native/`:
  - Kiểm tra môi trường: `.\tooling\pikdraw.ps1 doctor`
  - Biên dịch ứng dụng: `.\tooling\pikdraw.ps1 build` (hoặc `.\tooling\native\build.ps1`)
  - Chạy test C++ mục tiêu: `.\tooling\pikdraw.ps1 test -TestTarget <target_name>` (hoặc `.\tooling\native\build.ps1 -Test -TestTarget <target_name>`)
  - Chạy kiểm tra kiến trúc: `.\tooling\pikdraw.ps1 check -Suite architecture` (hoặc `.\tooling\native\check.ps1 -Suite architecture`)
- **Khóa build an toàn**: Build runner sử dụng `.agent/native-build.lock` (có tự động chờ và tự thu hồi khóa nếu tiến trình cũ đã thoát) để bảo vệ cây build `build/win-dev`.
- **Đóng gói phát hành**: `.\tooling\pikdraw.ps1 package` (hoặc `.\tooling\native\package.ps1`).

## 3. Quy chuẩn mã nguồn & Bàn giao

- **Ngưỡng độ dài file**: Mỗi file code tối đa **2000 dòng** (khuyến nghị 300–1800 dòng). Không nối đuôi tính năng vào file đã tiệm cận hoặc vượt ngưỡng 2000 dòng.
- **Git commit sạch sẽ**: Sau khi hoàn thành tính năng và test pass (`tooling/native/build.ps1 -Test`), cho phép tạo Git commit rõ ràng, giữ working tree gọn gàng.
- **Báo cáo ngắn gọn**: Trả kết quả trực tiếp cho người dùng kèm danh sách file đã sửa và kết quả kiểm thử thực tế. Không tạo tài liệu snapshot hay phiếu handoff cồng kềnh.
