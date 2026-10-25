# Raster Export dialog

## Mục tiêu và trạng thái

Cho phép xuất artwork của trang đang mở hoặc mọi trang thành TIFF/JPEG theo DPI và RGB ICC do người dùng chọn. Đây là tác vụ tạo file; nó không sửa document và không tạo Undo. Phần SVG hiện tại giữ nguyên ở **File > Export SVG…**. Raster v1 chưa xuất riêng các object đang chọn vì renderer hiện chỉ nhận snapshot trang đầy đủ.

Trạng thái 21/10/2026: hộp thoại cấu hình native và lệnh menu đã được triển khai. Phiếu này là hợp đồng UI hiện hành; kiểm thao tác/visual trên cửa sổ native vẫn chưa được nghiệm thu.

## Vị trí và action

| Mục | Quyết định |
| --- | --- |
| Menu chính | `File > Export Raster…`, đặt cạnh `File > Export SVG…` |
| Action ID | `action.file.export_raster`; một lần, cùng handler từ menu và shortcut nếu sau này được gán |
| Standard toolbar | Không thêm icon; export là tác vụ ít dùng và có cấu hình nhiều trường |
| Toolbox / flyout | Không áp dụng; đây không phải công cụ vẽ |
| Property bar | Không áp dụng; thiết lập chỉ thuộc tác vụ file, không phải trạng thái tool/selection |
| Properties | Không áp dụng; DPI/profile/format là tùy chọn cho lần xuất, không phải thuộc tính document |
| Context menu | Không thêm alias riêng |
| Dialog | Modal native settings trước hộp lưu file hệ thống; Cancel không tạo file |

## Bố cục và field

Dialog dạng form gọn, nhãn ở trái control, nút Browse sát tên profile, Export/Cancel ở cuối. Thứ tự tab: Format → Scope → Resolution → JPEG quality → Browse RGB profile → Embed profile → Export → Cancel. Không có toolbar, toolbox, contextual bar, Properties, hay icon mới.

| Field | Giá trị và validation | Phạm vi / đồng bộ |
| --- | --- | --- |
| Format | TIFF (LZW, lossless) hoặc JPEG (lossy, 4:4:4) | Chọn loại file và filter ở hộp lưu; JPEG quality chỉ bật với JPEG |
| Scope | Active page hoặc All pages (mỗi trang một file) | Không phụ thuộc selection; file batch được xác nhận trước khi bắt đầu |
| Resolution | 72, 150, 300, 600 DPI; mặc định theo DPI của trang nếu có preset tương ứng, nếu không là 300 | Chỉ thuộc export; kích thước px tính từ W/H mm của trang |
| JPEG quality | 80%, 90%, 100%; mặc định 100% | Chỉ JPEG; không ảnh hưởng TIFF |
| Output profile | System sRGB mặc định; Browse nhận ICC RGB hợp lệ, tối đa 1 MiB | Renderer chuẩn hóa page colors sang sRGB rồi writer chuyển pixel sang profile này |
| Embed output profile | Bật mặc định | Nhúng đúng bytes của profile đích. Tắt chỉ bỏ metadata; pixels vẫn được convert và người dùng phải gắn profile đích ở workflow sau |

Selection export chưa được cung cấp; dialog phải nói rõ đang xuất toàn bộ artwork trang. Profile CMYK/Gray bị từ chối vì TIFF/JPEG writer hiện phát RGB. Nếu profile trang không thể preview/CMM-convert, export báo lỗi thay vì âm thầm gán màu khác. Trước render, giới hạn 16.384 px mỗi cạnh, 32 MP và buffer RGBA 128 MiB; lỗi giới hạn hiện trước khi tạo file.

## Labels, help, status và hành vi

- Menu: `Export Raster…` / `Xuất Raster…`; accessible name cùng tên menu, không có icon.
- Field labels: `Format / Định dạng`, `Scope / Phạm vi`, `Resolution / Độ phân giải`, `JPEG quality / Chất lượng JPEG`, `Output profile / Profile đầu ra`, `Embed ICC profile / Nhúng profile ICC`.
- Tooltip không cần thiết cho control có label và mô tả trực tiếp. Browse có accessible name `Browse RGB ICC profile / Chọn profile ICC RGB`.
- Hint trong dialog nêu trang luôn có nền trắng, output RGB, selection bị bỏ qua, màu được chuyển sang profile đích và artwork không đổi.
- Status: đang xuất có số file đã xong/tổng số và Esc yêu cầu hủy sau file hiện tại; hoàn tất báo số file; lỗi nêu giới hạn hoặc profile.
- Batch ghi từng file atomic; hủy/lỗi sau một trang có thể giữ các trang đã commit và báo số file đã xong.
- Cancel trước khi bắt đầu không tạo file. Lỗi validation không ghi đè file đích. Sau khi bắt đầu, Cancel không rollback file đã commit.

## Acceptance

- `AC-RASTER01`: active page TIFF 300 DPI khớp tỷ lệ mm→px, có sRGB ICC và không sửa revision/dirty/Undo.
- `AC-RASTER02`: JPEG giữ DPI/profile, quality và alpha flatten theo output profile được nêu rõ.
- `AC-RASTER03`: page ICC RGB/CMYK được preview qua CMM rồi chuyển sang RGB destination; profile sai model/semantic bị từ chối.
- `AC-RASTER04`: All Pages tạo hậu tố `-page-001`, xác nhận ghi đè batch, báo partial completion sau cancel/error.
- `AC-RASTER05`: invalid path, existing output, oversized target/profile, render error và cancel trước start không để file tạm.
- Kiểm tab order, focus, cửa sổ hẹp, EN/VI và light/dark trên Win32 runtime; mockup/spec hoặc CTest không thay nghiệm thu UI thật.

## Bằng chứng hiện có

Lệnh `.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_actions,test_pikdraw_interop_raster_image_writer,test_pikdraw_interop_svg_exporter -TestRegex '^(test_pikdraw_desktop_actions|test_pikdraw_interop_raster_image_writer|test_pikdraw_interop_svg_exporter)$'` đạt **3/3 CTest**. Bằng chứng này xác nhận route action và writer SVG/TIFF/JPEG trong harness; không xác nhận tab order, hình ảnh dialog, file picker hay tương tác thật trên cửa sổ native.

Bổ sung 21/10/2026: `test_pikdraw_interop_raster_image_writer` đạt **1/1 CTest** sau khi writer chuyển pixel bằng `ProfileColorTransform`. Fixture tạo profile RGB có primaries khác sRGB, so sánh pixel TIFF không gắn profile với scalar transform oracle trong sai số tối đa 1 mức 8-bit; alpha được giữ và profile nhúng phải trùng chính xác profile đích. TIFF gắn profile được decode qua đường WIC sang sRGB với sai số tối đa 5 mức 8-bit trên máy này (regression cho phép tối đa 8). Đây là kiểm tra bulk-vs-scalar và decoder readback trên Windows, chưa phải đối chiếu với ứng dụng/RIP độc lập. Profile output mismatch bị từ chối trước khi tạo tệp. UI native vẫn chưa được nghiệm thu trực quan.
