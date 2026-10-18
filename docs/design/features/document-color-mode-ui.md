# Hệ màu tài liệu và DPI hiệu ứng raster

Task: `color-system-overhaul-core-20261018`, `color-system-overhaul-ui-20261018`.
Theo PLAN-COLOR-DPI-20261018, chuẩn UI và workspace native hiện có.

## Quyết định giao diện

| Bề mặt | Thiết kế |
|---|---|
| Menu chính | Edit > Document Color Mode > CMYK / RGB; Edit > Document Raster Effects DPI > 300 / 150 / 72 |
| Action ID | `action.edit.color_mode_cmyk`, `action.edit.color_mode_rgb`, `action.edit.raster_dpi_300`, `action.edit.raster_dpi_150`, `action.edit.raster_dpi_72` |
| Loại lệnh | Một giao dịch document, toàn bộ trang/layer, một Undo; menu con đánh dấu giá trị hiện tại |
| Toolbar/toolbox | Không áp dụng: đây là thuộc tính tài liệu, không phải công cụ vẽ |
| Property bar | Bỏ nút CMYK · 300 DPI gán cứng; không thêm nút thay thế |
| Properties | Bảng màu tài liệu khóa theo mode; nhãn RGB/CMYK, tên profile và hướng dẫn vào Edit |
| Mở Properties | Window > Properties hiện có; dock đóng không ảnh hưởng menu Edit |
| Icon SVG | Không áp dụng với lệnh menu có nhãn và dấu chọn; không tạo bộ icon mới |
| Hộp thoại | Tái dùng File > New; xác nhận đổi màu giải thích mất gamut, Ctrl+Z; Cancel không thay revision |
| Shortcut | Ctrl+Z/Ctrl+Y qua lịch sử hiện có; chưa gán phím riêng cho mode/DPI |

## Field, trạng thái và hướng dẫn

| Field EN / VI | Phạm vi, giá trị | Binding / commit |
|---|---|---|
| Document Color Mode / Hệ màu tài liệu | Document: RGB hoặc CMYK; New mặc định CMYK | Page settings đồng nhất mọi trang, immutable snapshot; xác nhận rồi commit một command |
| Raster Effects DPI / DPI hiệu ứng raster | Document: 72, 150, 300; New RGB=72, CMYK=300 | Snapshot, command riêng; đổi mode giữ DPI đã chọn |
| ICC profile / Profile ICC | RGB: sRGB IEC61966-2.1; CMYK: U.S. Web Coated (SWOP) v2 | Tên chuẩn trong document; desktop dùng ICC Windows tương ứng cho preview/chuyển kênh khi profile có sẵn |
| Palette / Bảng màu | Default đối tượng mới hoặc selection | Mode đọc từ snapshot hiện hành; swatch process đúng mode, Spot giữ tên/tint |

Có document: lệnh hoạt động ở mọi selection, kể cả đối tượng/layer ẩn hoặc khóa vì scope toàn tài liệu.
Không có document: không có ngữ cảnh chỉnh sửa. Draft text được commit trước đổi màu; draft effect/gesture bị hủy.
Hủy xác nhận giữ tài liệu và history. Lỗi validation không publish một phần. Undo/Redo và đổi tab đọc lại snapshot.
Màu Gray nhập được chuẩn hóa sang process mode khi ghi; Spot vẫn giữ định danh và alternate RGB.
Accessible name dùng nhãn text native. Tooltip/focus help của menu con mô tả đổi toàn tài liệu và Ctrl+Z;
status sau thao tác báo hệ màu/DPI và phương pháp ICC hoặc gần đúng. Mặc định desktop nạp sRGB và SWOP đã cài trên Windows;
profile người dùng đã chọn được giữ lại, còn profile không khả dụng được thay bằng cặp mặc định.

## Chính sách và giới hạn

File native v9 lưu mode, tên profile, DPI và cờ mode đã được xác lập.
v2–v8 không có mode đáng tin cậy: mở với metadata mặc định RGB/sRGB/72 nhưng giữ nguyên channels nguồn;
lần đổi mode tường minh đầu tiên chuẩn hóa tất cả paints và bật ràng buộc cho các lần ghi sau.
Đổi DPI trên file cũ bật sampling DPI riêng, không tự chuyển màu nguồn; Undo phục hồi cả cờ này.
Không giả định raster RGBA hay ICC bytes nguồn được chuyển thành separations; giữ nguyên asset/provenance.
SVG/DXF đi qua adapter hiện có rồi writer chuẩn hóa process paint trong tài liệu đã xác lập mode.
PDF/AI/CDR/EPS và PDF/X không được mở capability mới bởi tính năng này.
Tên profile mặc định chưa chứng minh proof/in offset. ICC conversion dùng transform Windows khi có đủ profile nguồn/đích;
nếu profile không khả dụng, shell dùng công thức subtractive dự phòng và báo rõ.

## Nghiệm thu

1. File > New, chọn CMYK hoặc RGB: vẽ shape/text/table/line/pen, kiểm source paints và palette đúng mode.
2. Edit > Document Color Mode, xác nhận: path fill/stroke, gradient, text và bóng trên mọi trang đổi trong một revision;
   Ctrl+Z phục hồi chính xác roots ban đầu; Spot/overprint/IDs/geometry giữ nguyên.
3. Edit > DPI: chọn 150; lưu/mở lại và Undo/Redo giữ giá trị. Màu và DPI độc lập.
4. Chuyển tab và Undo cập nhật nhãn/palette; không còn nút ribbon hoặc nút RGB/CMYK per-object tự do.
5. Kiểm thử native tiếng Anh/Việt, dark/light và cửa sổ hẹp; ghi riêng evidence thực tế sau build.

Các test tự động không thay nghiệm thu proof, hiệu năng hay thiết bị in.

## Bằng chứng triển khai 18/10/2026

- Nhóm regression mục tiêu: 14/14 suite pass, gồm color/ICC, commands/storage/gradient,
  render rasterizer/compositor, native color workflow, editor tools, desktop shell và vertical slice.
- Test chuyển đổi 303 đối tượng trên ba trang xác minh paints ẩn/khóa, gradient, text,
  shadow, Spot, overprint và Undo phục hồi đúng immutable source roots.
- Fixture v8 độc lập giữ trạng thái nguồn chưa gán mode; native v9 round-trip màu/DPI và từ chối metadata hỏng.
- CPU tăng buffer scratch khi DPI tăng; Direct2D thay sampling; Undo DPI khôi phục pixels CPU.
- Native workflow chạy tạo RGB/CMYK, shape gesture, SVG/table/text, hủy/xác nhận,
  menu Undo/Redo, DPI, đổi tab và save/reopen. Editor nhập màu dùng đúng process model của document.
- Đã xem ảnh native 1424×861: English/dark và Vietnamese/light, palette và menu Edit.
  Chưa đo màn hình thật nhiều DPI, cửa sổ hẹp, proof hay separations từ RIP.
- Architecture gates 12/12 pass, gồm LOC ≤800; doctor: 18 module, 0 issue.
- Giữ nguyên local edits và tích hợp các file dùng chung theo xác nhận trực tiếp của người dùng.
