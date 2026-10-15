# Desktop — bản nháp spike A

Source cửa sổ Qt6 Widgets và Skia raster đã có; chưa build hoặc chạy GUI do thiếu Qt/Skia SDK tương thích. CMake yêu cầu dependency thật. AUTOMOC nhận header viewport rõ ràng.

Pan/zoom chỉ thay camera. Buffer preview giới hạn 64 MiB; lỗi render hoặc nội dung chưa hỗ trợ được hiển thị thành diagnostic. QPainter chuyển ảnh raster lên widget và vẽ diagnostic; không có vector engine thứ hai.

Thời gian QPainter submission không phải present interval. Chưa có GPU, đo input-to-visible, recovery hay kết luận 60 FPS. Xem báo cáo leader; không xem source scaffold là editor đã triển khai.
