# Worktree cho nhóm agent

Git worktree tách filesystem và branch; các task dùng chung bảng điều phối ở checkout chính.
Root .git/config, hooks và registry worktree vẫn là Git state dùng chung. Không dùng worktree như sandbox bảo mật.

## Tạo nhiệm vụ
Các lệnh dưới đây là mẫu thao tác cho nhiệm vụ được giao; `main` cần có commit foundation.

```sh
python tooling/agent_harness.py task create vector-offset --title "Contour chỉnh lại được" --module vector --goal "Prototype contour có undo, giữ biên dạng gốc" --provider codex --base main
python tooling/agent_harness.py task claim vector-offset --owner codex-session-01 --provider codex
python tooling/agent_harness.py worktree create --task-id vector-offset --provider codex --owner codex-session-01 --base main
python tooling/agent_harness.py context --task-id vector-offset
```

Branch: `agent/<provider>/<task>`. Checkout: `.worktrees/<provider>-<task>`.
Provider là nhãn định tuyến, không gọi dịch vụ AI. Owner/session phải duy nhất.
Mỗi agent làm trong đường dẫn worktree trả về; task board vẫn được tìm từ Git common-dir.

## Sở hữu file
Owned paths mặc định từ module registry. Dùng `task create --path` khi nhiệm vụ cần phạm vi khác
đã thống nhất; đó không phải quyền sửa module khác tùy ý.
Không gộp nhiều module không liên quan vào một task. Contract dùng chung được coordinator giao riêng.
Module instructions nằm cạnh code; các quy tắc kiến trúc chung nằm tại root.

Harness ngăn claim chồng lấn, nhưng hiện chưa có OS sandbox chặn ghi file ngoài phạm vi.
Reviewer kiểm tra diff/owned paths trước tích hợp. Không sửa file của task khác.

Lease không tự hết hạn hoặc cho takeover theo thời gian. Coordinator cần xác nhận task đã dừng trước release; giữ checkout và uncommitted files để task có thể tiếp tục. Các checkout đã chuẩn bị từ base cũ cần đồng bộ/review integration và contract revision trước nhận implementation, không dùng base cũ như nguồn kiến trúc mới nhất.

## Bàn giao và tích hợp
Commit thay đổi trong branch task; ghi evidence và xuất handoff theo CLI `handoff --help`.
Reviewer làm trong checkout riêng và đánh giá kết quả, không chỉ lời báo cáo.
Integrator dùng cherry-pick/merge sau kiểm tra contract, tests và diff.
Chỉ integrator cập nhật docs/project-state.json cho trạng thái đã tích hợp.

Integrator kiểm tra actual dependencies và consumer semantic fixtures trên merged SHA, không chỉ diff văn bản hoặc kết quả branch riêng. Nếu checkout chính có local edits được người dùng cho sửa, lưu baseline/bản sao scoped files rồi xác nhận baseline còn khớp trước áp kết quả. Không reset/stash/drop hoặc force để mất việc khác; handoff cần ghi nội dung local được kế thừa và limits review.

## Giữ và dọn worktree
`worktree list` chỉ đọc trạng thái. Harness không tự xóa checkout hoặc chiếm lại lease.
Trước dọn, xác nhận task không chạy, thay đổi đã commit/tích hợp hoặc đã được lưu.
Dùng Git worktree remove với đường dẫn đã kiểm tra; không force để bỏ mất thay đổi.
Đồ thị branch/Git là lịch sử code; board/handoff là trạng thái phối hợp, không thay thế nhau.

## Sự cố ghi metadata sau khi tạo checkout
Nếu Git đã tạo worktree nhưng lưu task thất bại, harness giữ checkout và báo path để không mất dữ liệu.
Hiện chưa có lệnh tự phục hồi registration. Đây là ngoại lệ bảo trì metadata dành cho coordinator:
xác nhận không có writer/task chạy, sao lưu record hiện có, kiểm tra Git worktree list cùng branch/path/base commit,
rồi sửa đúng trường worktree của task và chạy doctor. Không xóa checkout hoặc tạo lại branch để che sự cố.
Nếu không xác minh được Git state, giữ nguyên checkout/record và ghi blocker; không giả registration thành công.
