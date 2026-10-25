# Kế hoạch hiệu năng, file .pr, MCP và nền tảng thương mại

Ngày: 24/10/2026. Đây là kế hoạch triển khai dựa trên source, không phải báo cáo benchmark đã đạt.
Đối chiếu bổ sung C1–C9 ngày 24/10/2026: xem [chỉ mục contract hiện hành](../../contracts/README.md).
Workflow theo [AGENTS](../../AGENTS.md). Đọc [architecture](../architecture/architecture.md),
[PR format](../architecture/native-pr-format.md), C2/C3/C5/C6/C8/C9 đúng phạm vi trước sửa.
Mỗi gói bên dưới được triển khai trực tiếp, báo diff/lệnh/kết quả/giới hạn; không tạo task/lease/handoff.

## 1. Đánh giá hiện tại và hướng sản phẩm

PikDraw đã có ranh giới core/document/rendering, command/Undo, workers và automation typed.
Đây là nền có thể phát triển sản phẩm thương mại. Các gate performance, portable asset, durability,
package/update và entitlement vẫn cần hoàn thành trước khi gọi là sẵn sàng bán rộng rãi.

EXE Debug lớn có thể do debug symbols và static linking; không dùng số MB EXE để suy ra thời gian
khởi động hoặc FPS. Build Release, symbols riêng và package dependency đúng mới là số cần đánh giá.
Một EXE cũng không có nghĩa mọi module phải khởi tạo lúc mở app. Chỉ tách DLL/process khi có ranh giới
ABI, lifecycle hoặc crash isolation rõ; không tách cơ học để làm số MB của EXE nhỏ đi.
Release canonical `pikdraw.exe` sau build ngày 24/10/2026 có 9,996,800 bytes (9.53 MiB).
Đây chỉ là executable, chưa gồm installer/runtime/package; không phải số startup và không là so sánh
nội bộ của Corel/Adobe.

| Tiêu chí sản phẩm thiết kế chuyên nghiệp | PikDraw hiện tại | Việc cần làm |
| --- | --- | --- |
| Model chỉnh sửa, Undo, IDs và đơn vị thật | Đã có nền | Giữ semantics khi tối ưu copy/index/history |
| Viewport responsive, tài nguyên có budget | D2D hardware/WARP + GDI; một số cache/BVH | Direct present, invalidation theo vùng, raster tiles/LOD và đo frame thật |
| File portable, tài nguyên dùng lại, save bền vững | Schema v23/v24; atomic save ghi chunk trực tiếp, load còn full-buffer | Lazy/index/asset loading, dedup mở rộng và peak RAM gate |
| Module mở theo nhu cầu | Có dependency registry; nhiều chức năng tương lai planned | Lazy initialization, ABI/versioned workers, không block UI |
| Automation có transaction và quyền rõ | MCP/COM cùng service; có epoch/revision/batch | Indexed queries, concise result, cancellation và client compatibility |
| Phân phối và license | ZIP package; chưa có activation/entitlements | Release symbols/signing/update/recovery/key service có kiểm chứng |

Bảng là tiêu chí kỹ thuật để đánh giá PikDraw, không khẳng định kiến trúc nội bộ độc quyền của
Corel/Adobe. Qt/Skia là lựa chọn tương lai khi toolchain ABI thống nhất, không là điều kiện để
tối ưu pipeline hiện tại hoặc thêm nesting/CNC/3D.

## 2. Các điểm nghẽn thấy trong source

| Điểm cần đo/sửa | Source sở hữu | Nguy cơ |
| --- | --- | --- |
| Canvas render_frame rồi full-frame staging/readback về BGRA | apps/desktop/src/shell/native_window_canvas.cpp; core/rendering/src | Chờ GPU/CPU trên UI, băng thông theo viewport; GDI dirty copy không là GPU partial render |
| autosave_documents chạy sync từ UI writer | apps/desktop/src/shell/native_window_files.cpp | Một save lớn chặn input; nhiều tab dồn công việc |
| Atomic save đã stream payload; load đọc full file và giải nén toàn payload | core/document/src/storage | Peak RAM và latency khi open vẫn theo toàn document; quota đọc theo headroom, chưa có streaming load |
| RGBA pixels cùng encoded nguồn, có descriptor/tile foundation nhưng chưa nối đầy đủ | core/document/src/storage/object_codec.cpp; modules/raster | Ảnh nguồn lớn/duplicate/history giữ nhiều buffers |
| query_objects tạo toàn summaries rồi mới slice | modules/automation/src/automation_rpc.cpp, automation_service.cpp | Mỗi trang 100 vẫn tốn CPU/RAM theo toàn document |
| Khởi tạo document/render device và scan recovery khi startup | apps/desktop/src/shell/native_window.cpp, native_window_recovery.cpp | Cold start phụ thuộc driver/disk/recovery; cần timeline thực |

Đây là rủi ro và hướng đo từ source, chưa là tỷ lệ thời gian đo được. BVH, cache geometry,
image upload LRU và dirty GDI copy đã tồn tại; đo invalidation/coverage trước khi viết lại.

## 3. Baseline và ngân sách cần khóa trước tối ưu

Máy tham chiếu: Windows x64, 16 GiB RAM, NVMe, ghi CPU/GPU/driver/resolution/DPI thực tế.
Đo hardware và WARP riêng; Debug phục vụ correctness, Release phục vụ số hiệu năng.
Ghi source snapshot và build configuration trong kết quả ngắn, không lưu raw dumps vào docs.

| Workload | Cách dựng | Thuộc tính cần đo |
| --- | --- | --- |
| W0 | Empty app, 20 cold và 20 warm launches; recovery rỗng và nhiều checkpoint | Process→first frame→input ready, working set, device initialization |
| W1 | 10.000 object, 90% ngoài viewport; pan/zoom/drag 30 s | Culling, frame p50/p95/p99, input-to-present, cache hits |
| W2 | Tới 100.000 object (benchmark workload); path có nhiều phần tử | Pick/lasso, commit/Undo, index rebuild/refit, save/load và peak RAM |
| W3 | Một path rất nhiều segments + blur/clip/transparency | Tessellation/effect scratch; culling object không giải quyết được ca này |
| W4 | Raster 10.000² pixels, 100 instances cùng nguồn | Encoded/decoded/cache/history/IPC bytes, dedup, tile residency; v24 bỏ trần 256 MiB nhưng full-buffer vẫn tốn RAM |
| W5 | Trang vector 5 m × 2 m, 300 DPI output; ảnh + text Việt + ICC | Không cấp phát whole-page bitmap khi mở; export stripe/tile có oracle |
| W6 | MCP 1.000 và 100.000 objects, batch 1/64/256; UI đang kéo/nhập chữ | Query CPU/RSS/bytes/token, write atomicity, busy/stale/cancel |

Target đề xuất cho W0/W1 trên máy tham chiếu: cold startup p95 ≤3 s, warm ≤1,5 s;
frame p95 ≤16,7 ms và input-to-present p95 ≤50 ms; UI work slice p95 ≤8 ms.
Đây là ngân sách cần xác nhận sau baseline, không cam kết 60 FPS mọi scene hoặc WARP.
W2–W5 đặt budget cụ thể theo baseline/độ phức tạp; không giấu rejected/failed samples khỏi percentile.
Idle không redraw vô ích; cancel có phản hồi UI ≤100 ms, job dừng tại checkpoint có giới hạn rõ.

Đo riêng prepare/validation/commit/index/render/GPU readback/present/disk flush. Timer CPU
render hoặc offscreen image không chứng minh visible FPS. Ghi peak/process RSS, private bytes,
VRAM, encoded assets, decoded tiles, history retention, worker IPC và scratch đồng thời.
Admission theo job + process budget và headroom; chờ tới 95% tổng RAM không phải memory strategy.

## 4. Các gói triển khai theo thứ tự

### P0 — Instrumentation và workload tái lập

- Owned paths: core/rendering timing hooks; apps/desktop canvas/startup diagnostics;
  mở rộng tests/performance hiện có nếu phù hợp, tooling/native; không bật log mỗi mouse move mặc định.
- Thêm counters/ring buffer bounded, phase timeline và source/config/hardware metadata.
  Counters có thể tắt, không log artwork/text/private path. Đo overhead bật/tắt.
- Gate: thu W0–W6 trước thay thuật toán; xuất summary ngắn, dọn mọi output sau khi đọc.
  Không đặt baseline từ executable cũ hoặc lấy CPU-only audit cũ làm FPS hiện tại.
- Trạng thái 24/10/2026: đã có benchmark executable với workload `baseline`, W1, W2, W3;
  ghi Release/Debug, số luồng CPU, backend/GPU flag, p50/p95/max và thời gian query MCP.
  Đây là benchmark tái lập hữu ích nhưng chưa phải bộ instrumentation P0 hoàn chỉnh.
- Trace tuỳ chọn đã nối vào Win32 input, paint/render, first frame và recovery scan qua ring QPC
  bounded; native save/open bổ sung event begin/end gắn correlation ID. Chỉ bật khi đặt
  `PIKDRAW_PERF_TRACE_PATH`; worker nền ghi CSV versioned, không ghi
  tọa độ hoặc nội dung artwork. `test_pikdraw_canvas_trace` kiểm ring MPSC và trace trên cửa sổ
  ẩn; không đo OS present/input-to-display. Trace CSV ghi build, CPU, RAM,
  display/DPI và trạng thái tăng tốc; `test_pikdraw_canvas_trace` xác nhận.
  Visible W1/W2 chưa qua gate.
- W0 preliminary: Release `--headless`, 20 process launches trên Windows 10 Pro, Intel i7-3537U,
  4 logical processors, 7.9 GiB RAM, Intel HD 4000 + AMD HD 8500M/8700M, màn hình 1366×768 DPI 96.
  Lần đầu trong batch launcher-call→FirstFrame 286.91 ms; warm 19 p50/p95 269.89/440.97 ms.
  QPC `StartupBegin`→FirstFrame warm p50/p95 150.96/183.19 ms; `StartupReady`
  151.08/183.31 ms. Trace-on A/B cohort StartupBegin→FirstFrame p50/p95
  112.51/138.28 ms. Sampled working set warm p50/p95 28.8/29.1 MiB (batch trước);
  phép đo headless này chưa là cold OS-cache hoặc visible-present acceptance.
- Trace A/B mới: 20 process Release `--headless` mỗi nhóm, xen kẽ trace bật/tắt; metadata ghi CSV.
  Process lifetime p50 on/off 284.56/285.19 ms; p95 390.61/349.38 ms; paired delta
  p50 +4.28 ms, p95 +158.95 ms. Tail nhiễu, chưa cô lập overhead nhân quả. Trace chứa
  build=release, 4 CPU logic, RAM 8,452,857,856 B, display 1366x768/DPI 96 và
  renderer=direct2d_hardware. Không suy kết quả này thành visible-present evidence.
- Số đo Release hiện có trên máy 4 luồng CPU, D2D hardware/GPU, viewport 800×600:
  W1 (10k objects, xấp xỉ 1M path elements) `render_frame` p95 15.69 ms, direct pick
  2.40 ms, warm indexed pick 0.027 ms, indexed window/crossing 0.494/0.383 ms;
  SoftwareRasterizer warm render p95 19.03 ms. W2 (100k objects, xấp xỉ 1M path
  elements) latest run `render_frame` p95 62.38 ms, warm indexed pick 0.015 ms, indexed
  window/crossing 5.99/5.24 ms; SoftwareRasterizer warm render p95 71.43 ms.
  W3 là một path 100k nodes; phép đo cũ `render_frame` p95 5.998 ms chưa bao gồm
  blur/clip/transparency nặng. Giá trị W1/W2 thay đổi theo lần chạy.
- Giới hạn: số trên đo renderer/offscreen buffer và CPU fallback, không đo visible present,
  input-to-present, cold OS startup, recovery rỗng so với nhiều checkpoint, RAM/VRAM, effect scratch
  hoặc W4/W5. W2 vẫn vượt ngân sách 16.7 ms; chưa có kết luận 60 FPS. Chưa thu ETW.

### P1 — UI scheduling, cancellation và startup

- Depends: P0. Owned: apps/desktop/src/shell, platform/workers lifecycle.
- Coalesce pointer updates theo frame; có đúng một frame pending, giữ input cuối và final commit.
  Tách interaction overlay khỏi snapshot render; không làm queue tích lũy render theo từng mouse move.
- I/O, recovery scan và autosave chuẩn bị qua jobs; writer chỉ snapshot/validate/publish.
  Completion kiểm document/epoch/revision/generation; close/cancel nhả handle/cache đúng vòng đời.
- `AsyncProposalMetadata` giữ page ID và `page_navigation_generation`. Core validator nay từ chối
  object-scoped proposal với `StalePageContext` nếu active page hoặc generation đổi, kể cả revision
  không đổi; target IDs được kiểm trong object map của page đã chụp. Import có target page tường minh
  vẫn commit vào page ID đó sau khi người dùng chuyển trang, như regression `test_pikdraw_desktop_import`
  quy định. Công việc async mới phải khai báo semantics target page/object trước khi được publish.
- Startup dựng shell/first frame trước tác vụ tùy chọn; không tải AI model, plugin catalog,
  font toàn bộ, nesting/CNC/3D hoặc gọi server key đồng bộ. Device initialization giữ thread affinity;
  không chỉ chuyển COM/D2D object sang thread bất kỳ.
- Gate: W0 timeline, W1 input latency; kéo/nhập chữ trong import/save; save R/edit R+1;
  đóng/reopen tab khi job chạy; một Undo/gesture, stale jobs không publish.
- Trạng thái 24/10/2026: auto-recovery checkpoint được đưa sang worker nền từ immutable snapshot;
  tối đa 8 job đồng thời, kết quả chỉ cập nhật cờ recovery khi tab/document/epoch/revision còn khớp.
  Lưu file thành công trong lúc job chạy sẽ dọn checkpoint cũ khi worker hoàn tất. Test recovery mục tiêu pass.
- Startup recovery scan đã chuyển khỏi UI thread: một worker ưu tiên thấp bắt đầu sau frame đầu,
  trả metadata qua window message và truyền cancellation vào lúc decode checkpoint khi đóng cửa sổ.
  Prompt startup chỉ hiện nếu người dùng chưa thao tác/chuyển tài liệu. Nạp checkpoint được chọn
  cũng chạy trên worker; UI xác thực lại document ID/epoch/revision trước khi tạo tab. Cả scan lẫn
  phục hồi vẫn full-buffer; DEFLATE/CRC nay poll mỗi cửa sổ tối đa 64 KiB, còn snapshot validation
  và thời gian dừng thực tế chưa được đo.
  `test_pikdraw_save_recovery` kiểm tra scan async/cancel và phục hồi được chọn bất đồng bộ.
- UI `.pr` save/open chuyển sang một worker ưu tiên thấp. Save chỉ serialize/ghi immutable snapshot;
  completion đánh dấu đúng saved revision nên edit mới hơn vẫn dirty. Open parse snapshot nền rồi UI
  chỉ commit khi tab, epoch, revision, view và input context còn khớp. `test_pikdraw_save_recovery`
  kiểm stale-open rejection, save R trong lúc edit R+1, chuyển sang tab khác và đóng tab đích trong lúc
  open đang chạy. Các completion cho tab không còn active hoặc đã đóng đều bị bỏ. Save giờ
  stream payload bằng buffer tối đa 1 MiB/chunk vào file tạm nguyên tử; open vẫn full-buffer và
  nhận cancellation khi đóng tab hoặc shutdown; đọc file theo khối 1 MiB,
  CRC/parse và v24 DEFLATE/CRC poll tối đa mỗi 64 KiB. DEFLATE ghi thẳng vào payload, bỏ buffer
  chunk phụ; dựng `DocumentSnapshot` chưa cooperative và thời gian dừng thực tế chưa đo. Save vẫn
  không hủy để giữ tính bền vững.
- Import qua isolated worker và SVG/Raster export chạy job nền; worker của import/export và recovery/
  autosave hiện có được đặt dưới normal priority. Import progress notification chỉ giữ một thông điệp
  đang chờ, tránh xếp hàng message theo từng record/chunk. SVG progress đã giới hạn tần suất; Raster
  báo theo file hoặc khi đổi trạng thái chờ RAM.
- Pointer move hiện giữ sample mới nhất trong một timer UI 16 ms; chỉ tạo một timer khi chưa pending,
  flush trước message khác và trước commit mouse-up. Regression coalescer cùng save/recovery pass.
  Đây là giới hạn tần suất cập nhật, không đồng bộ VSync; coupling vào frame và tách interaction overlay
  khỏi snapshot render vẫn chưa triển khai, cũng chưa có input-to-present evidence.
- Raster evaluator kiểm tra hủy tối đa mỗi 256 pixel ở vòng biến đổi và mỗi 64 KiB
  trong block sao chép hàng; regression xác nhận hủy giữa chừng và nhả bộ nhớ.
- `test_pikdraw_document_writer_safety` pass với regression page-context. Lần chạy cùng
  `test_pikdraw_desktop_import` dừng ở EPS commit (`ExecutionFailed`, dòng 1296); import target-page
  được kiểm trước đó trong suite, nhưng toàn bộ import suite chưa pass và cần điều tra riêng.
- P1 chưa xong: W0 cold/recovery/visible-present, input-to-present và thời gian hủy thực tế
  còn phải đo. Open `.pr`, recovery scan và nạp checkpoint đã chọn truyền cancellation qua đọc,
  CRC, DEFLATE theo cửa sổ 64 KiB và parse; kết quả scan bị hủy không publish, recovery xác thực lại
  metadata trước khi tạo tab. Shutdown yêu cầu hủy job rồi join; dựng snapshot chưa cooperative.
  `test_pikdraw_document_storage`, `test_pikdraw_document_storage_fidelity` và
  `test_pikdraw_save_recovery` pass; latency hủy chưa đo.
  Lệnh Release storage:
  ```powershell
  ./tooling/native/build.ps1 -Configuration Release -Test -TestTarget @('test_pikdraw_document_storage') -TestRegex '^test_pikdraw_document_storage$' -VerboseTests -LockWaitSeconds 120
  ```
  Kết quả: 1/1 CTest pass, 0.43 s.

### P2 — Trình bày canvas và invalidation

- Depends: P0/P1. Owned: core/rendering, apps/desktop canvas/overlays; không đổi model geometry.
- Prototype DXGI swap-chain target cho viewport để bỏ CPU readback của frame tương tác.
  Overlays composited đúng clipping/DPI/order; readback chỉ khi screenshot/export/oracle cần pixels.
  Có fallback GDI/WARP, device-lost handling và resize; cần đo prototype trước thay default.
- Damage propagation theo object/effect halo; retain scene commands/caches giữa revisions;
  BVH refit khi transform nhỏ, rebuild theo ngưỡng đo; giữ painter order và clip isolation.
  Geometry/text-layout cache key gồm revision/transform/font/profile phù hợp, có byte budget/eviction.
- Gate: W1/W2/W3 visible present, pixel compare với oracle ở zoom/DPI/effects/PowerClip;
  không mất frame cuối khi mouse-up; multi-monitor/device reset; peak VRAM có giới hạn.
- Trạng thái 24/10/2026: `D2DRenderBackend` có HWND `IDXGISwapChain1` được tạo
  trên cùng D3D device; `render_gpu` nhận đúng target type, resize/present và device-loss
  recovery; capture đọc pixels đồng bộ chỉ cho oracle/screenshot. Release build và
  `test_pikdraw_swap_chain`, `test_pikdraw_swap_chain_warp` đều pass: render màu đã biết,
  capture pixel, present, resize và kiểm lại màu trên driver mặc định/WARP.
  Cửa sổ test ẩn; `PIKDRAW_DIRECT_PRESENT=1` ghép swap-chain vào NativeWindow theo thứ tự
  GDI chrome → D2D canvas viewport → GDI canvas overlays/handles → GDI modal/menu/popup/
  tooltip; mặc định vẫn full-frame readback/GDI. Smoke screenshot thấy modal card bị D2D
  che vì lớp GDI modal được vẽ trước canvas. Đã tách canvas handles và topmost UI thành
  GDI layers sau D2D; canonical Release app build/link thành công. App được khởi chạy rồi
  đóng bình thường; Computer Use không chụp được ảnh mới do trusted RPC chưa cấu hình,
  nên modal fix chưa có xác nhận hình.
  Full `test_pikdraw_desktop_shell` trước đó segfault sau khi chọn Artboard. Root cause là
  `set_active_tool(14)` rơi vào status-text table chỉ có 10 mục; đã thêm nhánh Artboard
  tường minh. Sau khi bỏ instrumentation chẩn đoán, canonical Release app và shell test
  được build lại; `test_pikdraw_desktop_shell` pass 1/1 (6.15 s). Overlay order vẫn chưa
  được xác nhận bằng ảnh runtime mới.
  Overlay composition, visible W1/W2/W3, DPI/multi-monitor, mouse-up frame, VRAM và
  input-to-present còn mở; không coi code opt-in hoặc test ẩn là FPS hay hoàn tất P2.
- Đo A/B Release, cùng tiến trình/cùng snapshot 10k objects, 800×600, 3 warm-up + 21
  samples, driver hardware, HWND ẩn: PixelBuffer+readback p50/p95/max 9.639/15.623/21.303 ms;
  swap-chain `render_gpu` 5.821/6.959/8.703 ms; `Present(0)` 0.219/0.917/1.850 ms.
  Đây là backend phases của workload tổng hợp; Present(0) trên cửa sổ ẩn không đo scanout,
  visible FPS, input-to-present hoặc W1 hardware acceptance.
- Regression rasterizer Release ban đầu vấp guard 8.3 ms (9-sample median 8.686 ms).
  Guard đó suy nhầm hardware D2D thành màn hình 120 Hz, trong khi kế hoạch W1 đặt 16.7 ms
  cho 60 Hz; sửa guard offscreen về 16.7 ms và ghi rõ đây không đo present/FPS. Chạy lại
  `test_pikdraw_rendering_rasterizer`, `test_pikdraw_swap_chain` và
  `test_pikdraw_swap_chain_benchmark`, `test_pikdraw_swap_chain_warp`: 4/4 pass.
  Latest offscreen 9-sample median 14.369 ms, dưới guard 16.7; W1 visible p95,
  input-to-present và hardware reference acceptance vẫn chưa xác nhận. Failed-run output
  đã dọn bằng cleaner chuẩn.
- Tham chiếu: [Microsoft về reuse/cache/batching](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance)
  và [D2D device context với swap chain](https://learn.microsoft.com/en-us/windows/win32/direct2d/devices-and-device-contexts).

### P3 — Raster assets, tile/LOD và export lớn

- Depends: P0/P1. Owned: modules/raster, core/document asset references,
  core/rendering image consumers, modules/interop và platform/workers raster protocol.
- Nối disk-backed source store/tile cache/edit graph đang có vào import→document→render→export.
  Immutable asset handles dùng chung; decode visible tiles, prefetch bounded, hủy khi zoom/tab đổi.
  Upload GPU theo tile; eviction tính bằng bytes và pins của jobs, không chỉ số entries.
- Export/in theo tiles/stripes, effect halos và color transform đúng source ICC; full-resolution
  output không dùng thumbnail/proxy làm nguồn. Audit tất cả consumers của U64 lengths/offsets.
- Gate: W4/W5, portable reopen, duplicate/history không nhân encoded data; seam/alpha/profile
  oracle; BigTIFF offsets >4 GiB cả IPC lẫn document consumer; lỗi/cancel không rò file handles.
- Progress 24/10/2026: asynchronous raster preview cache hashes crop/flip/rotate/exposure và đủ
  channel/input/output parameters của Levels; `test_pikdraw_raster_preview_service` pass. Bitmap
  master CMYK16 TIFF now retains the exact ICC profile and passes a raw-sample white-composite oracle;
  `test_pikdraw_interop_raster_image_writer` and `test_pikdraw_raster_bitmap_export` pass. That writer
  currently emits classic TIFF and rejects output reaching its 4 GiB offset limit. Page export remains
  RGBA8. Import still assembles RGBA8/full source bytes; disk-backed source/tile integration end-to-end,
  streamed page export and W4/W5 remain open.
- Gate 24/10/2026: `test_pikdraw_raster_assets` pass 1/1 với sparse linked TIFF có kích thước
  `(1 << 32) + 16 KiB`; `register_linked`, `open` và `read_at` đọc đúng marker tại offset >4 GiB.
  Đây chỉ là xác nhận random-read của asset-store API, không chứng minh raster import, `.pr` portable
  save/reopen, IPC consumer, tile rendering hoặc export hỗ trợ toàn ảnh >4 GiB.
- Giữ acceptance chi tiết trong [large raster plan](raster-import-large-assets-20261021.md)
  và [import fidelity/performance](import-fidelity-performance-20261022.md); không tạo engine song song.

### P4 — Snapshot, spatial index và lịch sử

- Depends: P0. Owned: core/document snapshots/history/commands, core/geometry queries,
  modules/vector selection/snap; giữ command boundaries hiện có.
- Profile COW/copies theo transaction; share immutable chunks/assets; cập nhật index theo touched IDs.
  Tách object-local geometry khỏi viewport matrix; tránh rebuild document index vì pan/zoom.
- Rà large-coordinate precision: D2D cache cast path-local points sang float, dù translation đã trừ
  camera bằng double. Rebase geometry/cache theo C1/C3 với oracle trước/sau; không sửa tọa độ source
  hoặc coi camera-relative translation là đã giải quyết mọi path local rất lớn.
- Budget history bằng retained bytes, eviction/checkpoint policy rõ; hiện chỉ có giới hạn số bước
  mặc định 100 theo C2. Đo retained assets/snapshots trước khi chốt byte budget.
- Gate: W2/W3 commit+Undo p95/RSS, ID/revision monotonic, nested objects/locks/selection;
  snapshot đang dùng bởi save/render không bị evict. Tối ưu phải giữ đúng kết quả pick/snapping.
- Trạng thái 24/10/2026: đã thêm broad-phase dùng bounds sẵn có để bỏ qua flatten/scan path
  ngoài vùng; selection index tái sử dụng world bounds từ snapshot hierarchy; region selection
  thoát sớm khi bounds chứng minh kết quả; SoftwareRasterizer tái sử dụng SpatialIndex.
  Selection/culling/rasterizer regression tests mục tiêu đã pass.
- `PersistentObjectMap` already shares immutable AVL branches. Snapshot publication now tracks
  transaction-touched IDs and updates the persistent `SceneHierarchy` entries for compatible
  edits, rebuilding the changed subtree and ancestor bounds. Layer-root changes, group-child
  topology changes, and untracked replacements use the full rebuild path. Nested-group bounds,
  old-snapshot immutability, Undo, map sharing and touched-ID tracking pass targeted Release tests.
- Stable-ID and structural validation now uses the touched-object path when object membership,
  page/layer roots, group topology, editable path IDs, gradients and effect IDs are unchanged.
  It validates each touched object and inherits global uniqueness/reachability from the prior
  validated snapshot. Add/remove, layer/page changes, group edits, imports and identity changes
  remain on the complete validation path.
- Undo/Redo restores the immutable validated snapshot directly and shares its hierarchy storage;
  Release regressions cover this alongside page/object state and monotonically published revisions.
- Release benchmark (10 edit/Undo samples per profile): W1/10k objects commit p95 27.814 ms,
  Undo 0.0857 ms; W2/100k commit 136.625 ms, Undo 0.8765 ms; W3/one 100k-node path commit
  28.6346 ms, Undo 0.0185 ms. The previous W1 Undo p95 was 307.024 ms. W2 commit still needs
  a post-change benchmark. RSS, history retained-byte budget/eviction and large-coordinate rebase remain open;
  these measurements do not represent visible FPS, input latency, save/open or snapping.

### P5 — Container .pr có nén và streaming

- Depends: P3/P4 và schema review C5. Owned: core/document/storage, modules/raster assets,
  apps/desktop save/recovery; dependency mới ở third_party/cmake có pin/license.
- Trạng thái 25/10/2026: payload schema v23 giữ nguyên cho tài liệu không có encoded raster source;
  schema v24 có bảng nguồn/sample dedup và container v25. Payload >=1 MiB dùng chunk 1 MiB,
  STORE/DEFLATE và checksum; reader nhận container v2–v25.
  Đã bỏ trần toàn container 256 MiB và quota cứng 100k object/1m element; giới hạn wire u32
  và quota đọc theo 1/2 RAM vật lý còn trống vẫn áp dụng. `test_pikdraw_document_storage`
  có regression chunk compression và chế độ `PIKDRAW_TEST_LARGE_NATIVE_CONTAINER=1` cho atomic
  save/open file .pr thực lớn hơn 256 MiB.
- Atomic save nay encode payload theo buffer 1 MiB, phát từng chunk vào temp file và patch metadata/CRC;
  không tạo thêm vector payload toàn tài liệu. `NativeFormat::serialize()` vẫn có hợp đồng trả vector.
  Reader vẫn đọc toàn file và dựng payload giải nén trước khi parse nên open chưa bounded-memory/lazy.
- Còn lại của [PR format](../architecture/native-pr-format.md): versioned object index, dedup PDF/profile/
  preview, portable external resources, streaming load/lazy assets, peak RSS, save/open latency,
  W2/W4/W5, malformed-input/power-failure/cancel/no-partial-publish gates. Nén và stream save không
  được tính là đã giải quyết hết peak RAM hoặc UI blocking.
- Gate đã qua cho v24/v25: reader/writer round-trip, chunk corruption rejection, legacy storage/fidelity
  regressions và atomic save/open file thực lớn hơn 256 MiB. Chunked writer v24/v25 giữ threshold
  1 MiB cho payload schema v23; schema v24 raster luôn ghi container v25.
  Gate còn lại cho package P5: PR01–PR07 phần index/dedup/portable resources; peak RSS, save/open
  latency, W2/W4/W5, malformed input, power-failure simulations, cancellation và no partial publish.
- UI: nén lossless v24 tự động, không thêm lựa chọn trên Save As. Tùy chọn Optimize ảnh có quality/
  effective DPI rõ vẫn là hướng tương lai; extension-only không thêm controls. Giữ nguyên mọi thao tác
  thiết kế khi lưu mặc định.

### P6 — MCP nhanh và kết quả gọn

- Depends: P0/P4; P5 chỉ cần khi mở lazy scene. Owned: modules/automation,
  apps/mcp-server, apps/com-server và desktop automation adapter. Core không phụ thuộc MCP.
- Query từ snapshot index/cursor thay vì materialize tất cả summaries trước pagination.
  `fields` projection và filters theo ID/layer/region/type đã được thêm cho root objects của trang
  active; cursor v2 ràng buộc với active page và filter set. `objects.changes` trả changed IDs cho
  chuỗi tối đa 128 revision liên tục khi chỉ có edit object giữ nguyên topology/identity, tối đa
  4.096 IDs mỗi kết quả; structural changes, Undo/Redo, epoch mismatch và revision ngoài cửa sổ yêu
  cầu full query với danh sách delta rỗng. Delta subscription và nested query vẫn là API riêng chưa triển khai.
- Batch validate/apply dùng cùng validators, tối đa hiện tại 256 ops, một transaction/Undo;
  receipt ngắn gồm revision, created/client_ref mapping, changed IDs và diagnostics cần xử lý.
  Idempotency/request lookup xử lý timeout; không tự phát write mới khi chưa biết write trước đã commit.
- Ưu tiên object metadata/bounds/IDs; path nodes/pixels/artifact chỉ trả khi yêu cầu. Capability/tool
  schema cache theo version, không gửi lại toàn catalog hoặc toàn document mỗi thao tác.
- Structured output + outputSchema theo [MCP tools](https://modelcontextprotocol.io/specification/2025-06-18/server/tools).
  Spec khuyến nghị text JSON cho tương thích; chỉ giảm bản text trùng lặp sau khi kiểm client negotiated
  behavior của Codex/Claude/stdio/HTTP. Không bỏ text một phía rồi làm client cũ không đọc được kết quả.
- Long operations trả job handle/progress/cancel có TTL, không block message loop;
  scope/grants và policy người dùng không bị bỏ để giảm token.
- Gate: W6, pagination nhất quán theo epoch/revision; unknown fields bị từ chối; stale/busy/cancel/retry,
  đồng nhất HTTP/stdio/COM; đo bytes/token trên cùng task và tokenizer đã ghi rõ, không hứa % tiết kiệm.
- UI: Settings > Automation hiện có cho enable/revoke/grants; query/schema optimization không thêm
  toolbox/property bar/Properties. Workflow agent hiện dùng được ở [quickstart](../development/automation-quickstart.md).
- Trạng thái 25/10/2026: `objects.query` đọc trực tiếp ID từ snapshot theo trang, hỗ trợ cursor
  `v1` legacy và cursor `v2` gắn active page/filter, kiểm epoch/revision; projection và filter
  từ chối field/type/region/layer không hợp lệ. RPC regressions cover ID/type/region và filtered paging.
  `objects.changes` giữ cửa sổ 128 revision trong snapshot bất biến, trả tối đa 4.096 changed IDs
  cho edit object tương thích; structural change/Undo/Redo/gap hoặc delta quá lớn trả
  `requires_full_query=true` cùng danh sách ID rỗng. RPC và MCP schema regressions đã được thêm;
  cần chạy lại sau khi hoàn tất code.
  Release harness W2 (100 objects/trang) ghi first-page legacy 268,329 bytes và
  projection 22,629 bytes (giảm khoảng 91.6% payload); p95 lần lượt 7.31 ms và 0.470 ms trong
  benchmark in-process.
- W6 batch microbenchmark (10 mẫu/cỡ, Release, document riêng cho mỗi apply) ghi `batch.validate`
  p95 0.112/0.321/1.359 ms và `batch.apply` p95 0.277/5.176/46.330 ms cho 1/64/256 ops;
  receipt 275/2,597/9,857 bytes. Apply kiểm số IDs trả về và đúng một revision. Timing không gồm
  transport, parse/wire JSON hoặc serialize receipt, và không chạy khi UI đang kéo/nhập chữ.
- Chưa xong W6: RSS/tokenizer, timeout/idempotency qua transport thật, busy/stale/cancel,
  đối chiếu HTTP/stdio/COM và Codex/Claude end-to-end. Giữ output text tương thích.

### P7 — Nhiều module và thương mại

- Depends: lifecycle P1/P3/P5; phần package/license phát triển độc lập khi có hợp đồng rõ.
  Owned: modules/production, modules/scene3d, modules/plugin-sdk, apps/desktop composition,
  tooling/native/package.ps1 và service entitlement mới có boundary riêng.
- Nesting/CAM toolpath là derived jobs từ snapshot + tolerance/material/machine profile;
  invalidated khi source đổi. CNC settings và postprocessor có version, simulator và physical gate.
  3D có scene/cache riêng, tham chiếu stable 2D IDs; image edits qua nondestructive graph.
  AI chạy worker/service riêng với timeout/cancel, trả proposal có revision để host duyệt/apply.
- Plugin discovery lazy; plugin binary/ABI pin, untrusted worker isolation; module vắng không
  làm core editor mất khả năng mở/lưu tài liệu được hỗ trợ.
- Entitlement service tách khỏi document/render loop: signed license claims với expiry/features,
  verify local public key, private signing key chỉ ở server; cache/grace/offline policy rõ, clock
  rollback/revoke/device transfer có thiết kế và test. Không có HTTP check trên paint hay startup critical path.
- Hết quyền phải có UX bảo vệ công việc chưa lưu và đường export/read theo chính sách sản phẩm;
  không mã hóa .pr bằng key kích hoạt hoặc khóa artwork theo máy. Không hứa chống crack tuyệt đối.
- Release pipeline: optimized binary + symbols riêng, dependency/license inventory, ký binaries/installer,
  cập nhật có xác minh/rollback và crash diagnostics có opt-in. Chưa có chứng nhận sản phẩm thương mại.
- Gate: module absent/load/unload, crash worker, offline/expired/revoked/clock cases; Release clean-machine
  install/open/save/update/recovery; production chỉ công bố sau kiểm máy vật lý theo profile.
- Trạng thái 24/10/2026: P7 vẫn là định hướng kiến trúc. Chưa có entitlement/license server,
  activation, chữ ký phát hành, updater rollback hoặc clean-machine commercial release gate.

## 5. Thứ tự giao việc và giữ chất lượng

P0 → P1; P2 sau baseline/scheduling; P3 và P4 là nền cho P5; P6 dựa index/receipts đã có;
P7 dùng lại các lifecycle này. Khi có nhiều agent được người dùng giao, chia owned paths theo gói,
không sửa đồng thời cùng renderer/storage boundary; một build lock chung, không task harness.

Không dồn feature vào file >2.000 dòng. Tách module theo responsibility trước khi bổ sung.
Đo correctness và performance cùng snapshot/config; rollback tối ưu gây mất dữ liệu/fidelity.
Mỗi gói cập nhật trạng thái ngắn trong project-state, không nối nhật ký benchmark.
Test source có giá trị giữ trong suite; one-off source/output/log/screenshots phải dọn ngay sau đọc kết quả.
Hoàn thành một gói không tự chứng minh mọi file hết lag, khả năng sản xuất CNC hoặc feature parity.
