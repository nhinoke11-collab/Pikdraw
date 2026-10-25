# ADR 0001 — nền tảng và ranh giới dữ liệu

Ngày gốc: 14/10/2026. Hợp nhất trạng thái hiện hành: 23/10/2026.
Quyết định được giữ: core độc lập UI, canonical document, capability theo bằng chứng.
Các mô tả task board/worktree và snapshot prototype cũ đã được bỏ.

## Quyết định

1. Document giữ geometry nguồn, units, Unicode, colors/profiles và stable IDs.
   Geometry nguồn, approximation sản xuất và render cache là ba biểu diễn riêng.
2. Mọi write đi qua command/transaction và một logical writer; readers dùng snapshot bất biến.
   Async completion phải kiểm epoch/revision/generation, Undo không làm revision quay lùi.
3. Modules theo [registry](../../registry/modules.json), không phụ thuộc ngược desktop.
4. Adapter theo format/version normalize vào model trung lập. Import render/edit/export/production
   là các capability riêng, không được nâng trạng thái bằng nhãn “parser chạy thành công”.
5. Parser/plugin ngoài process nhận input có quyền giới hạn; host validate toàn bộ proposal.
   Process isolation không tự chứng minh filesystem/network containment.
6. Workflow chỉ theo [AGENTS](../../AGENTS.md): source gốc, giữ local edits và một build lock.
   Harness lịch sử không là điều kiện để sửa code hoặc đọc tài liệu.

## Runtime hiện hành

C++20 / Win32 Native Shell, Direct2D 1.1 / D3D11 hardware hoặc WARP, staging/readback BGRA
và GDI present; SoftwareRasterizer là CPU fallback/oracle. Qt/Skia là định hướng chưa tích hợp.
Document storage .pr dùng PKDR v23, package nén streaming là phần dự kiến.

[Architecture](../architecture/architecture.md), [document runtime](../architecture/document-runtime.md)
và [kế hoạch nâng cấp](../roadmap/performance-storage-automation.md) mô tả source và thứ tự hiện tại.
ADR này giữ nguyên tắc nền tảng; không xác nhận benchmark, vendor parity hay production readiness.
