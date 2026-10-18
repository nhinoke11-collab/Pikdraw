# Phiếu thiết kế UI: Hệ thống phím tắt tùy biến & Phím điều hướng thứ tự CorelDRAW

## 1. Mục tiêu và trạng thái

- **Tên tính năng / task ID / module / owner**: Phím tắt tùy biến & điều hướng Z-order CorelDRAW / `shortcuts-zorder-settings-20261018` / `desktop` / `antigravity`.
- **Người dùng cần hoàn thành việc gì, đầu vào và kết quả nhìn thấy**:
  - Người dùng có thể dùng các phím tắt chuẩn CorelDRAW để thay đổi thứ tự lớp của đối tượng: `Shift+PgUp` (Lên trên cùng), `Shift+PgDn` (Xuống dưới cùng), `Ctrl+PgUp` (Lên một lớp), `Ctrl+PgDn` (Xuống một lớp).
  - Người dùng có thể di chuyển (nudge) đối tượng bằng phím mũi tên: `Up/Down/Left/Right` (1.0 mm), `Shift+Up/Down/Left/Right` (Super Nudge: 10.0 mm), `Ctrl+Up/Down/Left/Right` (Micro Nudge: 0.1 mm).
  - Người dùng có thể mở mục **Phím tắt & Thao tác (Tab 4)** trong hộp thoại **Cài đặt & Tùy chọn (Settings Ctrl+J)** để xem danh sách toàn bộ lệnh, chọn lệnh và tùy biến / gán lại phím tắt mới theo ý muốn với cơ chế phát hiện & xử lý xung đột, cũng như khôi phục về mặc định của từng profile (CorelDRAW Standard, Adobe Illustrator, PikDraw Mặc định).
- **Capability hiện có**: Hệ thống `ActionDispatcher` quản lý đăng ký action và routing shortcut; `DocumentWorkflow` và `ReorderObjectInLayerCommand` quản lý Z-order; modal Settings đã có Tab 4 nhưng trước đây chỉ hiển thị text tĩnh.
- **Phần mở rộng**: Bổ sung bảng quản lý phím tắt tương tác trong Tab 4 (lọc danh mục, danh sách có thanh cuộn, nút gán phím với chế độ lắng nghe phím thực tế, nút xóa, nút khôi phục mặc định); kết nối phím tắt Z-order và nudge vào native canvas shortcut router.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định cụ thể |
| --- | --- |
| Menu chính | `Object > Order > To Front (Shift+PgUp)`, `To Back (Shift+PgDn)`, `Forward One (Ctrl+PgUp)`, `Back One (Ctrl+PgDn)`. `Tools > Options > Shortcuts & Hotkeys (Ctrl+J)`. |
| Action/command ID | `action.object.order_to_front`, `action.object.order_to_back`, `action.object.order_forward_one`, `action.object.order_back_one`, `action.object.nudge_up`, `action.object.nudge_down`, `action.object.nudge_left`, `action.object.nudge_right`, `action.object.super_nudge_up`, `action.object.super_nudge_down`, `action.object.super_nudge_left`, `action.object.super_nudge_right`. |
| Loại thao tác | Action một lần, giao dịch có hỗ trợ Undo/Redo tức thì. Hộp thoại cấu hình trong Settings modal. |
| Standard toolbar | Tích hợp trong footer của bảng Layers Dock (`Lên ▲`, `Xuống ▼`). |
| Toolbox / flyout | Không áp dụng (đây là thao tác object order & canvas navigation). |
| Property bar | Hiển thị thứ tự và nút điều hướng khi có đối tượng được chọn. |
| Properties / Settings modal | Tab 4 trong hộp thoại Settings (`Ctrl+J`) hiển thị toàn bộ bảng phím tắt cho phép tìm kiếm, gán phím mới, xóa và đặt lại. |
| Context menu / shortcut | Chuột phải vào đối tượng > menu `Order` hoặc bấm trực tiếp phím tắt trên canvas. |
| Dialog/panel mới | Tái sử dụng Tab 4 của hộp thoại Settings hiện có. |

## 3. Trường thuộc tính

| ID / label EN + VI | Bar / Properties / dialog | Kiểu, unit, range, default | Scope và source binding | Khi nào enable / Mixed | Preview, commit, validation |
| --- | --- | --- | --- | --- | --- |
| Profile Selector | Settings Tab 4 | 3 nút chuyển: CorelDRAW / Illustrator / PikDraw | Ứng dụng (App preference) | Luôn enable | Commit ngay khi bấm, cập nhật bảng phím |
| Category Filter | Settings Tab 4 | Tất cả / Sắp xếp / Di chuyển / Chỉnh sửa / Công cụ | Lọc bảng hiển thị | Luôn enable | Cập nhật bộ lọc bảng |
| Shortcut List Table | Settings Tab 4 | Bảng danh sách cuộn mượt | Danh mục action trong `ActionDispatcher` | Luôn enable | Chọn dòng để thao tác |
| Assign Key Button | Settings Tab 4 | Nút hành động | Lệnh đang chọn trong bảng | Enable khi có lệnh được chọn | Kích hoạt chế độ bắt phím mới `[Nhấn phím...]` |
| Clear Shortcut Button | Settings Tab 4 | Nút hành động | Lệnh đang chọn | Enable khi lệnh có phím tắt | Xóa phím tắt đã gán |
| Reset Default Button | Settings Tab 4 | Nút hành động | Lệnh đang chọn / Toàn bộ bảng | Luôn enable | Khôi phục phím tắt mặc định |

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Phím tắt Z-Order | Phím tắt Nudge | Bảng Shortcuts Settings | Hint / Trạng thái |
| --- | --- | --- | --- | --- |
| Không có selection | Bị bỏ qua | Bị bỏ qua | Vẫn cấu hình bình thường | Chọn ít nhất một đối tượng |
| Có 1 đối tượng chọn | Thay đổi Z-order trong layer | Dịch chuyển tọa độ | Cấu hình bình thường | Đã dời thứ tự / dịch chuyển |
| Nhiều đối tượng chọn | Lần lượt áp dụng Z-order | Cùng dịch chuyển theo vector delta | Cấu hình bình thường | Đã áp dụng cho các đối tượng chọn |
| Đối tượng locked | Từ chối thao tác | Từ chối thao tác | Cấu hình bình thường | Đối tượng đang bị khóa |
| Đang mở Settings Tab 4 | Tạm dừng canvas | Tạm dừng canvas | Bắt phím nếu đang ở mode Record | Nhấn tổ hợp phím để gán |

## 5. Icon, tên và hướng dẫn

| Nội dung | EN / VI |
| --- | --- |
| Lên trên cùng (To Front) | `Shift+PgUp` · Chuyển đối tượng lên trên cùng của lớp |
| Xuống dưới cùng (To Back) | `Shift+PgDn` · Chuyển đối tượng xuống dưới cùng của lớp |
| Lên một lớp (Forward One) | `Ctrl+PgUp` · Chuyển đối tượng lên trước một bước |
| Xuống một lớp (Back One) | `Ctrl+PgDn` · Chuyển đối tượng xuống sau một bước |
| Dịch chuyển (Nudge) | `Mũi tên / Arrow keys` · Dịch chuyển 1.0 mm |
| Dịch bước lớn (Super Nudge)| `Shift + Mũi tên` · Dịch chuyển 10.0 mm |
| Dịch bước nhỏ (Micro Nudge)| `Ctrl + Mũi tên` · Dịch chuyển 0.1 mm |
| Hướng dẫn gán phím | Bấm `Ctrl+J` > chọn tab `Phím tắt & Thao tác` > chọn lệnh > bấm `Gán phím tắt` > nhấn tổ hợp phím muốn đặt trên bàn phím. |
