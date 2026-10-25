# Chỉ mục tài liệu hiện hành

Đọc [START](START.md) để vào việc. Quy tắc chung duy nhất: [AGENTS](../AGENTS.md).
Trạng thái capability: [project-state.json](project-state.json) và [registry](../registry/modules.json),
luôn đối chiếu source trước khi báo tính năng đã chạy.

| Nhu cầu | Tài liệu chính |
| --- | --- |
| Quy tắc kỹ thuật giữa các module | [C1–C9 và trạng thái hiện hành](../contracts/README.md), rà soát source ngày 24/10/2026 |
| Kiến trúc hiện tại, module tương lai | [Architecture](architecture/architecture.md), [document/runtime](architecture/document-runtime.md) |
| FPS, lag, khởi động, tài liệu lớn, thương mại | [Kế hoạch hiệu năng/storage/MCP](roadmap/performance-storage-automation.md) |
| Công cụ sửa ảnh, kênh màu RGB/CMYK, ICC và in test | [Thiết kế sửa ảnh và màu in](roadmap/image-editing-color-print-20261024.md), từ cơ bản đến nâng cao; chưa nghiệm thu runtime |
| File .pr, nén ảnh, portable assets | [Định dạng PR](architecture/native-pr-format.md), [C5](../contracts/c5-storage.md) |
| Agent thao tác app ít token | [MCP quick workflow](development/automation-quickstart.md), [MCP server](../apps/mcp-server/README.md) |
| Build/test, hàng đợi giữa các task, retry và dọn output | [Native build](development/native-build.md), [acceptance](quality/feature-acceptance.md) |
| UI/Properties | [UI standard](design/ui-ux-standard.md), [workspace](architecture/workspace-layout.md) |
| Điều chỉnh ảnh raster đang triển khai | [Ánh sáng](design/features/raster-light-adjustment.md), [Color Balance](design/features/raster-color-balance.md), [Channel Mixer](design/features/raster-channel-mixer.md), [HSL](design/features/raster-hsl-adjustment.md), [HSL theo dải màu](design/features/raster-selective-hsl.md), [Curves](design/features/raster-curves-adjustment.md), [White Balance](design/features/raster-white-balance.md), [Vibrance](design/features/raster-vibrance-adjustment.md) |
| Mask chỉnh ảnh cục bộ | [Phơi sáng cục bộ và mask chữ nhật](design/features/raster-local-exposure-mask.md) |
| Nhập, so sánh và báo cáo số đo màu | [Measurement Run](design/features/measurement-run-import.md) |
| Import/render/edit/export fidelity | [Vector interoperability](architecture/vector-resource-interoperability.md), [C8](../contracts/c8-import-fidelity.md), [C9](../contracts/c9-appearance-effects.md) |
| Tổ chức code | [Code organization](development/code-organization.md), scoped AGENTS/README của module |

Các roadmap feature còn trong roadmap/ là spec theo domain; phần trạng thái có ngày là snapshot
lúc viết, không ghi đè source hoặc workflow hiện tại. Đã hợp nhất workflow cũ, các bản kế hoạch
renderer/architecture trùng lặp và audit renderer CPU cũ vào các tài liệu chính trên.
Không tạo thêm skill/rule chỉ để lặp lại AGENTS. Khi sửa capability, cập nhật tài liệu sở hữu
capability đó và snapshot hiện hành; không sinh bản handoff/log/ảnh test dưới docs.

Hướng dẫn build/workflow lặp trong ADR 0007 đã được bỏ; dùng AGENTS và Native build ở trên.
Ví dụ lệnh trong roadmap/audit có ngày là ngữ cảnh của lần chạy đó, không thay hướng dẫn hiện hành.
