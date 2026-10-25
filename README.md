# PikDraw

PikDraw là ứng dụng desktop thiết kế vector, in quảng cáo và sản xuất, phát triển bằng C++20.
Runtime hiện tại là Win32 Native Shell: Direct2D 1.1/D3D11 hardware hoặc WARP dựng frame,
đọc về BGRA và GDI trình bày canvas/overlays. SoftwareRasterizer là fallback/oracle.
Trạng thái từng khả năng và giới hạn nằm trong [project-state](docs/project-state.json).

File thiết kế chỉ dùng **.pr**. Payload schema v23 được lưu trong container v23 khi nhỏ hơn 1 MiB;
payload lớn dùng container chunk v24, còn raster source/sample dedup dùng schema v24 trong container
v25. Atomic save stream payload theo chunk vào file tạm; mở file vẫn đọc toàn container và dựng
payload giải nén trong memory. Chi tiết nằm trong [định dạng PR](docs/architecture/native-pr-format.md).

## Phát triển

Đọc [AGENTS.md](AGENTS.md) và [START](docs/START.md), sửa trực tiếp trong source gốc.
Native app: build/win-dev/apps/desktop/pikdraw.exe.

~~~powershell
.\tooling\native\build.ps1
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_document_storage
~~~

Giữ regression tests, xóa probe/output/log sinh trong lượt kiểm tra sau khi báo kết quả.
[Native build](docs/development/native-build.md) là hướng dẫn build/test: hàng đợi giữa các task,
giữ cấu hình hiện có, chọn test, retry incremental và cleanup.

## Module hiện tại và hướng mở rộng

Core gồm document, geometry, rendering, color và typography. Vector, interop, automation,
workers và desktop có implementation. Raster đang tích hợp asset trên đĩa/tile/edit graph;
production/nesting/CNC, scene3d và plugin SDK vẫn là kế hoạch.
MCP HTTP trong app, sidecar stdio và COM đi qua cùng semantic automation service.
Agent có thể query/batch/validate/save; capability công bố giới hạn thật của từng thao tác.

[Kiến trúc hiện tại](docs/architecture/architecture.md) và
[kế hoạch hiệu năng, storage, automation](docs/roadmap/performance-storage-automation.md)
quy định hướng thêm module theo nhu cầu, worker tính toán nặng và ngân sách tài nguyên.
Định hướng này chưa xác nhận startup/FPS, khả năng CNC hay production fidelity.

## Tính năng và tài liệu

PowerClip native có Edit Contents/Frame, nested scope, Fit/Fill/Center, Extract/Release
và Undo. CDR PowerClip parity còn cần fixture và pixel oracle riêng:
[hướng dẫn](docs/design/features/powerclip-editing.md).

[Chỉ mục tài liệu](docs/README.md) phân biệt nguồn hiện tại, hợp đồng và kế hoạch.
Roadmap/ADR ghi thiết kế và bối cảnh; source cùng test đúng revision quyết định trạng thái runtime.

Giấy phép repo Apache-2.0; giữ notice MIT của foundation tại LICENSES/MIT-foundation.txt.
Rà license dependency/asset thực đóng gói trước khi phân phối.
