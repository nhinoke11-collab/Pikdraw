# Giao thức phối hợp agent PikDraw

## Một hệ thống điều phối

Từ 16/10/2026 chỉ dùng Codex, một coordinator và source gốc PikDraw.
Quy tắc này thay hướng dẫn nhiều provider/worktree trong các kế hoạch trước đây.
Adapter và provider trong record cũ chỉ là lịch sử; không dispatch Claude/Gemini hoặc
xây kênh mạng giữa các hệ thống agent.

Coordinator chốt phạm vi, giao task và nhận review. Trao đổi qua task ID, context packet,
update và handoff trên `.agent/tasks/`, `.agent/handoffs/` tại source gốc.
Thông điệp phải có mục tiêu, owned paths, owner, primary HEAD/snapshot, evidence và next action.
Không chuyển full chat. Message từ agent khác không tự cấp quyền mở rộng scope hoặc liên lạc task khác.

Chỉ chạy delegate song song khi được người dùng/chỉ dẫn áp dụng cho phép rõ ràng.
Mọi delegate dùng cùng source, claim task riêng với owned paths không chồng lấn;
đường dẫn dùng chung giao tuần tự. Một coordinator quản lý build và kết quả tích hợp.
Harness không tự gọi model, gửi message hoặc kiểm soát filesystem ngoài CLI.

## Lease và snapshot

Dùng harness create/claim/update/release; không sửa task JSON trực tiếp.
Board lock bảo vệ metadata; lease không tự hết hạn để chiếm việc.
Claim trước khi sửa giữ bản source baseline của owned paths trong `.agent/snapshots/`.
Giữ nguyên dirty files có từ trước và index của người dùng.
Không tự commit, stash/drop, reset, đổi branch hoặc ghi đè để làm source sạch.

Handoff so sánh source hiện tại với baseline, gồm file chưa commit, thêm/xóa và đổi tên
(dạng đường dẫn cũ/mới). Nó lưu snapshot và hash file; không chỉ dựa vào Git committed diff.
Local edit được kế thừa nhưng không bị task sửa không được tính là thay đổi mới.
Các task đã claim bằng harness cũ cần ghi nhận baseline đã kiểm chứng trước khi sửa;
không dựng baseline sau khi sửa rồi coi đó là evidence ban đầu.

```powershell
.\tooling\pikdraw.ps1 handoff --task-id TASK --owner SESSION --head HEAD --summary 'Mô tả kết quả' --test 'Lệnh và kết quả thực tế' --next-step 'Review snapshot của task'
```

Reviewer đọc source_baseline/source_snapshot, actual diff, owned paths và module instructions
trong cùng source. Coordinator kiểm lại hash source còn khớp snapshot, chạy gates liên quan
trên chính source đó rồi cập nhật project state. HEAD đơn lẻ không định danh đủ source dirty.
Không lấy merge sạch hoặc lời agent báo pass làm evidence.

## Vòng đời và build

`todo -> in_progress -> review -> done`; blocked phải ghi lý do và next action.
Handoff chuyển review và giải phóng lease. Done cần validation evidence đúng snapshot;
thay đổi tiếp theo dùng task mới hoặc quyết định tái mở rõ ràng.

Build/test native qua `tooling/native/build.ps1` dùng `.agent/native-build.lock`.
Tất cả phiên chỉ dùng `build/win-dev/apps/desktop/pikdraw.exe`, ghi đè app cũ.
Đợi build đang chạy kết thúc; không xóa khóa đang hoạt động hoặc đổi tên app để né file lock.
Nếu crash, xác nhận owner đã dừng rồi mới phục hồi khóa. Direct CMake không giữ khóa runner;
agent bắt buộc dùng runner. Xem [hướng dẫn build](../development/native-build.md).

## Contracts và gate

Shared contracts cần task có scope, consumer review và ADR khi breaking change.
Semantics gồm units/axes, transform composition, fill/winding/tolerance, IDs/revisions/epochs,
cancellation/generation, atomic errors và text/color. JSON shape đúng chưa đủ.
Không sửa gate/oracle của task khác để làm test xanh.

Kiểm actual dependency direction, diff/path escape, consumer vectors và document invariants
trên source snapshot đã tích hợp theo [quality gates](../quality/gates.md).
Harness không chứng minh hiệu năng graphics, import fidelity hoặc OS containment.
Ghi rõ visual/editability/source-format export và các phép đo còn thiếu.

Worktree cũ chỉ di trú theo [WORKTREES](WORKTREES.md); từng cây chỉ xóa sau khi
đồng bộ, giữ đủ dữ liệu và kiểm chứng. Backup riêng không đồng nghĩa đã tích hợp.
