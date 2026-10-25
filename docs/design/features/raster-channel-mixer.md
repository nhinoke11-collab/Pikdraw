# Bộ trộn kênh RGB ảnh raster

Trạng thái: evaluator U8/U16, graph schema v13, mask, Undo/Redo, save/reopen và menu/dialog đã được thêm; chưa nghiệm thu native UI hoặc đồng bộ Properties.

## UI contract

- Menu: Bitmaps > Channel Mixer… — action ID action.bitmap.adjust.channel_mixer.
- Nhãn tiếng Việt: Bộ trộn kênh…; tiếng Anh: Channel Mixer….
- Toolbox/standard toolbar: không áp dụng; đây là chỉnh sửa ảnh theo selection, không phải công cụ canvas.
- Property bar: chưa triển khai. Properties: chưa triển khai. Modal và các bề mặt chỉnh sửa còn lại cần dùng cùng ma trận/node ID/validation; không được tạo thêm Undo khi mở bảng.
- Field scope: đúng một bitmap RGB đang chọn; một ma trận 3×3 theo thứ tự hàng R/G/B đầu ra và cột R/G/B đầu vào. Một lệnh Apply tạo/cập nhật cùng một node; mask hiện có của node được giữ.
- Giá trị ban đầu là ma trận identity. Mỗi hệ số nằm trong [-2, 2]. Tính trên linear-light sRGB, clamp output về [0,1], alpha giữ nguyên. Đây là semantics PikDraw; không cam kết giống một vendor.
- Dialog có nhãn cho từng output/input pair, Apply/Cancel, tab order cho chín field, nội dung focus help về đơn vị hệ số và clipping. Không có SVG icon riêng trong menu native hiện hành.
- Status hint thành công: “Đã cập nhật bộ trộn kênh RGB · Ctrl+Z hoàn tác”.

## Acceptance

- Đổi kênh có analytic RGB primary oracle; identity phải bit-exact ở U8/U16; giữ alpha; mask trộn theo coverage; reject hệ số ngoài miền.
- Lưu .pr/reopen giữ ma trận/schema v13; một node có ID ổn định; Undo/Redo phục hồi giá trị.
- Properties/bar sharing và thao tác dialog bằng cửa sổ native chưa được xác nhận.
