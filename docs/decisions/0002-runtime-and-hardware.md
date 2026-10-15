# ADR 0002 — thích ứng phần cứng, runtime và phối hợp agent

Ngày: 2026-10-15. Cập nhật sau thẩm định cùng ngày.
Trạng thái: **Accepted cho nguyên tắc resilience/ownership; Proposed cho backend, executor topology và ngưỡng hiệu năng**.

## Bối cảnh và sửa quyết định

Máy xưởng RAM 8 GB/GPU tích hợp và workstation RTX đều là đối tượng phục vụ. Tài liệu có khổ hàng chục mét, 100.000+ control point/segment, chữ, ảnh và transparency. Khổ vật lý hoặc số object đơn thuần không mô tả chi phí.

Bản đầu ghi Accepted trước benchmark. Bản này thay tầng theo nhãn GPU bằng capability probing, bỏ cam kết đúng bốn thread/cứu mọi lỗi driver, thay tiêu chí không merge conflict bằng integration gates. Runtime dưới đây chưa được triển khai.

PS00 ngày 2026-10-15 chưa khóa backend, executor topology hoặc budget. Independent Qt/Skia run đo CPU phases trên Qt offscreen nhưng không đo present; document tests pass correctness subset nhưng chưa đóng đầy đủ stale schedules/durability; worker containment chỉ chặn rõ 4/5 canaries. Xem [ma trận evidence và raw artifact](../reports/foundation/ps00-acceptance-20261015.md). Các mode/budget bên dưới vẫn Proposed và mọi chỉ số thiếu được để trống trong [baseline](../quality/baseline.json).

## Nguyên tắc có hiệu lực

- Render không sửa document; source/production không phụ thuộc chất lượng preview.
- UI không chờ parse/encode, geometry lớn hoặc save trong event loop.
- Có software 2D khi GPU không khả dụng trong các tình huống đã kiểm chứng; công bố chất lượng/giới hạn.
- Budget chung cho caches, undo/snapshots, assets, compute và workers; iGPU chia sẻ RAM với hệ thống.
- Provider là nhãn phiên; task có owner, base, owned paths và worktree; contracts cần consumer review.

## Backend theo capability

Baseline spike là C++20/Qt/Skia qua facade. Chọn rõ Ganesh/Graphite và build revision; không trộn capability của các implementation thành backend đã hoạt động.

| Mode đề xuất | Cách chọn | Điều kiện |
| --- | --- | --- |
| GPU đã kiểm chứng | Probe context/surface, required features/limits, driver/build và correctness | Đạt budget corpus trên từng máy; có tùy chọn ép backend |
| GPU thay thế | Backend khác được build/test; ví dụ GL ES qua ANGLE/D3D11 trên Windows | Không chọn chỉ từ nhãn Intel/AMD hoặc GPU rời |
| Software 2D | Ép CPU, GPU init thất bại hoặc device-loss xử lý được | Raster/present hoạt động, budget riêng, giữ source data |

iGPU có thể chạy API hiện đại; dGPU có thể lỗi driver/backend. Metal phụ thuộc OS. Tên API không chứng minh build Skia có backend đó. [Skia ANGLE](https://skia.org/docs/user/special/angle/) mô tả GL ES qua ANGLE, không phải D3D11 switch tự động.

Khởi tạo cần probe, feature correctness smoke test và policy driver lỗi có version. Diagnostics ghi mode/lý do/limits cục bộ; không ghi nội dung khách hàng vào telemetry. Không retry GPU lỗi vô hạn.

## Mất GPU và software

Device-loss có tín hiệu xử lý được: dừng submission, abandon/release theo backend lifecycle, hủy GPU handles/caches, tạo software surface và rebuild từ snapshot/assets CPU. Giữ source/asset reference để tái tạo; không phụ thuộc duy nhất vào texture GPU. [GrDirectContext](https://api.skia.org/classGrDirectContext.html) cung cấp context lifecycle; host chịu trách nhiệm phục hồi.

Crash/hang native driver cùng process không bảo đảm quay lại host để switch CPU. Nếu bắt buộc sống sót qua lỗi đó, cần spike GPU process riêng. Trước khi có evidence, công bố giới hạn và checkpoint/restart recovery; device-loss injection không chứng minh chịu mọi driver crash.

Software đa luồng cần implementation/benchmark, không phải một flag cho mọi draw call. Tile rendering giữ compositing order, clip/group semantics và vùng phụ trợ filter. Present software phải hoạt động khi không có accelerated context.

## Budgets và phép đo

Các số dưới là **ngưỡng đề xuất cho spike; chưa phải kết quả app hoặc CI gate đã thực thi**. Chốt corpus checksum, hardware/driver, resolution/DPI, power mode, flags và commit trước khi đo. Thu raw frame/input timestamps, p50/p95/p99, missed deadlines và khoảng dừng lớn nhất; báo từng máy/scenario.

| Profile | Mục tiêu khảo sát | Phạm vi |
| --- | --- | --- |
| GPU tối thiểu, 1080p/60 Hz | Frame p95 ≤ 16,7 ms; p99 ≤ 33,3 ms; input-to-visible p95 ≤ 50 ms | Pan/zoom, warm/cold cache, zoom-to-fit và invalidation |
| Software tương tác | 30 FPS khảo sát: frame p95 ≤ 33,3 ms; input-to-visible p95 ≤ 100 ms | Cache/LOD công khai; chưa cam kết full-quality 60 FPS |
| Stress ngoài corpus duyệt | Báo latency/memory và preview giảm chất lượng | Không suy ra 60 FPS cho mọi scene từ một fixture |
| Startup/idle | < 300 ms / < 100 MB là stretch targets | Cold/warm, first window/editable document, empty/loaded và phương pháp đo rõ |

Đo private/working-set RAM, peak CPU assets/caches/history/workers; báo GPU dedicated/shared và cách tránh đếm trùng. Atlas RGBA8 4096² là 64 MiB chưa gồm mipmap/intermediates. Memory/regression budgets được duyệt sau baseline. 120 FPS là khảo sát sau 60 Hz. Idle redraw khi có thay đổi, không giữ loop chỉ để tăng FPS counter.

## Culling, cache và render precision

- R-tree/BVH nhất quán với revision; bounds gồm stroke/effects.
- Object-level culling không giải quyết một path lớn hoặc zoom-to-fit. Spike nhiều object và ít path nhiều segment; khảo sát chunking/LOD bảo toàn semantics.
- Model/transform binary64; trừ camera/tile origin bằng double trước khi cast float. Không ghi precision mất ở parser/render ngược vào source.
- Tile/raster cache key gồm dependency revision, scale bucket, DPI, profile, clip/effect và backend generation. Invalidation theo dependency và compositing order.
- Atlas cho glyph/image resources; cache lớp tĩnh có tile/memory cap. Test pan lộ vùng mới, zoom, drag, font/profile change và device-loss.
- LOD chỉ thay preview. Export đọc nguồn document, không lấy raster cache làm vector/CNC.

## Executors và ownership

| Vai trò | Trách nhiệm |
| --- | --- |
| Main/UI | Events, tool preview, command routing; Qt GUI ở main; commit ngắn có budget |
| Document writer | Một writer logic tuần tự, transaction nguyên tử, revision đơn điệu; có thể trên main nếu được đo |
| Render owner | GPU context/resources, submit/present; thread placement phụ thuộc Qt prototype |
| Compute | Pool giới hạn; snapshot in, proposal có revision/generation out; không mutable pointer |
| I/O | Serialize snapshot chốt, checkpoint/journal và atomic replace theo nền tảng |
| Worker processes | Parser/plugin lifecycle, IPC validation, cancellation/quota/containment |

[Qt thread rules](https://doc.qt.io/qt-6/threads-qobject.html) áp dụng GUI/QObjects. GPU context không tự thread-safe nhờ immutable document. Đo present/lifetime trước khi chốt UI-render chung hay tách.

Scheduler giới hạn jobs/bytes, ưu tiên input/render, tránh dùng hết cores cho compute khi CPU renderer đang chạy. Coalesce frame/preview lỗi thời, không bỏ command. Validate worker response trước commit; cancel/close/undo/source edit vô hiệu proposal theo [document/runtime](../architecture/document-runtime.md).

## Multi-agent và integration gates

Dynamic claim giữ provider như metadata. Harness có lease/overlap, committed-diff scope và registry checks; chưa có OS write sandbox, code dependency scanner hoặc semantic contract runner đầy đủ.

Gate cần actual diff kể cả rename/config, actual includes/links, units/axes/fill/tolerance, consumers và revision sau merge. Branch xanh hoặc merge sạch không chứng minh semantics. Shared gate/fixture/oracle không thuộc quyền sửa tùy ý của task feature. Geometry/interop/storage/breaking contracts cần reviewer độc lập; xem [protocol](../agents/PROTOCOL.md) và [gates](../quality/gates.md).

## Điều kiện chốt runtime

Chạy [ba spike hai tuần](../roadmap/foundation-spikes.md), lưu source/dependency revisions/licenses, corpus rights/checksums, raw measurements và limits. Mode/máy chưa đo là thiếu evidence. ADR implementation tiếp theo chọn backend, budgets, writer/present topology và containment sau review; không đổi targets thành measured results.
