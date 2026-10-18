# R03 — Khôi phục preview canvas và lưu đúng tab

Task `remed-r03-canvas-overlays-tabpath`, module desktop, owner `codex:executor`.
Đối chiếu chuẩn UI và workspace hiện có; sửa lỗi trên bề mặt Win32 đang dùng.

| Bề mặt | Quyết định |
| --- | --- |
| Menu/action | Giữ File > Open/Save/Save As (`FileOpen/FileSave/FileSaveAs`); Line `action.tool.line`, Polyline `action.tool.polyline`, Pen, Node, Contour và Gradient giữ binding hiện có. |
| Toolbox/toolbar | Giữ toolbox trái và toolbar hiện có; Line `L`, Polyline `Shift+L`; không thêm control. |
| Property bar/Properties | Giữ field Stroke và thông số tool hiện có; không thay scope, miền giá trị, enable/mixed state hay cách commit. Không có field mới. |
| Icon SVG/label | Tái dùng icon, tên ngắn và accessible name hiện có; không thêm asset hay nhãn. |
| Tooltip/focus help | Giữ trợ giúp hiện có; preview xuất hiện khi di chuyển chuột sau điểm đầu. |
| Status/hướng dẫn | Chọn Line (L), nhấp điểm đầu, di chuyển để xem đường dự kiến, nhấp điểm cuối; Escape hủy. File > Save lưu tài liệu của tab đang chọn. |

Overlay của Line/Polyline chạy độc lập với tool Gradient. Node là tool 11,
Contour là tool 12; dùng ID thực tế thay cho số nhầm trong roadmap.
Chữ đã commit được renderer vẽ theo transform và thứ tự đối tượng; GDI chỉ giữ
phiên soạn chữ trực tiếp với hộp nhập/caret. Không đổi document hay Undo khi paint.
Đường dẫn có scope từng tab: mở/lưu thành công cập nhật tab, chuyển/tạo/đóng tab
đồng bộ đường dẫn hiện hành; tab mới không kế thừa đích lưu của tab trước.
Dialog file vẫn có hủy/lỗi như hiện tại. Không áp dụng adapter vector tải mạng.

AC-R03-overlay: Given Line có một điểm đầu, When rê chuột chưa commit điểm cuối,
Then bitmap canvas chứa đường preview màu cyan; kiểm tra pixel trong test UI có sẵn.
AC-R03-save: Given hai tab có đường dẫn khác nhau, When lưu, chuyển tab, lưu tiếp
và đóng tab, Then từng tệp mở lại có đúng số đối tượng của tab tương ứng.
Kiểm tra native mục tiêu: `test_pikdraw_drawing_tool_ui`, `test_pikdraw_pen_native_shell`.
Không suy ra chất lượng mọi font, DPI hay toàn bộ UX từ hai test này.
