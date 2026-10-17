# Phiếu thiết kế UI: Loại bỏ 3 nút Del, Dup và Page trên Property Bar

Tài liệu thiết kế theo [chuẩn UX/UI](../ui-ux-standard.md) và mẫu [phiếu UI](../feature-ui-template.md).

## 1. Mục tiêu và trạng thái

- Tên tính năng / task ID / module / owner: `desktop-ribbon-cleanup-del-dup-page-20261017` / `desktop` / `codex`.
- Người dùng yêu cầu: Loại bỏ hoàn toàn 3 nút `[✕ Del]`, `[⧉ Dup]` và `[⌖ Page]` hiển thị trên thanh ribbon (property bar) khi đang chọn đối tượng (theo ảnh người dùng gửi).
- Lý do thiết kế theo CorelDRAW / Illustrator chuẩn:
  - Thao tác Xóa (Delete) được thực hiện bằng phím `Delete`, `Edit > Delete`.
  - Thao tác Nhân bản (Duplicate) được thực hiện bằng `Ctrl+D`, `Edit > Duplicate`.
  - Thao tác Căn giữa trang (Center to Page) được thực hiện bằng phím `P`, `Object > Align > Center to Page`.
  - Property bar cần tinh gọn, chỉ tập trung vào các trường số kích thước/tọa độ (X, Y, W, H, Lock ratio) và các công cụ biến đổi hình học (Flip, Rotate, Group). Không để các nút hành động văn bản dài gây rối mắt và chiếm dụng không gian của palette màu và các thuộc tính khác.

## 2. Thay đổi giao diện chi tiết

1. **Xóa `[✕ Del]` và `[⧉ Dup]`**:
   - Vị trí cũ: x=468..518 và x=524..574 trên thanh ribbon.
   - Thao tác xóa và nhân đôi vẫn hoạt động bình thường qua phím tắt (`Delete`, `Ctrl+D`) và qua menu `Edit`.

2. **Xóa `[⌖ Page]`**:
   - Vị trí cũ: nút chữ `[⌖ Page]` tại đầu nhóm thao tác đơn lẻ.
   - Thao tác căn giữa trang in vẫn hoạt động qua phím tắt `P` và menu `Object > Align > Center to Page`.

3. **Căn chỉnh vị trí các nút tiếp theo**:
   - Nhóm nút biến đổi (Flip H, Flip V, Rotate CW, Rotate CCW, Ungroup, Isolation Exit) được đẩy gọn gàng bắt đầu ngay sau ô nhập H (x=468).
   - Màu sắc palette và các tùy chọn khác có thêm không gian hiển thị rộng rãi, không bị che khuất trên màn hình độ phân giải tiêu chuẩn.
