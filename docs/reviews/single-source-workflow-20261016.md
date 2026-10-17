# Bàn giao quy tắc một source — 16/10/2026

Đã thực hiện bước sửa docs/harness theo yêu cầu chủ dự án; không commit và không sửa tính năng app.
Primary HEAD vẫn là `11512046d23fac08059ecd37382179283b4530a0`; source có local edits được kế thừa.
Hai task: `single-source-harness-20261016`, `single-source-docs-20261016`.

## Kết quả

- Một source gốc, Codex, một coordinator và task board; không tạo worktree/clone/branch task mới.
- Harness chặn worktree create, task provider mới ngoài Codex và metadata writes từ linked checkout. Context lấy quy tắc từ primary source.
- Claim giữ baseline; handoff ghi source snapshot/hash và changed paths gồm thay đổi chưa commit, untracked, xóa/đổi tên; giữ nguyên index và local edits kế thừa.
- Runner configure/build/test dùng shared lock và một preset/output; Debug/Release cùng `build/win-dev/apps/desktop/pikdraw.exe`.
- CMake chặn linked source và output khác. Direct CMake có thể bỏ qua khóa runner nên các phiên phải dùng runner.
- START, protocol, kỹ năng dự án, team registry và hướng dẫn roadmap đã bỏ workflow worktree/provider/build riêng. Quyết định: ADR 0007.

## File map và metric

| Trách nhiệm | File | Dòng không rỗng |
| --- | --- | ---: |
| CLI, task, lease, snapshot và handoff | `tooling/agent_harness.py` | 374 |
| Path/Git/JSON và atomic storage | `tooling/harness_support.py` | 123 |
| Module/context/doctor | `tooling/harness_context.py` | 148 |
| Native build/test và shared lock | `tooling/native/build.ps1` | 65 |
| Hồi quy task/snapshot/source | `tests/harness/test_agent_harness.py` | 331 |
| Hồi quy CMake/runner/lock | `tests/harness/test_native_workflow.py` | 87 |

Các file mới/sửa ở bảng nằm dưới cap của loại tương ứng; không sửa shared size policy hoặc exception để pass.

## Evidence đã chạy

- Python 3.12.14 stdlib runtime cục bộ: `python -m unittest discover -s tests/harness -v`: **35/35 pass**, 110.409 giây. Các Git mutation của test chỉ xảy ra trong temporary repos.
- Bao phủ root-only writes, provider/worktree rejection, primary context, ownership/lease, bảo toàn dirty/untracked, staged rename/deletion, snapshot metadata collision, Git refs không đổi khi create bị chặn.
- CMake thực tế trong fixture tạm từ chối output khác và linked source trước compile. Runner dùng mock CMake để kiểm shared output, active lock và giải phóng lock khi configure lỗi; không build app thật.
- `pikdraw.ps1 doctor`: **ok=true**, 15 module, issues rỗng.
- PowerShell parser: pass cho native scripts. Audit 13 tài liệu, 47 local links, code fences và JSON: pass.
- `git diff --check` trên phạm vi docs/harness/CMake/registry/skills: pass.

## Giới hạn và bước tiếp theo

Đã đếm 69 Git worktree đăng ký, gồm source gốc và **68 worktree cũ dưới `.worktrees`**.
Chưa đồng bộ/xóa worktree nào, chưa dọn app alias/build folder cũ và chưa rebuild app trong task này.
Các regression harness/runner không chứng minh graphics, fidelity, native feature acceptance hoặc full CTest ứng dụng.
Final snapshot/handoff có thể được review; không gắn nhãn có independent review cuối khi chưa thực hiện.

Tiếp theo: coordinator kiểm kê dirty/untracked/ignored và changes đã có ở root; bảo toàn baseline rồi tích hợp,
kiểm chứng từng worktree và xóa ngay cây đã đạt theo `docs/agents/WORKTREES.md`. Giữ cây còn conflict/thiếu evidence.
Sau di trú, build/kiểm tra trên canonical output và dọn các output cũ bằng inventory riêng.
