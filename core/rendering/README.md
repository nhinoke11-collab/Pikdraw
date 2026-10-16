# PikDraw rendering

Trạng thái: CPU renderer có kiểm thử tính đúng đắn; chưa nghiệm thu hiệu năng M1 hoặc GPU.

| Tính năng | API | Implementation | Test |
| --- | --- | --- | --- |
| Camera mm/binary64, local projection | `camera.hpp` | Header | `tests/camera`, pixel validation |
| Painter order, layer visibility, conservative stroke culling | `culling.hpp` | Header | `tests/culling`, pixel semantics |
| RGBA8/stride/source-over | `frame_target.hpp` | Header | pixel semantics |
| Curve LOD riêng nguồn, fill NonZero/EvenOdd, stroke | `software_rasterizer.hpp` | `src/raster/`, `software_rasterizer.cpp` | pixel semantics, rasterizer |
| Genuine glyph outline consumer | same | `software_rasterizer.cpp` | text-backend test |
| Backend retry orchestration | `renderer.hpp` | `renderer.cpp` | injected status state machine |

Renderer đọc snapshot bất biến. `query_visible_refs` trả con trỏ mượn, chỉ hợp lệ trong lifetime snapshot; frame không sao chép geometry. Compatibility `query_visible` vẫn trả bản sao. Layer đầu/vật thể đầu là phía sau, không sắp theo object ID. Bounds là query tuyến tính bảo thủ, chưa có R-tree/BVH hoặc tile cache.

Path nguồn không đổi. Bézier được chia thích nghi theo sai số màn hình 0,25 px; fill dùng tâm pixel, đóng ngầm subpath và giữ winding/lỗ. Stroke dùng độ rộng mm trong local space rồi transform, round caps/joins và union coverage trước alpha. Chưa có thuộc tính dash/miter/cap selector trong document; không tuyên bố hỗ trợ các thuộc tính đó. Preview RGB/CMYK/spot alternate phụ thuộc module color, không phải proof ICC/overprint.

Text chỉ thành công khi typography cung cấp `PIKDRAW_TYPOGRAPHY_REAL_OUTLINES` cùng backend thật; gọi layout/outline public API, đổi baseline y-down sang source +Y-up. Text layout/outline hiện được tính mỗi frame, chưa có cache khóa theo font backend generation; chưa cam kết hiệu năng text. Backend lỗi/font unsupported trả `false`; không vẽ block giả. Arc trực tiếp, group/hierarchical transform chưa triển khai: visible object trả `false`, không đếm như đã render. RenderMetric có diagnostics kèm object ID/error/message, tối đa 64 chi tiết và một summary; font fallback giữ source descriptor và báo face thực. Caller phải hiển thị lỗi/cảnh báo và không nhận frame thất bại như đầy đủ. Pixel buffer có thể chứa phần frame đã vẽ khi trả lỗi.

Giới hạn CPU để tránh treo/overflow: tối đa 8.388.608 pixel/frame, mỗi chiều ≤ 16.384; tọa độ màn hình tuyệt đối ≤ 10^12; 200.000 node/điểm dẫn xuất mỗi path; depth Bézier ≤ 24; ≤ 20 triệu edge-row intersections hoặc stroke candidate pixels mỗi path. Vượt budget trả lỗi, không giảm chất lượng âm thầm. Những giới hạn này là guardrail, chưa phải performance acceptance hoặc cam kết 60 FPS; chưa có scheduler frame deadline/cancellation. Target không sở hữu buffer: caller phải cấp ít nhất stride × height byte. Device status injection chỉ kiểm thử state machine, không mô phỏng driver crash hoặc chứng minh recoverability thật.

Build/test tại root với toolchain CMake/Ninja được khám phá bởi `tooling/native/env.ps1`: configure Debug với tests ON, build targets `test_pikdraw_rendering_*`, chạy `ctest --test-dir build/render-audit -R test_pikdraw_rendering --output-on-failure`. Pixel regression dùng check runtime còn hiệu lực ở Release. Bộ test legacy vẫn dùng assert; không dùng chúng một mình để nghiệm thu Release.

Dependencies: document, geometry, color, typography. Không có Qt/UI/graphics handles trong source model. C3 rotation/DPR, GPU/Skia, damage rectangles, cache/invalidation, hierarchy và reference-hardware present/input latency vẫn chưa thực hiện/đo; không ghi M1 complete từ các test CPU này.
