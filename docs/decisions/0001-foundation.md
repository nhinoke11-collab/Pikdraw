# ADR 0001 — nền tảng kho mã và giới hạn quyết định stack

Ngày: 2026-10-14. Trạng thái: **Accepted cho nguyên tắc scaffold/harness; Proposed cho stack ứng dụng**.

## Bối cảnh

PikDraw cần vector chuyên sâu, tương thích nhiều định dạng, in/CNC, CAD 2D và 3D cơ bản. Người phát triển chủ yếu là AI agent của nhiều nhà cung cấp, làm việc trong worktree riêng và cần nạp đúng context nhanh. Kho mã hiện bắt đầu từ scaffold, chưa có sản phẩm hoặc số liệu benchmark.

## Quyết định có hiệu lực

1. Một kho mã với các ranh giới core, modules, desktop, interop, workers và plugin SDK. Dependency có hướng, API/schema có phiên bản; module ownership và scope được khai báo.
2. Một document model dùng chung; module đóng góp command/contribution qua interface. Editor không chứa toàn bộ logic format và geometry.
3. Plugin bên ngoài/importers chạy ngoài process theo thiết kế. Sandbox OS là hạng mục riêng cần chứng minh; chỉ process isolation không đủ.
4. Harness trung lập provider dùng **Python standard library** cho validation tùy chỉnh của task/handoff, kiểm tra cấu trúc, context routing và worktree workflow mà không buộc cài SDK AI. Agent-message JSON Schema là đặc tả, chưa có JSON Schema validator/transport mạng; provider là nhãn, chưa phải API client. Harness là tooling phát triển; engine không phụ thuộc nó.
5. Worktree là đơn vị cô lập chỉnh sửa. Task/message có version, ID, owner và revision; Git commit/diff và tài liệu repository là nguồn xác thực. Memory/context packs có thể tái tạo, không thay thế source/ADR.
6. Tài liệu và các placeholder ghi rõ trạng thái; không gọi scaffold là vector editor đã chạy hoặc mô tả targets như kết quả kiểm thử.

## Stack ứng dụng đang đề xuất

Ứng viên đầu là desktop native **C++/Qt** cho shell và engine hình học hiệu năng cao, render facade có backend được chọn sau benchmark. Không chốt Qt version, rendering library, geometry library, storage engine hoặc license dự án trong ADR này. Mọi dependency phải được rà giấy phép, platform và khả năng phân phối mã nguồn mở trước khi chọn.

Qt/C++ là **đề xuất cần prototype**, không phải quyết định đã khóa. Rendering cần so sánh độ chính xác text/curve/color, latency, cache, CPU fallback, driver behavior và tài liệu lớn trên cấu hình thực. Không chọn engine chỉ dựa trên benchmark marketing hoặc tên thư viện.

Các dự án như Inkscape/FreeCAD có thể là nguồn tham khảo hoặc adapter trong khảo sát, nhưng chưa quyết định fork, embed hay dùng lõi của chúng. Không giả định engine của một ứng dụng hoàn chỉnh có thể nhúng thành module dễ dàng; cần spike kỹ thuật và đánh giá maintenance/license riêng.

## Những ADR bắt buộc tiếp theo

| Chủ đề | Bằng chứng trước khi Accepted |
| --- | --- |
| Desktop/build toolchain | Prototype docking/input, Windows packaging, license và CI build |
| Geometry/render/text/color | Fixtures chính xác, performance baseline, CPU/GPU failure behavior |
| Native storage/units | Schema, precision tests, migration, atomic save và recovery fault injection |
| Format adapters | Version/entity/effect matrix, fixture licenses, editability/export reports |
| Plugin RPC/containment | Protocol compatibility, resource/capability enforcement, OS sandbox tests |
| License và distribution | Dependency inventory, obligations và quy trình cung cấp source |

Một ADR có thể đổi đề xuất nếu bằng chứng tốt hơn. Đổi contract/data model đã có consumers cần kế hoạch migration và task impact; không “sửa hết” trong một worktree không có scope.

## Hệ quả

Team có thể xây module contracts, workflow AI và fixture ngay mà chưa phụ thuộc nhà cung cấp model hoặc engine chưa chọn. Giá phải trả là các tính năng đồ họa cần prototype và benchmark trước implementation diện rộng. Harness checks chỉ chứng minh phần workflow được kiểm tra; không chứng minh chất lượng app.

Tài liệu liên quan: [kiến trúc](../architecture/architecture.md), [sản phẩm](../product/brief.md), [layout](../architecture/workspace-layout.md), [quality gates](../quality/gates.md).
