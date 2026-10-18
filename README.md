# PikDraw

PikDraw là ứng dụng desktop chỉnh sửa vector cho thiết kế, in quảng cáo và sản xuất.

Runtime hiện tại dùng C++20 / Win32 Native Shell kết hợp Direct2D 1.1 / D3D11 hardware hoặc WARP qua `ViewportRenderer`. Frame được staging/readback về BGRA rồi GDI trình bày canvas và overlays; SoftwareRasterizer là CPU fallback/oracle. Qt 6/Skia vẫn là định hướng tương lai. Nhiều phần của editor và document đã chạy, nhưng M1, hiệu năng thiết bị, độ chính xác sản xuất và tương thích định dạng chưa được nghiệm thu đầy đủ. Phạm vi đã làm và bằng chứng nằm trong docs/project-state.json.

## Bắt đầu

Đọc AGENTS.md và docs/START.md trước khi nhận task.

Trên Windows:
- .\tooling\pikdraw.ps1 doctor
- .\tooling\pikdraw.ps1 task list
- .\tooling\native\build.ps1
- .\tooling\native\build.ps1 -Test

Cần Python 3.10+ cho harness. Launcher PowerShell tự tìm Python đã có trên máy.

## Cấu trúc mã

- apps/desktop: cửa sổ và giao diện Win32/GDI; actions, presenters, runtime, shell, tools và workflows.
- core/document, geometry, rendering, color, typography: dữ liệu tài liệu, hình học, vẽ, màu và chữ.
- core/localization: chỗ ghi đặc tả; chưa có runtime localization.
- modules/vector và modules/interop: công cụ vẽ và adapter định dạng hiện có.
- modules/raster, production, scene3d: module kế hoạch, chưa có implementation sản phẩm.
- platform/workers: host tiến trình worker và kiểm tra proposal.
- packages/plugin-sdk: đặc tả; plugin host/SDK runtime chưa hoàn tất.
- tests ở cấp repo chứa harness, architecture và integration audit; test C++ nằm cạnh module sở hữu.
- contracts, registry và tooling: schema, dependency map, task harness và build/check runners.

## Tài liệu

- Trạng thái và giới hạn: docs/project-state.json
- Sơ đồ source hiện tại: docs/development/code-organization.md
- Kiến trúc và ranh giới module: docs/architecture/architecture.md
- Quy tắc build: docs/development/native-build.md
- Quy tắc chất lượng theo rủi ro: docs/quality/gates.md
- Cấu trúc và cách chọn test nghiệm thu tính năng: docs/quality/feature-acceptance.md
- Thiết kế UI: docs/design/ui-ux-standard.md
- Một source và xử lý worktree cũ: docs/agents/WORKTREES.md

Tài liệu roadmap là kế hoạch, không chứng minh code đã có. Task board và evidence theo source snapshot mới nhất mới là trạng thái thực thi.

Giấy phép repository là Apache-2.0; thông báo MIT cho phần foundation có trước được giữ tại LICENSES/MIT-foundation.txt. Kiểm tra license của dependency và tài sản trước khi phân phối.
