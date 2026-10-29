# Hợp đồng kỹ thuật PikDraw

Rà soát C1–C9 ngày 2026-10-24 theo source đang làm việc. Đây là các quy tắc giữa module:
đơn vị, dữ liệu, command/Undo, render, lưu file, worker, UI và tương thích định dạng.
Agent chỉ cần đọc hợp đồng liên quan đến phần đang sửa rồi mở source authority ở cuối tài liệu.

| Hợp đồng | Phạm vi | Trạng thái cần nhớ |
| --- | --- | --- |
| [C1 Geometry](c1-geometry.md) | mm/binary64, transform, curves, editable nodes/handles | Handle là tọa độ tuyệt đối trong hệ local; topology acceptance theo từng command. |
| [C2 Document](c2-document.md) | Writer, snapshot, IDs, revision, transaction, async | Undo giới hạn số bước; byte budget và một số guard chuyển trang còn thiếu. |
| [C3 Rendering](c3-rendering.md) | Camera, projection, culling, damage, effects | NativeWindow đọc full frame về GDI; backend có swap-chain prototype opt-in, chưa tích hợp overlay/visible-present. |
| [C4 Color/typography](c4-color-typography.md) | Màu nguồn, ICC, overprint metadata, Unicode/font | Preview không chứng nhận PDF/X/separations; fallback phụ thuộc font/OS. |
| [C5 Storage](c5-storage.md) | .pr, PKDR, save/recovery/migration | Payload v23–v25; container v23–v26, reader v2–v26; atomic save streams chunks, load full-buffer; index/lazy assets còn mở. V4/v5 là phụ lục lịch sử. |
| [C6 Workers](c6-workers.md) | Cô lập process, source handle, IPC, cancel/RAM | V2 đã tích hợp nhiều adapter; pressure waiter chưa là memory budget toàn app. |
| [C7 Actions/UI](c7-actions-ui.md) | Action routing, Properties, shortcuts, ngôn ngữ | Win32 Native Shell; Qt/QSS reload helper không chứng minh Qt UI runtime. |
| [C8 Import fidelity](c8-import-fidelity.md) | Adapters, staging/review/apply, report/provenance | Đã có import và report lưu theo page; full per-element/source round-trip còn giới hạn. |
| [C9 Appearance](c9-appearance-effects.md) | Opacity, blend, blur/shadow, gradient authority | Native Path/Text và SVG opacity subset; capability theo effect/representation cụ thể. |
| [C10 Automation](c10-agent-automation.md) | Agent automation, MCP/COM sidecar, typed document services | Service dùng chung cho stdio MCP và Windows COM IDispatch; guards busy interaction. |
| [C11 Production](c11-file-export-production.md) | File export and production geometry, CNC/CAM preflight | 10 gates nghiệm thu C11-G1..G10, ngân sách sai lệch Fine profile 0,01 mm. |

## Cách đọc trạng thái

- **Có trong source**: API/nhánh xử lý tồn tại ở paths được dẫn. Chưa tự chứng minh UI, FPS, fidelity, containment hoặc release readiness.
- **Yêu cầu/đề xuất/còn mở**: semantics đích hoặc gate chưa đủ evidence; không được quảng cáo thành feature đã hoàn tất.
- **Lịch sử**: schema cũ để migration/fixtures; không chọn writer, runtime hoặc workflow hiện tại.
- `Version` ở đầu tài liệu là phiên bản tài liệu. PKDR version, IPC major và effect schema là các số độc lập; chỉnh docs không đổi chúng.

Ngày audit ghi snapshot source đã đọc, không phải ngày chạy test. Link regression chỉ vị trí coverage;
khi cần công bố kết quả phải có lệnh, build/revision, fixture/profile và evidence của lượt chạy tương ứng.
Giữ các semantic vectors hữu ích; output/log/probe dùng một lần dọn theo [AGENTS.md](../AGENTS.md).

## Nguồn bổ sung

- [PR format](../docs/architecture/native-pr-format.md): thiết kế container nén/index/chunks/assets còn phải triển khai.
- [Plan hiệu năng, storage và automation](../docs/roadmap/performance-storage-automation.md).
- [Automation quickstart](../docs/development/automation-quickstart.md): typed service chung MCP/COM và document guards.
- [Vector interoperability](../docs/architecture/vector-resource-interoperability.md): adapter riêng theo format/version, dùng chung semantics sau normalize.

Workflow agent chỉ ở AGENTS.md. Contract không tạo thêm task-board/lease/handoff bắt buộc.
JSON schemas và plugin manifest trong thư mục này vẫn là đặc tả khi chưa có host tương ứng;
chúng không xác nhận đã có plugin loader. Breaking API/wire/schema changes cần version/migration
và review consumer; lượt audit này chỉ sửa tài liệu.
