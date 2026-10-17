# Một source gốc và di trú worktree cũ

## Workflow hiện hành

Từ 16/10/2026, mọi tính năng/fix/review dùng source gốc
`C:/Users/CK ART STAGE/Downloads/PikDraw` và hệ thống Codex duy nhất.
Không tạo worktree/clone/checkout/branch task mới; không đổi branch dùng chung khi đang có phiên hoạt động.
`worktree create` của harness bị vô hiệu hóa. `worktree list` chỉ đọc inventory cũ.
Không thực hiện implementation từ `.worktrees` hoặc dùng harness cũ trong đó.

```powershell
.\tooling\pikdraw.ps1 task create vector-offset --title 'Contour chỉnh lại được' --module vector --goal 'Contour có undo, giữ biên dạng gốc' --provider codex --base HEAD
.\tooling\pikdraw.ps1 task claim vector-offset --owner codex-session-01 --provider codex
.\tooling\pikdraw.ps1 context --task-id vector-offset
```

Ví dụ chỉ áp dụng khi task đúng yêu cầu và chưa tồn tại. Claim trước sửa tạo source_baseline.
Owned paths/lease vẫn bắt buộc để tránh các phiên ghi cùng file; task riêng cho shared contracts.
Handoff snapshot ghi nhận cả thay đổi chưa commit, không cần commit để chuyển review.
Một source không tạo sandbox: agent vẫn phải tuân scope và review actual diff.

## Di trú lần lượt `.worktrees`

Chỉ coordinator làm việc này, từng worktree một, sau khi docs/harness được cập nhật.
Quy trình không đồng nghĩa cho phép chép nguyên cây cũ đè source mới.

1. Kiểm tra worktree/task không còn writer đang chạy; ghi absolute path, branch, HEAD, merge-base và trạng thái tracked/untracked/ignored. Không chiếm lease theo thời gian.
2. Giữ baseline source gốc (working files, staged/unstaged patch và hash). Sao lưu thay đổi worktree, untracked và ignored cần thiết, bao gồm evidence/customer files cần giữ ở nơi cục bộ phù hợp. Giữ branch/commit làm mốc phục hồi; không đưa bí mật/binary vào Git.
3. So sánh base/branch/current source để xác định thay đổi đã có và phần còn thiếu. Merge có chọn lọc; xử lý conflict theo semantics, giữ source mới và local edits. Không thay tài liệu hiện hành bằng quy tắc cũ.
4. Kiểm actual diff/owned paths, dependency/consumer tests liên quan trên source gốc. Ghi evidence từng worktree; thiếu hoặc conflict thì giữ nguyên worktree và next action.
5. Chỉ khi **đã đồng bộ và kiểm chứng**, xác minh lại absolute path nằm trực tiếp dưới `.worktrees`, checkout không đổi từ inventory và dữ liệu cần giữ còn đủ; dùng `git worktree remove` đúng path. Không dùng force để bỏ dirty/untracked hoặc dữ liệu chưa được kiểm kê.
6. Xóa ngay worktree vừa đạt bước 5, cập nhật task/handoff thành migrated/removed với bằng chứng. Không tuyên bố migration hoàn tất khi mới backup hoặc archive. Giữ branch/history cho phục hồi; không tự xóa branch.

Nếu Git remove từ chối do dirty, locked hoặc ignored files chưa xử lý, bảo toàn cây và giải quyết
nguyên nhân; không recursive-delete hoặc dùng force để vượt kiểm tra.
Metadata phục hồi chỉ do coordinator thực hiện sau khi sao lưu record, xác minh Git state
và không có writer; không dùng phục hồi để mở rộng owned paths của task đang chạy.

## Một build chung

Mọi phiên dùng `tooling/native/build.ps1`, `.agent/native-build.lock`,
`build/win-dev/apps/desktop/pikdraw.exe`. Không build tại worktree.
Không tạo app alias/timestamp hoặc build directory mới. Đổi Debug/Release ghi đè cùng output.
Các output cũ chỉ dọn trong bước có inventory và kiểm tra dữ liệu cần giữ riêng.
