# Phiếu thiết kế UI: Nhập kích thước chiều dài (L) và Góc (Tab) trong Pen Tool

Tài liệu thiết kế theo [chuẩn UX/UI](../ui-ux-standard.md) và mẫu [phiếu UI](../feature-ui-template.md).

## 1. Mục tiêu và phạm vi

- **Tên tính năng**: Tinh chỉnh Pen Tool nhập kích thước (L) và góc (Tab).
- **Task ID**: `desktop-pen-dimension-input-20261017` / module: `desktop` / provider: `codex`.
- **Yêu cầu người dùng**:
  - Khi đang dùng công cụ Pen Tool (Tool 8), bấm phím tắt `L` sẽ hiển thị ô nhập kích thước (chiều dài đoạn thẳng).
  - Nhập kích thước xong bấm `Enter`: Tạo ra đoạn thẳng (line segment) có kích thước vừa nhập, tính từ gốc điểm neo trước đó theo hướng chuột hiện tại.
  - Hoặc: Nhập `L` -> nhập kích thước -> bấm `Tab` -> chuyển sang ô nhập góc (ví dụ nhập `30` cho góc 30° so với phương ngang trục X). Chỉ đối chiếu với 1 trục X duy nhất ($dx = L \cdot \cos\theta, dy = L \cdot \sin\theta$), bỏ trục Y và các logic phức tạp.
  - Bấm `Enter`: Tạo đoạn thẳng với kích thước và góc đã nhập.
  - Bấm `Tab` có thể đảo qua lại giữa ô Chiều dài và ô Góc.

## 2. Các bề mặt giao diện (UI Surfaces)

### 2.1. Contextual Property Bar (Thanh thuộc tính Ribbon trên đỉnh)
- Vị trí: `topbar_h + tabbar_h` (y = 66..98).
- Thành phần khi Pen Tool đang vẽ:
  - **Nhãn**: `Pen:` (màu Teal, font đậm).
  - **Ô Chiều dài (L)**: `x = 52..190`. Hiển thị `L: <giá trị> mm` (hoặc `L: Chiều dài` khi trống). Nhấp chuột hoặc bấm `L` sẽ focus ô này.
  - **Ô Góc (Angle)**: `x = 196..320`. Hiển thị `Góc: <giá trị>°` (hoặc `Góc: 0–360°` khi trống). Nhấp chuột hoặc bấm `Tab` sẽ focus ô này.
  - **Nút Kết thúc**: `x = 328..420`, nhãn `↵ Kết thúc`.
  - **Nút Hủy**: `x = 428..500`, nhãn `Esc Hủy`.
  - **Gợi ý thao tác**: `L: Chiều dài · Tab: Góc (trục X) · Enter: Đặt điểm / Kết thúc`.

### 2.2. Canvas Floating Dynamic HUD (Badge nhập liệu nổi trên Canvas)
- Hiển thị trực tiếp trên Canvas gần con trỏ chuột (`pen_last_pointer_world_`):
  - Khung chữ nhật bo góc 4px nền tối bán trong suốt.
  - Hiển thị song song hai trường: `[ L: <giá trị> mm ]` và `[ ∠ <giá trị>° ]`.
  - Trường đang active có viền sáng màu Teal, kèm con trỏ nhập liệu nhấp nháy `|`.
  - Giúp người dùng tập trung hoàn toàn vào khung vẽ mà không phải nhìn lên thanh công cụ.

## 3. Tương tác & Phím tắt

1. **Phím `L`**:
   - Nếu chưa có điểm neo nào: tự động lấy vị trí chuột hiện tại làm điểm neo gốc (A).
   - Kích hoạt chế độ nhập chiều dài (`NumericFieldId::PenLength`).
   - Xóa bộ đệm cũ, sẵn sàng nhận số.
2. **Nhập số**:
   - Nhận các phím `0-9`, dấu chấm `.` hoặc `,`.
   - Xem trước (live preview) trên canvas cập nhật tức thời theo giá trị đang gõ:
     - Nếu chưa nhập góc: đoạn thẳng dài đúng `L` mm, xoay theo hướng con trỏ chuột.
     - Nếu đã nhập góc: đoạn thẳng dài đúng `L` mm, khóa theo góc $\theta$ so với trục ngang X.
3. **Phím `Tab`**:
   - Chuyển đổi qua lại giữa `PenLength` và `PenAngle`.
   - Giữ nguyên giá trị đã nhập ở trường trước đó.
4. **Phím `Enter`**:
   - Nếu đang ở `PenLength` hoặc `PenAngle`:
     - Xác nhận điểm neo mới theo kích thước (và góc nếu có).
     - Thêm đoạn thẳng vào bản nháp Pen.
     - Đưa trạng thái nhập số về `None`.
   - Nếu không trong chế độ nhập số:
     - Hoàn tất phiên Pen (finish open path) đưa đối tượng vào tài liệu.
5. **Phím `Escape`**:
   - Hủy bỏ ô nhập số hiện tại, trở về chế độ di chuột tự do.
