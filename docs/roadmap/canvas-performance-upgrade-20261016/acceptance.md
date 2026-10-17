# Corpus, đo lường và nghiệm thu

Các ngưỡng dưới đây là proposed và phải được P03 ghi vào manifest versioned trước implementation. Thiếu máy/counter/tool/oracle ghi `not_measured`; không đánh dấu pass. Performance gate đo Release trên canonical executable. Debug/sanitizer dùng correctness và concurrency, không so tốc độ trực tiếp với Release.

## 1. Ngưỡng theo trải nghiệm

| Metric | GPU đã kiểm chứng, 1080p/60 Hz | Software trên máy 8 GB | Cách đọc |
|---|---:|---:|---|
| Displayed present interval trong continuous interaction | p95 ≤16,7 ms, p99 ≤33,3 ms | p95 ≤33,3 ms, p99 ≤66,7 ms | Frame thực hiển thị; báo repeated/dropped frames riêng |
| Input-to-first-correct-visible-response | p95 ≤50 ms, p99 ≤100 ms | p95 ≤100 ms, p99 ≤150 ms | Preview transform đúng hoặc action feedback; không tính busy icon thay kết quả drag |
| UI event handler | p95 ≤2 ms, p99 ≤8 ms | giống GPU | Tính cả synchronous prepare/validation, allocation/destructor |
| Hit-test/snap query trên corpus bình thường | p95 ≤2 ms, p99 ≤5 ms | p95 ≤4 ms, p99 ≤8 ms | Pathological case trả pending rõ, query exact nền; không trả miss giả |
| Atomic document publication | p95 ≤2 ms, p99 ≤8 ms | giống GPU | Root/index/history publication; prepare lớn đo riêng |
| Menu/tool switch/ack lệnh nặng | visible feedback p95 ≤50 ms | ≤100 ms | Không đồng nghĩa Boolean/filter đã tính xong |
| Hủy job | UI acknowledgement ≤50/100 ms tương ứng; worker observes cancel p95 ≤100 ms, p99 ≤250 ms | như GPU với cap worker | Codec không preempt được: process isolation/timeout có evidence |
| Visible full-quality settle sau gesture | p95 ≤250 ms với S1/S2; ≤1 s với S4/S5 | ≤500 ms S1/S2; ≤2 s S4/S5 | Đo quality/coverage, không báo settle khi chỉ có cache cũ |
| Tắc UI liên tục | không đoạn >100 ms do app trong traces S1–S6 | cùng ngưỡng | OS stall ghi riêng, không tùy tiện loại outlier |

S3/S7/S8 là stress: vẫn giữ phản hồi/hủy và giới hạn bộ nhớ; full-quality settle có thể lâu hơn với progress. Nếu stress trở thành workload sản phẩm bắt buộc, coordinator thêm ngưỡng cụ thể trước nghiệm thu. Không hạ chất lượng vô hạn: preview coverage S1/S2 ≥99%, S4/S5 ≥95% sau 100 ms trên GPU (200 ms software); source item đang thao tác luôn phải có preview chính xác. Báo độ phân giải effect/mip, chất lượng và tuổi cache trong từng trace.

Mục tiêu nội bộ một frame GPU: input/publication 2 ms, scene/index 2 ms, render CPU prepare 4 ms, GPU/compose 6 ms, margin 2,7 ms. Các phase có thể overlap; không cộng CPU+GPU durations để giả lập presented frame time. Nếu component đạt mà end-to-end fail thì task vẫn fail.

## 2. Corpus version 1

Generator deterministic seed `20261016`; ghi hash file và semantic manifest. File ảnh/font phải có quyền sử dụng hoặc synthetic. Không đưa file khách hàng vào repo. Có các biến thể sparse và all-overlap để tránh spatial index che chi phí worst case.

| ID | Tải | Các biến thể bắt buộc |
|---|---|---|
| S0 | Empty + 100 shapes | Khởi động cold/warm, DPI resize, tab switch |
| S1 | 10.000 object, tổng 100.000 path elements, trang 20×10 m | Zoom fit/cận, stroke miter/round, màu/alpha; 10% và 100% visible |
| S2 | 100.000 object, tổng 1.000.000 elements | Phân tán và overlap, 100 layers; group nesting 8; many tiny objects |
| S3 | 1 path 100.000 segments, thêm biến thể 1.000.000 segments | Holes/self-intersection, giant coordinates, reflection/nonuniform scale |
| S4 | 20 ảnh 6.000×4.000 và 2 ảnh 20.000×20.000, nén hỗn hợp | Source tổng 1,28 Gpixel ≈4,77 GiB RGBA8 nếu decode tất cả; visible ít/nhiều; reuse cùng asset 100 lần; crop/ICC/orientation |
| S5 | S1 + 100 nodes effect (50 blur + 50 shadow), stack sâu 4 | Sigma tương đương 2/16/64/256 px tại reference zoom; boundary crossing, alpha, group clip, transparent overlap |
| S6 | 10.000 objects + 100.000 glyphs Unicode Việt, fonts có provenance | Ligature/combining marks, glyph atlas eviction, font substitution, ICC switch |
| S7 | Mixed 100.000 objects + S4 + S5 | Stress vượt cache, cold tiles, tab switch, memory pressure và software forced |
| S8 | Native file 256 MiB, 1 GiB, 4 GiB source trên disk | v4 candidate; file thực có random/noise asset chunks, không dùng sparse zero file thay decode workload; early open/save/cancel/recovery |

S8 chỉ dùng khi v4 reader có limits/fixtures. Legacy v3 vẫn từ chối file quá 256 MiB một cách có kiểm soát. Không sửa fixture count xuống để pass. Node/segment/object/glyph/megapixel là các đơn vị độc lập, luôn ghi đủ trong manifest.

## 3. Trace thực tế

Mỗi scenario ít nhất 3 run ×60 giây sau warmup 10 giây; warmup/cold riêng. Thứ tự A/B đảo qua các lượt khi so sánh để giảm ảnh hưởng thermal/cache. Không gọi là cold nếu chưa kiểm cache state; ghi OS-cache-unknown nếu cần. 4K là profile riêng, không suy từ 1080p.

1. Pan 10 giây qua vùng có dữ liệu mới; zoom vòng fit→800%→fit 10 giây.
2. Hover/hit-test/snap 10 giây trên vùng dense; window/crossing selection 10 giây.
3. Drag/resize/rotate 10 giây một object rồi multi-selection; blur slider 10 giây khi supported.
4. Trace command riêng: Boolean/offset/dedup 100/1.000/10.000 paths; gọi, hủy, undo/redo; sửa source khi worker đang chạy; save khi edit tiếp.
5. 30 phút interactive soak tối thiểu; 2 giờ workload luân phiên mở/đóng/tabs/undo/save/effects cho release candidate. Không thay bằng idle sleep.

Tách traces thay vì gộp tất cả percentile. Chạy cảnh unsupported chỉ để kiểm diagnostic, không tính nó là pass feature hoặc performance. Handlers chưa có capability phải được bổ sung correctness trước.

## 4. Đo cái gì và bằng công cụ nào

- Clock monotonic/QPC cho timestamps; record input sequence và frame IDs để join. Capture `OS input arrival`, app dequeue, command begin/ack/commit, render queue/start/end, GPU submit/complete nếu có timestamp query, present, displayed event. Không gọi `paint()` duration là FPS.
- [PresentMon](https://github.com/GameTechDev/PresentMon) là candidate thu graphics presentation trên Windows; P01 phải chứng minh PID/surface/backend support và frame matching. GDI/old driver hoặc API không có trace hiển thị thì để trống, dùng ETW/OS tương ứng hoặc high-speed camera external. Không giả WM_PAINT/BitBlt return là pixel đã hiển thị.
- Input-to-photon thực có thể cần high-speed camera/photodiode và đồng bộ event; phần mềm chỉ chứng minh event-to-displayed-frame estimate khi correlation hợp lệ. Ghi phương pháp chính xác. Không có native input automation thì chạy trace người dùng theo script và ghi input, hoặc bổ sung test driver có scope; task không được đóng performance gate bằng screenshot mode.
- CPU: per-process thread CPU time, utilization một core và toàn máy, sample profiler stacks, context switches; phase/alloc counters. GPU: engine utilization theo PID, frame GPU timers asynchronous, local/nonlocal residency, uploads/readbacks. Không có instance không đồng nghĩa 0%.
- RAM: private commit, working set, peak, system available/commit, job/undo/cache/source/pinned bytes, page faults/hard faults riêng. Worker process tính trong tổng. CPU cache misses/bandwidth chỉ báo khi PMU/tool hỗ trợ; không gọi file I/O bytes là memory bandwidth.
- I/O: bytes read/written, requests, parse/decode/serialize/flush/replace duration; first-window, first-visible-preview, first-editable-data, full-source-ready là bốn metric riêng.
- Đo overhead instrumentation bằng cùng binary feature flag on/off 3 lượt; target overhead <3%. Nếu vượt, giảm sampling/log hoặc báo độ lệch. Tránh synchronous file log trên frame thread.

Raw CSV/JSON tối thiểu: run/scenario/input/frame IDs, timestamps, revision/generation, visible object count/elements/pixels, dirty area, cache hit/miss/eviction, preview scale/coverage/age, CPU/GPU/memory/I/O và dropped reason. File metadata chứa executable hash, source snapshot hash, build flags, dependency pins, OS/driver, màn hình/DPI/Hz, power/thermal state và background load. Dữ liệu log không chứa text/path/customer image payload.

P50/p95/p99/max dùng sample timestamps thực, nearest-rank khai báo; báo từng run và số samples. Outlier có raw event và phân loại, không âm thầm bỏ. 5 screenshot trials không đủ để nghiệm thu p99. Regression gate mỗi run: không tăng p95/p99 >10% so baseline cùng scenario/hardware nếu chưa có giải trình và quyết định coordinator; absolute gates vẫn phải đạt.

## 5. Memory, file và load/save gate

8 GB profile: soft 2 GiB tổng allocations quản lý, pressure injection 2,5 GiB theo architecture; đo process+workers+shared GPU không cộng trùng. Cùng workload giữ asset pins cố định, sau 100 vòng edit/undo/cache eviction không tăng live managed bytes quá max(16 MiB, 5% điểm ổn định); OS working set có thể giữ allocator cache, phải báo riêng. Đây là leak gate workload-specific, không chứng minh mọi leak vắng mặt.

Cache tests inject cap 64/128/256 MiB; bounded queues, không OOM hoặc evict pinned data. Source/document vượt envelope phải streaming hoặc diagnostic; CPU/raster jobs không chạy vô hạn khi pressure.

V4 S8 trên SSD có throughput ghi lại: mục tiêu first preview ≤2 s, first editable visible page ≤3 s trên profile GPU; software ≤4/5 s. Có progress trong ≤100 ms; vẫn pan/zoom/cancel trong lúc còn hydrate. Full-ready và full-save thời gian đo theo dung lượng/codec thực, không hứa 4 GiB ghi xong dưới 1 giây. Streaming encoder scratch mục tiêu ≤64 MiB ngoài resident source/caches; thử tạo file >4 GiB + offsets crossing 32-bit trên test machine phù hợp. Save snapshot không trộn revisions, không decode/reencode unchanged original assets.

## 6. Correctness và faults không được đánh đổi

| Nhóm | Oracle / lỗi phải bắt |
|---|---|
| Spatial | Indexed culling/hit results so linear reference; delete/move/undo stale; bounds stroke/effect; group order/clip |
| Raster | Golden/reference full frame so tiled; seam diff ở halo/crop/zoom; premultiplied alpha, transparent RGB, nonuniform transform |
| Effects | CPU reference kernel với declared color/alpha/edge policy; sigma=0, large sigma, stack order, shadow offset, nested group |
| Vector/text | Stable node IDs, source semantic hash không đổi khi LOD; winding/topology; Unicode/font/glyph layout; không thay source bằng outline tự động |
| Async | Late result sau edit/delete/undo/redo/cancel/close/reopen/device reset; strict base rejection; một commit/một undo, no half-apply |
| File | V2/v3→v4 reopen semantic equality; legacy unsupported reports; missing chunk/asset, CRC/hash mismatch, overlap offsets, integer overflow, unknown required feature, decompression bomb |
| Durability | Fault/kill lúc serialize/write/flush/replace/checkpoint/journal tail/compaction; reopen bản cũ hoặc mới hợp lệ, không báo success file dở; save r không clear dirty r+1 |
| Hardware | Init fail, recoverable loss, allocation failure, minimization/restore, resize/DPI/monitor, sleep/wake; native fatal crash giới hạn công bố |

Pixel thresholds khóa trước code: baseline exact deterministic CPU tests nên match exact hoặc tolerance giải thích rounding; candidate GPU settled default target max channel delta ≤2/255 trên vùng interior, edge mask riêng với geometric edge distance ≤0,5 px. Không dùng tolerance này áp mù cho blur/ICC; effect/ICC vectors có reference high precision và tolerance theo thuật toán được P03/P22 khóa. Không tăng tolerance sau thấy fail nếu chưa review semantics. Golden từ implementation đang kiểm không phải oracle độc lập.

## 7. Các gate tích hợp

- G0: source/test inventory, dependencies/licenses, contracts/semantic vectors khóa, current Release app baseline có evidence hoặc missing flag rõ.
- G1: index và interaction pipeline correctness + input responsiveness S1/S2, stale schedules; software path luôn dùng được.
- G2: bitmap/group/text, tiles, GPU/software output correctness + hardware capability matrix; dữ liệu không mất.
- G3: blur/shadow/effect UI + cancel/undo/round-trip trong scope format hỗ trợ + visual oracle và S5.
- G4: native v4 migration/load/save/recovery, limits/faults, S8; mới cho writer v4 thành default.
- G5: S0–S6 theo feature envelope trên máy Intel iGPU 8 GB, AMD iGPU 8 GB và dGPU; stress S7/S8 giới hạn công bố; forced software, soak/recovery, raw evidence trên binary tích hợp cuối.

Nếu chưa tiếp cận một máy: ghi `not_measured`, chỉ phát hành thử cho cấu hình đã kiểm. Không phát hành tuyên bố chung “mọi máy/file đều không lag”.
