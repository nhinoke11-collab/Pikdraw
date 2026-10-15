# Desktop — bản nháp spike A

Source cửa sổ Qt6 Widgets và Skia raster đã có. Khi có Qt/Skia SDK tương thích, chạy app với `--trace-out <file.json>` để ghi tối đa 1.000 frame CPU trace sau khi đóng cửa sổ; dùng `QSaveFile` để không để lại JSON ghi dở. `actual_present_not_measured` luôn là `true`, nên trace không được diễn giải thành FPS/present hoặc input-to-visible.

Build độc lập MSVC 2022 x64, Qt 6.8.3 và Skia source `08a5439a6be726021c1c1905d23ce298a3edc5e4` đã thành công. CMake app yêu cầu Qt/Skia thật; AUTOMOC nhận header viewport rõ ràng. MSVC bắt được lỗi C++20: `std::isfinite` không constexpr trong toolchain này, nên `is_finite()` không còn khai báo `constexpr`.

Pan/zoom chỉ thay camera. Buffer preview giới hạn 64 MiB; lỗi render hoặc nội dung chưa hỗ trợ được hiển thị thành diagnostic. QPainter chuyển ảnh raster lên widget và vẽ diagnostic; không có vector engine thứ hai.

Thời gian QPainter submission không phải present interval. Chưa có GPU, đo input-to-visible, recovery hay kết luận 60 FPS. Xem báo cáo leader; không xem source scaffold là editor đã triển khai.

## Kiểm chứng độc lập trên Qt/Skia thật

Ngày 2026-10-15, build Release bằng MSVC 19.44.35229.0, Qt 6.8.3 MSVC x64, CMake 3.31.10 và Ninja 1.13.2. Skia được biên dịch từ mã nguồn với CPU raster, `/MD` để khớp CRT của Qt và stub PNG encoder; không bật backend GPU. Build GUI và cả hai CTest đều pass: math tests và Skia raster integration.

Integration test raster 100.000 path trên fixture kích thước `19989.5 × 19926.25 mm`; nó kiểm tra bounds, node count, số node visible/culled và pixel buffer có pixel khác nền. Lệnh viewport chạy offscreen với `--nodes 100000 --spacing-mm 63.25 --synthetic-pan-ms 3000`. Trace ghi 188 frame; visible 45–77, culled 99.923–99.955; CPU tổng p50 4.66 ms, p95 9.71 ms, lớn nhất 11.99 ms. Pan là event tổng hợp trong Qt, không phải input vật lý. Trace gốc ở [`evidence/viewport-100k-20m-offscreen.json`](evidence/viewport-100k-20m-offscreen.json), SHA-256 `2ca4ef22bb0ff058dbf71594d6cab2ad17adbdd5cd9dcd734b790570f06e5dad`.

Đây là bằng chứng cho CPU raster và culling trên đúng máy test. Môi trường chạy dùng Qt offscreen, nên số frame không phải refresh/present FPS; chưa đo độ trễ input-to-visible, GPU, phục hồi thiết bị hoặc máy tham chiếu 8 GB. Không dùng p95 CPU để kết luận đạt 60 FPS.
