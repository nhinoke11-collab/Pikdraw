# HSL chọn lọc theo dải màu cho ảnh raster

Action action.bitmap.adjust.selective_hsl nằm trong Bitmaps > Selective HSL… /
Bitmaps > HSL theo dải màu…. Chỉ bật khi chọn một ảnh raster có nguồn được giữ lại,
mẫu đã xác định là sRGB và tài liệu không khóa nội dung. Dialog Apply/Cancel ghi node bằng
SetRasterEditNodeCommand; mỗi dải có node riêng, mở lại dải đã lưu dùng cùng ID,
và Undo/Redo theo lịch sử tài liệu.

## Bề mặt giao diện

- Action ID: action.bitmap.adjust.selective_hsl.
- Label EN/VI: Selective HSL… / HSL theo dải màu….
- Tooltip/focus help: điều chỉnh một dải Hue có feather; màu xám trung tính và alpha giữ nguyên.
- Toolbox: không áp dụng. Property bar/Properties: chưa có điều khiển thường trực.
- SVG icon: không áp dụng cho mục menu. Status sau Apply báo preview và Ctrl+Z.
- Hướng dẫn: chọn ảnh raster sRGB, mở menu Bitmaps, chọn Reds/Yellows/Greens/Cyans/Blues/Magentas,
  nhập giá trị rồi Apply. Mỗi lần Apply cập nhật dải đang chọn; đổi dải sẽ nạp lại giá trị đã lưu
  của dải đó. Mỗi dải rộng 60°; Feather điều khiển độ mềm ở biên. Cancel bỏ thay đổi nháp.

## Phạm vi và tính toán

| Trường | Miền | Tác động |
| --- | --- | --- |
| range | Sáu tâm hue cách nhau 60° | Một node cho mỗi dải màu |
| hue_degrees | −30…30° | Xoay hue theo trọng số dải |
| saturation_percent | −100…100% | Co/giãn saturation trong dải |
| lightness_percent | −100…100% | Tăng/giảm lightness trong dải |
| feather_percent | 0…100%; mặc định 50% | Làm mềm biên của dải 60° |

Khoảng cách hue là khoảng cách góc tròn đến tâm dải. Feather 0 tạo biên cứng; Feather lớn tạo
chuyển tiếp smoothstep ở hai phía của dải. Mỗi thành phần HSL được nhân cùng trọng số vùng; kết quả
được giới hạn trong miền hợp lệ rồi đổi lại sRGB mã hóa. Hue không xác định ở pixel xám nên pixel
đó được sao chép nguyên byte. Alpha cũng được sao chép nguyên mẫu. U8/U16 straight-alpha được hỗ trợ.
CMYK, Gray, Lab, mask, eyedropper/sample target và nhận diện da chưa được hỗ trợ.

## Lưu và kiểm chứng

Wire kind ổn định là SelectiveHsl = 10; node này được giới thiệu trong graph schema v9 để lưu enum dải
cùng bốn giá trị f64. Graph hiện tại là schema v13; reader duy trì migration schema cũ và từ chối node
này trong graph schema dưới v9. Cache preview fingerprint
bao gồm mọi tham số và graph được đưa qua cổng yêu cầu xử lý sRGB.

Regression kiểm tra vùng đỏ và xanh lam, feather biên, xám, alpha, U16, identity và miền sai; vertical
slice kiểm tra pixel output, .pr save/reopen, Undo/Redo; storage kiểm tra mọi trường; menu/action
test xác nhận nhãn và ID. Dialog native đã biên dịch nhưng chưa được kiểm tra trực quan.
