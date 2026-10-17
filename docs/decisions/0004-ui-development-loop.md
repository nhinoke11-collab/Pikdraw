# ADR 0004: Vòng lặp phát triển giao diện (Qt Widgets & UI Reload)

- **Trạng thái**: Accepted (Khóa tại M0 Foundation)
- **Ngày**: 2026-10-15
- **Người đề xuất**: Gemini 3.8 Flash High & Codex Coordinator
- **Phạm vi**: `modules/desktop`

---

## 1. Bối cảnh và Thách thức

Phát triển ứng dụng desktop C++20 thường gặp trở ngại về tốc độ lặp (iteration cycle): Mỗi lần chỉnh sửa một chút khoảng cách padding, màu sắc nút bấm hay bố cục panel trong UI lại phải biên dịch lại toàn bộ C++, liên kết các thư viện lớn (Qt, Skia) và khởi động lại app. Điều này làm chậm năng suất của lập trình viên và nhà thiết kế.

---

## 2. Quyết định Kiến trúc

1. **Phân tách Rạch ròi giữa View và Presenter**:
   - Giao diện người dùng được mô tả dưới dạng khai báo bằng các file `.ui` (Qt Designer XML) và các stylesheet `.qss`.
   - Logic điều khiển, Presenter và Document Controller độc lập với cấu trúc hiển thị cụ thể của widget.
   - Presenter lắng nghe các sự kiện và cập nhật View qua các interface/slots được bảo vệ bởi thế hệ giao diện (`ui_generation`).

2. **Cơ chế Nạp lại Giao diện lúc phát triển (Development Reload UI)**:
   - Trong quá trình phát triển (Debug mode), phím tắt `F5` hoặc nút "Reload UI" cho phép nạp lại các file `.ui` và `.qss` trực tiếp từ ổ đĩa.
   - Ứng dụng phân tích lại form, dựng cây widget mới trong bộ nhớ, gỡ kết nối các signals cũ và hoán đổi hiển thị mà **không hủy tài liệu đang mở**, không mất lịch sử Undo/Redo, và không thay đổi góc nhìn camera của Viewport.

3. **An toàn trong Bản Release thương mại**:
   - Trong cấu hình Release (`NDEBUG`), tính năng nạp file rời ngoài ổ đĩa bị vô hiệu hóa hoàn toàn bằng cờ tiền xử lý (`#ifndef NDEBUG`).
   - Mọi form `.ui` và stylesheet được biên dịch cứng thành tài nguyên nhị phân Qt (`.rcc`) đã ký duyệt, ngăn chặn việc chèn mã hoặc form độc hại từ bên ngoài.

## Làm rõ phạm vi ngày 16/10/2026

ADR này mô tả hướng phát triển/reload cho Qt, không chứng nhận adapter Win32/GDI hiện tại đã có Qt runtime.
Nó không khóa visual design Cyber hay menu/panel placement. Phần tổ chức UX/UI theo
[ADR 0008](0008-editor-ui-organization.md) và [chuẩn UX/UI](../design/ui-ux-standard.md);
workflow source/build theo ADR 0007. Xem project-state để biết implementation/evidence thực tế.
