# PikDraw: khởi động một phiên agent

## Sự thật hiện tại
PikDraw đang ở giai đoạn foundation. Có bộ khung/harness; chưa có editor hoặc converter chạy được.
Trạng thái máy đọc ở `docs/project-state.json`. Dependency và context module ở `registry/modules.json`.
Kiến trúc native desktop được đề xuất; ngôn ngữ lõi và renderer phải qua spike/ADR trước khi chốt.
Windows: dùng `.\tooling\pikdraw.ps1` thay `python tooling/agent_harness.py` nếu alias Python chưa hoạt động.

## Đọc tối thiểu
1. `AGENTS.md` — invariants và cách bàn giao.
2. `docs/project-state.json` — trạng thái ngắn.
3. Task được giao và context packet: `python tooling/agent_harness.py context --task-id TASK`.
4. Module README/AGENTS và skill liên quan được liệt kê trong packet.

Harness cung cấp bản tóm tắt và đường dẫn theo nhiệm vụ, không tải toàn bộ tài liệu.
Nếu không có task, xem `task list`; không đoán phạm vi hoặc quyền sửa.

## Nguồn sự thật
- Ranh giới module: `registry/modules.json`.
- Quyết định đã chấp nhận: `docs/decisions/`.
- Quy tắc cụ thể: scoped AGENTS.md; hợp đồng: `contracts/`.
- Tiến độ task trên cùng máy: bảng chung `.agent/tasks/`, được harness quản lý.
- Đã kiểm chứng: evidence trong handoff; đề xuất thiết kế không phải tính năng đã triển khai.

## Trước khi sửa
Claim task bằng owner/session duy nhất; tạo worktree từ commit base; đọc `docs/agents/WORKTREES.md`.
Không sửa main trong lúc triển khai. Không tự mở rộng owned paths của task đang chạy.
Chỉ coordinator tích hợp kết quả sau review và checks; root project state cập nhật theo bản đã tích hợp.

## Tiết kiệm context
Không đọc full repository, corpus nhị phân, generated files hoặc lịch sử chat.
Packet mặc định có giới hạn ký tự; chỉ mở thêm file khi code/contract/bug đòi hỏi.
Cuối phiên giữ: mục tiêu, base/head commit, thay đổi, evidence, câu hỏi còn mở, bước tiếp theo.
