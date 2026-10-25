# Xuất bản gốc ảnh raster đã chọn

Ngày 24/10/2026. ID: `action.bitmap.export_master`. Module: desktop + raster + interop.

## 1. Mục tiêu và trạng thái

Cho phép xuất riêng một ảnh đã chọn ở đúng kích thước pixel của graph hiện hành thành TIFF không mất dữ liệu. Ảnh nguồn trong tài liệu không bị sửa; graph chỉnh sửa được áp khi tạo tệp đầu ra. Master RGBA16LE phải đi ra TIFF 16-bit, không qua preview RGBA8. Mẫu pixel giữ profile RGB nguồn khi chưa được chuyển; mẫu đã chuyển/giả định sRGB nhận profile sRGB.

Đã triển khai pipeline code-level và kiểm thử ảnh crop 16-bit → TIFF 16-bit. Chưa kiểm thao tác menu/Save native trực quan, chưa đo ảnh lớn/tile, chưa hỗ trợ xuất master CMYK, linked source lazy, TIFF BigTIFF hoặc chọn profile đầu ra khác. Xuất trang tổng hợp vẫn là RGBA8.

## 2. Placement và tương tác

| Mục | Quyết định |
| --- | --- |
| Menu chính | `Bitmaps > Export Bitmap Master (TIFF)…` / `Bitmaps > Xuất bản gốc ảnh (TIFF)…`; thao tác thuộc ảnh pixel đang chọn |
| Action ID | `action.bitmap.export_master`; enable khi chọn đúng một ImageObject, pixel buffer khớp kích thước và profile mẫu đã xác định |
| Loại thao tác | Tác vụ tệp một lần, chạy nền, có thể hủy sau tệp hiện hành |
| Standard toolbar | Không áp dụng; không chiếm thêm icon cho thao tác ít dùng |
| Toolbox / flyout | Không áp dụng; không thao tác trực tiếp trên canvas |
| Property bar | Không áp dụng; không có thuộc tính thường dùng hoặc draft để sửa nhanh |
| Properties | Không áp dụng cho action một lần; thông tin ảnh/export detail sẽ nằm trong Properties khi có panel Image Info |
| Mở bảng thuộc tính | Không áp dụng; lệnh chỉ mở Save dialog sẵn có |
| Context menu / shortcut | Chưa có alias hoặc shortcut; menu chính là đường tìm lệnh |
| Dialog | Dùng Save dialog TIFF hiện có. Không thêm form tham số vì đầu ra bảo toàn kích thước và profile hiện hành |

## 3. Trường, label và help

Không có field chỉnh sửa. Pixel dimensions lấy từ graph đã đánh giá; bit depth theo master; DPI lấy metadata nguồn (96 DPI nếu metadata không hợp lệ). ICC nhúng là profile sRGB cho pixel `AssumedSRgb`/`ConvertedToSRgb`, hoặc ICC RGB nguồn đã xác thực cho `EmbeddedProfileUnapplied`. Màu không rõ basis hoặc thiếu profile bị từ chối trước khi publish file.

| Nội dung | EN / VI |
| --- | --- |
| Icon SVG | Không áp dụng: menu native hiện dùng mục chữ; không thêm icon giả |
| Label | `Export Bitmap Master (TIFF)…` / `Xuất bản gốc ảnh (TIFF)…` |
| Accessible name | Theo label menu; Save dialog dùng accessible controls native Windows |
| Tooltip / focus help | Menu native hiện chưa có tooltip riêng; help của action: “Export selected image pixels at full resolution as a lossless TIFF master.” / “Xuất pixel ảnh đã chọn đủ độ phân giải thành TIFF không mất dữ liệu.” |
| Status | “Exporting full-resolution TIFF bitmap master · Esc cancels after this file” / “Đang xuất ảnh gốc đủ pixel thành TIFF · Esc hủy sau tệp hiện tại” |
| Error / disabled | Chọn đúng một ảnh có pixel đầy đủ và profile đã xác định; lỗi worker hiển thị diagnostic cụ thể |
| Help đầy đủ | Chọn ảnh → Bitmaps → Export Bitmap Master (TIFF) → chọn vị trí → mở TIFF để kiểm kích thước, bit depth và ICC. Xuất không đổi artwork; Undo không áp dụng |

## 4. Ma trận ngữ cảnh và acceptance

| Ngữ cảnh | Hành vi |
| --- | --- |
| Không có tài liệu hoặc không selection | Action disabled |
| Chọn một ảnh có RGBA8/RGBA16LE đầy đủ | Mở Save dialog; worker đánh giá graph và ghi TIFF |
| Chọn nhiều đối tượng, vector, ảnh khóa pixel hoặc thiếu buffer | Action disabled |
| Profile `EmbeddedProfileUnapplied` và ICC RGB nguồn hợp lệ | Cho phép graph hình học rồi nhúng profile nguồn; thao tác chỉnh màu bị từ chối |
| Profile `LegacyUnknown` hoặc thiếu profile của mẫu unmanaged | Action disabled hoặc worker trả lỗi trước khi tạo tệp |
| Đang xuất / chờ RAM | Hiện tiến độ và đường hủy qua Esc; không chờ worker trong event loop |
| Thành công | TIFF lưu đủ pixel theo graph, depth, DPI và ICC; document revision/history không đổi |

Given ảnh RGBA16LE 2×1 có crop node chọn pixel thứ hai, When xuất Bitmap Master, Then TIFF phải là 1×1 RGBA16, giữ các mẫu 16-bit (sai số khi đọc qua cùng ICC tối đa 1 code), alpha, DPI và profile. Một ảnh `LegacyUnknown` phải bị từ chối và không để lại output. Regression nằm trong `test_pikdraw_raster_bitmap_export`; menu/action ID có assertion trong `test_pikdraw_desktop_actions`.

Chưa đạt: Save dialog native, focus, DPI scaling, output lớn, cancel giữa encode, linked/lazy asset và page composition 16-bit cần kiểm tra riêng. Không suy ra các gate này từ unit test hoặc build.
