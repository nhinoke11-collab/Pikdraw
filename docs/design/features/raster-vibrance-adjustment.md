# Tăng cường màu Vibrance cho ảnh raster

Action `action.bitmap.adjust.vibrance` nằm trong `Bitmaps > Vibrance…` / `Tăng cường màu…`.
Action chỉ bật khi chọn một ảnh có nguồn được giữ lại, mẫu đã được xác định là sRGB và tài liệu
không khóa nội dung. Lệnh mở hộp thoại Apply/Cancel và ghi `RasterVibranceParams` vào graph bằng
`SetRasterEditNodeCommand`; mở lại dùng cùng node ID, Undo/Redo dùng lịch sử tài liệu.

## Đường thao tác và bề mặt

- Menu/action ID: `action.bitmap.adjust.vibrance`.
- Label EN/VI: `Vibrance…` / `Tăng cường màu…`.
- Tooltip/focus help: màu xám giữ nguyên, màu ít bão hòa tăng mạnh hơn màu đã rực; không nhận diện tông da; alpha giữ nguyên.
- Toolbox: không áp dụng; đây là adjustment cho ảnh đã chọn.
- Property bar/Properties: chưa có điều khiển thường trực; dialog và graph command là bề mặt chỉnh sửa.
- SVG icon: không áp dụng cho mục menu hiện tại.
- Status sau Apply: thông báo node được cập nhật, preview đang dựng và Ctrl+Z để hoàn tác.
- Hướng dẫn: chọn ảnh raster sRGB, mở menu, nhập mức từ −100% đến +100%, chọn Apply; Cancel bỏ bản
  nháp. Có thể mở lại để sửa cùng node hoặc dùng Ctrl+Z/Ctrl+Y.

## Trường và thuật toán

| Field | Phạm vi | Validation |
| --- | --- | --- |
| `raster.vibrance.amount` | Ảnh đang chọn; mặc định 0%; giới hạn −100…100% | Số hữu hạn trong miền; 0 là identity |

Evaluator chuyển RGB sRGB mã hóa sang HSL, giữ hue và lightness, rồi tính saturation `S'` như sau.
Pixel trung tính `S = 0` được giữ nguyên vì Hue không xác định. Với pixel có màu:

- Với giá trị không âm `a`: `S' = S + (1 − S) × a`.
- Với giá trị âm `a`: `S' = S × (1 + a)`.

`a` là mức điều chỉnh chia 100. Kết quả giới hạn trong [0, 1], chuyển lại RGB sRGB mã hóa và giữ nguyên
mẫu alpha. Cách này đẩy màu nhạt xa hơn màu đã bão hòa khi tăng; đây là định nghĩa xác định của PikDraw,
không giả lập thuật toán nội bộ của ứng dụng khác. RGB/RGBA U8 và U16 được hỗ trợ; CMYK, Gray, Lab,
mask, vùng màu chọn lọc và nhận diện da chưa được hỗ trợ.

## Lưu trữ và kiểm chứng

Node dùng wire kind ổn định `Vibrance = 9`, algorithm version 1 và graph schema v8. Reader tiếp tục
đọc schema cũ; Vibrance trong schema dưới v8 bị từ chối. Preview fingerprint bao gồm amount và node
được tính là adjustment cần sRGB processing. Regression cần kiểm tăng/giảm bão hòa, màu xám, màu đã
bão hòa, U16, alpha, identity, tham số sai miền, `.pr` save/reopen, pixel evaluation, Undo/Redo và menu.

Hộp thoại native chưa được xác nhận trực quan trong lượt triển khai này; kiểm tra action/menu và test
code-level không thay thế cho thao tác runtime.
