# Ba lane phát triển PikDraw

Registry ở `registry/team.json` khai báo ba lane để nhóm AI có thể bắt đầu song song.
Tên Codex/Claude/Gemini là nhãn chuẩn bị; chưa có dịch vụ bên ngoài được kết nối hoặc gọi.
Người điều phối có thể tạo task mới cho provider khác; không đổi record của task đang có lease.

| Task | Provider label | Module | Worktree chuẩn bị |
| --- | --- | --- | --- |
| desktop-layout | codex | desktop | `.worktrees/codex-desktop-layout` |
| document-spike | claude | document | `.worktrees/claude-document-spike` |
| interop-spike | gemini | interop | `.worktrees/gemini-interop-spike` |

Các task ban đầu có trạng thái todo và không giữ lease sau khi chuẩn bị checkout.
Agent nhận việc phải claim bằng owner/session của mình; dùng task show để lấy path/base, rồi làm trong worktree đó.
Các lane tách owned paths; thay đổi contracts/registry chung cần task riêng và coordinator review.

Runtime board và checkout không đưa vào Git. Khi clone repository ở máy mới, tạo lại task và worktree
theo WORKTREES.md, dùng title/module/goal từ registry/team.json và base commit đã chọn.
Đây là cấu hình điều phối ban đầu, không phải phân công cứng theo khả năng của từng model.

Reviewer và integrator dùng checkout riêng phù hợp với nhiệm vụ; không dùng một lane implementation làm nơi hợp nhất mọi module.
