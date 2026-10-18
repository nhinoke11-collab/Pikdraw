# R08 — Tỷ lệ DPI của cửa sổ

Giữ menu/action ID, toolbox trái, toolbar, property bar và Properties hiện có.
Không thêm field, label, icon SVG, tooltip hay focus help; scope/enable/mixed state
và command/Undo giữ nguyên. Status hint và hướng dẫn công cụ hiện có vẫn áp dụng.
Người dùng đổi tỷ lệ màn hình hoặc chuyển cửa sổ giữa màn hình; UI tự cập nhật.

Tất cả bề mặt shell dùng tọa độ DIP: token 38/28/36/26/52/280/20 là DIP,
GDI ánh xạ sang pixel vật lý một lần. Pointer Win32 được đổi về DIP trước hit-test,
kể cả wheel, pan, timer, menu, dialog và popup. Không nhân từng token lần nữa.
Font GDI vẽ qua cùng mapping, backbuffer và canvas bitmap có kích thước vật lý.
Tiêu đề Selection trùng với tab Objects được bỏ để tránh chữ chồng lên tab.
Renderer tạm dùng camera/target vật lý, khôi phục camera DIP trước overlay/input.
`WM_DPICHANGED` nhận DPI/RECT hệ thống, hủy gesture chưa commit, tạo lại font/cache,
áp RECT và cập nhật viewport. Cửa sổ chưa initialize mặc định 100%.

AC-R08: Given cửa sổ native, When nhận 120/144/192 DPI rồi quay về 96 DPI,
Then DIP mapping, kích thước ảnh chụp, viewport và toolbox hit-test khớp tỷ lệ;
paint không thay geometry/Undo. Kiểm trong `test_pikdraw_drawing_tool_ui`.
Test thông điệp tổng hợp không thay nghiệm thu chuyển màn hình thật hoặc kiểm font
trên từng thiết bị; chưa có bằng chứng độ trễ hay UX toàn bộ màn hình 4K/5K.
Không áp dụng adapter/tài nguyên vector tải mạng.
