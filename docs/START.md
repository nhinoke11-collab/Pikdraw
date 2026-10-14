# PikDraw: khởi động một phiên agent

## Sự thật hiện tại
PikDraw đang ở giai đoạn foundation. Có bộ khung/harness; chưa có editor hoặc converter chạy được.
Trạng thái máy đọc ở `docs/project-state.json`. Dependency và context module ở `registry/modules.json`.
Baseline khảo sát là C++20/Qt/Skia; backend, schema geometry/document, executor topology và budgets chưa khóa. Ba spike phải có evidence trước triển khai diện rộng: [kế hoạch hai tuần](roadmap/foundation-spikes.md). Không suy ra năng lực app từ harness checks.
Windows: dùng `.\tooling\pikdraw.ps1` thay `python tooling/agent_harness.py` nếu alias Python chưa hoạt động.

## Khi được giao triển khai sau ba spike

Đọc [kế hoạch sau spike](roadmap/post-spike-implementation.md) và entry được giao trong
[catalog 26 task](roadmap/post-spike-tasks.json). Bắt đầu bằng `ps00-accept-spikes` để nghiệm thu
evidence, rồi contracts/build trước các module song song. Chỉ nhận task có prerequisites đã
tích hợp, gate được nhận và lease không chồng lấn; không suy ra pass từ trạng thái chat hoặc hết hai tuần.

Catalog là đặc tả, harness chưa tự đọc dependencies hoặc gọi agent. Task board là trạng thái thực tế.
Prepared worktrees từ base cũ và phần localization chưa commit phải được kiểm kê/đồng bộ an toàn;
không ghi đè để chạy theo catalog. UI dự kiến dùng shell Qt thật, runtime `.ui`/QSS trong dev;
thay C++ vẫn cần build tăng dần. Hiện chưa có native preview hoặc các lệnh native trong kế hoạch.

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
- Runtime semantics: [document, precision và snapshots](architecture/document-runtime.md); [ADR 0002](decisions/0002-runtime-and-hardware.md) tách principles Accepted khỏi implementation Proposed.

## Trước khi sửa
Claim task bằng owner/session duy nhất; tạo worktree từ commit base; đọc `docs/agents/WORKTREES.md`.
Không sửa main trong lúc triển khai. Không tự mở rộng owned paths của task đang chạy.
Chỉ coordinator tích hợp kết quả sau review và checks; root project state cập nhật theo bản đã tích hợp.

## Tiết kiệm context
Không đọc full repository, corpus nhị phân, generated files hoặc lịch sử chat.
Packet mặc định có giới hạn ký tự; chỉ mở thêm file khi code/contract/bug đòi hỏi.
Cuối phiên giữ: mục tiêu, base/head commit, thay đổi, evidence, câu hỏi còn mở, bước tiếp theo.
