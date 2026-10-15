# Desktop — bản nháp spike A

Source cửa sổ Qt6 Widgets và Skia raster đã có. Khi có Qt/Skia SDK tương thích, chạy app với `--trace-out <file.json>` để ghi tối đa 1.000 frame CPU trace sau khi đóng cửa sổ; dùng `QSaveFile` để không để lại JSON ghi dở. `actual_present_not_measured` luôn là `true`, nên trace không được diễn giải thành FPS/present hoặc input-to-visible.

Đã build core rendering Release bằng LLVM-MinGW 23.1.3 với CMake 3.31.10/Ninja 1.13.2; math test 1/1 pass. Cấu hình target desktop hiện dừng với `Qt6::Widgets is required`; cấu hình Skia dừng với `Skia headers or library were not found`. Vì vậy GUI chưa build/chạy; chưa có viewport, present, input-to-visible, GPU/software comparison hay hardware evidence. CMake app yêu cầu dependencies thật. AUTOMOC nhận header viewport rõ ràng.

Pan/zoom chỉ thay camera. Buffer preview giới hạn 64 MiB; lỗi render hoặc nội dung chưa hỗ trợ được hiển thị thành diagnostic. QPainter chuyển ảnh raster lên widget và vẽ diagnostic; không có vector engine thứ hai.

Thời gian QPainter submission không phải present interval. Chưa có GPU, đo input-to-visible, recovery hay kết luận 60 FPS. Xem báo cáo leader; không xem source scaffold là editor đã triển khai.
