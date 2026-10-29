# WS3 — Rendering pipeline và GPU

Owned paths: `core/rendering/**`, `apps/desktop/src/shell/native_window_canvas.cpp`,
`apps/desktop/src/shell/native_window_direct_present.cpp`. Không sửa `native_window_events.cpp` (WS4).

Contract: [C3 Rendering](../../../contracts/c3-rendering.md), [C9 Appearance](../../../contracts/c9-appearance-effects.md).
Kế hoạch nền: [P2 trong roadmap hiệu năng](../performance-storage-automation.md).

Quy tắc đo: chạy benchmark rendering trước và sau (W1/W2/W3, Release, cùng máy), ghi p50/p95 ngắn vào roadmap
hiệu năng; offscreen timing không phải FPS hiển thị. Pixel output phải khớp oracle `SoftwareRasterizer` theo test hiện có.

Test chung: `test_pikdraw_rendering_rasterizer` (kiểm tên), `test_pikdraw_swap_chain`, `test_pikdraw_swap_chain_warp`,
`test_pikdraw_effect_render_warning`, `test_pikdraw_canvas_view`, `test_pikdraw_desktop_shell`.

---

## WS3-01 [Critical] [x] Cache geometry D2D bị xóa 100% sau mỗi edit

- Bằng chứng (đã xác minh): `core/rendering/src/gpu/d2d/d2d_geometry_cache.cpp:22` xóa entry có
  `revision < current_revision`; backend dùng `snapshot.revision()` (toàn tài liệu) làm khóa.
- Cách sửa:
  1. Đổi khóa thành identity nội dung của object: con trỏ `shared_ptr<const Entry>` trong `PersistentObjectMap`
     (bất biến, đổi khi object đổi) hoặc per-object content revision nếu document có. Giữ `kind`, `even_odd`, ID.
  2. Giữ `shared_ptr`/weak ref để tránh ABA khi địa chỉ được tái dùng (lưu `weak_ptr` và so sánh `lock()`).
  3. Eviction theo LRU frame-age + byte/entry budget, không theo revision toàn cục.
- Test: unit test cache — sửa 1 trong N object chỉ tạo lại 1 geometry (expose counter `created_count()` cho test);
  pan/zoom không tạo geometry mới; Undo tái dùng geometry cũ.
- Nghiệm thu: benchmark W1 "edit 1 object rồi render" giảm rõ; pixel oracle không đổi.

## WS3-02 [Critical] [x] Cache geometry text là biến cục bộ trên stack

- Bằng chứng (đã xác minh): `d2d_render_backend.cpp:702` `d2d::D2DGeometryCache text_geometry;`.
- Cách sửa: dùng `geometry_cache` của `Impl` với khóa theo WS3-01; tra cache **trước** khi gọi `obj.layout()`
  và `text_to_path_outlines()`. Text-on-path dùng khóa gồm cả identity path tham chiếu.
- Test: render 2 frame liên tiếp cùng snapshot → `layout` không chạy lần hai (counter test); pixel khớp.

## WS3-03 [High] [ ] Hình học thủ tục được đánh giá lại mỗi frame trước khi tra cache

- Bằng chứng (cần xác minh dòng): `d2d_render_backend.cpp` khoảng `:436` contour, `:468-513` dimension,
  `:537-581` annotation, `:617-639` detail view (duyệt đệ quy scene).
- Cách sửa: tạo cache "derived geometry" khóa theo identity object + (với detail view) revision của tập nguồn bị
  tham chiếu; chỉ evaluate khi miss. Detail view phụ thuộc nguồn khác → khóa phải gồm identity của các object nguồn
  hoặc revision snapshot (chấp nhận invalidation khi document đổi, nhưng không invalidation khi pan/zoom).
- Test: pan 30 frame không gọi evaluate (counter); sửa object nguồn của detail view làm detail view cập nhật.

## WS3-04 [Critical] [x] SpatialIndex rebuild mỗi nấc zoom và không cull Group/Image/Text

- Bằng chứng (đã xác minh): `core/rendering/src/culling/spatial_index.cpp:59` so `cached_zoom`;
  `:109` truyền zoom vào bounds; `culling.hpp` `world_bounds_for_object` trả bounds không hợp lệ cho Image/Text/Group,
  `query_visible_refs` luôn include khi bounds không hợp lệ (cần xác minh dòng `:385-436`).
- Cách sửa:
  1. Build BVH bằng bounds world không phụ thuộc zoom; cộng margin pixel (`2.0 / zoom`, stroke hairline) vào
     rect truy vấn lúc query. Bỏ `cached_zoom` khỏi `matches()`.
  2. Lấy bounds Group/Image/Text từ `SceneHierarchy::world_bounds()` (core/document đã tính); chỉ fallback conservative
     khi bounds thật sự không xác định.
  3. Cull trong đệ quy group (`d2d_render_backend.cpp` khoảng `:883-895`, `software_rasterizer.cpp` khoảng `:621-628`):
     bỏ qua child có world bounds ngoài viewport (giữ clip/PowerClip đúng).
  4. Bounds stroke/effect halo phải bao trọn vùng vẽ (effect blur/shadow mở rộng bounds) — kiểm C3/C9.
- Test: `test_pikdraw_canvas_view`/culling tests — zoom không rebuild (counter); text/image ngoài viewport không
  được render; object chỉ có shadow lấn vào viewport vẫn được render; pixel oracle ở nhiều zoom.

## WS3-05 [High] [x] `render_gpu` thiếu `image_cache.begin_frame()`

- Bằng chứng (đã xác minh): `begin_frame()` chỉ ở `d2d_render_backend.cpp:1016` (đường pixel buffer).
- Cách sửa: gọi `image_cache.begin_frame()` ở đầu `render_gpu`, cùng chỗ `geometry_cache.begin_frame`.
- Test: swap-chain test render >120 frame không dùng ảnh → ảnh bị evict theo frame-age.

## WS3-06 [High] [ ] Readback + unpremultiply đơn luồng toàn khung

- Bằng chứng (cần xác minh dòng): `d2d_render_backend.cpp` khoảng `:1076-1152` map staging rồi chia `/alpha` từng pixel;
  desktop lại chuyển sang GDI.
- Cách sửa (từng bước):
  1. Nếu consumer là GDI canvas: trả BGRA premultiplied trực tiếp (nền canvas đục nên không cần unpremultiply) qua
     một target type riêng; chỉ unpremultiply cho export/oracle.
  2. Khi vẫn cần straight alpha: bỏ qua pixel alpha 0/255 nhanh; dùng LUT reciprocal hoặc SSE2/AVX2.
- Test: oracle pixel; benchmark readback p95 trước/sau trên viewport 800×600 và 2560×1440.

## WS3-07 [High] [x] Effect GPU chỉ có Blur/Shadow Normal; 5 effect còn lại làm hỏng frame

- Bằng chứng (cần xác minh): `d2d_effects_pipeline.cpp:244-277` trả lỗi cho Feather/OuterGlow/InnerGlow/InnerShadow/
  ColorOverlay và DropShadow blend khác Normal; `render_to_pixel_buffer`/`render_gpu` fail frame khi có diagnostic lỗi.
- Cách sửa:
  1. Ngắn hạn: fallback từng object — rasterize object đó bằng `ObjectCompositor` vào bitmap tạm, upload và vẽ; không
     hủy cả frame. Có budget pixel (`kMaxEffectSurfacePixels`).
  2. Dài hạn: graph D2D built-in (Morphology, GaussianBlur, Flood, Composite, ArithmeticComposite) cho từng effect,
     so pixel với CPU compositor trong tolerance định nghĩa ở C9.
- Test: `test_pikdraw_effect_render_warning` + test mới: document có cả 7 effect render được trên GPU, không có banner lỗi;
  so pixel với oracle trong tolerance.

## WS3-08 [Medium] [ ] Tạo `ID2D1DeviceContext` mỗi object có effect mỗi frame

- Bằng chứng (cần xác minh): `d2d_document_effects.cpp:32-40` khi `document_raster_dpi > 0`.
- Cách sửa: tái dùng một context phụ + pool bitmap theo kích thước; cache kết quả effect theo identity object + scale.
- Test: counter số context tạo/frame = 0 sau warm-up.

## WS3-09 [Medium] [ ] Blend mode khác Normal copy toàn render target mỗi object

- Bằng chứng (cần xác minh): `d2d_scene_compositor.cpp:27-132` EndDraw/BeginDraw + CopyFromBitmap toàn viewport.
- Cách sửa: giới hạn backdrop copy trong bounds object (∩ viewport); tái dùng bitmap backdrop theo kích thước.
- Test: pixel oracle blend modes; benchmark scene 50 object Multiply.

## WS3-10 [High] [ ] `SoftwareRasterizer` thiếu `contour:v1` và `dimension:v1`

- Bằng chứng (cần xác minh): D2D xử lý 4 loại metadata thủ tục (`d2d_render_backend.cpp:429-654`), còn
  `software_rasterizer.cpp:227-458` và `object_compositor.cpp:597-740` chỉ có `detail_view:v1`, `annotation:v1`.
- Cách sửa: trích logic chuẩn bị geometry thủ tục thành hàm dùng chung trong `core/rendering/src/` (ví dụ
  `procedural_geometry.cpp`) trả `Path2D` + text outlines; cả hai backend và compositor gọi chung. Đây cũng là nền
  cho cache WS3-03.
- Test: oracle pixel D2D vs CPU cho contour và dimension, có và không có effect.

## WS3-11 [Medium] [ ] `render_path_with_appearance` tính mỗi pixel hai lần

- Bằng chứng (cần xác minh): `object_compositor.cpp:530-591`, viewport > 262144 px tắt cache output, vòng 1 chỉ để
  kiểm `wrote_any_pixel`.
- Cách sửa: gộp một vòng — blend trực tiếp và đặt cờ; nếu cần rollback khi không ghi gì, không cần (không ghi = không đổi target).
- Test: oracle pixel; benchmark effect CPU.

## WS3-12 [Medium] [ ] `DamageTracker` cập nhật nhưng không dùng

- Bằng chứng (cần xác minh): `native_window_canvas.cpp` khoảng `:113-118`, `:278-286`; `damage_tracker.cpp:101-167` O(N)/edit.
- Cách sửa (chọn sau khi đo):
  a. Dùng damage rect để clip `render_frame` + `StretchDIBits` vùng bẩn (cần renderer hỗ trợ partial render với
     clip và giữ nội dung cũ ngoài vùng), hoặc
  b. Nếu chưa làm partial render trong đợt này: tắt cập nhật tracker để không tốn O(N) vô ích, ghi giới hạn trong C3.
  Damage tính từ touched IDs của transaction thay vì quét toàn page.
- Test: thay đổi 1 object → vùng render ⊆ bounds cũ ∪ mới (+halo); pixel cuối khớp full render.

## WS3-13 [High] [ ] Các lỗi của `PIKDRAW_DIRECT_PRESENT=1`

- Bằng chứng (cần xác minh dòng):
  - `native_window_direct_present.cpp:67-162` lấy `GetDC/ReleaseDC` ba lần mỗi frame, pass 1 vẫn BitBlt toàn cửa sổ.
  - `render_gpu` xóa `canvas_rect` (`d2d_render_backend.cpp` khoảng `:1447`) làm mất page shadow/border/artboard outline
    do GDI vẽ trước.
  - `native_window_canvas.cpp:270` `canvas_cache_valid_ = frame_rendered && !direct_present_frame_` → mọi repaint UI
    (hover, caret 500 ms) render lại scene; eyedropper không có pixel.
  - `render_gpu` gọi `GetBuffer` + `CreateBitmapFromDxgiSurface` mỗi frame (khoảng `:1400-1413`).
- Cách sửa:
  1. Vẽ page shadow/border/artboard overview bằng D2D trong `render_gpu` (hoặc vẽ GDI sau D2D trong pass overlay).
  2. Giữ `ID2D1Bitmap1` canvas offscreen cache; khi snapshot/camera không đổi chỉ composite cache + overlay.
  3. Cache target bitmap của back buffer theo buffer index; tạo lại khi resize/device-lost.
  4. Eyedropper: đọc pixel 1×1 từ bitmap cache khi cần (CPU_READ nhỏ), không readback toàn khung.
  5. Gom GDI chrome thành một lần GetDC nếu thứ tự lớp cho phép (chrome → canvas D2D → overlay → topmost).
- Test: `test_pikdraw_swap_chain*`; test mới chụp pixel vùng page border khi direct present; hover chrome không
  tăng counter render scene. Visible capture thủ công ghi rõ là chưa đo FPS.
- Nghiệm thu: chỉ bàn đến bật mặc định sau khi pass W1/W2 visible-present theo P2.
