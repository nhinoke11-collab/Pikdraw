# P00 — Baseline triển khai Pen Tool

Ngày 16/10/2026 · Task `pen-implementation-p00-baseline-20261016`.
Mục tiêu ghi trạng thái trước khi chạm runtime; không khẳng định app chạy hay Pen đã tích hợp.

> Snapshot lịch sử tại lúc claim P00. Sau đó P00b khôi phục test sources, P00d ghi nhận Debug CTest 63/63 trên snapshot tích hợp, và P03–P05 đã thêm Pen core/controller/numeric code. Các nhận định source/app bên dưới không phải trạng thái hiện tại.

## Baseline source và app

- Primary HEAD lúc claim và kiểm tra: `11512046d23fac08059ecd37382179283b4530a0`.
- Checkout đã dirty trước task Pen (113 tracked paths theo snapshot `git status --short --untracked-files=no`), gồm desktop, vector, geometry, document, CMake, state và roadmap. Đây là inherited work; không được dùng HEAD một mình so sánh hoặc ghi đè.
- Executable chuẩn `build/win-dev/apps/desktop/pikdraw.exe` hiện có; SHA-256 lúc kiểm tra: `8D5CEECDBC7B7C413DEDCCD2D6A9F4AB11602BD18921613CBBCC49EAD465D44D`. Hash identifies artifact only, chưa chứng minh nó khớp dirty source hoặc có Pen.
- Không thấy process `pikdraw` đang chạy và không thấy `.agent/native-build.lock` ở thời điểm chụp. Đây là quan sát nhất thời; kiểm tra lại ngay trước build.

## Test/build prerequisites

- Inventory [P00a](canvas-test-inventory.md) ghi 72 source test/helper đã bị xóa cùng provenance.
- [P00b task record](../../.agent/tasks/canvas-upgrade-p00b-restore-tests-20261016.json) xác nhận khôi phục đủ 72/72 file từ archive/snapshot/index, mọi hash sau chép khớp; không sửa production source/CMake.
- Task `canvas-upgrade-p00c-validation-20261016` đang `in_progress` với owner `session-canvas-p00c`; scope gồm toàn bộ 72 file đã kiểm kê và các CMakeLists/CMake helper. Vì vậy Pen không chạy build/CTest/harness, không sửa source test hoặc CMake trong lúc task validation giữ các path này.
- P00b cho phép dùng test source đã khôi phục; không phải quyền để task khác cùng lúc sửa những path P00c đang sở hữu.
- P00 review source/controller/action/UI hiện có: Pen ID được khai báo nhưng chưa có action handler, active-tool dispatch, menu/toolbox consumer hoặc vector Pen controller. D10/D11 vẫn `planned` trong roadmap cũ. Kết luận gốc và path evidence ở [audit kế hoạch](../roadmap/pen-tool-20261016/audit.md).

## Điều kiện mở tiếp

1. Dùng baseline task snapshot `.agent/snapshots/pen-implementation-p00-baseline-20261016/source-ein12f7r/manifest.json` làm xuất phát điểm cho tài liệu này; mọi code task mới cần claim/manifest riêng.
2. Không build trong khi P00c sở hữu build configuration; chờ task P00c handoff/release và kiểm lại lock/process/dirty paths.
3. Trước khi claim paths CMake/native shared, xem task board hiện thời. Pen source mới có thể phát triển trong paths hẹp mới, nhưng link libraries/action registry/integration shared vẫn cần serialized task/review.
4. Hợp đồng D01 hiện còn `proposed_pending_consumer_review`; Pencil/Pen semantic/input extension phải có task/scoped consumer review trước khi coi là khóa.
5. Native tests hiện đã được khôi phục theo P00b. Các sửa đổi test/CMake đang thuộc P00c; tránh ghi đè hoặc assume chúng ổn định trước handoff.

## Giới hạn P00

Không chạy build hay test; không đổi mã runtime, test, CMake, shared contract hoặc project state.
Đây chỉ là baseline quan sát tại một thời điểm trong primary dirty checkout.

