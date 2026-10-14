# Kế hoạch ba spike foundation trong hai tuần

Ngày thiết kế: 2026-10-15. Trạng thái: **kế hoạch chưa thực thi; chưa có engine benchmark hoặc format coverage đã đạt**. Hai tuần là 10 ngày làm việc tính từ ngày đủ toolchain, máy và quyền corpus; không là cam kết ngày phát hành.

Mục tiêu: cho phép bác bỏ stack/backend/data model bằng số liệu trước triển khai diện rộng. Theo [ADR 0001](../decisions/0001-foundation.md), [ADR 0002](../decisions/0002-runtime-and-hardware.md), [document/runtime](../architecture/document-runtime.md) và [quality gates](../quality/gates.md).

## Chuẩn bị và ownership

Ngày 1–2: coordinator xác nhận máy/corpus có quyền, baseline commit, versions/licenses, budgets và semantics tối thiểu. Tạo task contracts riêng nếu cần DTO/API; consumer review khóa units/axes/transforms/revisions/cancel/errors trước song song. JSON shape không thay semantic vectors.

| Lane đề xuất | Scope task cần giao | Handoff |
| --- | --- | --- |
| A: viewport | `core/rendering` và minimal host trong `apps/desktop` theo owned paths cụ thể | Build/run, raw frame/input/memory, backend correctness/recovery |
| B: document/geometry | `core/document`, `core/geometry` theo paths cụ thể | Precision/topology oracles, transactions/history/recovery and timing |
| C: interop/containment | `modules/interop`, `platform/workers` theo paths cụ thể | Capability/loss reports, edit-save-export, invalid IPC/permissions/crash |

Đây là scope đề xuất, không tự mở rộng tasks có sẵn. `desktop-layout`, `document-spike`, `interop-spike` hiện là lanes scaffold hẹp; coordinator tạo tasks thay thế hoặc cập nhật assignment khi đã dừng/released theo protocol. Không chạy A cùng task khác sở hữu desktop. Mọi branch dùng cùng contract revision; consumer migrations nằm trong chuỗi integration có review.

Reviewer kiểm tra artifact trên exact head; integrator kiểm tra merged revision. Chỉ chạy song song khi có instruction của coordinator/user và leases không chồng lấn. Provider là metadata; tài liệu không chứng minh đã kết nối Claude/Gemini/OpenAI.

## Spike A — viewport và hardware

**Câu hỏi:** C++20/Qt/Skia có present/input đúng và đạt budget 2D trên máy xưởng không? Backend/software nào dùng được thực tế?

Minimal window/surface, source coordinates double, relative render coordinates, pan/zoom, hit-test, clipping, text và instrumentation. Đo build release; không cần toàn bộ docking/editor. Pin Skia implementation/backend (Ganesh/Graphite) và dependency revisions.

Fixtures có checksum, số objects/control points/segments/glyphs, image megapixels/bytes, effects/overdraw và khổ/units:

- 100.000+ control points phân bố trên nhiều paths/objects.
- Một/vài paths chứa tổng 100.000+ segments, để tránh culling che bottleneck.
- Zoom-to-fit toàn scene, zoom cận, pan lộ tile mới, node drag gây invalidation.
- Chữ tiếng Việt, gradient, ảnh và transparency/clip/group/filter theo feature set được công bố.
- Font/profile/DPI change và cold/warm cache; output/reference checks cho tile boundaries.

Máy: Intel onboard 8 GB, AMD onboard 8 GB và RTX có thông số/driver/power mode rõ. Nếu chưa có một máy thì ghi `not_measured`, không gộp hay thay bằng máy RTX. Forced software trên máy thật, mode không tạo GPU context, VM không acceleration bổ sung correctness/present; VM không thay baseline laptop.

Quy trình đề nghị: fixed interaction trace, ít nhất ba lần chạy mỗi scenario, ghi warmup riêng và ít nhất 60 giây samples/run. Đo CPU/GPU frame phases và **present intervals** để tránh FPS submission che dropped frames; ghi input event → visible response cùng phương pháp đo. Báo từng run/scenario, không chỉ trung bình.

| Gate khảo sát | Điều kiện |
| --- | --- |
| GPU minimum 1080p/60 Hz | Frame p95 ≤ 16,7 ms; p99 ≤ 33,3 ms; input-to-visible p95 ≤ 50 ms cho từng corpus scenario đã duyệt |
| Software interaction | Frame p95 ≤ 33,3 ms; input-to-visible p95 ≤ 100 ms với preview/cache policy rõ |
| Correctness | Units/source không đổi do LOD; culling/cache/clip/compositing qua fixture oracle; present hoạt động khi GPU init thất bại |
| Recovery | Inject recoverable device-loss, rebuild software từ nguồn; công bố giới hạn driver native crash/hang |
| Memory | Báo peak private/working set, history/assets/caches/worker và GPU shared/dedicated; budget duyệt từ baseline |

Full-quality CPU 60 FPS không phải gate đã chấp nhận; nếu trở thành yêu cầu cứng phải đổi corpus/budget qua ADR và chứng minh đúng. Startup < 300 ms/idle < 100 MB là stretch targets; đo cold/warm, first window/editable document và empty/loaded, không ghi pass từ một empty window.

Fail: phải sửa cache/chunking/tessellation/present policy hoặc đổi candidate; không bỏ scene khó để giữ số đẹp. GPU process isolation là quyết định spike riêng nếu cần bảo đảm qua driver fatal crash.

## Spike B — geometry, document và concurrency

**Câu hỏi:** model double, snapshot COW và single writer có giữ precision/topology/undo nhất quán khi worker/save chồng lên edits không?

Harness nhỏ có IDs/revisions/epochs, commands/transactions, snapshots/index cùng revision, undo/redo, một Boolean/offset, save/checkpoint tối thiểu. Clipper2 polygon pipeline là ứng viên, ghi rõ curve flattening/error budget; chưa full CAD kernel/storage engine.

Fixtures/oracles:

- Biển 20 m với chi tiết nhỏ, dữ liệu xa gốc và transforms/reflection/nonuniform scale.
- Rectangle/circle/arc có đáp án analytic hoặc oracle độc lập; tangent/near-contact, holes/slivers, self-intersection/degenerate inputs.
- Units conversion và parser precision boundary; error budget approximation/quantization/algorithm/export theo mm.
- Ít nhất 10.000 commands với seed/trace cố định; undo/redo đủ để so semantic state và references.
- Worker trả muộn sau edit/delete/undo/redo/cancel/replacement/close-reopen; strict base revision policy.
- Save r chồng edit r+1; snapshot/index không trộn revisions; nguồn/tài nguyên tồn tại trong job.
- Kill/fault lúc serialize/write/flush/replace/checkpoint; quyền ghi lỗi và output dở dang.

Gate: stale proposal từ chối 100% các schedules fixture; cancel/error không half-commit; undo/redo khôi phục geometry/style/hierarchy/source metadata theo oracle. Dung sai khảo sát ≤ 0,01 mm trên corpus production đã duyệt, topology đúng hoặc lỗi có diagnostic. Unknown/unrepresentable inputs phải từ chối có kiểm soát.

Đo prepare/validation/commit/invalidation riêng, retention qua snapshot/undo/jobs và cleanup latency. Tổng UI work cho command cùng render phải giữ budget A; vượt thì chuyển prepare/index work hoặc sửa data layout. Không coi atomic pointer swap là pass nếu UI đã copy toàn graph.

Recovery nhận bản cũ hoặc mới hợp lệ theo durability policy, ghi rõ cửa sổ mất edits; save r không làm sạch r+1. Publish lifetime/synchronization được kiểm tra bằng stress schedules và sanitizer khả dụng. Thiếu oracle hoặc fault point là thiếu evidence, không là pass.

## Spike C — interop và containment

**Câu hỏi:** readers giữ được những semantics nào, mất gì và worker có bị giới hạn quyền/tài nguyên thực tế không?

Khoảng 20–30 fixtures nhỏ, có nguồn/quyền, version/feature/units/font/profile và expected result. Corpus gồm PDF, AI bật/tắt PDF compatibility, các phiên bản CDR được chọn và DXF với entity đã chọn. Reader candidates: libcdr, PDFium/Poppler, dxflib; lựa chọn/pin/license ghi riêng. Không tự cài tất cả hoặc coi parser là authoring engine.

Chuỗi nghiệm thu: **nhập → sửa một object đại diện → lưu native → mở lại → xuất một format đích được chọn**. Tách visual report, semantic/geometry/color/text report, editability và export/reopen report. Chỉ reference render không đủ. Với không-supported AI/CDR/effects, báo từ chối hoặc substitution rõ và giữ provenance; không promise native round-trip.

Host/worker minimal: version negotiation, handles cấp đúng nguồn, budgets count/bytes/depth/time/memory, cancellation, atomic application và validation trước commit. Inject malformed/nonfinite/oversized/deep payload, missing font/ICC/linked asset, timeout, crash và response sau cancel.

Containment Windows khảo sát AppContainer cho permissions và Job Objects cho quotas/lifecycle. Test worker cố đọc/ghi canary ngoài allowlist, mở mạng chưa cấp, spawn child trái policy và dùng handle không được cấp. Chỉ process riêng/timeout không chứng minh sandbox. Nếu chưa có containment, chỉ trusted-only và ghi gate chưa đạt.

Gate: mỗi fixture có classification/report, không mất dữ liệu âm thầm; output geometry trong tolerance corpus đã duyệt; worker crash không commit source hoặc làm chết host; payload lỗi bị chặn; quyền bị từ chối đúng OS policy. License/dependencies bắc cầu/font/fixture rights có inventory trước lựa chọn phân phối.

## Lịch và kết quả quyết định

| Ngày làm việc | Đầu ra |
| --- | --- |
| 1–2 | Machines/corpus/tools đủ điều kiện; contracts/semantic vectors và ownership được review |
| 3–7 | Ba prototype bounded chạy được; measurements/oracles/fault cases |
| 8–9 | Negative tests, consumer tests, correctness/performance review và rerun sau fixes |
| 10 | Integration checks và ADR implementation: accept/adjust/reject candidate, limits và next action |

Mỗi artifact ghi base/head/integrated SHA, exact run commands, compiler/dependency versions, machine/driver, fixture checksum/rights, raw data và report. Kết quả là `pass`, `fail`, `partial` hoặc `not_measured` theo từng gate; không lấy harness tests làm evidence app. Chưa chốt memory/regression thresholds phải ghi open decision.

Sau spike, ưu tiên vector/native recovery/output đã kiểm chứng. CAD constraints, CAM/G-code, physical lighting, native CDR export/AI round-trip và rộng hóa plugins là milestones riêng. Không triển khai diện rộng trước khi những giả thuyết quyết định của A/B/C có kết luận review được.

## Bàn giao sang implementation

[Kế hoạch triển khai sau spike](post-spike-implementation.md) và [catalog 26 task](post-spike-tasks.json)
quy định công việc tiếp theo, owned paths, dependencies, reviewer và acceptance.
Coordinator bắt đầu `ps00-accept-spikes`: đối chiếu evidence, chốt baseline/stack/coverage và
kiểm kê code có thể tái sử dụng. Gate bắt buộc còn fail/partial/not_measured thì tạo task sửa spike;
không đánh dấu ps00 done hoặc mở implementation phụ thuộc.

Sau nghiệm thu: `ps01-contract-pack` → `ps02-build-gates` → geometry/color/native preview song song,
rồi document/render/tools/storage/interop để đóng lát cắt editor đầu tiên. Catalog chưa là
scheduler; harness/task board và protocol vẫn quyết định assignment/lease thực tế.
