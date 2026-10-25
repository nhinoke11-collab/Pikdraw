# Điều chỉnh HSL cho ảnh raster

Trạng thái: đã nối action, graph node, evaluator sRGB RGBA8/RGBA16LE và lưu `.pr`;
chưa nghiệm thu trực quan trong cửa sổ ứng dụng.

## Đường thao tác và bề mặt

- Menu chính: `Bitmaps > Hue/Saturation/Lightness…`.
- Action ID: `action.bitmap.adjust.hsl`; cùng action dùng cho menu và kiểm tra enable.
- Label tiếng Việt: `Sắc màu/Bão hòa/Độ sáng HSL…`; tiếng Anh:
  `Hue/Saturation/Lightness…`.
- Tooltip/focus help của action: `Adjust encoded-sRGB hue, saturation, and lightness`.
- Toolbox: không áp dụng; đây là thao tác chỉnh một ảnh đã chọn, không phải công cụ vẽ trên canvas.
- Toolbar/property bar: không áp dụng ở phiên bản này; không thêm một hàng điều khiển thường trực
  cho một modal điều chỉnh ít dùng.
- Properties: không áp dụng; modal Apply/Cancel dùng chung graph command và Undo của tài liệu.
- SVG icon: không áp dụng; menu entry dùng chữ, chưa tạo biểu tượng riêng.
- Focus help trong dialog: HSL tính trên sRGB đã mã hóa, Hue của màu xám không đổi và alpha được giữ.
- Status sau áp dụng: `Đã cập nhật HSL · đang dựng xem trước · Ctrl+Z hoàn tác`.
- Hướng dẫn: chọn đúng một ảnh raster sRGB có nguồn giữ lại và mở lệnh trên; nhập Hue, Saturation
  hoặc Lightness; chọn Áp dụng để ghi node không phá hủy, Hủy để bỏ thay đổi; dùng Ctrl+Z để hoàn tác.

## Trường và trạng thái

| Field ID | Nhãn | Phạm vi / giá trị | Validation và trạng thái |
| --- | --- | --- | --- |
| `raster.hsl.hue` | Hue / Sắc màu | Ảnh đang chọn, độ; mặc định 0° | Số hữu hạn trong ±180°; hue tuần hoàn, mặc định là identity |
| `raster.hsl.saturation` | Saturation / Bão hòa | Ảnh đang chọn, %; mặc định 0% | Số hữu hạn trong ±100%; scale saturation HSL và clamp về [0, 1] |
| `raster.hsl.lightness` | HSL Lightness / Độ sáng HSL | Ảnh đang chọn, %; mặc định 0% | Số hữu hạn trong ±100%; tăng giảm tương đối đến biên đen/trắng |

`HSL Lightness` không phải linear luminance hay Lab L*. Hiện không có chọn dải màu, picker,
mask, layer cục bộ, preview trước/sau trong dialog hoặc xử lý ảnh native CMYK/Lab. Lệnh tắt khi
không chọn đúng một ảnh, ảnh khóa/không có source pixel khả dụng, hoặc pixel basis là ICC chưa áp
dụng/không rõ. Alpha straight được giữ nguyên.

## Kiểm chứng

Regression evaluator kiểm primary hue rotation, pixel xám, alpha, identity, giới hạn tham số và
RGBA16LE. Regression document storage kiểm node HSL qua save/reopen `.pr`; desktop action suite kiểm
menu, label và action ID. Các kiểm tra này không chứng minh thao tác chuột/native dialog hoặc hiển thị
preview trong ứng dụng; cần xác nhận runtime riêng trước khi đóng acceptance UI.
