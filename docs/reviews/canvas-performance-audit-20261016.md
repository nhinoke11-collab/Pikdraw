# Kiểm tra hiệu năng canvas và độ trễ thao tác

Ngày kiểm tra: 2026-10-16  
Phạm vi: audit mã và bằng chứng hiệu năng đã lưu; không sửa runtime code.  
Base task: `11512046d23fac08059ecd37382179283b4530a0`; working tree đã có nhiều thay đổi kế thừa.

## Kết luận

- **FPS/present thật: chưa đo.** Không có present interval hoặc sự kiện input-to-visible trên cửa sổ desktop đang hiển thị. Không thể kết luận app đạt hay không đạt FPS thực tế chỉ từ thời gian CPU.
- **Redraw CPU: chưa đạt budget đã đề ra.** Bằng chứng Release gần nhất ghi source redraw p95 **35,42–57,05 ms** qua ba lượt, với 1.000 path/101.000 path elements. Mức này vượt budget software interaction 33,3 ms và budget GPU 60 Hz 16,7 ms. Nghịch đảo thời gian tương ứng khoảng 17,5–28,2 redraw/giây, chỉ là thông lượng CPU của trace, **không phải FPS màn hình**.
- **Cache-hit native paint:** p95 **10,46–14,09 ms** trong benchmark gọi `WM_PAINT` trên cửa sổ ẩn. Nó đo CPU paint/blit, không đo desktop compositor hay present.
- **GPU, peak RAM/working set, CPU utilization và input-to-visible: chưa đo.** Renderer tích hợp hiện là software CPU; GDI có thể được Windows compositor xử lý, nhưng điều đó không chứng minh renderer dùng GPU.
- **Rủi ro thuật toán chính:** mỗi source redraw quét tuyến tính scene; pan/zoom làm đổi camera nên cache nguồn bị dựng lại; renderer chạy đồng bộ trong đường `WM_PAINT` của UI thread. Hover và region selection cũng chưa có spatial index.

## Đường xử lý canvas hiện tại

1. `WM_MOUSEMOVE`/wheel/pan cập nhật interaction hoặc camera và gọi `InvalidateRect` cho toàn cửa sổ.
2. `WM_PAINT` gọi `NativeWindow::paint` đồng bộ. Hàm này vẽ vào một backbuffer GDI giữ lại theo kích thước cửa sổ.
3. `paint_document_canvas` kiểm tra cache theo snapshot/revision, camera, kích thước, profile và theme. Nếu cache cần dựng lại, nó gọi `DocumentWorkflow::render_snapshot` ngay trong paint.
4. Software renderer ghi RGBA, desktop đổi RGBA sang BGRA bằng một vòng lặp CPU, đưa buffer vào bitmap canvas qua `StretchDIBits`, rồi dùng `BitBlt` để vẽ canvas và copy backbuffer ra window DC.
5. Khi chỉ đổi hover/selection overlay mà source/camera không đổi, source bitmap được tái dùng; tuy vậy canvas và phần còn lại của cửa sổ vẫn được vẽ lại.

Các vị trí source: `WM_PAINT` ở `apps/desktop/src/shell/native_window_events.cpp`; cache, chuyển màu và blit ở `apps/desktop/src/shell/native_window_canvas.cpp`; backbuffer và composition ở `apps/desktop/src/shell/native_window_paint.cpp`; giới hạn target ở `apps/desktop/src/workflows/document_workflow.cpp` và `core/rendering/src/software_rasterizer.cpp`.

## CPU, thuật toán và độ trễ

`SpatialCuller::query_visible_refs` duyệt layer/object IDs theo thứ tự vẽ, tra bounds và gom đối tượng nhìn thấy bằng vector/hash set. Đây là baseline **O(N)** theo số object trước khi raster phần geometry nhìn thấy; chưa có R-tree/BVH, tile cache hay dirty-region rendering. Dùng borrowed refs giúp tránh copy geometry nhưng không bỏ lượt quét scene. `SelectionModel::hit_test` được gọi trên đường hover khi pointer di chuyển; tài liệu vector xác nhận point/region query hiện quét tuyến tính.

Pan và zoom đổi camera, mà camera là một phần điều kiện hợp lệ của cache; do đó mỗi bước pan/zoom có thể kích hoạt full source redraw. `WM_PAINT` thực hiện render ngay trên cùng đường xử lý cửa sổ, nên một redraw chậm cũng giữ UI thread khỏi xử lý input kế tiếp. Cache có lợi cho overlay ổn định, nhưng không giảm chi phí full redraw khi camera thay đổi.

Benchmark source tại `HEAD` có các phép đo riêng `source_redraw`, `point_pick`, `window_region`, `crossing_region` và `cached_native_cpu_paint`. Tuy nhiên working tree hiện đánh dấu source benchmark đã xóa; executable benchmark không có trong `build/win-dev/apps/desktop`, và không tìm thấy kết quả số còn lưu cho point pick/Window/Crossing. Vì vậy chưa có latency distribution cho thao tác gọi chức năng hoặc input-to-visible.

## RAM và lưu lượng buffer

Đường desktop giữ hai `std::vector<uint8_t>` cho RGBA/BGRA và một bitmap GDI canvas; `NativeWindow::paint` giữ thêm bitmap backbuffer toàn cửa sổ. Renderer giới hạn một target ở 8.388.608 pixel, tương đương **32 MiB cho mỗi buffer RGBA8**. Hai vector canvas có thể cần 64 MiB ở giới hạn đó; bitmap canvas và backbuffer cộng thêm, chưa tính snapshot/document, hash/vector tạm, raster mask và allocator. Đây là kích thước buffer suy ra từ source, **không phải peak RAM đo được**. `std::vector` có thể giữ capacity lớn sau khi viewport thu nhỏ.

Ví dụ 1920×1080, theo canvas rect hiện tại (1568×932): mỗi buffer pixel khoảng 5,57 MiB; hai vector khoảng 11,15 MiB. Nếu bitmap GDI canvas là 32-bit thì cộng khoảng 5,57 MiB; backbuffer 1920×1080 cộng khoảng 7,91 MiB. Tổng khoảng **24,63 MiB cho các surface này**, chưa gồm phần còn lại của app; định dạng lưu của GDI bitmap tùy device nên đây là ước lượng.

Vòng RGBA→BGRA đọc 4 byte và ghi 4 byte cho mỗi pixel, tức ít nhất 8 byte/pixel lưu lượng CPU ở mỗi lần dựng lại; `StretchDIBits`/`BitBlt` có thêm công việc sao chép/composition. Chưa đo memory bandwidth hoặc mức CPU/GPU của các bước GDI. Đọc/ghi file document là đường riêng, không có số latency trong audit canvas này.

## Bằng chứng và giới hạn

| Chỉ số | Bằng chứng đã lưu | Diễn giải |
|---|---|---|
| Source redraw | Release, 1.000 path/101.000 elements, i7-3537U, RAM khả dụng ghi là 8,45 GB; p95 35,42–57,05 ms qua ba lượt | Thời gian CPU vượt 30 Hz software interaction budget; không phải present FPS. |
| Cached native paint | Cửa sổ ẩn, CPU paint p95 10,46–14,09 ms | Không đo desktop present hoặc input latency. |
| CPU offscreen spike khác | Trace synthetic pan 188 frame, p95 9,705 ms trong `docs/reports/foundation/ps00-acceptance-20261015.md` | Prototype/branch khác, một run, `actual_present_not_measured=true`; không thay thế benchmark app hiện tại. |
| Live process | Không có process PikDraw khi kiểm tra | Không lấy được working set/private bytes/CPU delta hiện tại. |
| GPU/present/input-to-visible | Không có số liệu | `not_measured`. |
| Pick/Window/Crossing latency | Có phép đo trong benchmark source ở HEAD, không có executable/kết quả tái chạy trong working tree | `not_measured` cho kết quả hiện tại. |

Nguồn chính: `docs/project-state.json` mục limitations; `apps/desktop/README.md`; `core/rendering/README.md`; `docs/roadmap/foundation-spikes.md`; các source path nêu trên. Test source đã được người dùng yêu cầu dọn trước đó; audit không khôi phục source hoặc chạy test/build.

## Bước đo tiếp theo được khuyến nghị

Để xác nhận trải nghiệm thật, cần ba lượt tối thiểu 60 giây cho từng trace trên máy iGPU 8 GB và máy GPU riêng: pan/zoom, hover/pick, Window/Crossing, drag/resize/rotate và source edit. Ghi riêng CPU render phases, present interval, input timestamp→frame hiển thị, miss-frame count, per-process private/working set, system commit và GPU engine; báo p50/p95/p99/max từng lượt. Giữ corpus/kích thước/DPI/driver/power mode/hash app cố định. Phần rerun benchmark phải dùng task có scope cho khôi phục benchmark/test source theo quy tắc hiện hành.

Ưu tiên kỹ thuật nên đo thử trước: index không gian dùng chung cho culling/hit-test, cache tile/damage hoặc preview khi pan/zoom, và giảm số full-frame buffer/copy. Giữ correctness bằng ảnh/pixel và selection oracle; không đánh đổi source geometry hay tính đúng của sản phẩm để hạ thời gian.
