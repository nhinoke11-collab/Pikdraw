# PikDraw

Bộ khung mã nguồn mở cho ứng dụng desktop thiết kế vector chuyên nghiệp, chỉnh ảnh cơ bản, in quảng cáo, CNC, bản vẽ thi công và 3D cơ bản.

**Hiện có:** cấu trúc module, tài liệu kiến trúc, quy tắc thiết kế, skill dự án và harness phối hợp agent bằng Git worktree.
**Chưa có:** engine đồ họa, trình chỉnh sửa chạy được, bộ đọc PDF/CDR/AI/EPS/DXF/SKP hoặc plugin host. Các mục này là công việc phát triển tiếp theo.

## Bắt đầu

Cần Git và Python 3.10+. Không cần thư viện Python bên ngoài.

Windows PowerShell có launcher tự tìm Python hoặc runtime đã có tại máy:

```powershell
.\tooling\pikdraw.ps1 doctor
.\tooling\pikdraw.ps1 task list
```

Nếu dùng Python ở vị trí khác, đặt `PIKDRAW_PYTHON` trỏ đến executable. Launcher không tải hoặc cài phần mềm.

```sh
python tooling/agent_harness.py doctor
python -m unittest discover -s tests/harness -v
python tooling/agent_harness.py task list
```

Agent mới đọc [AGENTS.md](AGENTS.md), [docs/START.md](docs/START.md), sau đó lấy context riêng của task. Không nạp toàn bộ kho tài liệu.

- [Trạng thái hiện tại](docs/project-state.json)
- [Kiến trúc](docs/architecture/architecture.md)
- [Document, precision và concurrency](docs/architecture/document-runtime.md)
- [Ba spike foundation trong hai tuần](docs/roadmap/foundation-spikes.md)
- [Kế hoạch triển khai sau spike](docs/roadmap/post-spike-implementation.md) — 26 task có dependencies, scope, acceptance và vòng lặp UI native
- [Catalog task sau spike](docs/roadmap/post-spike-tasks.json) — đặc tả điều phối, chưa là scheduler hoặc task đang chạy
- [Bố cục giao diện](docs/architecture/workspace-layout.md)
- [Blueprint giao diện](docs/architecture/workspace-blueprint.svg)
- [Quy trình worktree](docs/agents/WORKTREES.md)
- [Giao thức agent](docs/agents/PROTOCOL.md)
- [Ba lane phát triển](docs/agents/PREPARED-LANES.md)
- [Registry module](registry/modules.json)
- [Cổng chất lượng](docs/quality/gates.md)

## Cách tổ chức

```text
apps/desktop/             shell và layout desktop
core/                     document, geometry, rendering, typography, color
modules/                  vector, raster, production, scene3d, interop
platform/workers/         host tác vụ cách ly
packages/plugin-sdk/      hợp đồng plugin và API mở rộng
contracts/                schema công khai có phiên bản
registry/                 module, dependency và định tuyến context
skills/                   skill dùng chung giữa các nhà cung cấp agent
tooling/                  harness Git worktree và bàn giao
docs/                     quyết định, trạng thái và tài liệu đọc theo nhu cầu
tests/                    kiểm thử harness; corpus đồ họa sẽ bổ sung
.agent/                   bảng task dùng chung cục bộ, không đưa vào Git
.worktrees/               checkout riêng từng task, không đưa vào Git
```

Các adapter Claude/Gemini/Copilot chỉ dẫn về cùng một bộ quy tắc; chúng không tự kết nối hay chạy dịch vụ AI. Việc gọi nhà cung cấp và trao đổi giữa máy khác nhau cần tích hợp riêng.

Giấy phép MIT áp dụng cho mã/tài liệu gốc của bộ khung. Giấy phép bản phân phối ứng dụng phải được chốt trong ADR khi chọn các dependency đồ họa và chuyển đổi file.

Baseline khảo sát là C++20/Qt/Skia; ngôn ngữ/backend/schema và budgets ứng dụng còn chờ spike. Mục tiêu 60 FPS chỉ nghiệm thu trên corpus/máy đã xác định; software có budget riêng. Native CDR export, AI round-trip, CAM và mô phỏng quang học chưa thuộc lát cắt đầu.
