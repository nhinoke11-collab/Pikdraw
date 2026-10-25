# Điều chỉnh Light cho ảnh raster

Trạng thái: triển khai lát cắt sRGB U8/U16 qua action, graph, evaluator, `.pr` và TIFF master export. Properties có tab Light với EV, Contrast, Highlights, Shadows, Whites và Blacks; EV đọc cùng graph với field EV trên property bar. Các regression UI đã thêm nhưng đang chờ lượt Release cuối.

## Phiếu UI

| Mục | Quyết định |
| --- | --- |
| Menu chính | `Bitmaps > Light…` mở nhóm sáu thông số; `Bitmaps > Exposure…` mở hộp thoại Exposure đầy đủ. |
| Action ID | `action.bitmap.adjust.light` và `action.bitmap.adjust.exposure`; chỉ bật với một bitmap editable có preview sRGB và nguồn được giữ. |
| Loại thao tác | Action mở modal; Apply cập nhật cùng node Light, Cancel không đổi tài liệu; Undo/Redo dùng command tài liệu. |
| Toolbar / toolbox | Không áp dụng ở lát cắt này: đây là adjustment theo selection, không có tool stroke. |
| Property bar | Với một bitmap editable, field `EV` đọc node Light mới nhất (−5…+5 EV) hoặc node Exposure (−10…+10 EV); cập nhật cùng node ID, enabled state và mask. |
| Properties | Object inspector có tab Light, Color Balance và HSL. Tab Light có sáu field; tab HSL có Hue/Saturation/Lightness/Vibrance. Các trường sửa một node ổn định qua cùng validator, document command và Undo/Redo; Color Balance giữ ba dải màu riêng. EV dùng cùng `NumericFieldId::RasterExposureEv` với property bar. |
| SVG / help | Menu chữ không cần icon riêng; field số không có icon. Focus hiện status hint song ngữ, miền giá trị và model sRGB; Enter/blur áp dụng, Esc hủy, Ctrl+Z hoàn tác. Dialog ghi rõ thuật toán Light tuyến tính, HSL dùng sRGB mã hóa và alpha được giữ. |

Modal có sáu field: Exposure (−5…5 EV), Contrast (−100…100%), Highlights, Shadows, Whites và Blacks (các trường phần trăm −100…100%). Selection là một ảnh duy nhất; mixed selection, locked, profile chưa áp dụng và ảnh không có source đều disabled. Tab order đi theo thứ tự field rồi Apply/Cancel. Giá trị khởi tạo lấy từ node Light mới nhất của ảnh.

## Ngữ nghĩa và dữ liệu

Node `RasterLightParams` là algorithm version 1, graph schema v12, dùng trên encoded-sRGB RGBA8 hoặc RGBA16LE. Evaluator đổi từng RGB sample sang linear light, áp Exposure và Contrast quanh pivot 0.18, rồi trộn hiệu chỉnh Shadows/Highlights/Blacks/Whites bằng smoothstep trên các dải sáng cố định. Positive Highlights giảm vùng sáng; positive Shadows nâng vùng tối. Alpha được giữ nguyên, mask node blend kết quả theo coverage/density. Đây là thuật toán PikDraw có version; không tuyên bố khớp Photoshop/Corel.

Graph v12 được ghi bằng wire kind `Light = 11`; reader cũ v1–v11 vẫn migrate và node Light bị từ chối nếu khai báo schema cũ. Fingerprint preview chứa đủ sáu giá trị. Command kiểm tra miền số và giữ stable ID khi sửa lại.

## Nghiệm thu

- Regression evaluator: dark/mid/highlight, identity bit-exact, alpha, U16 khác biệt dưới 8-bit và tham số ngoài miền.
- Regression storage và command: lưu/mở `.pr`, cập nhật cùng node, Undo/Redo và từ chối EV ngoài miền.
- Desktop action test: ID, nhãn EN/VI và route menu.
- Properties HSL: Hue âm và dấu thập phân, node ID, Undo/Redo và `.pr` reopen qua pointer/field handlers.
- Workflow test mở modal qua menu, kiểm model và export full graph TIFF; lượt Release tích hợp cuối vẫn đang chờ theo yêu cầu chỉ chạy test một lần.
- Còn mở: shared editor cho toàn bộ Light/Levels/Curves/color node, preview trước/sau trong dialog, native visual/DPI/focus walkthrough, channel CMYK/Gray/Lab và histogram masked/channel.

Exposure EV nhập trực tiếp ở bar/Properties dùng `NumericFieldId::RasterExposureEv`, kiểm finite và
giới hạn theo node đang sửa (Light ±5 EV, Exposure ±10 EV), chỉ nhận một bitmap sRGB editable,
giữ selection/document revision và ghi bằng `SetRasterEditNodeCommand`. Node hiện có giữ ID, enabled
state và mask; nếu chưa có node phù hợp, cấp ID tiếp theo. Các field Contrast/Highlights/Shadows/
Whites/Blacks trong tab Properties cập nhật riêng giá trị tương ứng trên cùng Light node. Dialog Light
vẫn mở đủ sáu field; dialog Exposure cũng giữ node/mask khi thay tham số. Regression UI, Undo/Redo và
reopen đã được thêm; lượt Release cuối và native pointer/focus walkthrough vẫn chờ.

Control field ID là `raster.exposure_ev`, quantity EV, scope là node Light/Exposure mới nhất của một
bitmap được chọn. Numeric field Light dùng key `raster.light.*`, quantity percent và range −100…+100.
Mixed selection, ảnh khóa, profile nhúng chưa xử lý hoặc nguồn không hợp lệ không mở được field; giá trị
ngoài miền không commit. Bar/Properties đọc lại từ graph sau Undo/Redo, không giữ local draft.
