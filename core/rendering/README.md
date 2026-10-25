# PikDraw rendering

Trạng thái runtime Windows: `ViewportRenderer` chọn `D2DRenderBackend` khi Direct2D/D3D11 hardware hoặc WARP khởi tạo được; nếu không, runtime dùng `SoftwareRasterizer`. Mặc định NativeWindow vẫn render PixelBuffer, đọc staging về BGRA rồi dùng Win32/GDI trình bày. Chế độ thử nghiệm `PIKDRAW_DIRECT_PRESENT=1` nối HWND DXGI swap chain vào NativeWindow theo lớp GDI chrome → D2D canvas → GDI canvas overlays/handles → GDI modal/menu/popup/tooltip. Smoke ảnh trước đó phát hiện canvas che modal; đã tách và chuyển handle + topmost UI thành các lớp GDI sau D2D, canonical Release đã build/relink. App khởi động/đóng bình thường nhưng Computer Use không chụp được ảnh mới do trusted RPC chưa cấu hình, nên fix chưa có xác nhận hình. Chưa có visible-present/FPS hoặc overlay parity acceptance.

| Tính năng | API | Implementation | Test |
| --- | --- | --- | --- |
| Camera mm/binary64, local projection | `camera.hpp` | Header | `tests/camera`, pixel validation |
| Direct2D 1.1 GPU backend & WARP | `d2d_backend.hpp` | `src/gpu/d2d/`; HWND swap-chain prototype là opt-in | `test_pikdraw_swap_chain`, `test_pikdraw_swap_chain_warp`, `test_pikdraw_swap_chain_benchmark`, `tests/rasterizer` |
| Spatial Index (BVH) culling | `spatial_index.hpp` | D2D backend dùng BVH cho snapshot trên 256 đối tượng; snapshot nhỏ dùng culling tuyến tính | `tests/culling`, rasterizer benchmark |
| Damage Tracking (Dirty Rect) | `damage_tracker.hpp` | NativeWindow hợp nhất bounds cũ/mới; vùng dirty nhỏ chỉ cập nhật phần RGBA→BGRA/GDI cache tương ứng | `tests/culling`, native shell paint |
| Painter order, layer visibility, conservative stroke culling | `culling.hpp` | Header | `tests/culling`, pixel semantics |
| RGBA8/stride/source-over | `frame_target.hpp` | Header | pixel semantics |
| Curve LOD riêng nguồn, fill NonZero/EvenOdd, stroke | `software_rasterizer.hpp` | `src/raster/`, `software_rasterizer.cpp` | pixel semantics, rasterizer |
| Genuine glyph outline consumer | same | `software_rasterizer.cpp` | text-backend test |
| Backend retry orchestration | `renderer.hpp` | `renderer.cpp` | injected status state machine |

Renderer đọc snapshot bất biến. `query_visible_refs` trả con trỏ mượn, chỉ hợp lệ trong lifetime snapshot; frame không sao chép geometry. Compatibility `query_visible` vẫn trả bản sao. Layer đầu/vật thể đầu là phía sau, không sắp theo object ID. BVH giữ thứ tự painter; `DamageTracker` tính hợp nhất bounds cũ/mới. Dirty-rect cache chỉ giảm chuyển đổi và chép vùng vào Win32 bitmap. Mặc định Direct2D NativeWindow vẫn dựng và readback toàn frame; chế độ swap-chain opt-in bỏ readback cho frame canvas nhưng chưa có GPU partial-render hoặc tile cache.

Path nguồn không đổi. Bézier được chia thích nghi theo sai số màn hình 0,25 px; fill dùng tâm pixel, đóng ngầm subpath và giữ winding/lỗ. Stroke dùng độ rộng mm trong local space rồi transform, round caps/joins và union coverage trước alpha. Chưa có thuộc tính dash/miter/cap selector trong document; không tuyên bố hỗ trợ các thuộc tính đó. Preview RGB/CMYK/spot alternate phụ thuộc module color, không phải proof ICC/overprint.

Path của D2D hỗ trợ line, quadratic/cubic Bézier và arc, transform cha/con, fill solid/linear/radial gradient, text qua glyph outlines thật khi `PIKDRAW_TYPOGRAPHY_REAL_OUTLINES` được bật, bitmap từ `ImageObject::pixels_rgba`, và duyệt `GroupObject`. Gaussian Blur, Drop Shadow và object blend dùng Direct2D effects; shadow có blend mode riêng khác Normal hiện trả lỗi rõ ràng. Text layout/outline tính mỗi frame, chưa có cache theo font backend generation. Backend lỗi hoặc font không hỗ trợ trả `false`, không vẽ block giả. `RenderMetric` mang diagnostics kèm object ID/error/message; caller không được coi frame lỗi là đầy đủ. Pixel buffer có thể chứa phần frame đã vẽ khi backend báo lỗi.

`SoftwareRasterizer` là CPU fallback có chủ đích và golden reference cho kiểm tra pixel; mặc định chỉ được chọn nếu D2D khởi tạo thất bại, hoặc khi `PIKDRAW_RENDER_BACKEND=cpu`. Lỗi tài nguyên/render sau khi D2D đã bắt đầu được trả về caller, không phát lại frame qua CPU.

Giới hạn CPU để tránh treo/overflow: tối đa 8.388.608 pixel/frame, mỗi chiều ≤ 16.384; tọa độ màn hình tuyệt đối ≤ 10^12; 200.000 node/điểm dẫn xuất mỗi path; depth Bézier ≤ 24; ≤ 20 triệu edge-row intersections hoặc stroke candidate pixels mỗi path. Vượt budget trả lỗi, không giảm chất lượng âm thầm. Những giới hạn này là guardrail, chưa phải performance acceptance hoặc cam kết 60 FPS; chưa có scheduler frame deadline/cancellation. Target không sở hữu buffer: caller phải cấp ít nhất stride × height byte. Device status injection chỉ kiểm thử state machine, không mô phỏng driver crash hoặc chứng minh recoverability thật.

Build/test tại root qua runner chuẩn `.\tooling\native\build.ps1 -Configuration Release -Test -TestTarget test_pikdraw_rendering_rasterizer -TestRegex '^(test_pikdraw_rendering_rasterizer|test_pikdraw_swap_chain|test_pikdraw_swap_chain_warp|test_pikdraw_swap_chain_benchmark)$' -VerboseTests` (dùng chung `build/win-dev`). Pixel regression, swap-chain correctness và backend benchmark chạy ở Release.

Dependencies: document, geometry, color, typography. Không có Qt/UI/graphics handles trong source model. C3 camera rotation/DPR, Qt/Skia, GPU partial-render, tile cache, full overlay parity, reference-hardware input-to-present latency và performance ngoài máy benchmark hiện tại chưa được thực hiện/đo; không suy ra từ các pixel/test harness pass.
