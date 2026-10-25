# 04 — Nghiệm thu file lớn, hiệu năng và tính đúng

[Mục lục](../raster-import-large-assets-20261021.md). Tất cả case dưới đây là **yêu cầu chưa chạy cho kiến trúc mới**, không lấy kết quả 5/5 của prototype làm bằng chứng thay thế.

## 1. Corpus và môi trường

Giữ fixture manifest/hash/generator/version/license trong nơi test hiện có; file GiB nằm ngoài Git ở thư mục acceptance riêng. File thực chứa dữ liệu ảnh cần xử lý; file được đệm trailing bytes tới 1 GiB chỉ là test transport, không chứng minh large-image decode. Không dùng dữ liệu khách hàng làm fixture công khai.

| Nhóm | Bộ dữ liệu tối thiểu có lý do |
| --- | --- |
| R-Small | PNG indexed/tRNS/RGBA/gray; JPEG RGB/CMYK/EXIF 1–8; TIFF LE/BE, tiled/strip, DPI và ICC có/không/malformed; BMP top-down/bottom-up |
| R-Resolution | 24/100/600 MP theo khả năng từng format; source encoded nhỏ nhưng decoded vượt 192 MiB; họa tiết/alpha/chi tiết vượt độ phân giải preview |
| R-GiB | TIFF tiled/strip hợp lệ **encoded ≥1 GiB**, pixels giải nén nhiều GiB; PNG/JPEG lớn thực có thể tạo/thu thập hợp lệ theo giới hạn codec, ghi đúng source bytes và pixels |
| R-BigTIFF | BigTIFF có IFD/tile data thực được đọc ở offset >4 GiB; một fixture sparse để bắt lỗi U32 và một file dense lớn trên máy acceptance đủ đĩa để đo throughput thực |
| R-Codecs | JPEG baseline/progressive, PNG non-interlaced/Adam7/16-bit, TIFF single-strip lớn và tiled/strip LZW/Deflate/JPEG theo backend được pin, grayscale/RGB/CMYK/float có gating riêng |
| R-Frames | TIFF ≥300 trang, chọn trang 300; frame có kích thước/DPI khác nhau, lazy enumeration, không decode tất cả để lấy một trang |
| R-Document | Một nguồn dùng 100 placements, 20 nguồn lớn riêng, document đa tab, effects/clip/rotation và mixed vector/raster |
| R-Vector | Corpus CDR/PDF/AI PDF-compatible/EPS đang có; asset nhúng lặp và affine; SVG image chỉ khi capability đó đã bật; DXF geometry regression |
| R-Fault | Truncated nguồn, bad ICC/IFD cycles, corrupt tile CRC, worker crash/timeout, disk full, cancel, source change, close/reopen, device loss và low-budget |

Máy chuẩn benchmark: ghi CPU/RAM/GPU/driver/SSD/Windows build, display resolution/DPI, toolchain/build type và executable hash. Mục tiêu số bên dưới lấy máy x64 16 GiB RAM + SSD làm mốc. Chạy WARP và CPU riêng; không suy số hardware sang fallback.

## 2. Cách đo và mục tiêu

Đây là **ngân sách mục tiêu để nghiệm thu**, không phải số đã đạt và không phải quota từ chối file.

| Chỉ số | Cách đo / mục tiêu ban đầu |
| --- | --- |
| UI phản hồi | Heartbeat/input handling khi đang metadata/copy/decode/save: p95 ≤50 ms, không một block UI >200 ms trong thao tác chuẩn; Cancel bắt đầu xử lý ≤250 ms |
| Thời gian first-visible | Bắt đầu user chọn file → preview ảnh thật có thể nhận diện. TIFF tiled có overview/cache phù hợp: mục tiêu p95 ≤2 s ở cold local SSD; placeholder không tính là preview |
| Trường hợp sequential | PNG/strip TIFF/progressive không có overview có thể phải đọc nhiều/toàn nguồn; báo thời gian và bytes đọc trước preview, không áp mục tiêu 2 s sai bản chất codec. UI/progress/cancel vẫn phải đạt |
| Peak RAM first-visible/import | `max_t(sum PrivateBytes(host + mọi worker của job, t)) - baseline` mục tiêu ≤512 MiB trên corpus R-GiB chuẩn; đo cả full cache build, không chỉ trước decode xong |
| RAM sau hoàn tất | Idle delta host+workers mục tiêu ≤320 MiB với default CPU cache 256 MiB; thêm source/placements không tạo độ dốc tuyến tính theo N byte toàn ảnh |
| Edit/export RAM | Mục tiêu peak tăng thêm ≤768 MiB trên output nhiều GiB của corpus chuẩn; phải giải thích scratch/halo/codec nếu điều chỉnh ngân sách, không điều chỉnh bằng cách cap source |
| GPU image memory | Default derived image tile cache ≤256 MiB theo resident-resource accounting; ghi riêng staging/render target và GPU shared/system memory; OOM/device loss không mất source |
| Streaming copy/hash | Managed embed cùng thiết bị mục tiêu ≥70% throughput copy+hash độc lập cùng input/điều kiện; không giữ toàn file. Thời gian hoàn tất tăng theo bytes I/O là hợp lý |
| Warm reuse | Transform/pan/reselect không làm decode/upload lại toàn asset. Đo cache hit/miss, bytes đọc và số tiles; không chỉ nhìn stopwatch |
| Zoom/pan | CPU render/submission p95 mục tiêu ≤33 ms ở viewport 1920×1080 cho corpus chuẩn; nếu không đo presented frames thì không gọi con số đó là FPS |

Lấy ít nhất 5 lần cho hồ sơ so sánh tiêu chuẩn, báo median/p95 và raw timings ngắn. Cold/warm phải định nghĩa rõ (app cache, derived disk cache, OS cache). Không tự drop global OS cache hoặc ăn RAM toàn hệ thống trên máy người dùng. Phép đo low-memory dùng injectable budget trước, nghiệm thu pressure thật trong môi trường có kiểm soát.

Private Bytes là commit riêng, working set là RAM resident; file-backed pages/OS file cache và VRAM phải báo riêng. Không cộng peak riêng từng process ở thời điểm khác nhau để giả thành aggregate peak; lấy tổng theo mẫu thời gian 50–100 ms và ghi interval/khả năng bỏ lỡ spike. Internal counters/reservations cần đối chiếu OS metrics; Job Object limit không phải số RAM đã đo.

Vendor comparison nếu có Corel/Illustrator hợp lệ: cùng file/source dimensions, cùng máy, mode Linked/Embedded, cùng view/output scale, cold/warm tương đương; tách metadata, placement, embed completion và output. Không có ứng dụng/máy so sánh thì ghi “chưa đo parity”, không thay bằng con số trên mạng.

## 3. Các ca nghiệm thu ổn định

### AC-RI-01 — Baseline và khả năng tái tạo

- **Given:** source/binary/worker hash, fixture manifest và máy chuẩn đã ghi; prototype hiện tại còn caps.
- **When:** đo import và phase timings trên corpus nhỏ/lớn ở mode thực sự chạy được.
- **Then:** báo chính xác failure/cap hiện tại, memory peak và pipeline stages; không gọi file bị từ chối là pass performance.
- **Verify:** benchmark runner/native telemetry + OS process counters; lưu report ngắn và command/fixture IDs, không append số liệu vào project-state.

### AC-RI-02 — File nguồn ≥1 GiB mở được bằng cả hai mode

- **Given:** TIFF nguồn encoded ≥1 GiB với chi tiết biết trước; Linked và Embedded.
- **When:** import qua entry point sản phẩm, đặt ảnh, zoom vào vùng ngoài preview.
- **Then:** không bị chặn bởi 128/192 MiB hoặc ngưỡng byte khác thay thế; source dimensions/chi tiết đúng, thao tác được và RAM không theo tổng source bytes.
- **Verify:** desktop integration/acceptance runner thật, query object/source metadata + cropped pixel oracle + aggregate memory trace. Embedded phải hoàn tất self-contained, không chỉ hiện proxy.

### AC-RI-03 — Nguồn và native offsets vượt 4 GiB

- **Given:** BigTIFF có tile tại offset >4 GiB và native embedded chứa blob/index vượt U32.
- **When:** seek/decode tile đó, save, đóng và reopen.
- **Then:** exact source bytes/hash, tile pixels và graph còn đúng, không truncate/cast về U32 hoặc read-all vector.
- **Verify:** worker I/O + native streaming storage test với sparse fixture cho boundary; dense fixture riêng kiểm I/O/RAM thật. Cả hai kết quả phải được ghi, không lấy sparse làm benchmark.

### AC-RI-04 — Decode đúng đặc tính từng codec và trang TIFF lớn

- **Given:** baseline/progressive JPEG, PNG Adam7/16-bit, TIFF tiled/single-strip và TIFF 300 trang.
- **When:** probe/preview/full cache, chọn trang 300.
- **Then:** sample/layout/frame đúng; không frame-limit 256; không lặp full decode cho từng tile; progressive coefficient storage được tính/spill đúng.
- **Verify:** mở rộng decoder/worker suite hiện có + codec memory/bytes-read instrumentation; pixel oracle độc lập. Variant chưa hỗ trợ phải report riêng, không tính pass cho cả nhóm.

### AC-RI-05 — First-visible trước full copy/hash và nguồn bất biến

- **Given:** nguồn lớn được pin và managed Embedded import.
- **When:** đọc metadata/preview trong lúc copy/hash nền chưa hoàn tất; thử sửa/thay file nguồn cạnh tranh.
- **Then:** preview đến từ đúng pinned version; partial hash không được dùng content-dedup; initial placement có backing sống, không trỏ temp chết.
- **Verify:** deterministic I/O barriers trong worker/broker integration, event ordering và version/digest assertions; thử thay cùng size/mtime để không chỉ dựa fingerprint yếu.

### AC-RI-06 — Embedded tự chứa, cache có thể xóa

- **Given:** Embedded copy hoàn tất, đã crop/adjust và save.
- **When:** đóng app, đổi tên/xóa nguồn ngoài trong fixture, xóa derived cache, reopen rồi zoom/export.
- **Then:** original trong native/store được dựng lại, chi tiết full-res và edits còn; không cần file nguồn cũ hoặc temp session.
- **Verify:** native storage + desktop import consumer và full-res sampled output, source hash trước/sau bằng nhau.

### AC-RI-07 — Duplicate và sửa placement không nhân bản nguồn

- **Given:** một asset lớn và 100 ImageObjects cùng version.
- **When:** duplicate, move/scale một object, chỉnh Exposure riêng rồi Undo/Redo.
- **Then:** một authoritative blob; không 100 pixel/source vectors; object IDs độc lập, edit không lan ngầm; transform không clear toàn GPU image cache.
- **Verify:** asset/ref counts, native blob index, memory plateau và decode/upload counters qua document/renderer consumer.

### AC-RI-08 — Tile/LOD/seams và giới hạn texture

- **Given:** ảnh rộng hơn device maximum bitmap size, pattern/alpha quanh tile boundaries, affine/clip.
- **When:** pan/zoom qua các mức LOD và render CPU/D2D.
- **Then:** vùng nhìn đúng, không đường nối/mất hàng/đảo Y, không cần texture toàn ảnh; edge interpolation/halo đúng.
- **Verify:** mở rộng rendering pixel/rasterizer tests, so ROI với full reference nhỏ độc lập ở các seam/LOD; real large-frame native capture riêng.

### AC-RI-09 — Bộ nhớ có giới hạn xuyên nhiều assets/jobs

- **Given:** nhiều tab và nhiều nguồn lớn, prefetch và cache build đang chạy.
- **When:** pan qua ảnh, đổi selection, import lặp và hạ budget.
- **Then:** eviction/spill/backpressure hoạt động; tổng CPU scratch/in-flight/cache được tính; source/authoritative deltas không bị evict; RAM không tăng theo tổng toàn ảnh.
- **Verify:** scheduler/reservation tests + OS aggregate trace ở R-Document; đối chiếu outstanding jobs/pins và disk cache footprint.

### AC-RI-10 — UI, hủy và pressure

- **Given:** đang import/copy/decode file lớn; RAM vật lý đạt ngưỡng 95%.
- **When:** import cần cấp phát thêm bộ nhớ.
- **Then:** job giữ trạng thái chờ, không đánh dấu nguồn lỗi; tiếp tục khi headroom đủ hoặc timeout sau 5 phút cộng dồn, vẫn nhận lệnh hủy của người dùng.
- **Verify:** desktop workflow/heartbeat timings + injected pressure/reservation; nghiệm thu pressure thật riêng, không tự bơm RAM nguy hiểm trong test mặc định.

### AC-RI-11 — Stale result và vòng đời document

- **Given:** các tile/metadata/job đang chờ, sau đó relink/đổi graph/close/reopen và generation thay đổi.
- **When:** kết quả cũ hoàn tất, kể cả sau khi window/tab được thay.
- **Then:** không commit/present sai ảnh, không use-after-free; transform không đổi source vẫn reuse đúng cache khi phù hợp.
- **Verify:** barriers deterministic trong writer/async và raster scheduler consumer; kiểm object/asset revision, graph hash, event adoption và pin cleanup.

### AC-RI-12 — Exposure sửa lại cùng node

- **Given:** một ảnh có Exposure v1 đã apply.
- **When:** clear selection, chọn lại, mở Exposure, đổi EV, Apply và Undo/Redo.
- **Then:** giữ node instance ID/count/order, revision chỉ đổi khi commit; pixel ra theo công thức linear exposure/alpha, source hash giữ nguyên.
- **Verify:** document command/graph + tile evaluator + native UI consumer; oracle điểm ảnh tính độc lập, không chỉ kiểm field tồn tại.

### AC-RI-13 — Crop, EXIF và trạng thái đang refine

- **Given:** ảnh EXIF 90/270 độ, crop chứa chi tiết vượt proxy và đang decode nền.
- **When:** crop/flip/rotate, Esc một draft rồi Apply draft khác; reselect và Undo.
- **Then:** crop theo source basis đúng, không áp orientation hai lần, physical bounds/clip đúng; dữ liệu ngoài crop còn phục hồi được.
- **Verify:** graph/geometry/pixel assertions + thao tác native; test source→crop→placement transform và save/reopen.

### AC-RI-14 — Save/Recovery atomic và revision R/R+1

- **Given:** Embedded còn staging, save snapshot R, có edit R+1 và bản native cũ hợp lệ.
- **When:** Save/Recovery hoàn tất hoặc fault injection disk-full/cancel/crash giữa manifest/blob/replace.
- **Then:** file cũ còn hợp lệ hoặc file mới tự chứa đầy đủ; không Saved giả, không mất dirty R+1; recovery không phụ thuộc handle process cũ.
- **Verify:** mở rộng storage/save-recovery và writer safety tests; kiểm raw reopened source/graph và orphan/temporary cleanup.

### AC-RI-15 — ICC, alpha và sample precision

- **Given:** RGBA/tRNS, hidden RGB alpha=0, RGB/gray 16-bit, source ICC và EXIF fixtures.
- **When:** preview, đổi LOD, Exposure/crop, save/reopen và export theo mode được hỗ trợ.
- **Then:** không double ICC/EXIF/premultiply; alpha và sample precision đúng; source channels/ICC không bị rewrite bởi screen conversion.
- **Verify:** ICC/profile/decoder/rendering oracle và independent crop samples. CMYK/float/separations chỉ claim những variant có assertion và reference phù hợp.

### AC-RI-16 — Output worker sai và arithmetic 64 bit

- **Given:** malformed descriptors, forged handles/asset IDs, truncated/duplicate tiles, offset+length overflow, stride/layout sai và IFD cycles.
- **When:** host nhận metadata/cache/tile proposal.
- **Then:** reject trước allocate/adopt quá budget; không mutate doc hoặc mở path worker gửi; giữ UI hoạt động và trả reservations.
- **Verify:** mở rộng `test_pikdraw_worker_v2_integrity`/raster worker tests và validator tests hiện có; không chỉ fuzz parser mà bỏ host consumer.

### AC-RI-17 — Raster nhúng không phá scene vector

- **Given:** CDR/PDF/AI PDF-compatible có ảnh nhúng lặp, layers/transforms và các feature subset đã hỗ trợ; DXF/EPS regression.
- **When:** import qua adapter riêng vào shared asset sink, edit một raster placement và save/reopen.
- **Then:** vector geometry/hierarchy/IDs/units còn, ảnh lặp chia sẻ asset, metadata nguồn đúng; không dùng thumbnail toàn page thay scene.
- **Verify:** desktop corpus + adapter structural assertions/pixel ROI, báo coverage/appearance/editability/round-trip riêng theo C8. SVG ảnh ngoài không tự được bật nếu chưa có gate.

### AC-RI-18 — Cache/source capability không mở rộng sandbox

- **Given:** source read handle và cache/output capability cụ thể; canary file/network/child-process không được cấp.
- **When:** decoder/cache builder cố truy cập canary, path traversal hoặc handle ngoài job.
- **Then:** bị từ chối trong sandbox đã chọn; không cần quyền toàn thư mục nguồn để stream.
- **Verify:** live containment regression theo grants mới; Job Object/worker process pass không thay bằng chứng này.

### AC-RI-19 — Legacy và version mới

- **Given:** native các version đã hỗ trợ, gồm v18 hiện tại với paint ICC/units/path specs/raster inline.
- **When:** mở qua migration, lưu version container mới và mở lại.
- **Then:** source/geometry/profile/graph mặc định giữ đúng; asset lớn chuyển theo stream; old reader reject version mới rõ; unknown required nodes không bị bỏ.
- **Verify:** storage fidelity fixtures và clone/validation regressions; chọn version mới theo source thực lúc triển khai, không hardcode giả định v19 còn trống.

### AC-RI-20 — Linked, relink, embed, unembed và package

- **Given:** Linked source bị đổi/mất và image có crop/Exposure/placement transform.
- **When:** Update/Relink, Undo, Embed/Unembed và Package sang folder khác.
- **Then:** stable object/graph IDs và bounds mm giữ; source version đổi qua command; incompatible mask/edit có lựa chọn rõ, không tự bỏ; package mở được với relative links.
- **Verify:** desktop source workflow + native reopen trên vị trí mới, source hashes/graph/relative path assertions; Edit Original chỉ chạy sau user action.

### AC-RI-21 — Hardware/WARP/CPU và device loss

- **Given:** tile cache đang dùng trên các backend khả dụng.
- **When:** ép device loss/fallback, chuyển render path rồi pan/zoom.
- **Then:** texture tái dựng theo budget, source/graph còn, CPU không decode full frame; không claim driver-crash recovery từ lỗi device-loss mô phỏng.
- **Verify:** renderer integration + runtime trên hardware/WARP/CPU, ghi backend thực/driver và trường hợp chưa chạy.

### AC-RI-22 — Xuất chất lượng đầy đủ sau sửa

- **Given:** cache chỉ có preview/ít visible tiles, nguồn nhiều GiB và graph crop/Exposure.
- **When:** export TIFF/BigTIFF/JPEG theo format/output size/profile được hỗ trợ.
- **Then:** pixels lấy từ source+graph đủ độ phân giải, metadata output đúng, RAM bounded, không upscale proxy; output lỗi không thay file cũ.
- **Verify:** writer/export integration, independent pixel crops ở vùng chưa decode trước đó, 16-bit fixture khi capability bật; JPEG loss có tolerance riêng, không đòi byte-identical re-encode.

### AC-RI-23 — UI có đường truy cập và trạng thái thật

- **Given:** các menu/action/bar/Properties/Links ở English/Vietnamese, mixed/missing/staging/ready selections.
- **When:** thao tác bằng keyboard/mouse ở các kích thước/DPI trong spec, mở lại node và đóng panel.
- **Then:** cùng validation/value/command/Undo giữa bề mặt; focus/help/disabled rõ; không panel tự bật, không nút chưa có handler, không thông báo Saved/Full quality giả.
- **Verify:** native shell tests + nghiệm thu UI thực; semantic action/structure queries ưu tiên, screenshots chỉ bổ sung layout. Ghi rõ resolution/scale chưa thử.

### AC-RI-24 — Gate hiệu năng và workflow trọn vẹn

- **Given:** canonical app/worker build sau P0–P6 và corpus/hardware đã ghi.
- **When:** import→move/scale→crop/Exposure→save→close→xóa cache→reopen→export trên R-GiB/R-BigTIFF/R-Document.
- **Then:** đạt mục tiêu mục 2 hoặc có điều chỉnh budget được chứng minh bằng trace, giữ yêu cầu mở file lớn và toàn vẹn; không dùng source cap/preview-only để đạt số.
- **Verify:** report median/p95/RAM/VRAM/disk và danh sách AC; so sánh vendor nếu có môi trường tương đương, thiếu thì ghi chưa đo. Không phát biểu “không tốn RAM”.

## 4. Nơi đặt test và lệnh

Ưu tiên mở rộng `modules/interop/tests/adapters/test_raster_image_decoder.cpp`, `test_raster_worker_adapter.cpp`; `platform/workers/tests/raster_worker_import_tests.cpp`, `v2_integrity_tests.cpp`; document model/commands/snapshots/storage tests; rendering pixel/rasterizer suite; `apps/desktop/tests/test_svg_dxf_import.cpp`. Module raster mới chưa có suite nên được tạo các suite bền vững theo asset/scheduler/edit, không tạo một file `.cpp` tạm cho từng AC rồi để rải rác.

Các command dưới là target đã tồn tại ở audit; agent chạy sau edit đúng phạm vi. Targets mới như `test_pikdraw_raster_assets`, `test_pikdraw_raster_scheduler`, `test_pikdraw_raster_edits` phải đăng ký CMake trước, không báo đã có sẵn.

```powershell
.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_interop_raster_image_decoder','test_pikdraw_interop_raster_worker_adapter','test_pikdraw_interop_capabilities','test_pikdraw_worker_raster_import','test_pikdraw_worker_v2_integrity') -TestRegex '^(test_pikdraw_interop_raster_image_decoder|test_pikdraw_interop_raster_worker_adapter|test_pikdraw_interop_capabilities|test_pikdraw_worker_raster_import|test_pikdraw_worker_v2_integrity)$'

.\tooling\native\build.ps1 -Test -TestTarget @('test_pikdraw_document_commands','test_pikdraw_document_storage','test_pikdraw_document_storage_fidelity','test_pikdraw_document_writer_safety','test_pikdraw_document_clone_utils') -TestRegex '^(test_pikdraw_document_commands|test_pikdraw_document_storage|test_pikdraw_document_storage_fidelity|test_pikdraw_document_writer_safety|test_pikdraw_document_clone_utils)$'

.\tooling\native\build.ps1 -Test -TestTarget @('pikdraw','test_pikdraw_desktop_import','test_pikdraw_rendering_pixels','test_pikdraw_rendering_rasterizer','test_pikdraw_interop_raster_image_writer') -TestRegex '^(test_pikdraw_desktop_import|test_pikdraw_rendering_pixels|test_pikdraw_rendering_rasterizer|test_pikdraw_interop_raster_image_writer)$'
```

Không khởi chạy thêm build khi shared native lock còn active; không xóa lock của process khác. Large/cold benchmark chạy riêng suite nhanh, label/timeout rõ để không gộp 5 GiB vào desktop test timeout 300 s rồi hiểu timeout là thuật toán lỗi. Thiếu fixture phải báo **not run**, không skip rồi gọi toàn bộ AC pass.

Nếu dùng CLI headless để smoke import có fidelity review, `--headless --accept-import-loss` chỉ là lựa chọn tường minh cho test đó; không bypass validation và không chứng minh native UI/fidelity. Quét target/test list trước khi chạy vì agent khác có thể thêm/đổi CMake.

## 5. Báo cáo hoàn thành ngắn, đủ bằng chứng

Ghi các path đổi, schemas/version/codec builds, AC pass/fail/not-run, exact commands, corpus IDs, stage timings và peak host+worker/GPU/disk. Giữ một report benchmark tái tạo được và regression có ích; dọn ảnh/output/fixture tạm. Không đưa dữ liệu khách hàng, đống log hoặc lịch sử số đo vào project-state.

Chỉ nâng `modules/raster` từ planned sau khi implementation tích hợp và tests/runtime tương ứng có thật. Chỉ ghi “file lớn được hỗ trợ” với format/variant đã kiểm; giữ nguyên các giới hạn fidelity/output/production chưa chứng minh. Khi bàn giao, user không phải đọc mọi log để biết còn gate nào chưa đạt.
