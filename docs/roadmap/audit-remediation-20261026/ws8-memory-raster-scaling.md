# WS8 — Bộ nhớ, raster, typography và co giãn phần cứng

Owned paths: `modules/raster/src/**`, `core/typography/src/shaping/**`, `core/color/src/preview/**`,
`core/document/src/storage/save_recovery.cpp` và phần load của storage (phối hợp WS1/WS2),
`core/rendering/src/gpu/d2d/d2d_device_manager.cpp`, `d2d_internal.hpp` (budget — phối hợp WS3).

Đọc trước: [P3/P5 roadmap hiệu năng](../performance-storage-automation.md), [PR format](../../architecture/native-pr-format.md),
[raster large assets](../raster-import-large-assets-20261021.md).

Mọi hạng mục ở đây là Medium: cần benchmark trước/sau (Release), ghi peak RSS/private bytes khi liên quan bộ nhớ.

Test chung: `test_pikdraw_raster_evaluator`, `test_pikdraw_raster_preview_service`, `test_pikdraw_raster_cache`,
`test_pikdraw_raster_histogram_service`, `test_pikdraw_typography`, `test_pikdraw_icc_preview`,
`test_pikdraw_document_storage`, `test_pikdraw_save_recovery`.

---

## WS8-01 [Medium] [ ] Open `.pr` giữ đồng thời file, payload giải nén và snapshot

- Bằng chứng (cần xác minh): `save_recovery.cpp:124-167` đọc toàn file vào vector; `native_format.cpp` decode toàn payload
  rồi parse trong khi hai buffer còn sống.
- Cách sửa từng bước:
  1. Giải phóng `bytes` ngay sau khi inflate xong (move vào scope nhỏ) — giảm một bản sao đỉnh.
  2. Reader đọc payload qua interface `ByteSource` chunk-by-chunk (inflate theo chunk 1 MiB khi parser cần) thay vì
     vector liền khối; giữ API `deserialize(data, len)` cho consumer cũ.
  3. Đo peak RSS với fixture lớn (`PIKDRAW_TEST_LARGE_NATIVE_CONTAINER=1`).
- Nghiệm thu: peak RSS khi open giảm ≥ một lần kích thước payload; corrupt/cancel không publish một phần.

## WS8-02 [Medium] [ ] `RasterTileCache` chưa nối vào evaluator

- Bằng chứng (cần xác minh): `tile_cache.cpp:56-140` có LRU; `evaluator.cpp:1430-1518` và `edit_preview_service.cpp:328-516`
  xử lý full-frame buffer.
- Cách sửa: evaluator nhận vùng yêu cầu (viewport ∩ ảnh, theo LOD); xử lý theo tile với halo cho node có lân cận
  (bilinear, clone stamp); cache tile kết quả khóa theo (asset, graph hash, LOD, tile index).
- Test: output tile ghép = output full-frame (byte-exact hoặc tolerance định nghĩa); cancel giữa tile nhả bộ nhớ.

## WS8-03 [Medium] [x] Evaluator raster scalar đơn luồng, `std::pow` mỗi kênh mỗi node

- Bằng chứng (cần xác minh): `evaluator.cpp:247-257` (`srgb_to_linear`/`linear_to_srgb` dùng `std::pow` double), mỗi node
  một vòng toàn ảnh (`:344-1112`, `apply_graph` `:1293-1426`).
- Cách sửa:
  1. LUT 256 (U8) và 65536 hoặc nội suy (U16) cho chuyển sRGB↔linear.
  2. Hợp nhất các node pointwise liên tiếp thành một pass trong không gian linear (chỉ chuyển đổi đầu/cuối).
  3. Chia hàng/tile song song trên thread pool có giới hạn (số luồng = min(logical-1, cấu hình)); giữ cancel mỗi 256 px.
  4. (Pha sau) D2D custom effect/HLSL cho preview viewport; CPU vẫn là oracle/export.
- Test: kết quả khớp phiên bản cũ trong tolerance ±1 LSB (U8)/định nghĩa cho U16; benchmark ảnh 24 MP.

## WS8-04 [Medium] [ ] `ProfileColorTransform` khóa mutex, đơn luồng

- Bằng chứng (cần xác minh): `profile_color_transform.cpp:260,380` `lock_guard` quanh `TranslateColors`.
- Cách sửa: kiểm WCS transform có thread-safe không; nếu không, pool transform theo thread (mỗi worker một handle) để
  chuyển song song theo block 8192 px. Preview viewport dài hạn: 3D LUT (33³) sinh từ transform, áp bằng GPU/CPU SIMD.
- Test: `test_pikdraw_icc_preview`, `test_pikdraw_raster_profile_conversion` — kết quả song song = tuần tự.

## WS8-05 [Medium] [x] Shaper tạo lại font collection/font face mỗi dòng

- Bằng chứng (cần xác minh): `core/typography/src/shaping/shaper.cpp:74-152` — `GetSystemFontCollection`,
  `FindFamilyName`, `CreateFontFace`, `CreateTextFormat` mỗi lần; locale cứng `vi-vn` (`:138`).
- Cách sửa: cache system collection (làm mới khi font hệ thống đổi), cache `IDWriteFontFace` theo (family, weight, style,
  stretch, file path) có giới hạn entry; locale lấy từ thuộc tính text/document, mặc định `vi-vn`.
- Test: shape 1000 dòng cùng font → font face tạo 1 lần (counter); kết quả glyph không đổi.

## WS8-06 [Medium] [x] Budget bộ nhớ tĩnh không co giãn theo máy

- Bằng chứng (cần xác minh): preview service 512+192 MiB (`native_window.cpp:855-857`), histogram 256 MiB
  (`histogram_service.cpp:18`), D2D image cache 256 MiB (`d2d_internal.hpp:75`).
- Cách sửa: module `MemoryBudgetPolicy` (trong `core/` hoặc `platform/`, không phụ thuộc desktop) tính từ
  `GlobalMemoryStatusEx().ullTotalPhys` và VRAM (`IDXGIAdapter::GetDesc` `DedicatedVideoMemory`): ví dụ tổng cache ≤ 15% RAM,
  image cache ≤ 25% VRAM (min/max kẹp). Desktop truyền budget vào service khi tạo. Có override qua settings/env cho test.
- Test: policy unit test với RAM 4/8/32/128 GiB và VRAM 0 (WARP)/2/24 GiB cho kết quả trong kẹp.

Tiến độ WS8-01: `SaveRecoveryService::load` map file thay cho vector chứa thêm toàn bộ container,
giảm một bản sao heap của input. Deserializer vẫn giữ payload giải nén và snapshot trong RAM;
streaming parse/lazy assets cùng peak-RSS evidence vẫn còn mở.

Triển khai hiện tại dùng `document/resources/MemoryBudgetPolicy` chung cho preview, histogram
và D2D cache/upload. Không còn các trần mặc định 512/192/256 MiB: tổng reservation theo 1/4 RAM,
cache theo 1/16 RAM, admission xét RAM/commit/address space hiện thời và chừa 5% RAM. D2D xét
thêm ngân sách DXGI local, WARP dùng RAM. Cache preview nằm trong working reservation; pixel
còn được renderer giữ sau eviction vẫn giữ reservation. Nguồn đo có thể inject để test pressure,
recovery và cấu hình 4/8/32/128 GiB mà không cấp phát lượng RAM giả lập. Đây là hoàn thành logic;
chưa đo peak RAM/FPS trước-sau trên các máy thật của từng phân khúc. Readback/GDI (WS3-06),
streaming open (WS8-01) và tile evaluation (WS8-02) vẫn là các phần riêng còn mở. WARP hiện
tự chọn HWND swap-chain direct-present theo mặc định để tránh full-frame staging readback/GDI
khi đường này hoạt động; fallback GDI vẫn còn và cần visible/runtime evidence.

## WS8-07 [Medium] [ ] D2D factory single-threaded, không bật multithread optimizations

- Bằng chứng (cần xác minh): `d2d_device_manager.cpp:127,140`.
- Cách sửa: đánh giá `D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS` (an toàn với một context dùng từ
  một thread) bằng benchmark W1/W2/W3; chỉ đổi factory sang multithreaded khi có nhu cầu dựng geometry trên worker.
- Nghiệm thu: bật chỉ khi p95 cải thiện và oracle pixel không đổi.

## WS8-08 [Low] [ ] Giới hạn canvas 33.554.432 px trên màn 8K

- Bằng chứng (cần xác minh): `native_window_canvas.cpp:167-168` từ chối render khi canvas vượt ngưỡng.
- Cách sửa: thay vì từ chối, render theo dải/tile hoặc giảm scale preview có nhãn; đường direct present (WS3-13) không
  cần buffer CPU nên không bị giới hạn này. Phối hợp WS3 (file thuộc WS3).

## WS8-09 [Low] [ ] Boolean hình chữ nhật cũ có gán lỗ theo bbox-center

- Bằng chứng (cần xác minh): `core/geometry/src/operations/boolean.cpp:54-205` `boolean_rectangles` và gán lỗ O(L²)
  bằng tâm bbox. Boolean tổng quát dùng Clipper2 ở `boolean_polygon.cpp`.
- Cách sửa: tìm call site; nếu không còn dùng thì xóa; nếu còn, chuyển sang đường Clipper2 hoặc dùng point-in-polygon
  thực cho gán lỗ.
- Test: `pikdraw_geometry_tests` — lõm bao bbox của contour rời được gán đúng.

## WS8-10 [Low] [ ] Boolean đồng bộ trên UI thread không cancel

- Bằng chứng (cần xác minh): `boolean_polygon.cpp:248-351` tới 1.000.000 điểm, không cancellation.
- Cách sửa: thêm cancellation token + progress vào API; desktop chạy boolean lớn (trên ngưỡng điểm) qua job nền rồi commit
  theo revision.
- Test: boolean lớn hủy được; kết quả nhỏ vẫn đồng bộ, không đổi hành vi.
