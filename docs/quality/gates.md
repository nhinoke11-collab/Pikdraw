# Quality gates của PikDraw

Trạng thái: **mục tiêu và quy trình đề xuất; chưa có baseline đo từ ứng dụng**. Scaffold/harness checks không chứng minh vector engine, tương thích file hay khả năng chạy mượt. Gate chỉ được ghi “pass” khi có artifact và dữ liệu đo tương ứng.

## Mức kiểm tra

| Gate | Bằng chứng cần có |
| --- | --- |
| G0 — thay đổi review được | Task scope, owner, diff, dependency/contract impact, tài liệu mới đúng trạng thái |
| G1 — nền tảng build | Build trên cấu hình hỗ trợ, static checks, dependency graph không vòng, smoke test ứng dụng |
| G2 — đúng chức năng | Test có oracle độc lập/fixture; command undo/redo; document invariants; migration nếu đổi schema |
| G3 — tương thích và độ chính xác | Semantic report, ảnh so sánh, font/color/units checks và phạm vi format/version công bố |
| G4 — ổn định/hiệu năng | Benchmark fixture và máy tham chiếu cố định; cancellation/crash/recovery evidence |
| G5 — phát hành | Review độc lập, license inventory, bản build tái tạo, release notes/known limits, bộ đơn xưởng đã kiểm tra |

G0 có thể thực thi ở scaffold. G1–G5 đang chờ engine, fixture và môi trường chạy. Không tạo test chỉ lặp lại implementation để tăng số lượng. Chọn kiểm tra theo rủi ro thay đổi; broaden khi có thay đổi/failure hoặc vấn đề chưa giải quyết.

Harness hiện kiểm tra registry khai báo, lease và committed diff ở handoff; chưa scan dependency source/link thực tế, thực thi semantic contracts hoặc OS write isolation. Các gate tự động dưới đây là yêu cầu implementation tiếp theo, không được ghi là đã enforced.

## Gate chống semantic drift và tích hợp

| Gate cần triển khai | Điều kiện chặn |
| --- | --- |
| Actual ownership | Diff ngoài owned paths, kể cả rename/config; kiểm tra symlink/submodule/path escape và base/head đúng |
| Actual dependencies | Include/import/link/re-export vượt registry hoặc vòng thực tế; không chỉ scan registry JSON |
| Source organization | File mới/sửa vượt cap trong code-organization.md mà thiếu exception được review; minify/artificial parts/generated marker giả hoặc feature diff sửa size policy/oracle |
| Contract semantics | Producer/consumer không đạt cùng vectors units/axes/transform/fill/winding/tolerance/revision/cancel/error |
| Document invariants | IDs/references/index revisions sai, half-commit, stale response được áp dụng, undo mất dữ liệu |
| Independent correctness | Geometry vượt tolerance/sai topology; interop mất metadata/text/color không report; thiếu oracle |
| Integrated revision | Build/consumer tests và benchmarks liên quan fail trên merged SHA hoặc artifact không thuộc revision đó |

Shared gates/oracles/fixtures chuẩn được bảo vệ ngoài quyền sửa tùy ý của task feature. Thay gate phải là scope riêng có review, không hạ threshold/đổi expected result để che regression. Geometry/interop/storage và breaking contracts có reviewer độc lập đủ chuyên môn.

Áp dụng [quy tắc tổ chức mã](../development/code-organization.md) khi review: file theo feature/trách nhiệm, public/private boundary nhỏ, file map và physical/nonblank-line report. ps02 phải triển khai file-size checker/policy/exception registry và negative fixtures; hiện đây là rule review và yêu cầu tooling, chưa là gate tự động của harness. Legacy unchanged không buộc refactor; code spike được tiếp nhận vào runtime phải được tách trong scope module. Cap LOC không thay oracle correctness hoặc performance.

Sanitizers khả dụng và stress schedules kiểm tra memory/race/lifetime; fuzz parser/IPC có size/time/resource limits. Các tools bổ sung không thay semantic oracle. Contract shape/schema pass không chứng minh units, topology hoặc cancellation đúng. Thử negative case mm/inch, reflection, near-tangent, undo rồi nhận job cũ và output sau cancel.

## Fidelity gate

Mỗi fixture có nguồn và quyền sử dụng, format/version, hệ đơn vị, profile màu, font cần thiết, nội dung kỳ vọng và ảnh chuẩn. Ma trận ghi riêng import, editability và export. Tài liệu có giới hạn được đánh dấu `partial`, `unsupported` hoặc `research`; không biến một file pass thành cam kết cho cả định dạng.

| Lớp kiểm tra | Oracle cần có |
| --- | --- |
| Hình học | Bounding box, vị trí, kích thước, winding, path closure, cung/spline và tolerance theo tác vụ |
| Đối tượng | Số lượng/loại node, hierarchy, layer, instance, transform, clips/masks |
| Chữ | Nội dung Unicode, shaping/line breaks, font mapping, trạng thái text sống/outline |
| Hình ảnh | Pixel size, orientation, crop/mask, alpha, liên kết/nhúng, profile |
| Màu/in | ICC pipeline, spot/CMYK theo phạm vi hỗ trợ, transparency và output trên RIP tham chiếu |
| Lưu/chỉnh sửa | Nhập → sửa đại diện → lưu native → mở lại; undo/redo và xuất đích |

Pixel diff dùng để tìm thay đổi, không là oracle duy nhất: antialias, font rasterizer và quản lý màu có thể làm ảnh khác dù hình học đúng. Tolerance khởi đầu cho đường sản xuất có thể khảo sát mức 0,01 mm; chỉ chốt sau khi đối chiếu đơn vị, khổ, máy và nhu cầu xưởng. Không dùng một tolerance cho cả CNC lẫn preview.

Error budget tách curve approximation, quantization, algorithm và export; integer grid 1 µm không chứng minh sai số cuối 1 µm. Kiểm tra precision cả parser boundary. Giữ topology/holes/closure và report degenerate inputs; không simplify source hoặc bỏ slivers âm thầm.

Interop phân loại PDF-compatible AI, non-PDF/legacy AI và CDR theo version/feature. Không suy ra native round-trip từ PDF render hoặc libcdr import. Chuỗi nhập → sửa → native save/reopen → xuất đích có editability report. PDF/X/separations cần scope và output/RIP oracle riêng; RGBA preview không đủ.

Lỗi chuyển dạng phải có report: nguồn, đối tượng, lý do, mức mất khả năng sửa và lựa chọn đã dùng. Trường hợp dữ liệu bị mất mà không có cảnh báo chặn release.

## Performance gate

Trước benchmark phải ghi OS/build, CPU, RAM, GPU/driver, màn hình/DPI, power mode, compiler flags và commit. Chọn một máy Windows tham chiếu cùng bộ fixture có checksum; sau đó mới chốt budget. Máy khác có report riêng, không gộp trung bình để che cấu hình chậm.

Các profile khởi đầu:

| Profile đề xuất | Fixture cố định cần xây |
| --- | --- |
| Small | Một trang, khoảng 1.000 đối tượng vector/chữ, ảnh nhỏ |
| Typical | Tám trang, khoảng 10.000 đối tượng, ảnh in lớn, clipping và gradient |
| Stress | Khoảng 100.000 đối tượng, nhiều tài nguyên/layer và tọa độ lớn |

Con số trên là gợi ý xây bộ đo, không phải khả năng sản phẩm đã đạt. Fixture phải ghi cả số segment, glyph, megapixel, asset bytes và loại hiệu ứng vì số đối tượng đơn thuần không phản ánh chi phí.

Ngưỡng spike theo [ADR 0002](../decisions/0002-runtime-and-hardware.md) và [kế hoạch foundation](../roadmap/foundation-spikes.md): GPU minimum 1080p/60 Hz frame p95 ≤ 16,7 ms, p99 ≤ 33,3 ms, input-to-visible p95 ≤ 50 ms trên từng scenario duyệt. Software tương tác khảo sát 30 FPS, frame p95 ≤ 33,3 ms/input p95 ≤ 100 ms với preview policy rõ. Stress ngoài corpus có budget/limits riêng. Đây chưa là performance CI đã thực thi; gate trở thành enforced sau baseline/tooling được review.

Đo present intervals, missed deadlines, raw timestamps và khoảng dừng lớn nhất; không lấy submission FPS hoặc gộp runs/máy che tail latency. Tách warm/cold cache, zoom-to-fit, pan lộ vùng mới, một path lớn, node drag/invalidation, effects/text/ảnh, software trên máy thật và GPU init failure. Phương pháp input-to-visible, warmup, run duration và số runs phải có trong artifact.

Report peak private/working set RAM, GPU dedicated/shared, assets/history/snapshots/caches/worker bytes và cách tránh đếm trùng. Memory/regression budgets chốt từ baseline; cache hết budget/cleanup/job backpressure có kiểm tra. Startup < 300 ms/idle < 100 MB là stretch targets với cold/warm/first window/editable document/empty-loaded definitions, không là pass từ empty app.

Đo cold/warm open, save/export, pan/zoom, node drag, text edit, Boolean/offset, import cancellation, peak RAM, GPU memory và thời gian phục hồi. Báo p50/p95/p99, peak và workload; không chỉ báo FPS trung bình. Memory budget và regression threshold được đặt sau benchmark đầu tiên, qua ADR hoặc baseline artifact có review.

## Stability và phục hồi

- Inject crash vào importer/exporter/plugin: UI sống, tài liệu chưa commit không thay đổi, lỗi có hướng xử lý, worker có thể khởi động lại.
- Hủy ở các bước parse/convert/commit/export: không có output được ghi như thành công khi chỉ có file dở dang.
- Fault injection lúc lưu và autosave: sau mở lại phải có bản trước hoặc bản mới hợp lệ; không nhận file thiếu cấu trúc là tài liệu tốt.
- Kill ứng dụng khi sửa: mở được checkpoint đã commit; công bố cửa sổ thay đổi có thể mất dựa trên policy tự lưu đo được.
- Font/profile/linked asset thiếu: cảnh báo đúng và có lựa chọn sửa; không crash hoặc âm thầm thay đổi nội dung.
- Document lớn và phiên 8 giờ với chuỗi thao tác xác định: theo dõi leak, cache, handle, responsiveness và recovery. Đây là bài nghiệm thu đề xuất, chưa chạy.
- Fixture lỗi: payload vượt giới hạn, số không hữu hạn, nesting sâu, schema lạ, timeout và plugin version mismatch được từ chối có kiểm soát.
- Worker stale/cancel/replacement, edit/delete/undo/close-reopen không commit; revision đơn điệu và epoch/generation được kiểm tra. Snapshot/index/assets cùng revision; không có race check-then-apply.
- Save r xong khi current r+1 không làm sạch edits mới. Inject serialize/write/flush/replace/checkpoint faults; durability policy công bố thay đổi có thể mất, không đồng nhất atomic rename với chống mất điện.
- GPU init failure/recoverable device-loss có software raster/present/rebuild tests. Native driver fatal crash/hang cùng process là giới hạn riêng; chưa kiểm chứng GPU-process recovery thì không hứa seamless fallback mọi lỗi.

Process isolation không đủ chứng minh sandbox an toàn. Plugin untrusted chỉ được bật khi containment và quyền file/mạng đã được kiểm tra; trước đó phải công bố chế độ trusted-only.

Containment tests dùng canary ngoài allowlist: đọc/ghi file, mở mạng chưa cấp, child process trái policy và handles không được cấp phải bị từ chối. OS permissions và quota/lifecycle kiểm tra riêng; Job Objects hoặc timeout không tự tạo filesystem/network sandbox. Importer/plugin resource/IPC validation vẫn bắt buộc sau khi đã containment.

## Review và quyết định phát hành

Agent triển khai không tự xác nhận mọi thay đổi của mình. Reviewer đọc diff, contract và bằng chứng đúng revision. Với geometry/format/storage, review cần người/agent có năng lực tương ứng và fixture lỗi; bài test xanh không thay thế đánh giá mất dữ liệu.

Phân loại chặn phát hành: mất dữ liệu, sai đơn vị/đường sản xuất không được báo, crash lặp lại trong luồng chính, migration hỏng hoặc quyền plugin vượt phạm vi. Giới hạn format đã công bố có thể phát hành như `partial` nếu đúng scope và có report; không đổi nhãn thành “full” để vượt gate.

Mỗi release lưu artifact: commit, toolchain/dependency versions, licenses, test/benchmark reports, ma trận format, known limits và recovery policy. Chỉ log metadata cần thiết; nội dung khách hàng, file gốc và đường dẫn nhạy cảm không đưa lên telemetry mặc định.
