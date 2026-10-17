# Kế hoạch triển khai PikDraw sau ba spike

> Lưu trữ: kế hoạch này được viết trước khi runtime editor hiện tại hình thành. Các mô tả về Qt/Skia, M0/M1 chưa bắt đầu, native runner chưa có và cách dispatch cũ đã lỗi thời. Không dùng tài liệu này để xác định năng lực hiện tại hoặc mở task triển khai.

Trạng thái hiện tại xem tại [README](../../README.md), [project state](../project-state.json), [sơ đồ source](../development/code-organization.md) và task board. Runtime hiện tại là Win32/GDI với các phần document, geometry, rendering, vector, interop và worker đang có ở nhiều mức độ. Phạm vi chưa hoàn tất được ghi trong project state và module README tương ứng.

Tài liệu này còn giữ bối cảnh và lý do của kế hoạch cũ. Các ranh giới được chấp nhận xem trong [ADR](../decisions/0001-foundation.md); tiêu chí kiểm tra hiện dùng [quality gates](../quality/gates.md), còn quy trình làm việc xem [START](../START.md) và [protocol](../agents/PROTOCOL.md).

[Catalog ps00–ps13](post-spike-tasks.json) chỉ là bản lưu trữ cùng thời điểm, không phải task scheduler hay trạng thái thực thi.
