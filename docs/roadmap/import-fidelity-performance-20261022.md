# Hoàn thiện import: màu đúng, dữ liệu nguyên vẹn và thao tác mượt

Ngày rà cuối: 22/10/2026. Trạng thái: **đang triển khai; AC-IMP-19 và nền tảng P1b-F có evidence, toàn kế hoạch chưa đóng**.
Phạm vi: PNG/JPEG/TIFF/BigTIFF/BMP và SVG/DXF/EPS/PDF/AI PDF-compatible/CDR.
Giữ nguyên AC-IMP-01…14; bổ sung AC-IMP-15…19 cho các ranh giới còn thiếu.

## 0. Mục tiêu và điều kiện kết thúc

Người dùng cần đặt ảnh vào canvas, di chuyển/zoom/sửa/lưu/mở lại mà không xuất hiện ảnh giả,
mảng màu thay nội dung, sai kênh/alpha/ICC, mất nguồn hoặc khựng do sao chép toàn ảnh.
Tài liệu vector phải đi qua adapter riêng và giữ những geometry, layer, text, màu, ảnh nhúng
thuộc catalog đã công bố; không biến cả trang thành thumbnail để đạt tốc độ.

**Đầu ra bắt buộc của đợt triển khai:**

1. Ảnh nhỏ/4K và ảnh lớn dùng chung source authority, lifecycle, màu và renderer; transform/Undo
   không copy toàn pixel/source. Raster nhúng trong vector dùng cùng asset service sau normalize.
2. Import có tiến độ/hủy, chuẩn bị ngoài UI, validate rồi commit nguyên tử một Undo; kết quả cũ
   không đổi tài liệu hoặc canvas. Mở sai/thiếu file phải báo lỗi thật.
3. Mỗi format/version/representation/feature có hàng capability và oracle riêng cho
   appearance, editability, provenance, production và export; không suy rộng từ một mẫu pass.
4. Hoàn tất integration ảnh lớn theo [plan nguồn ảnh lớn](raster-import-large-assets-20261021.md),
   giữ AC-RI-01…24, gồm nguồn hợp lệ encoded ≥1 GiB và BigTIFF/native offsets >4 GiB.
   Asset store/descriptor hoặc preview đơn lẻ không phải điểm kết thúc.
5. Đo trên canonical app/worker sau sửa: UI trong lúc import, thao tác sau import, RAM toàn job,
   source/geometry/color integrity, save/reopen và output có capability thực tế.
6. Chỉ kết thúc ở trạng thái **release đạt phạm vi đã công bố** khi mọi gate bắt buộc ở mục 11 đạt.
   Một mục Fail/Not run không được đổi thành Pass bằng cách bỏ fixture hoặc thu hẹp yêu cầu ngầm.

**Giới hạn phạm vi có chủ đích:** không xây lại Qt/Skia, không thêm AI/brush/filter ngoài lát cắt
đã có trong plan ảnh lớn, không tự tải linked URL/font/image từ nội dung file, không viết mới
AI/CDR/EPS/DXF/PDF source-format exporters trong đợt này. AI private authoring, toàn bộ Corel live
effects, mọi PostScript operator, proof/Spot/DeviceN/overprint và physical CNC/press chưa có gate
thì phải giữ unsupported/not assessed; không map gần giống rồi gọi verified.
Từ chối rõ variant ngoài catalog là kết quả đúng của strict import; không được làm mất một
feature đang được công bố hỗ trợ rồi đổi nó thành unsupported để che hồi quy.

## 1. Cách agent bắt đầu và nguồn sự thật

1. Làm trực tiếp tại `C:/Users/CK ART STAGE/Downloads/PikDraw`. Đường Administrator trong
   hướng dẫn cũ không phải checkout phiên này. Không tạo worktree/clone/branch/task lease.
2. Đọc `AGENTS.md`, `docs/START.md`, `registry/modules.json`, scope AGENTS của module sắp sửa,
   diff từng file và snapshot hiện hành. Giữ mọi local edits; không reset/stash/ghi đè diện rộng.
3. Đọc [vector interoperability](../architecture/vector-resource-interoperability.md),
   [C8](../../contracts/c8-import-fidelity.md), [C9](../../contracts/c9-appearance-effects.md),
   [chuẩn UI](../design/ui-ux-standard.md), [workspace](../architecture/workspace-layout.md),
   [nghiệm thu](../quality/feature-acceptance.md), rồi tài liệu ảnh lớn P0–P7.
4. Source và consumer đang chạy xác định capability thực tế: `modules/interop/README.md`,
   `modules/interop/src/format_capability_matrix.cpp`, `FormatCapabilities`, adapter/worker,
   document/storage và desktop. C8 có phần Proposed; một số dòng trạng thái C9 còn lịch sử.
   Không dùng chúng để tuyên bố toàn contract đã Accepted hoặc adapter chưa tồn tại.
5. Thực hiện lần lượt theo dependency mục 5. Với mỗi task: đọc diff → triển khai → mở rộng
   regression đúng rủi ro → build/test target sở hữu → kiểm consumer → cập nhật trạng thái ngay
   trong kế hoạch/báo cáo ngắn. Không tạo phiếu hành chính hay snapshot lịch sử mới.
6. Giới hạn code ≤2000 dòng/file theo [quy chuẩn](../development/code-organization.md).
   Tách domain trước khi thêm state machine lớn vào import/storage/shell; giữ test target cũ khi
   tách translation unit. Không tách cơ học hoặc rewrite file dirty ngoài phạm vi task.
7. Schema, `object.hpp`, native storage, protocol, CMake, registry và shell shared files sửa
   tuần tự. Các scope dưới đây là phạm vi trách nhiệm, không yêu cầu tạo hoặc gọi agent con.
8. Build dùng `tooling/native/build.ps1` và shared lock `.agent/native-build.lock`; không dùng
   `-Force`/`-NoLock` để vượt lock đang có chủ. App chuẩn: `build/win-dev/apps/desktop/pikdraw.exe`.

Đợt triển khai 22/10 đã sửa source, regression và đặc tả cho RAM wait 95%, chunk IPC, tốc độ
proposal và click-to-place. Bảng evidence phải phân biệt test worker/headless, probe offscreen và
runtime UI; không dùng một loại bằng chứng để tuyên bố thay cho loại khác.

## 2. Baseline, nguyên nhân và khoảng trống đã biết

### 2.1 Nguyên nhân đã xác minh trong đợt sửa trước

| Vị trí | Trước sửa | Thay đổi đã có trong source cần bảo vệ |
| --- | --- | --- |
| `apps/desktop/src/workflows/document_workflow_import.cpp` | Ảnh đơn/source encoded dùng vector sở hữu riêng | Shared immutable pixels/source cho mọi placement, cả raster nhúng |
| `core/document/src/storage/object_codec.cpp`, `native_format.cpp` | Reopen đưa pixel/source về vector riêng | Giữ shared representation sau reopen |
| `core/rendering/src/gpu/d2d/d2d_internal.hpp` | Xóa image cache theo global document revision | Reuse ảnh không đổi; giữ owner; legacy inline so nội dung; LRU mặc định 1024 entry/256 MiB |
| `core/rendering/src/software_rasterizer.cpp` | Copy `ImageObject` chỉ để gắn transform cha | Truyền effective transform, tránh copy buffer mỗi frame CPU |
| `modules/interop/src/adapters/raster_image_decoder.cpp` | PBGRA fallback chưa bỏ premultiplication | Chuẩn hóa straight RGBA; fixture TIFF associated/unassociated alpha |
| Import progress | Progress theo mọi chunk có thể dồn message queue | Coalesce Reading khoảng 50 ms, stage transitions vẫn gửi ngay |
| `apps/desktop/src/main.cpp`, `native_window_import.cpp` | argv ANSI/UTF-8 sai với tiếng Việt; headless lỗi có thể exit 0 | Windows Unicode→UTF-8; kết quả import/commit quyết định exit status |

Hai PNG logo local không tái hiện mảng cyan trong probe/capture đã chạy. Ảnh chụp người dùng
không đủ để quy lỗi màu cho decoder, renderer hay bước chọn/commit; P0 phải ghi đúng điểm quan sát,
P3/P5 phải tiếp tục kiểm bằng oracle. Không suy rằng sửa PBGRA đã chứng minh nguyên nhân của ảnh đó.
Hai PNG logo khả nghi trong Downloads đã được probe lại sau row-stream/budget changes; cả hai đều
CPU/D2D 1:1 max channel error 0, vẫn giữ nội dung qua Undo/native reopen và không tái hiện mảng cyan.
Vì vậy lỗi screenshot vẫn chưa được quy về đúng source bytes hoặc trạng thái scene.

### 2.2 Bằng chứng trước bản rà cuối

Dùng ID trung tính; file riêng của người dùng, tên và đường dẫn không đưa vào Git.

| Hồ sơ | Kết quả quan sát được | Giới hạn |
| --- | --- | --- |
| LOCAL-PNG-A: 1448×1086, ~6 MiB RGBA | Import 2018.74 ms; commit 0.38 ms; CPU/D2D hardware 1:1 max channel error 0; source/pixel giữ qua Undo và native reopen | Debug, hai consumer không thay oracle ICC độc lập |
| LOCAL-PNG-A latest Debug native probe, one run | 1448×1086; import 1,829.81 ms (snapshot 13.55, worker startup 58.75, worker decode/proposal 1,161.20, host proposal decode 579.44, completion 16.80); commit 0.526 ms; 22 records; direct adapter 217.61 ms/file-handle adapter 203.01 ms; internal ghost 548×411 with 225,228 source-matched pixels; click commit, Undo, native save/reopen preserve source/pixels; CPU/D2D 1:1 error 0; 24 render samples command median/p95 0.099/0.192 ms, D2D 16.96/25.65 ms, native BGRA 12.86/13.18 ms | Debug, one import; internal NativeWindow harness, not cold visible-window input-to-present or Corel/Illustrator comparison |
| Cùng probe A, 24 mẫu 1024×768 | Command median 42.8972→0.099 ms trước/sau sửa; native BGRA D2D median/p95 13.2974/18.075 ms | Không phải FPS/input-to-present; sample count chưa đủ gate Release |
| LOCAL-PNG-B: 1427×1102, ~6 MiB | Import 2048.86 ms; commit 0.2825 ms; CPU/D2D error 0; native BGRA median/p95 12.9041/13.4522 ms | Chưa phủ mọi codec/profile/backend |
| LOCAL-PNG-B, latest re-run sau host proposal budget | Import 2296.08 ms; commit 0.3548 ms; CPU/D2D 1:1 max channel error 0; command median/p95 0.0917/0.1431 ms; native BGRA D2D median/p95 12.5834/12.7569 ms | Debug, 24 offscreen samples; timing dao động theo máy; không phải UI input-to-present hoặc ICC oracle độc lập |
| LOCAL-PNG-B, sau CRC32 table và zero-copy frame parsing | 3 lượt import 2075.09, 1622.55, 1714.61 ms; hai lượt lặp worker/proposal 1156.79/1223.62 ms; commit 0.29–0.47 ms; System.Drawing và imported RGBA trùng 6/6 pixel mẫu; D2D/CPU 1:1 error 0 | Debug/offscreen; baseline cùng probe 2879.30 ms, giảm khoảng 40% ở các lượt lặp; chưa phải cold UI p95 hoặc so sánh trực tiếp Corel/Illustrator |
| Cùng LOCAL-PNG-B, Release, 5 tiến trình mới | Import 323–407 ms (median 366 ms); worker decode/proposal stream 218–288 ms; host proposal decode 8.5–11.9 ms; commit 0.09–0.14 ms | Offscreen/worker harness, không phải UI input-to-present; source vẫn giữ nguyên; không so trực tiếp với Corel/Illustrator |
| LOCAL-PNG-B, Release, NativeWindow placement probe sau read-only source pin | Ba lượt end-to-end trước khi đặt: 386.7/404.8/412.1 ms (median 404.8 ms); regression tìm thấy ghost 548×452 bị kéo méo vì trần viewport width/height áp riêng; sau sửa ghost 548×411 giữ tỷ lệ nguồn 1448:1086, 225,228 pixel BGRA premultiplied được so với source; click commit giữ shared source/pixels | NativeWindow + HDC memory surface, chưa phải live visible-window/input-to-present hoặc phép so trực tiếp Corel/Illustrator |
| PNG logo đúng file trong `Downloads` (1448×1086; 2,234,307 source bytes; 5.999 MiB decoded), Release sau sửa tỷ lệ preview | Sáu lượt cùng binary/source `--raster-probe`: 354.5/459.2/532.2/423.8/424.1/368.4 ms (median 424.0, p95 532.2); commit 0.116–0.147 ms, 22 records; ghost 548×411 giữ tỷ lệ và 225,228 pixel buffer; sáu mẫu RGBA khớp Pillow 12.3 đọc trực tiếp PNG (không có embedded ICC); CPU/D2D 1:1 error 0; 24 lượt 1024×768 D2D p95 12.56–14.02 ms, native BGRA p95 13.08–15.43 ms. Một lượt breakdown: startup 92.0, decode/stream 314.8, host decode 12.5 ms | Offscreen HDC, cache nóng, sáu lượt; Pillow độc lập là decode oracle, không phải CMM/profile oracle; p95 nhỏ mẫu. Không chứng minh cold visible-window input-to-present hoặc so trực tiếp Corel/Illustrator |
| Raster P3b, TIFF CMYK gắn profile Windows `RSWOP.icm` | WIC giữ ICC, chuyển basis pixel sang sRGB; Pillow 12.3.0/LittleCMS 2.19 intent 0 độc lập cho trắng/mid-K/đen `(255,255,255)` / `(154,155,155)` / `(24,24,23)`; RSWOP SHA-256 `48d1cfdccf06dabfecf0c2b535eacd8a5f49560f0182691cdd713b6389a30510`; WIC hiện lệch tối đa 1/255 | Oracle độc lập được đo trên máy; Release `test_pikdraw_desktop_import` pass 1/1 (72.30 s), assertion so sánh 9 kênh RGB ±1 chỉ bật khi SHA-256 profile khớp. Chỉ một profile/3 pixels, chưa phải catalog CMM/source-app; relative intent lệch 10/255 ở K=100% |
| SWOP ICC worker → desktop → native reopen | Release `test_pikdraw_desktop_import` pass 1/1; kiểm tra ICC, source TIFF, sRGB basis và pixel qua worker proposal, commit, NativeFormat serialize/deserialize | Pixel đối chiếu với direct WIC decode trên cùng máy; ICC profile chỉ có trên Windows có cài `RSWOP.icm`; chưa có oracle vendor/CMM độc lập |
| LOCAL-PNG-A, re-run sau row-stream/budget changes | Import 1785.39 ms; commit 0.3465 ms; CPU/D2D 1:1 max channel error 0; command median/p95 0.0915/0.1123 ms; native BGRA D2D median/p95 12.9643/13.1081 ms | Debug, 24 offscreen samples; không tái hiện cyan nhưng không chứng minh screenshot dùng đúng source/scene |
| Native capture và CLI | Canvas thể hiện artwork vàng; Unicode open exit 0, missing file exit 2 | Capture mode chưa chứng minh thao tác UI trực tiếp |
| Raster staging không có quota byte cố định | Release `test_pikdraw_interop_raster_worker_adapter` pass 1/1 (0.42 s); regression stream nguồn >128 MiB và decoded >192 MiB qua worker không cắt theo staging cap, đồng thời kiểm tra trường hợp vượt budget thật | Không chứng minh RAM plateau dưới OS pressure hoặc ảnh >1 GiB |
| BigTIFF 64-bit offsets | Release decoder fixture pass 1/1: sparse BigTIFF có IFD và RGBA strip tại offset >4 GiB được đọc theo hàng, pixel RGBA đúng; decoder/capability pass 2/2; raster worker adapter, raster worker và desktop real corpus pass 3/3 (desktop 72.64 s) | Decoder chỉ hỗ trợ uncompressed RGB(A), strip đơn, orientation mặc định, không ICC; multi-IFD bị từ chối trước khi phát pixel. Worker/desktop còn giới hạn source length U32, nên chưa chứng minh BigTIFF import end-to-end |
| Cumulative RAM wait + host reservation boundary | Release `test_pikdraw_raster_cache`, `test_pikdraw_worker_v2_integrity`, `test_pikdraw_worker_raster_import` pass 3/3 (0.06/0.86/4.17 s); waiter kiểm pause/resume, caller cancel, timeout cộng dồn; host retry khi headroom đổi trước reserve | Probe waiter dùng injected headroom; chưa chạy controlled OS-pressure plateau ngoài máy thật |
| Suite vector adapter/worker | 10/10 pass: DXF/SVG/PDF-AI/EPS/CDR và DXF/SVG import workers | Không chứng minh vendor pixels, full effects/layers hoặc source round-trip |
| Desktop real corpus | Full focused suite pass 9/9: raster cache, capabilities, raster decoder/adapter, PDF/CDR adapters, worker integrity, desktop real corpus và AC-IMP-19 E2E | Không có quota staging byte cố định; waiter 95%/5 phút nối qua raster/vector worker, CDR raster và host checkpoints. Chưa có controlled OS-pressure plateau/multi-window trace hoặc aggregate peak trace |
| Placement ghost + canvas color + RAM fallback | E2E kiểm DIB ghost 2×1 và logo thật 1427×1102 với màu/alpha nguồn; click/Undo; Win32 canvas sau commit giữ pixel đỏ opaque và alpha-blend; Direct2D cache gate 95% dùng CPU fallback từ pixel nguồn khi thiếu RAM | Release pass 2/2 focused tests; optional logo probe passed. Chưa có live-window DPI capture hoặc controlled OS-pressure trace |
| Doctor | Pass | Không chứng minh UX/performance/fidelity |

Máy đo cũ khoảng 7.87 GiB RAM. Không so con số Debug này trực tiếp với gate Release/máy chuẩn.

### 2.3 Phần source mới có nhưng chưa được coi là integration hoàn chỉnh

- `modules/raster` đã có immutable disk-backed source store, budget, tile cache và tests
  `test_pikdraw_raster_assets`/`test_pikdraw_raster_cache`; không viết một asset store thứ hai.
- `core/document/include/pikdraw/document/model/raster_edit_graph.hpp` và native version **19**
  đã có descriptor/edit graph. Header/container hiện còn `payload_bytes` U32 và API serialize
  trả vector toàn payload; v19 không tự chứng minh native streaming >4 GiB.
- Desktop import, stream native blobs, codec tile decode, renderer provider và edit evaluation
  còn cần integration. Khi đổi container phải chọn version chưa dùng theo source lúc thực thi;
  không mặc định v19 còn trống theo các audit cũ.
- Raster writer đã có implementation riêng, trong khi `FormatCapabilities`/matrix còn có
  dòng import-only. P6 phải nối đúng export route/provider và thống nhất consumer capability,
  không chỉ đổi bool để mở quyền export.
- PDF source hiện xử lý page rotation 0/90/180/270; không còn mô tả mọi nonzero rotation là reject.
  Object-level rotated/sheared text, source color/effect/OCG coverage vẫn là gate riêng.

## 3. Hợp đồng triển khai không được phá

### 3.1 Pipeline và trạng thái

`source broker → adapter theo format/version → staged canonical proposal → host validation
→ fidelity review → prepared candidate → writer recheck → one transaction → immutable snapshot`.

Trạng thái thiết kế: Idle → Acquiring/Inspecting → Staging/Decoding → Validating → Review
→ Preparing → ReadyToCommit → Committed; mọi stage có nhánh Cancelled/Failed.
Đây là state contract cần map vào enum/API thực, không phải tên runtime đã tồn tại đầy đủ.
Copy/hash/preview có thể chồng thời gian theo plan ảnh lớn nhưng phải cùng pinned source version.

- Chốt document ID, epoch, strict base revision, job ID/generation, source version và capability
  ngay khi tạo job. Kiểm lại trước adopt preview và trước writer commit; base đổi thì reject
  toàn proposal, không rebase ngầm. Cancel tăng generation trước cleanup.
- Review dùng staging; chưa commit không thêm page/layer/object/Undo hoặc đổi selection/camera.
  Sau commit: stable IDs do writer cấp; source IDs chỉ là provenance.
- Chuẩn bị tree/index/assets nặng ngoài UI trên dữ liệu bất biến. Writer publish nguyên tử;
  không chia import thành nhiều transaction để nhường message pump.
- Progress queue có giới hạn/coalescing, không bỏ document records/commands. Lỗi/crash/timeout/
  cancel phải đóng worker, thu spool/handles/reservations, giữ tài liệu/dirty/history trước đó.

### 3.2 Source, asset và bộ nhớ

- Source encoded/descriptor là authoritative; preview, tile/LOD, GPU bitmap là derived cache.
  Embedded mặc định, Linked/Relink/Update theo plan ảnh lớn. Xóa cache không được làm mất nguồn.
- Clone/snapshot/Undo/duplicate/transform chỉ copy refs/params; cùng asset revision chia sẻ bytes.
  Cache key đủ source/asset revision, graph, sample basis, profile/display transform, LOD/ROI
  và device generation phù hợp. Document revision một mình không là content identity.
- Tính peak đồng thời host + tất cả workers + decoded/intermediate/proposal + prepared tree/index
  + Undo pins + CPU/GPU/effect/in-flight/cache; không coi giới hạn một IPC record là tổng budget.
- Cập nhật 22/10: bỏ mọi quota staging byte cố định theo nguồn, pixel, worker, preflight,
  aggregate host hoặc CDR. Source/pixel record vẫn có trần biểu diễn 32-bit. Khi một lần cấp phát
  không còn đủ headroom trước ngưỡng sử dụng RAM vật lý 95%, worker/host tạm chờ để ứng dụng khác
  nhả RAM rồi tiếp tục; allowance chờ cộng dồn tối đa 5 phút cho mỗi bên của luồng import. Người
  dùng vẫn có thể hủy job. Thời gian pause được cộng bù vào deadline worker-stream nên timeout xử
  lý nền không cắt lần chờ RAM sớm hơn; chỉ hết allowance chờ cộng dồn mới báo timeout do RAM.
  Raster admissions kiểm tra từng row band/output/chunk và host proposal;
  vector workers kiểm tra trước khi đọc nguồn đầy đủ, trước/sau khi dựng proposal và ở ranh giới
  record. Parser/codec trung gian của CDR/PDF/EPS/SVG/DXF chưa có checkpoint ở mọi allocation;
  host vẫn full-frame. Đây chưa đóng gate ảnh lớn hoặc chứng minh peak RAM. Trần C6/IPC, số object,
  U32 record và giới hạn codec là giới hạn biểu diễn/độ toàn vẹn, không phải quota tổng staging.
- Thiếu budget cho model/work hợp lệ phải là resource_limit/pause có lý do và cách retry,
  không là “file invalid”. Giới hạn codec/representational thật vẫn checked, có diagnostic.
- Renderer nhận provider interface trung lập; implementation raster service được inject ở
  composition root desktop. Core/rendering không phụ thuộc ngược vào modules/raster hoặc desktop.

### 3.3 Màu, alpha, orientation và độ chính xác

1. Giữ encoded source/hash/ICC/bit depth/channel layout/orientation/DPI và giá trị nguồn khi adapter
   truy cập được. RGBA8 preview không thay authority của ảnh 16/32-bit hoặc CMYK/Spot nguồn.
2. Mỗi buffer mang basis tường minh: source/converted-to-working-or-sRGB/profile-unapplied/unknown
   theo schema versioned. Decoder áp orientation và alpha normalization đúng một lần;
   renderer không lặp ICC hay EXIF. Straight/premultiplied được khai báo tại boundary.
3. RGB thiếu ICC chỉ dùng policy assumed-sRGB có ghi nhãn; không tự gắn ICC nguồn giả.
   CMYK/unknown/invalid ICC không đoán profile in. Retain source + warning/reject hoặc conversion
   được chọn rõ; không nâng appearance verified.
4. Profile/working/display thay đổi chỉ invalidate derived preview tương ứng; không rewrite source,
   coordinates, units hay original channels. Hidden RGB dưới alpha=0 có policy sample rõ,
   original encoded bytes luôn giữ; không ép alpha để che lệch màu.
5. PDFium RGBA hoặc libcdr→SVG có thể đã mất source color-space semantics. Không suy CMYK/Spot/
   DeviceN/overprint từ RGB. Chỉ gọi verified đúng output/profile/representation có oracle.
6. Native gradient/effect tồn tại không đồng nghĩa importer đã map semantics nguồn. Fill/stroke/
   object/group opacity, blend, clip/mask và effect order theo C9, thiếu mapping phải báo trước commit.
7. Geometry so expected coordinates/units/transform độc lập. Primitive affine mapping dùng tolerance
   đã ghi trong fixture; approximation phải có bound riêng và giữ loss report. Pixel pass không
   cấp quyền production; tolerance CNC/in theo profile công việc là gate ngoài đợt này.

### 3.4 Fidelity và capability

Host quyết định scope theo adapter/parser build + fixture catalog. Worker chỉ báo feature/loss,
không tự cấp appearance/production verified. Giữ riêng parse outcome, coverage, appearance,
editability, provenance, production và target export; old payload thiếu trường mặc định unknown.
Lưu choices/report/provenance cùng transaction và qua native reopen. C8/C9 chưa implemented đủ
phải có gap rõ; không copy enum tên đề xuất vào UI rồi xem như contract đã hoàn thành.

## 4. Ma trận format/version/representation và mục tiêu từng hàng

Mỗi catalog row phải ghi: fixture ID/hash/quyền dùng, header/source version, source app/build,
adapter/profile/build hash, feature/attachment scope, unit/profile/font assumptions,
appearance oracle+metric+tolerance, editable structure oracle, native lifecycle và output coverage.
Không biết source version thì giữ unknown; không tự cấp scope cả họ. Bảng dưới là target catalog,
không phải lời công bố mọi variant đã pass.

| Hàng / profile hiện có | Bắt buộc kiểm/giữ trong phạm vi | Ngoài catalog hoặc điều kiện reject | Gate riêng |
| --- | --- | --- | --- |
| PNG `raster-rgba8-v1` | RGB/gray/indexed/tRNS/RGBA; noninterlaced/Adam7, DPI/orientation/ICC; 8/16-bit source, preview ghi rõ precision | APNG/animation hoặc sample mode chưa có codec gate không được giả single-frame đầy đủ | Pixel/alpha/source; 16-bit output theo provider, không RGBA8 round-trip claim |
| JPEG cùng profile | Baseline/progressive, RGB/gray, EXIF 1–8, DPI, embedded ICC; CMYK có/không profile thành hàng riêng | Variant không biết color basis không được giả RGB certified; codec limits có diagnostic | JPEG lossy tolerance riêng, source bytes exact |
| TIFF/BigTIFF cùng legacy profile + stream profile mới | LE/BE; strip/tiled; associated/unassociated alpha; RGB/gray/CMYK; ≥300 frames/lazy selection; >4 GiB offsets | 16/32-bit/float/codec-compression theo từng hàng; RGBA8 không là HDR preservation | AC-RI-01…24, bit depth/original+full-res export riêng |
| BMP cùng legacy profile | Top-down/bottom-up, supported sample layout, orientation/DPI, channel/alpha | Layout/compression chưa có fixture phải reject rõ | Giữ backward compatibility, không cần tạo BMP layer editor |
| SVG `svg-hierarchy-v3` | Static SVG 1.0/1.1 subset: Path/Rect/Text/Group, units/viewBox/transforms, presentation opacity đúng scope | CSS/inheritance/defs-use/gradient/clip/mask/filter theo từng row; script/DTD/XXE/external fetch không bật | Native supported appearance không tự unlock SVG import; export subset độc lập |
| DXF `dxf-entities-v1` | ASCII model-space 2D: **LINE/ARC/CIRCLE/LWPOLYLINE**, `$INSUNITS`, layer/entity style, transform/lineweight; record `$ACADVER` của fixture | Binary/3D/paper-space/blocks/INSERT/SPLINE/DIMENSION/TEXT chưa có adapter gate thì reject, không bỏ entity; missing/unitless INSUNITS không tự đoán mm | ACI/BYLAYER/ACI7 background semantics khác TrueColor; tolerance/topology/unit trước production; chưa DXF writer |
| Plain EPS `eps-path-v1` | DSC EPSF, BoundingBox, allowlisted path/RGB/Gray/CMYK/transform; source profile xác định | Operator ngoài allowlist reject nguyên tử; không diễn giải PostScript trong host | CMYK appearance phải có profile oracle; không toàn bộ PostScript |
| DOS EPS converted | Isolated external Ghostscript → PDFium → canonical subset; ghi converter build và converted representation | Không có Ghostscript hoặc conversion loss ngoài lựa chọn đã hỗ trợ: diagnostic/cancel; không fake native EPS | License/runtime/timeout/isolation + PDF appearance/structure; không EPS export |
| PDF editable `pdf-page-set-v3` | All inspected pages kể cả blank, page boxes, rotations 0/90/180/270, path/text/image subset, font identities, page layers/OCG marks khi có | Rotated/sheared text theo object, clip/shading/blend/fonts/OCG/color spaces chỉ bật từng row có oracle | Header PDF version/catalog; source RGB preview không thay source process/spot/ICC semantics |
| PDF locked `pdf-print-source-v1` | Exact source bytes/pages và locked screen preview riêng | Không coi RGBA preview là editable artwork hoặc print-quality output | Preview oracle + source authority; print-quality raster export từ PDF gốc chưa nối là gap |
| AI PDF-compatible `ai-pdf-compatible-page-set-v3` | Cùng PDF row, ghi AI representation và inspected pages | Illustrator private authoring/layers/artboards/live effects không suy từ PDF view | Test riêng .ai; không đổi suffix để giả parser; không AI writer |
| AI legacy/non-PDF authoring | Detect đúng signature và outcome unsupported có lý do | Không thuộc adapter editable của đợt này | Negative fixture; không đưa vào số “AI editable full” |
| CDR `cdr-libcdr-svg-v2` | Pin libcdr 0.1.9/librevenge 0.0.6; RIFF và ZIP/ZCF có source version thực trong catalog; paths/text/groups/layer IDs, geometric clips và embedded assets theo subset | One-page current subset; Lens/Mesh/live blends/PowerClip variants/fonts/profiles/linked content phải có row riêng; generated SVG unsupported reject toàn nguồn | Pixel+structure từ source app, memory pressure thật; không CDR writer/round-trip |

Export/source production không được gộp vào một cột Supported. Native save/reopen bảo toàn canonical
data là bắt buộc; writer raster/target subset có route riêng, AI/CDR/PDF/EPS/DXF save-back vẫn unsupported.

## 5. Thứ tự triển khai và thẻ nhiệm vụ

**Điểm bắt đầu của agent nhận việc:** đọc diff và xác nhận P0 còn đúng; tiếp tục P1a/P1b-F,
không viết lại các fixes/cache/store đang có. Trạng thái sau là độ hoàn thành gate, không có nghĩa
mọi task Open đều chưa có dòng code nào.

| Nhóm | Trạng thái ở thời điểm rà kế hoạch | Việc tiếp theo |
| --- | --- | --- |
| P0 | Có fixes + baseline nhỏ, acceptance mới Partial | Xác nhận source/binary/catalog và bảo vệ regression |
| P1a | Có async/progress từng phần, full state gate chưa đóng | Writer prepare/cancel/stale/dirty lifecycle và UI stall trace |
| P1b-F/R | Partial/Open: không có quota staging byte cố định. Raster reservations và mọi vector worker (kể cả ảnh nhúng CDR) phát yêu cầu waiter 95%; V2MemoryWait giữ stream sống và host/worker tiếp tục sau headroom. Mỗi bên có tối đa 5 phút chờ cộng dồn; host retries reservation nếu headroom đổi giữa probe và allocation | Parser/codecs vector vẫn có allocation nguyên khối trước một số checkpoint; resident model/Undo, nhiều import đồng thời và controlled OS-pressure plateau vẫn cần kiểm sau P2d/P5-CDR |
| P1c | Desktop fidelity review preserves severity/source locator/feature/diagnostic. Native v20 stores typed per-page reports. Debug `test_pikdraw_desktop_import` pass 1/1 (158.95 s); corpus imports DXF LINE/LWPOLYLINE/ARC/CIRCLE, commits, saves/reopens, and checks object IDs, editable-path reconstruction, node anchors, segment topology and report fields. | Host catalog/parser hash, v2-v20 compatibility, review UI behavior and source application fidelity remain open. |
| P2a–d | Có store/cache/descriptor/v19; P2b hiện đã có WIC row stream + handle-backed worker source trong profile bounded. BigTIFF decoder-only có uncompressed RGB(A) strip đơn và offset >4 GiB; worker/desktop source wire vẫn U32. Host vẫn materialize full-frame; asset-store/native-stream/tile/render/output chưa nghiệm thu | Nối foundation vào codec/worker/storage/embedded assets, mở source length >U32 bằng contract/version compatibility, không tạo store thứ hai |
| P3a–b | Có color basis/ICC paths; RSWOP CMYK TIFF kiểm WIC so với document transform theo preferred intent trong ±5/255; profile/pixel/source tiếp tục nguyên qua worker, desktop commit và native reopen | Thêm oracle CMM/ứng dụng nguồn độc lập và mở rộng chart/version/profile; chưa đóng appearance verification |
| P4 | Image LRU/reuse đã sửa; Direct2D image-cache pressure dùng ngưỡng 95% và CPU fallback để không từ chối ảnh nhập | Invalidation/device lifecycle và controlled pressure/performance trace vẫn mở |
| P5 | Adapter đã tồn tại, 10 adapter/worker tests pass trước đó | Từng format/version row + pixel/structure/edit/lifecycle, không chỉ rerun unit tests |
| P6 | Release/offscreen raster placement regression pass; Escape routes through a testable native input handler. PNG probe checks requested canvas placement, source/pixel retention, one Undo and Escape invariants. V2 reader transfers an existing protocol frame per overlapped operation. Raster export no longer has the 128 MiB/32 MP frame caps; render and writer share one 95% RAM waiter with a cumulative five-minute timeout, and the UI reports the pause. Writer and waiter regressions pass Debug | Full-frame raster render and 16,384 px per-side limit remain; cold/visible UI, controlled OS-pressure plateau, vector corpus end-to-end and release gate remain open |

**Cập nhật triển khai 22/10:** P2b đọc raster qua `RasterByteSource` → read-only `IStream` 64-bit,
giải mã WIC theo dải hàng và phát pixel/source C6 theo chunk; worker không giữ thêm một `vector`
nguồn đầy đủ hoặc full-frame RGBA. Shared reservation charge WIC band/output/chunk và host raster
proposal buffers. Raster và vector worker bỏ quota Job Object cố định; waiter RAM 95%/5 phút được
dùng ở các điểm cấp phát/record hiện có. Vector adapters vẫn có giai đoạn dựng proposal nguyên khối,
nên chờ bên trong parser chưa được bảo đảm ở mỗi lần cấp phát. CDR package/proposal còn giữ dữ liệu
trung gian; không có cap decode riêng.
CRC32 IEEE được tính bằng bảng 256 mục thay cho tám vòng bit mỗi byte; proposal validator và host
spool đọc `V2ProposalChunkView` trực tiếp từ frame, tránh tạo lại packet và bản sao canonical lớn.
Probe LOCAL-PNG-B giảm từ 2.88 giây xuống 1.62–1.71 giây ở ba lượt đo mới; đây là thời gian worker
+ proposal + host decode trong test Debug, không phải UI input-to-present. Import commit vẫn là một Undo.
`test_pikdraw_interop_raster_image_decoder` kiểm pixel từng hàng, cancel, budget release và PNG trong
sparse file >4 GiB. Cập nhật cap 22/10: adapter pass BMP nguồn 156 MiB với RGBA8 209 MiB, phát đủ
source/pixel bytes; TIFF frame 299/300 pass. Capability + adapter + document model/storage 4/4 pass và
`test_pikdraw_desktop_import` pass 1/1 sau thay đổi. Logo cục bộ 1427×1102 đạt CPU/D2D 1:1 max error 0;
24 mẫu offscreen chưa phải UX measurement. Không còn cap byte staging cố định cho source/pixel/
worker/preflight/host/CDR; source/pixel record vẫn có trần biểu diễn 32-bit. RAM gate là chờ/tiếp tục
ở 95%, allowance cộng dồn tối đa năm phút. Host proposal vẫn materialize full-frame, native
file còn cap 256 MiB, BigTIFF chưa xác nhận, CDR worker
package/proposal queue chưa aggregate-budgeted; P1b-R,
P2a/P2c/P2d và P6 vẫn mở. Một lượt pass không chứng minh đã hết lỗi áp lực RAM.

Đợt bổ sung ICC/RAM cuối ngày: TIFF CMYK gắn `RSWOP.icm` được kiểm từ WIC decoder và worker IPC
đến desktop proposal/commit/native serialize/reopen; ICC profile, source, sRGB basis và pixels giữ
nguyên. Hai API màu cùng dùng Windows CMM nên chưa thay thế oracle ứng dụng/CMM độc lập. Release
`test_pikdraw_worker_v2_integrity` kiểm pause/resume/cancel và allowance cộng dồn; desktop host tiếp
tục chờ nếu headroom đổi ngay ở ranh giới reservation. Chưa chạy plateau áp lực RAM có kiểm soát.

Dependency bắt buộc:

```text
P0
 └─ P1a lifecycle/prepare ─ P1b-F budget foundation ─ P1c fidelity schema
      ├─ P2a asset/storage ─ P2b codecs/IPC ─ P2c tiles/render/export ─ P2d embedded vector assets
      ├─ P3a color contracts ─ P3b independent oracles
      └─ P4 vector cache/invalidation
P2d + P3b + P4 + P1c → P5-SVG → P5-DXF → P5-PDF → P5-AI → P5-EPS → P5-CDR
All above → P6 UI integration + final corpus/Release gates
P2d + P5-CDR → P1b-R pressure regression closure → P6
```

Đây là thứ tự mặc định của một agent; không đợi P2 xong mới viết oracle nhỏ P3.
Thay đổi shared model/protocol/storage luôn tuần tự; sau đổi shared contract phải kiểm lại những
adapter/consumer bị ảnh hưởng. Bắt đầu integration từ source đã có, không reimplement P0 fixes.
Dependency chỉ yêu cầu interface/gate nền tảng của bước trước; AC có nhiều owner giữ Partial cho
đến khi mọi consumer đạt. P1b-F mở đường P2; P1b-R đóng sau P2d/P5-CDR, tránh vòng phụ thuộc.

| Công việc dùng lại từ plan ảnh lớn | Nơi thực hiện trong plan này |
| --- | --- |
| Raster P0 baseline/feasibility | P0 và P2b codec spike |
| Raster P1 source model/native streaming | P2a |
| Raster P2 source/codec/IPC | P2b |
| Raster P3 coordinator/Embedded/Linked | P1a + P2a; UI ở P6 |
| Raster P4 cache/provider và P5 graph/evaluator | P2c, dùng contract màu P3 |
| Raster P6 full-res writer/Links/Properties | Backend P2c, UI/output route P6 |
| Raster P7 final corpus/Release | P6; đóng AC-RI-01…24 tại đây |

Hai kế hoạch mô tả cùng integration, không phải hai pipeline/asset store hoặc hai đợt thực thi lặp.

### P0 — Khóa baseline, catalog và kiểm P0 fixes

**Scope:** `apps/desktop/tests/test_svg_dxf_import.cpp`; adapter/rasterizer/storage tests hiện có;
tooling đo import theo domain. Manifest/runner mới nếu cần phải đánh dấu là đề xuất, đặt trong
cấu trúc tests/tooling hiện có; corpus lớn/private ở ngoài Git.

1. Inventory consumers của `ImageObject`, `rgba_pixels()`, `original_data()`, descriptors, clone,
   validation, storage, CPU/D2D, export; phân biệt inline/proxy/asset source, không suy bằng vector rỗng.
2. Xác nhận các sửa mục 2 vẫn tồn tại trong diff; bảo vệ bằng tests có thể fail ở code trước sửa.
   Không quay lại deepcopy để làm test dễ viết.
3. Catalog fixture IDs và expected independent outputs; xác nhận real corpus discovery, không skip CDR
   vì thiếu env var rồi gọi suite xanh. Thêm synthetic minimal reproduction nếu nguồn riêng không commit được.
4. Trace metadata/source copy+hash/decode/IPC/validation/prepare/review wait/commit/first useful paint riêng;
   dùng monotonic clock, queue delay và complete/cancel/fail endpoints. Review user-wait không tính decode.
5. Ghi source/worker/exe hash, build type, backend thực, viewport/DPI và OS aggregate memory.

**Deliverable/gate:** baseline tái tạo được, inventory/catalog, AC-IMP-01/02/03/07 partial được tách rõ;
không có claim toàn AC pass từ một probe. Phụ thuộc: không. Sau gate tiếp P1a.

### P1a — Import lifecycle và chuẩn bị ngoài UI

**Scope:** `apps/desktop/src/shell/native_window_import.cpp`, `native_window_files.cpp`;
`apps/desktop/src/workflows/document_workflow_import.cpp`; core document command/async validation.
Nếu thêm coordinator lớn, tách `document_workflow_import_job.*` mới theo domain, đăng ký CMake.

1. Implement state contract mục 3.1, one active job per target, cancel/generation và ownership rõ.
   UI event chỉ schedule/adopt light state; source read/normalize/tree/index preparation không chạy O(n) tại click.
2. Prepared candidate gắn strict base; writer transaction publish atomic. Nếu cần prepared-root command mới,
   thiết kế trong core/document, không để core gọi desktop hoặc parser callback.
3. Review có staging phù hợp, Cancel không đổi page/history/dirty/selection/camera.
4. Progress ≤20 Reading updates/s, transitions và terminal events đến ngay; bounded queue không drop records.
5. Fault/stale barriers kiểm edit/Undo/tab switch/close/reopen trong các stage, kể cả sau review trước commit.

**Deliverable/gate:** state transitions + cleanup assertions, một Undo, không partial/stale publish;
AC-IMP-08/15/16 và phần UI timing của AC-IMP-11. Tests desktop import, writer safety, worker integrity.
Phụ thuộc: P0. Không đóng task chỉ vì worker chạy background nhưng UI commit vẫn stall.

### P1b — Admission, peak accounting và lỗi RAM CDR

**Scope:** import workflow; `platform/workers/src/source_snapshot.cpp`, `job_object.cpp`,
`v2_worker_protocol.cpp`/`v2_proposal_validator.cpp`; adapter buffers;
`modules/raster/src/memory_budget.cpp` và budget service được inject.

1. Reproduce CDR pressure failure với ID/hash local; lấy aggregate trace theo thời gian, đếm mọi
   source copy/intermediate SVG/pixel/proposal/cache/tree/Undo và process sống sau từng fixture.
2. Xác định amplification/leak/queue bloat hay thiếu tài nguyên của model cuối; sửa owner/lifetime,
   stream/spool/chunk/backpressure trước khi nới admission. Có test chạy import lặp không tăng vô hạn.
3. Reserve trước allocate, tính in-flight/caches/concurrent workers; áp evict/throttle/spill hoặc pause
   theo budget. Giới hạn job/disk/time/representational thật còn hiệu lực; không đổi sai file thành hợp lệ.
4. Tách deterministic injected low-budget tests khỏi pressure thật. Không tự bơm RAM máy người dùng.
5. RAM gate hiện là 95% với backpressure/resume và timeout cộng dồn 5 phút; code review phải bảo đảm
   consumer mới gọi waiter ở điểm cấp phát/chunk phù hợp. Thiếu headroom được báo timeout/resource
   diagnostic, không phân loại nguồn thành corrupt.

**Gate P1b-F, trước P1c/P2:** có trace/root-cause inventory, budget/reservation API dùng chung,
deterministic reserve/release/cancel tests và kế hoạch sửa từng buffer/queue cụ thể. Các caps cũ
còn được giữ an toàn trên đường chưa migrate; đây chưa phải đóng lỗi CDR hoặc AC ảnh lớn.

**Gate P1b-R, sau P2d/P5-CDR:** re-run đúng CDR fixture cũ trong tài nguyên đã khai báo,
không leak/partial commit; thiếu budget của final model có resource_limit/retry chính xác.
Đóng AC-IMP-12/17 và AC-RI-09/10/16 khi các consumer liên quan cũng đạt; không đánh dấu pressure
bug fixed chỉ bằng chạy trên máy nhiều RAM. Phụ thuộc nền tảng: P1a; regression closure là gate P6.

### P1c — Report/provenance/capability tường minh

**Scope:** interop capabilities/report headers, worker semantic records/validator, document neutral
provenance/storage, workflow review. Report trung lập hiện nằm trong
`core/document/include/pikdraw/document/model/import_fidelity_report.hpp`; không tạo dependency ngược.

1. Version typed parse/coverage/appearance/editability/production/export statuses theo C8, object/source locators,
   loss codes, converter provenance và user choice; giữ canonical model không chứa vendor types.
2. Legacy payload thiếu fields nhận unknown/default an toàn; unknown required enum/schema reject;
   proposal và committed-document variants đều phải test.
3. Host gắn registry/catalog/parser hash, validate report completeness, không tin worker tự set verified.
4. Writer commit report cùng objects/assets trên từng trang. Native format v20 giữ typed status và issue
   fields; regression tối giản xác nhận serialize/reopen. Cần kiểm thêm save/reopen trên imported geometry
   và stable source-ID mapping; DXF ARC/CIRCLE hiện chạm lỗi authoring-source/adapter validation trước reopen.
5. Source private dùng neutral IDs trong log; không log tên/path/text/bytes khách hàng hoặc token URL.

**Deliverable/gate:** AC-IMP-10/14/18, legacy/current payload compatibility; document/storage/workers/UI
consumer review trước thay contract status. Phụ thuộc: P1a/P1b-F; report storage v20 đã có, nhưng AC-IMP-18
Partial tới khi host catalog/hash validation, imported-geometry round-trip và UI review gate hoàn tất.

### P2a — Tích hợp asset store và native streaming

**Scope:** `modules/raster/include/pikdraw/raster/asset_store.hpp`, `src/asset_store.cpp`;
document `model/object.hpp`, `model/raster_edit_graph.hpp`, validation/clone/commands;
`core/document/src/storage/native_format.cpp`, `object_codec.cpp`, save/recovery.
Stream container/blob/index helper mới phải tách domain, ghi schema/version trong C5.

1. Reuse asset ID/revision/descriptor/store đang có; backing discriminator rõ, migrate legacy inline.
   Không lưu HANDLE/vendor type trong model; register source+placement trong một transaction.
2. Native source/blob/index U64 và save/read streaming; audit mọi cast/size/CRC/offset+length.
   API vector cũ chỉ phục vụ bounded legacy compatibility, không production GiB path.
3. Managed embedded/lifetime pins/GC và linked source versions theo plan ảnh lớn;
   nguồn authoritative không bị evict cùng tiles.
4. Save snapshot R đồng thời có edit R+1, disk-full/cancel/crash/recovery và old native versions.
   Xóa derived cache rồi reopen vẫn tự chứa và dựng được full detail.

**Deliverable/gate:** AC-IMP-03/12/14, AC-RI-05/06/07/14/19/20; native blob >4 GiB có boundary
và dense-source evidence, không lấy sparse-only làm throughput. Phụ thuộc: P1c.

### P2b — Codec/source streaming và IPC tile jobs

**Scope:** raster decoder/worker adapter; codec/source-stream helpers mới theo domain;
`platform/workers/src/v2_worker_protocol.cpp`, source capability/semantic validators;
third_party lock/license nếu thêm codec.

1. Probe/preview/region/cache-build interfaces, source handle/stream 64 bit, không read-all/source_copy vector.
2. JPEG native shrink/sequential và progressive coefficient memory; PNG row/pass/Adam7 cache;
   TIFF strip/tile/BigTIFF/IFD paging; BMP compatibility. Codec/backend variant phải pin và đo scratch.
3. Worker produces bounded descriptors/tiles/spool outputs; host validates CRC/hash, dimensions,
   stride, channel/sample/basis, offsets, graph refs và reservations trước adopt.
4. Request/response capability negotiation versioned, backpressure/cancel/lifetime; legacy small
   raster records vẫn tương thích. Không cấp worker quyền mở tùy ý thư mục nguồn.
5. Multi-frame TIFF chọn trang/range/lazy metadata, truy cập trang ≥300; không decode cả file để chọn trang. Trần 256 frame đã được gỡ ở decoder; regression chọn frame 299 trong TIFF 300 frame.

**Deliverable/gate:** AC-IMP-04/12/16, AC-RI-02/03/04/08/16/18. Nguồn ≥1 GiB thật mở được, frame
decoded >192 MiB không còn cap cũ ở worker adapter; host aggregate staging không còn cap byte cố định và
materialize full-frame, nên đây chưa phải nghiệm thu file 1 GiB/ảnh lớn. Output banding một mình
không chứng minh bounded codec.
Phụ thuộc: P2a; không bật fallback decoder trong UI process.

### P2c — Tile/LOD provider, render, native lifecycle và full-resolution output

**Scope:** raster budget/tile cache/scheduler/evaluator, provider interface trong core/rendering;
`d2d_internal.hpp`, `d2d_render_backend.cpp`, `software_rasterizer.cpp` và desktop composition;
`modules/interop/src/writers/raster_image_writer.cpp`, native raster export workflow.

1. CPU/GPU/disk cache+in-flight+scratch reservations, single-flight, LRU và eviction; mặc định cache
   theo mục 8. Full image vượt texture limit dùng visible tiles, CPU fallback không full-decode.
2. LOD/ROI, Y/stride/affine/clip/interpolation/alpha/halo/seams có independent pixel oracle.
   Pending preview/refinement có nhãn; stale tiles không hiện sau source/graph/profile đổi.
3. Device loss/drop cache chỉ tái dựng derived data; descriptor/source/graph vẫn sống qua pins.
4. Renderer và output dùng cùng evaluator/basis. Writer kéo source+graph full-res theo strip/tile,
   vùng chưa xem vẫn đúng; không upscale proxy hoặc yêu cầu span toàn ảnh.
5. Hoàn tất backend/commands của crop/flip/rotate/Exposure, Linked/Embedded/Save/export theo plan
   ảnh lớn; giữ cùng graph và evaluator. UI/Links/Properties và final Release ở P6 của plan này;
   không nhân đôi graph hoặc mở rộng bộ filter ngoài scope.

**Deliverable/gate:** phần backend của AC-IMP-02/05/06/11/12/14 và
AC-RI-07/08/09/11/12/13/15/21/22: memory bound, full-res output, graph và cache-delete reopen.
Các phần UI/runtime/end-to-end giữ Partial đến P6; toàn AC-RI-01…24 đóng ở P6.
Phụ thuộc: P2b; P3 color contracts phải được dùng trước bật profile.

### P2d — Raster nhúng trong scene vector

**Scope:** PDF/CDR/SVG adapters và common raster sink interface; document import assembly.
Interface `raster_asset_sink.hpp` nếu cần là file mới đề xuất trong interop; implementation/service
được inject từ composition, không buộc core gọi module.

1. Mỗi embedded source/version tạo asset một lần, placements giữ affine/clip/page/layer/order riêng.
2. Source bytes không có từ API phải báo provenance missing, không gắn preview bytes làm original.
3. Migrate embedded image output sang sink/descriptor có backpressure, không buffer mọi raster cùng lúc.
4. Giữ vector scene native; không rasterize whole page hoặc thay ảnh nhúng bằng thumbnail.
   SVG external image không được bật chỉ vì sink mới; fetch policy vẫn deny.

**Deliverable/gate:** AC-IMP-09/13, AC-RI-17 và scene+pixel ROI+shared-asset checks;
DXF/EPS unaffected subset regression. Phụ thuộc: P2c/P1c.

### P3a — Color authority, metadata và cache invalidation

**Scope:** `core/color` profile/preview services; raster decoder and typed records; neutral document
color/source metadata/storage; PDF/EPS/CDR/SVG adapter color boundaries.

1. Thực thi đủ mục 3.3; mã hóa nguồn/preview/display basis và profile hash/generation versioned.
2. Chuẩn hóa channel order/stride/straight alpha trước IPC; ICC/EXIF đúng một lần, giữ source profile.
3. Detect profile missing/invalid/multiple; RGB assumed policy rõ, CMYK/Spot/DeviceN không đoán.
   Source-app RGB render chỉ là reference cho representation được ghi.
4. Color/profile/working/display changes invalidate đúng cache, save/reopen và native legacy defaults
   không double-transform. Geometry/source original không đổi bởi preview color conversion.
5. Những color spaces API không expose cần source-preserving/reference/explicit unsupported outcome;
   không suy ngược original CMYK/Spot từ RGBA.

**Deliverable/gate:** AC-IMP-04/05/13/18 và AC-RI-15; typed metadata từ worker→proposal→document→reopen,
không chỉ decoder unit test. Phụ thuộc: P1c; làm trước cùng lúc P2 integration.

### P3b — Oracle màu/alpha và semantics độc lập

**Scope:** decoder/color/rendering/adapter tests hiện có, fixture catalog/generator an toàn.
Reference outputs lớn/private giữ local; chỉ expected safe fixtures/hash/license được commit.

1. Small analytical charts kiểm kênh/alpha/row/EXIF/interior colors bằng expected bytes tự tính,
   có nền trắng/đen, alpha=0/1/127/254/255, associated/unassociated và minification.
2. ICC charts dùng named source/output profiles và reference CMM/source-app build độc lập;
   CPU==D2D dùng cùng decoded bytes không đủ chứng minh decoder/ICC đúng.
3. Per-fixture chốt metric/tolerance **trước** so kết quả: lossless direct 1:1 exact khi semantics
   exact; premultiply/resampling/JPEG/AA/ICC có tolerance và edge/ROI policy riêng. Không tăng
   tolerance sau failure để che hue/alpha/geometry shift; không đặt một DeltaE/PSNR cho mọi format.
4. Pixel oracle đi cùng coordinates/units/hierarchy/text/fonts/layers/painter order/effect scope.
   Thiếu source-app oracle phải ghi Not run và không nâng appearance verified.
5. Report failures bằng neutral fixture ID/ROI/numeric diff, không đưa artwork riêng lên Git.

**Deliverable/gate:** AC-IMP-01/04/05/09/13 và per-format catalog ready cho P5.
Phụ thuộc: P3a; có thể tạo fixtures/reference ngay từ P0.

### P4 — Reuse cache scene vector và invalidation chính xác

**Scope:** rendering D2D geometry/text/effects cache helpers, spatial culling/damage;
document immutable content generation nếu thiếu; renderer/consumer tests.

1. Audit/reuse object-local geometry đã có, xác định chỗ còn phụ thuộc global revision.
   Key gồm canonical content/style/font/profile/effect/device generation theo resource; không đổi
   geometry/upload chỉ vì pan/zoom, transform placement hoặc sửa object không liên quan.
2. Edit geometry/text/font/paint/opacity/clip/effect/source phải invalidate đúng kết quả và damage
   cả bounds cũ/mới. Undo/Redo/reused IDs không lấy stale resources.
3. LRU/budget riêng theo loại; effect scratch/bounds/halo theo C3/C9, không cắt effect để đạt số.
4. Device recreation/WARP/CPU fallback có pixel/bounds/original authority gate riêng.
5. Direct2D image upload dừng trước ngưỡng 95% RAM; canvas tiếp tục qua CPU renderer từ pixel nguồn đã có,
   không tạo bản copy toàn ảnh mới hoặc đánh dấu import thất bại. Pressure fallback vẫn cần controlled trace.

**Deliverable/gate:** AC-IMP-06/11/13, geometry/decode/upload counters và negative invalidation
tests; performance không đổi semantics. Phụ thuộc: P1c/P3a; hoàn tất sau P2 provider nếu chạm shared resources.

### P5 — Adapter theo hàng, không gom thành một “hỗ trợ mọi vector”

Mỗi subtask bắt buộc: detect exact source representation/version → mapping chỉ feature catalog
→ report phần ngoài catalog → host validation → review → commit → edit/Undo/Redo/native reopen
→ compare source oracle. Sau shared schema đổi, chạy lại consumers thật; unit parser pass chưa đóng subtask.

| Subtask / dependency | Owned paths chính | Đầu ra bắt buộc và negative gate |
| --- | --- | --- |
| P5-SVG; P1c/P2d/P3b/P4 | `svg_importer.cpp`, `svg_import_record_codec.cpp`, `svg_worker_adapter.cpp`; SVG/XML/path helpers và tests | Presentation opacity/fill/stroke đúng scope đi hết IPC→commit; units/viewBox/group transform/text/clip theo catalog. CSS/gradient/mask/filter/group opacity chưa map: report/reject trước commit; DTD/XXE/script/external/cyclic refs không fetch/execute. Không dùng native effect support để giả source mapping |
| P5-DXF; sau SVG | `dxf_source_reader.cpp`, `dxf_entity_parser.cpp`, `dxf_importer.cpp`, `dxf_worker_adapter.cpp` và tests | Giữ LINE/ARC/CIRCLE/LWPOLYLINE, INSUNITS, model-space/layers/BYLAYER/lineweight. ACI7 hiện có nhánh approximate→white: xác định canvas policy và diagnostic, không lẫn TrueColor; unknown entity/missing units reject, không bỏ entity hoặc tự đổi mm. Blocks/INSERT/SPLINE/Dimensions phải là task/capability riêng nếu bổ sung |
| P5-PDF; sau DXF | `pdf_worker_adapter.cpp`, `pdf_page_set_builder.cpp`, `pdf_worker_adapter.hpp` và tests | All pages/blank pages/boxes/rotation 0/90/180/270; fonts identity, paths/text/images, OCG membership đúng khi available. Thiếu font/glyph layout, object shear/clip/blend/ICCBased/CMYK/Spot/DeviceN/overprint phải có scope/outcome. Locked-source exact bytes và preview riêng; không gắn preview thành nguồn in |
| P5-AI; sau PDF | `pdf_ai_extractor.cpp` và PDF adapter/page-set integration, tests | Fixture .ai PDF-compatible riêng; ghi representation PDF, page/artboard semantics được expose. Non-PDF/private data detect unsupported; giữ private-authoring unknown; không hứa Illustrator layers/effects từ PDF pass |
| P5-EPS; sau AI | `eps_worker_adapter.cpp`, converted job/source snapshot paths, tests | Plain allowlist/BoundingBox/units/color/transform exact; malicious/unsupported operators reject nguyên tử. DOS EPS chain ghi Ghostscript build/conversion loss, missing tool/timeout/cancel đúng; no source-app fidelity claim từ interpreter exit 0 |
| P5-CDR; sau EPS | `cdr_worker_adapter.cpp`, `cdr_worker_adapter_geometry.hpp`, `cdr_libcdr_bridge.cpp`; SVG normalizer/sink và tests | RIFF/ZIP catalog versions thật; geometry/text/groups/layer IDs/geometric clips/raster placements không mất. Source fonts/profile/PowerClip/Lens/Mesh/live effects mỗi row riêng; unknown generated SVG reject; no thumbnails. Re-run đúng CDR fixture pressure cũ và loop memory plateau |

Các basename trong bảng thuộc `modules/interop/src/adapters/` trừ header public dưới
`modules/interop/include/pikdraw/interop/adapters/`; tests ở module và `platform/workers/tests`.
Không sửa parser vector ngoài subtask đang làm, trừ schema/sink chung có consumer regression rõ.

**Gate mỗi subtask:** AC-IMP-09/10/13/18 và fixture lifecycle/pixel/structure tương ứng.
P5-CDR còn AC-IMP-17. Không đóng cả P5 khi chỉ 10 adapter tests xanh mà corpus/oracle còn thiếu.

### P6 — Nối UI/output, hồi quy và release

**Scope:** native import/picker/Properties/Links/export surfaces hiện có; action registration/help/icons
theo mục 7; desktop/renderer/storage/worker suites, capability/README/feature specs và snapshot.

1. Thực hiện UI mục 7 chỉ khi backend tương ứng có thật; tất cả entry points gọi cùng actions/commands.
2. Nối full-res raster writer vào source/provider/graph pinned snapshot; output format/profile/precision
   được chọn đúng capability. Đồng bộ matrix/`FormatCapabilities`/UI route, giữ AI/CDR/PDF/EPS/DXF
   source-export unsupported. Failed output không overwrite file cũ.
3. Chạy corpus toàn workflow, low-budget/stale/fault tests, native direct interaction và performance
   Release hardware/WARP/CPU theo mục 8–10. Kiểm app và worker staged cùng build/profile.
4. Cập nhật capability **chỉ đúng hàng đã qua oracle**, README và snapshot ≤8000 byte, không append lịch sử.
   Báo đúng remaining fail/not-run. Dọn output/capture/fixture tạm, giữ reusable regression.
5. Kết thúc theo mục 11; không viết “đã mượt/đúng mọi màu/mọi version/production” dựa trên build pass.

**Gate:** toàn AC-IMP bắt buộc và AC-RI-01…24 của lát cắt ảnh lớn; native lifecycle/output/performance
đúng source snapshot. Phụ thuộc: mọi task trước.

## 6. Corpus, oracle và bằng chứng agent phải tạo

| Nhóm fixture | Mục đích và expected evidence |
| --- | --- |
| IMP-S1 | Hai PNG local đã tái hiện, synthetic 1K color/alpha chart có quyền dùng; original pixels/source/DPI/size, import→transform→undo→reopen |
| IMP-S2 | 4K RGB/RGBA/ICC/EXIF, downscale/rotate/clip, gray/indexed/tRNS; analytical pixel checks và native interaction |
| IMP-R | JPEG baseline/progressive/CMYK/EXIF1–8; TIFF LE/BE/associated/strip/tiled/16-bit/float variants; BMP row layouts; từng codec row có expected outcome |
| IMP-L | Reuse R-GiB/R-BigTIFF/R-Frames/R-Document trong AC-RI; thật ≥1 GiB, offsets >4 GiB, ≥300 TIFF pages, 100 placements/20 sources/multi-tab |
| IMP-V | Mỗi adapter có minimal known-good primitive/text/layer/image scene, mixed affine/units/opacity; real corpus version/app build xác định; stress scene lớn ghi counts riêng |
| IMP-F | Unknown feature/version, missing/invalid profile/font/source, malformed/truncated/CRC/stride/overflow/cycles, worker crash/timeout/cancel/stale/disk-full/device loss |

Manifest mỗi row lưu safe fixture ID/hash/size/dimensions/version/features/oracle IDs/outcome;
private local path chỉ là env/config ngoài Git. File đệm bytes tới 1 GiB không chứng minh large decode.
Benchmark code/fixtures an toàn đặt cạnh suite sở hữu; không tạo test .cpp dùng một lần rải rác.

Oracle phải độc lập với converter đang kiểm: không chỉ so PDFium→PikDraw với PDFium preview rồi
tuyên bố Illustrator/Corel fidelity. Geometry expected unit/coordinates có thể tính analytical;
vendor appearance/structure dùng source app/build thực hoặc reference có nguồn đã xác minh.
Không có môi trường source thì triển khai/kiểm phần độc lập tiếp, giữ row Not run/unknown.

Evidence tối thiểu sau mỗi task: paths thực sự đổi, source/binary/worker hashes, fixture IDs,
AC pass/fail/not-run, exact commands/exit, stage timing và aggregate peak khi task đổi memory/performance.
Ghi trực tiếp trong report ngắn/kế hoạch hiện hành, không tạo lịch sử snapshot hoặc log riêng dữ liệu khách hàng.

## 7. Đặc tả UI để agent không tự đoán lại

Theo chuẩn UI/workspace liên kết ở mục 1. English mặc định, Vietnamese qua catalog; metadata,
tên font/profile/file/units nguồn không dịch hoặc sửa chỉ vì đổi locale.

| Bề mặt | Action/placement và hành vi |
| --- | --- |
| Menu Import | `File > Import…`, `action.file.import`, Ctrl+I hiện có. Open dùng policy dirty-document hiện có; không tạo menu importer riêng |
| Picker | Embedded mặc định; Linked và TIFF page/range chỉ bật khi backend P2 đạt. TIFF một trang không bắt chọn; nhiều trang lazy list. Cancel không tạo document writes |
| Image placement | Ảnh raster đơn vào placement ngay sau worker validation, không chờ hộp thoại review; ghost alpha-preserving bám theo con trỏ, góc trên trái theo tọa độ canvas; status giữ cảnh báo ICC/màu preview. Click trong canvas commit một lần, Esc hủy không sửa document. Import dạng Open giữ luồng mở tài liệu |
| Progress/status | Stage ngắn: chuẩn bị/đọc/kiểm tra/tăng chi tiết; bytes/percent chỉ khi denominator có thật. Cancel/Esc khi import giữ focus có handler; đang nhập số/IME không giành Esc của draft |
| Review | PDF/AI/EPS/CDR và vector subset tiếp tục hiện coverage/appearance/editability/source/profile/loss với Import/Cancel. Raster đơn hiện cảnh báo màu/ICC gọn ở status trước placement vì source bytes/ICC vẫn được lưu; không chặn thao tác bằng modal. Strict unsupported vẫn chặn |
| Toolbar/toolbox | Reuse Import/icon hiện có; không thêm hàng toolbar. Import không là drawing tool. Crop và các source actions theo plan ảnh lớn, không thêm nút giả |
| Property bar | Trong lúc placement chưa mở contextual bar; sau commit, X/Y/W/H/rotation và quick source/crop controls theo selection hiện có. Không nhét metadata/graph dài trên bar |
| Properties | Trong lúc placement chưa sửa field hay selection; sau commit tái dùng right dock. `Object > Properties` với `action.object.properties.focus` hiện có; Window show/hide reuse runtime action thực, không tạo panel thứ hai |
| Image section | Source pixel W/H, bit depth/sample, source/preview ICC basis, EXIF, source/effective DPI, mode/status là read-only metadata; transform fields editable theo selection command |
| Source/Links | Update/Relink/Embed/Unembed/Links theo IDs đề xuất trong plan ảnh lớn: `action.bitmap.update_source`, `action.bitmap.relink_source`, `action.bitmap.embed`, `action.bitmap.unembed`, `action.window.links`. Kiểm collision/registration trước code; tên ở đây chưa xác nhận runtime |
| Export | `File > Export…` / raster route đang có; size/DPI/profile/precision từ capability provider. Preview không là nguồn xuất mặc định; thiếu source thì block output cần nguồn |
| Help | Menu/help entry và tooltip/focus help: cách import→xem ghost→đặt/sửa→Undo→save/reopen; giải thích preview/source, linked missing và format subset. F1 chỉ ghi nếu có binding thật |

**Field/selection contract:**

- Transform X/Y/W/H/angle: selection scope, page units hiện có; scale >0 và finite, angle finite,
  ratio lock theo command hiện tại. Giữ px-source-DPI/EXIF-axis policy; không đổi physical size ngầm.
- Source pixel count/ICC/basis/DPI read-only không tạo Undo. Effective DPI tính từ nguồn/orientation/
  placement, không lấy proxy dimensions. Linked frame/source update là command riêng.
- No selection: show document/import hint; one image: đúng metadata/source; multi images khác nhau:
  Mixed/—, không lấy object đầu giả common; locked/reference-only: không enable edit source geometry;
  pending/refining/missing/unsupported có reason riêng.
- Bar và Properties dùng chung values/validation/mixed/enable/command; Enter/Apply/gesture end tạo
  một Undo, Esc bỏ draft; focus/open panel/reselect không sửa source hoặc tăng revision.
- P0 không cần icon mới. P2 UI cần icon thì reuse hệ SVG; các asset mới được đề xuất trong plan ảnh lớn,
  ghi ID/license/light-dark/focus name, không emoji/brand icon. Không thay design system trong task này.
- Hints ví dụ phải khớp handler: “Import — Place an image or supported vector file”,
  “Move the image to its position · click to place · Esc cancels” / bản Việt tương đương,
  “Preview — Refining image detail”, “Missing source — Relink to continue full-quality export”.
  Không lộ protocol/class/epoch trong luồng thiết kế thường ngày.

**Nghiệm thu trực tiếp:** 1366×768 và 1920×1080; 100/125/150/200% DPI theo thiết bị/môi trường có thể kiểm;
light/dark, Tab/Enter/Esc, focus hiển thị, accessible names, numeric copy/paste/IME, bar hẹp và dock đóng.
Không thu font để ép form; không tự bật lại dock. Thiếu cấu hình nào ghi Not run.

**Luồng thủ công tối thiểu:** mở tài liệu dirty → Ctrl+I/chọn ảnh → quan sát progress và kéo object có sẵn
→ cancel một lần → import lại/review/commit → chọn ảnh/drag/pan/zoom → sửa transform từ bar rồi Properties
→ Undo/Redo → save/close/reopen → xóa derived cache rồi kiểm full-res/export → lặp một scene vector mỗi adapter.
So revision/history/selection/source/geometry trước và sau các điểm hủy/lỗi.

## 8. Phương pháp đo và budget nghiệm thu

Máy tham chiếu: Windows x64, 16 GiB RAM, SSD; ghi CPU/GPU/driver/Windows/toolchain/build/binary hash.
Máy 7.87 GiB hiện tại là profile bổ sung low-memory, không tự giả tương đương máy tham chiếu.
Release hardware/WARP/CPU báo riêng backend thực; không suy hardware pass sang fallback.

| Chỉ số / corpus | Budget mục tiêu và điều kiện |
| --- | --- |
| Warm transform IMP-S1/S2 | Command p95 ≤2 ms; không copy/decode/upload toàn ảnh khi chỉ placement đổi |
| UI đang import/copy/decode/save | Input/heartbeat p95 ≤50 ms, không block >200 ms; Cancel bắt đầu xử lý ≤250 ms |
| Drag/pan/zoom sau import IMP-S1/S2 | UI p95 ≤50 ms, không một block >100 ms; đo input→paint và input→present chỉ khi có present instrumentation thật |
| Render/submission warm | p95 ≤33 ms ở 1920×1080 cho S1/S2 và representative visible scene được ghi counts/segments/glyphs; hardware/WARP/CPU có kết quả riêng; profile chưa đạt giữ unqualified |
| Vector stress | Ghi objects/layers/segments/glyphs/raster/effect counts; stage throughput, peak và UI/cancel budget. Không áp một thời gian mở cố định cho mọi 60k-object AI/CDR |
| Small first useful visible | S1 cold local SSD target p95 ≤3 s; S2 ≤5 s trên máy tham chiếu; placeholder không tính. Đo metadata, decode, prepare, review-wait và commit riêng |
| Large first-visible | Kế thừa AC-RI: tiled TIFF có overview target p95 ≤2 s; sequential codec báo bytes/time thực thay vì ép cùng deadline; progress/cancel vẫn đạt |
| Peak import/first-visible | AC-RI R-GiB target aggregate PrivateBytes delta ≤512 MiB; full cache build cũng nằm trong trace |
| Idle / output RAM | Idle delta target ≤320 MiB với CPU cache mặc định 256 MiB; output target peak delta ≤768 MiB; không dùng cap source để đạt số |
| GPU images | Derived image cache ≤256 MiB; legacy entry cap 1024. Đo resident bytes, transient/in-flight và target/staging riêng; budget không được chỉ đếm map entries |
| Stream copy/hash | Target ≥70% throughput copy+hash độc lập cùng điều kiện, không read-all; embedded hoàn tất vẫn cần I/O theo bytes |

Budget trên là điều kiện đo, chưa phải kết quả đã đạt; không tự sửa budget sau fail.
Nếu fixture/codec cần ngoại lệ có lý do kỹ thuật, ghi đúng profile/trace/variant, giữ yêu cầu source lớn,
bounded RAM và integrity; ngoại lệ chưa được nghiệm thu thì release row chưa đạt.

Lấy ≥5 independent runs; từng run có ≥300 mẫu tương tác sau warmup, báo median/p95/p99,
worst stall và raw timing ngắn, cold/warm riêng. Cold phải định nghĩa app/derived disk/OS cache;
không tự drop OS cache toàn máy. UI-input injection/cancel barriers phải ghi phương pháp.

Memory lấy `max_t(sum PrivateBytes(host + all job workers, t)) - baseline`, sampling 50–100 ms,
đối chiếu reservations/counters; không cộng peak các process ở thời điểm khác nhau.
Working set, file-backed/OS cache, GPU VRAM/shared memory, disk spill và nguồn/Undo pins báo riêng.
Thiếu present instrumentation chỉ gọi render/submission/input-to-paint; không gọi FPS/60 Hz.
Muốn công bố 60 Hz phải có gate presented-frame riêng ≤16.7 ms đúng profile/máy.

## 9. Nghiệm thu ổn định và ánh xạ owner/test

Trạng thái bên dưới là evidence trước bản rà cuối: Partial không phải toàn ca Pass.
Mỗi fixture variant hỗ trợ cần đúng outcome; không nhân mọi tổ hợp khi không đổi rủi ro.

### AC-IMP-01 — Ảnh thật và dữ liệu cơ bản
- **Given:** IMP-S1/S2, source/hash/DPI/orientation và expected pixels độc lập.
- **When:** product import→review→commit→render 1:1.
- **Then:** đúng content/size/units/pixels, không cyan placeholder, không sửa nguồn.
- **Verify:** P0/P3b; desktop import consumer + native canvas; CPU/hardware/WARP, expected pixel chart và source hash.
- **Hiện trạng:** Partial; hai PNG CPU/hardware đã đạt, WARP/independent profile matrix còn mở.

### AC-IMP-02 — Reuse qua thao tác và lịch sử
- **Given:** shared asset, unrelated object và immutable source.
- **When:** ≥300 transform/pan/reselect, unrelated edit, Undo/Redo và duplicate.
- **Then:** stable IDs, source/pixel không copy toàn bộ; không stale/decode/upload full lại vì placement/global revision.
- **Verify:** P0/P2c/P4; rasterizer/cache counters, desktop command consumer, allocation/source-ref trace.
- **Hiện trạng:** Partial; unit sharing/LRU và 24-sample probe có, UI/performance gate chưa đủ.

### AC-IMP-03 — Native lifecycle không mất tối ưu hoặc màu
- **Given:** inline legacy và asset-reference imports, source/profile/report/graph.
- **When:** save→close→delete derived cache→reopen→transform→Undo/Redo.
- **Then:** nguồn/pixel/profile/physical size/basis giữ, asset refs còn, không phụ thuộc temp session.
- **Verify:** P2a/P3a; storage fidelity + desktop consumer, hashes/metadata/full-detail samples.
- **Hiện trạng:** Partial; small PNG save/reopen đã kiểm, streaming/profile/legacy matrix còn mở.

### AC-IMP-04 — Codec/alpha/orientation đúng
- **Given:** IMP-R charts, associated/unassociated alpha, hidden RGB, indexed/tRNS, EXIF1–8 và row layouts.
- **When:** decode→IPC→commit→white/black background render→scale/rotate.
- **Then:** đúng channel/row/sample/alpha/EXIF một lần, không dark fringe/double premultiply.
- **Verify:** P2b/P3b; decoder/raster-worker/rendering pixels, expected bytes độc lập và downscale/seam ROIs.
- **Hiện trạng:** Partial; alpha TIFF/decoder regression có, toàn variant matrix chưa xong.

### AC-IMP-05 — Color basis/ICC không chuyển sai hoặc hai lần
- **Given:** sRGB/wide-gamut/CMYK/missing/invalid/multiple profile fixtures và named output profile.
- **When:** import→profile preview change→edit→native reopen→supported export.
- **Then:** source/profile còn nguyên, một conversion đúng basis; unknown không thành verified, unsupported rõ.
- **Verify:** P3a/P3b; color/adapter+renderer/storage consumers, independent CMM/source-app metric catalog.
- **Hiện trạng:** Open; CPU==D2D không thay gate này.

### AC-IMP-06 — Cache bounded và invalidation
- **Given:** byte/entry budget nhỏ, legacy address reuse, source/graph/font/profile/device changes.
- **When:** render/evict/recreate/Undo.
- **Then:** LRU/owner lifetime đúng, stale result không dùng, không vượt reservations hoặc mất source.
- **Verify:** P2c/P4; rasterizer/cache tests, injected device loss và native backend runs, byte/decode/upload counts.
- **Hiện trạng:** Partial; image LRU/legacy mutation tests có, tile/full device lifecycle còn mở.

### AC-IMP-07 — Unicode và failure outcomes thật
- **Given:** đường dẫn Unicode, missing/truncated/mismatched-signature sources.
- **When:** canonical CLI và UI Open/Import.
- **Then:** đúng file commit khi hợp lệ; lỗi trả nonzero qua CLI, UI báo reason, doc/history không đổi.
- **Verify:** P0/P1a; `main.cpp`/desktop import tests + native capture/objects, exit codes và before/after state.
- **Hiện trạng:** Partial; Unicode success/missing exit 2 đã kiểm, truncated/UI matrix còn mở.

### AC-IMP-08 — Cancel/stale/dirty lifecycle
- **Given:** job ở mỗi boundary và tài liệu dirty; revision/epoch/generation có thể đổi.
- **When:** Cancel/edit/Undo/tab switch/close/reopen, rồi worker result cũ tới.
- **Then:** không partial commit/present, không mất draft/history/dirty/selection, handles/pins trả đúng.
- **Verify:** P1a/P2; deterministic barriers trong writer safety/desktop import/worker integration.
- **Hiện trạng:** Open cho full state matrix của pipeline mới.

### AC-IMP-09 — Vector editable đúng subset
- **Given:** IMP-V từng format/version/representation với structure+appearance oracle.
- **When:** import→select native object→edit geometry/text/layer/image→Undo/Redo→native reopen.
- **Then:** đúng hierarchy/units/order/font/color/geometry và edit thật; không toàn-page thumbnail.
- **Verify:** P2d/P5; adapter+desktop corpus + source-app pixel/structural comparisons.
- **Hiện trạng:** Partial; adapter suite pass, real corpus/appearance chưa hoàn tất.

### AC-IMP-10 — Unsupported và loss trước commit
- **Given:** clip/mask/gradient/blend/effect/font/color space chưa map của đúng version row.
- **When:** review strict hoặc chọn conversion/reference backend đã có.
- **Then:** reject hoặc choice tường minh kèm scope/loss; không normalize/bỏ/flatten/thay font ngầm.
- **Verify:** P1c/P5; negative per-feature fixtures, document before/after, saved report/choice.
- **Hiện trạng:** Partial; một số reject có test, chưa đủ catalog/report semantics.

### AC-IMP-11 — UI/performance có thể tái tạo
- **Given:** Release canonical app/worker và corpus/máy/hash/baseline đã ghi.
- **When:** ≥5 runs, import nền và ≥300 samples drag/pan/zoom/reselect theo mục 8.
- **Then:** đạt budgets đúng profile, cold/warm riêng, không suy offscreen thành FPS.
- **Verify:** P0/P1/P4/P6; native input/heartbeat trace + renderer/OS memory counters.
- **Hiện trạng:** Partial; Debug offscreen probe chưa đóng gate.

### AC-IMP-12 — Nguồn lớn và memory pressure
- **Given:** IMP-L ≥1 GiB, BigTIFF offsets >4 GiB, ≥300 frames và injected low budget.
- **When:** import/zoom/edit/save/reopen/export và cache rebuild.
- **Then:** nguyên bản/full-detail còn, RAM bounded, progress/cancel đạt; không arbitrary source cap/read-all.
- **Verify:** P1b/P2; toàn AC-RI-01…24, dense+sparse boundaries riêng, aggregate trace.
- **Hiện trạng:** Partial; Release decoder đọc sparse BigTIFF uncompressed RGB(A) strip đơn với IFD/pixel offset >4 GiB. Chưa qua worker/desktop vì source-size/chunk/native inline source hiện dùng U32/full-frame; compressed/multistrip/ICC BigTIFF, ≥1 GiB end-to-end; multi-IFD hiện reject an toàn, pressure plateau và document lifecycle còn mở.

### AC-IMP-13 — Canonical semantics đồng nhất
- **Given:** equivalent primitive/text/image/opacity/clip scenes từ adapters khác nhau.
- **When:** shared commands/transform/render/Undo/native storage.
- **Then:** canonical units/axes/winding/order/colors/alpha/bounds cùng expected semantics.
- **Verify:** P3/P4/P5; common semantic vectors + adapter-specific structure/pixel assertions.
- **Hiện trạng:** Partial; cần cross-adapter oracle có scope chính xác.

### AC-IMP-14 — Source, native và export/production là gates riêng
- **Given:** source preserved và target writer có capability chính xác.
- **When:** native reopen rồi supported output; nếu claim source-format round-trip thì source-app reopen.
- **Then:** native/source hash đúng, output full-res/profile/precision đúng; AI/CDR/EPS/PDF/DXF export không tự bật;
  production không assessed vẫn báo đúng.
- **Verify:** P1c/P2a/P2c/P6; storage/writer/source-app output tests; physical/RIP gate riêng nếu công bố sau này.
- **Hiện trạng:** Partial cho small native source retention; source-format/production không được công bố.

### AC-IMP-15 — UI reachable, scope và một Undo
- **Given:** empty/single/multi/mixed/locked/missing/pending selections, dock đóng/bar hẹp.
- **When:** menu/shortcut/bar/Properties, input số/IME, Apply/Esc và reopen panel ở DPI mục 7.
- **Then:** shared validation/value/enable/command; metadata focus không edit; không UI giả hoặc mất draft.
- **Verify:** P1a/P2/P6; native shell/action tests + direct UI workflow/capture, ghi cấu hình chưa thử.
- **Hiện trạng:** Open cho UI mới.

### AC-IMP-16 — Input worker không tin cậy và compatibility
- **Given:** old/current proposal schemas, malformed refs/stride/offset/hash/size/enums và worker faults.
- **When:** host validate/adopt/commit/reopen.
- **Then:** reject trước over-allocation/writer, không partial mutation, cleanup đầy đủ; legacy payload an toàn.
- **Verify:** P1c/P2b; worker v2 integrity, proposal validator, document writer/storage consumers.
- **Hiện trạng:** Open cho contract mới; test cũ cần giữ khi đổi schema.

### AC-IMP-17 — Regression CDR RAM pressure phải được giải quyết
- **Given:** đúng CDR fixture đã fail với baseline/aggregate process trace, và injected pressure tương đương.
- **When:** import một file, lặp corpus và đóng/Undo tài liệu.
- **Then:** owner/buffer amplification được sửa; usage plateau; job đủ budget không bị coi invalid chỉ do % RAM;
  nếu native model không fit phải resource_limit chính xác, không crash/leak/partial commit.
- **Verify:** P1b/P5-CDR/P6; re-run fixture hash cũ, deterministic reservation tests và controlled real run.
- **Hiện trạng:** Fail/open; không đóng bằng đổi máy/fixture hoặc bỏ guard.

### AC-IMP-18 — Claim/report do host cấp, persist đúng
- **Given:** worker tự khai verified hoặc thiếu/lệch catalog/source version/profile/report.
- **When:** host policy/review/commit/native reopen/capability UI.
- **Then:** confidence worker không cấp quyền: host gán unknown trừ khi trusted catalog/hash/oracle row khớp;
  forged source/catalog/hash hoặc enum/schema sai reject proposal. Loss/provenance/choice không mất;
  import/editability/render/output/production không gộp một bool.
- **Verify:** P1c/P3/P5/P6; report consumer/validator/storage/capability regression.
- **Hiện trạng:** Partial; desktop proposal giữ severity/source locator/feature/diagnostic; DXF ACI 7 regression kiểm đủ bốn trường; report native v20 giờ được kiểm serialize/reopen trên snapshot thật sau khi import DXF có ARC/CIRCLE. Validator chuẩn hóa đúng trường hợp adapter lưu thêm `LineTo(start)` ngay trước `Close`; các verb/tọa độ khác vẫn so khớp chặt. Release: geometry/dimension/import CTest 3/3 pass; desktop import 71.16s. Catalog/hash validation, compatibility matrix và kiểm tra trực quan UI còn mở.

### AC-IMP-19 — Raster import đặt theo con trỏ kiểu Corel
- **Given:** tài liệu hiện hành và một ảnh PNG/JPEG/TIFF/BMP hợp lệ sau worker validation.
- **When:** người dùng di chuyển con trỏ trên canvas, click vị trí muốn đặt hoặc nhấn Esc.
- **Then:** không có hộp thoại review chặn placement ảnh raster đơn; bitmap ghost alpha-preserving bám con trỏ với góc trên trái theo tọa độ canvas, giữ tỷ lệ ảnh khi downscale theo viewport, và status nêu rõ màu preview chưa xác minh; click trong canvas commit đúng một image và một bước Undo; Esc không đổi revision, selection hay history. Placement không tự đổi kích thước ảnh theo trang.
- **Verify:** native shell/E2E xác nhận move/status/click/cancel/Undo, DIB bitmap ghost đọc đúng pixel/alpha nguồn (synthetic và logo thật), buffer canvas sau commit hiển thị màu nguồn opaque và alpha-composited; kiểm trực tiếp UI ở DPI/bố cục mục 7.
- **Hiện trạng:** Partial; raster đơn đi thẳng từ worker result sang placement, bỏ hộp thoại modal. Probe tìm thấy và sửa méo tỷ lệ do preview bitmap bị giới hạn riêng theo viewport; Downloads PNG nay tạo ghost 548×411 đúng tỷ lệ từ viewport 548×452. Click/Undo/Esc, source-matched preview pixels và CPU/D2D 1:1 pixel oracle pass; sáu probe Release PNG gốc có median/p95 424.0/532.2 ms, commit 0.116–0.147 ms. UI capture timeout nên visible-window/DPI/Corel comparison vẫn mở.

## 10. Commands chuẩn và cách chọn kiểm tra

Các lệnh là hướng dẫn cho **agent triển khai sau code edit**, không phải kết quả chạy lại trong lần sửa docs này.
Target names đã đối chiếu CMake; vẫn kiểm tên sau reconfigure. Chọn nhóm theo task; chỉ mở rộng khi schema/
shared consumers thay đổi hoặc vào release gate. Không chạy lặp suite không có thay đổi/rủi ro mới.

```powershell
# Sức khỏe và canonical build
.\tooling\pikdraw.ps1 doctor
.\tooling\native\build.ps1

# Raster / storage / rendering
$rasterTargets = @('test_pikdraw_rendering_pixels','test_pikdraw_rendering_rasterizer','test_pikdraw_document_storage','test_pikdraw_document_storage_fidelity','test_pikdraw_interop_raster_image_decoder','test_pikdraw_interop_raster_worker_adapter','test_pikdraw_interop_raster_image_writer')
.\tooling\native\build.ps1 -Test -TestTarget $rasterTargets -TestRegex '^test_pikdraw_(rendering_(pixels|rasterizer)|document_storage(_fidelity)?|interop_raster_(image_decoder|worker_adapter|image_writer))$'

# Vector adapter/worker; không thay source-app fidelity gate
$vectorTargets = @('test_pikdraw_interop_dxf_adapter','test_pikdraw_interop_dxf_worker_adapter','test_pikdraw_interop_svg_adapter','test_pikdraw_interop_svg_worker_adapter','test_pikdraw_interop_pdf_ai_extractor','test_pikdraw_interop_pdf_worker_adapter','test_pikdraw_interop_eps_worker_adapter','test_pikdraw_interop_cdr_worker_adapter','test_pikdraw_worker_dxf_import','test_pikdraw_worker_svg_import')
.\tooling\native\build.ps1 -Test -TestTarget $vectorTargets -TestRegex '^test_pikdraw_(interop_(dxf_(adapter|worker_adapter)|svg_(adapter|worker_adapter)|pdf_ai_extractor|pdf_worker_adapter|eps_worker_adapter|cdr_worker_adapter)|worker_(dxf|svg)_import)$'

# Full desktop corpus: fixture discovery thật, fail/not-run phải báo đúng
.\tooling\native\build.ps1 -Test -TestTarget 'test_pikdraw_desktop_import' -TestRegex '^test_pikdraw_desktop_import$'

# Schema/lifecycle/source capability đổi thì chạy các consumers này
$lifecycleTargets = @('test_pikdraw_document_commands','test_pikdraw_document_writer_safety','test_pikdraw_document_clone_utils','test_pikdraw_worker_v2_integrity','test_pikdraw_worker_raster_import','test_pikdraw_interop_capabilities','test_pikdraw_raster_assets','test_pikdraw_raster_cache')
.\tooling\native\build.ps1 -Test -TestTarget $lifecycleTargets -TestRegex '^test_pikdraw_(document_(commands|writer_safety|clone_utils)|worker_(v2_integrity|raster_import)|interop_capabilities|raster_(assets|cache))$'
```

Raster module tests chỉ build được khi module được include trong dependency/build graph snapshot đó;
nếu chưa include thì nối registry/CMake integration đúng scope, không bỏ test rồi gọi foundation pass.
Codec/scheduler/evaluator tests mới phải đăng ký vào CMake và dùng cùng build runner; không báo target
đề xuất như thể đã tồn tại. Native live containment cần chạy khi thay source/cache/output grants,
theo C6; process/Job Object test không thay containment evidence.

Probe file riêng không nhúng tên/path vào plan:

```powershell
$env:PIKDRAW_RASTER_REGRESSION_SOURCE = Read-Host 'Đường dẫn ảnh local cần kiểm'
& '.\build\win-dev\apps\desktop\test_pikdraw_desktop_import.exe' --raster-probe
$probeExit = $LASTEXITCODE
Remove-Item Env:PIKDRAW_RASTER_REGRESSION_SOURCE
if ($probeExit -ne 0) { throw "Raster probe failed: $probeExit" }
```

CLI headless có review dùng `--headless --accept-import-loss` tường minh **chỉ cho smoke test đó**;
không bypass host validation/normal UI và không chứng minh visual/editable fidelity.

Release cùng build tree/canonical output, không tạo binary thứ hai để né workflow:

```powershell
.\tooling\native\build.ps1 -Configuration Release -Test -TestTarget @('pikdraw','test_pikdraw_desktop_import','test_pikdraw_rendering_pixels','test_pikdraw_rendering_rasterizer') -TestRegex '^(test_pikdraw_desktop_import|test_pikdraw_rendering_pixels|test_pikdraw_rendering_rasterizer)$'
```

Chạy native interaction/benchmark sau build này; nếu đổi lại Debug phải build qua runner và ghi build type
mới. Không chạy test binary stale làm evidence cho source vừa sửa. Large/cold corpus có timeout/label riêng,
không nhét file 5 GiB vào timeout desktop suite nhỏ rồi kết luận thuật toán lỗi.

## 11. Checklist kết thúc và prompt giao agent

Chỉ đánh dấu **release đạt phạm vi** khi:

- [ ] P0 fixes còn nguyên, IMP-S1/S2 nội dung/pixels/source/native lifecycle đạt trên backend được công bố.
- [ ] P1 lifecycle, provenance/report host policy và malformed/legacy/cancel/stale/fault gates đạt.
- [ ] Lỗi CDR pressure cũ có root cause+fix+retest đúng fixture; không leak/false-invalid/partial commit.
- [ ] P2 hoàn tất source/stream codec/tile/native/output integration; AC-RI-01…24 được báo từng ca.
- [ ] P3 independent color/alpha/ICC oracle đạt từng enabled row, không double conversion hoặc giả source color.
- [ ] P4 reuse/invalidation/device lifecycle đạt, không lấy tốc độ bằng mất effect/quality.
- [ ] P5 từng adapter có version/representation/feature rows + source oracle/structure/edit/lifecycle;
  variant ngoài catalog có exact unsupported outcome/negative test.
- [ ] Native UI trực tiếp, Properties/bar/help/accessibility và budgets Release có evidence mục 7–8.
- [ ] Full desktop corpus và affected shared-consumer tests đạt; missing source-app/device/fixture ghi Not run,
  giữ row unqualified và **không** gọi cả gate hoàn tất.
- [ ] Capability/docs phản ánh đúng snapshot, source hashes/report tái tạo được; project-state ≤8000 byte;
  temp output được dọn, reusable regression giữ lại, local edits ngoài task không bị gộp/ghi đè.

Nếu chỉ implementation/test module xong nhưng còn oracle/runtime/fixture thiếu, báo
**“đã triển khai phần X; chưa hoàn tất gate Y”** với bước tiếp theo cụ thể; tiếp tục các việc độc lập.
Không đổi Not run thành Pass, không dùng một unsupported fallback mới để né feature đã cam kết.
Source-format writers và physical production ngoài scope giữ unsupported/not assessed, không là blockers
giả buộc phải viết cả Illustrator/Corel để hoàn tất declared subset.

**Prompt thực thi:**

> Triển khai kế hoạch này tại source gốc theo P0→P1a/b/c→P2/P3/P4→P5 từng adapter→P6.
> Đọc diff/scope AGENTS và source capability hiện tại trước mỗi module; reuse các fixes và raster foundation
> đã có, không tạo worktree/branch/lease. Giữ AC-IMP-01…18 và AC-RI-01…24, tách source/preview và từng fidelity
> axis, giữ one-writer atomic import/Undo/stable IDs/units/Unicode/original metadata.
> Mỗi task phải có đầu ra, regression/consumer gate và evidence đúng binary; không dừng ở plan/prototype/
> build pass/thumbnail/source-cap removal. Thay whole-file/full-frame bằng source streaming/tile/budget trước
> bỏ caps, sửa CDR pressure regression, hoàn tất native save/reopen/full-resolution export và UI mượt.
> Color và vector feature chỉ bật theo exact catalog+independent oracle; missing/unsupported phải rõ trước
> commit. Agent tự quyết lựa chọn kỹ thuật/UI thường lệ trong scope, không chờ duyệt form.
> Báo paths, AC pass/fail/not-run, commands/exit, timings/aggregate memory và giới hạn thật; giữ local edits.

## 12. Tài liệu nền

- [Ảnh lớn: kiến trúc/codec/asset/edit/output](raster-import-large-assets-20261021.md) và các tài liệu con.
- [Color roadmap](color-system-advancement-plan-20261020.md): dùng lại profile/conversion foundation,
  không biến task import thành triển khai mọi proof/separation capability.
- [Direct2D resource reuse](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance)
  và [WIC pixel/alpha semantics](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-native-pixel-formats):
  nguồn API/semantics, không phải evidence performance/fidelity của PikDraw.
