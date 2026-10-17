# ADR 0007: Một source, một hệ thống agent và một app build

Ngày: 16/10/2026. Trạng thái: Accepted theo yêu cầu chủ dự án.

## Vấn đề

Các worktree/task branch và app alias tích lũy khiến phiên sau dễ sửa nhầm source,
quên tích hợp hoặc mở nhầm executable. Quy tắc cũ còn yêu cầu nhiều lane/provider,
checkout riêng và build theo configuration; không còn phù hợp cách làm hiện tại.

## Quyết định

- Mọi phiên dùng source gốc `C:/Users/CK ART STAGE/Downloads/PikDraw`, hệ thống Codex,
  một coordinator và task board chung; không tạo thêm worktree/clone/branch task.
- Owned paths, lease, bounded context, consumer review và các gate sản phẩm vẫn bắt buộc.
  Chỉ dùng delegate song song khi được phép rõ ràng, với các lease không chồng lấn.
- Claim giữ snapshot source trước sửa; handoff ghi primary HEAD và snapshot/hash kết quả,
  kể cả file chưa commit. Không tự commit hoặc thay index/dirty work của người dùng.
- Mọi configure/build/test native qua runner giữ một khóa; chỉ `build/win-dev` và app
  `build/win-dev/apps/desktop/pikdraw.exe`. Debug/Release ghi đè cùng app, không tạo alias.
- Worktree cũ được kiểm kê, bảo toàn, tích hợp và kiểm chứng từng cây trước khi xóa ngay
  cây đã đạt. Không coi backup là tích hợp và không force-delete phần chưa kiểm chứng.

Quyết định này thay riêng phần workflow worktree/provider/output trong ADR/kế hoạch cũ;
không thay hợp đồng document, fidelity, runtime isolation hoặc acceptance hiệu năng.

## Hệ quả và giới hạn

Không còn bước đồng bộ cho task mới vì mọi sửa đổi nằm tại source gốc. Đổi lại, file dùng chung
phải được giao tuần tự, build/configuration không chạy đồng thời, review phải kiểm đúng snapshot
vì HEAD chưa bao gồm local edits. Lease và snapshot vẫn là phối hợp, không phải OS sandbox.

Harness chặn worktree mới, provider mới ngoài Codex và metadata writes từ linked checkout.
CMake chặn linked source/build tree khác; direct CMake có thể bỏ qua khóa runner nên agent
phải theo entrypoint. Harness cũ trong worktree cũ không tự được cập nhật bằng quyết định này.

Docs/harness thay đổi trước; việc di trú/xóa worktree và dọn/rebuild output cũ có evidence
riêng, không được báo hoàn tất chỉ vì quy tắc mới đã được ghi.

Xem [START](../START.md), [protocol](../agents/PROTOCOL.md),
[di trú](../agents/WORKTREES.md), [build](../development/native-build.md).
