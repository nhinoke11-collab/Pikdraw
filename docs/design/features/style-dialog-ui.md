# Phiếu thiết kế UI: Hộp thoại chọn màu nguồn (Style Color Dialog)

Ngày: 18/10/2026. Trạng thái: **Đã triển khai và kiểm thử**.
Tuân thủ: [Chuẩn UX/UI](../ui-ux-standard.md) và [Bố cục Workspace](../../architecture/workspace-layout.md).

## 1. Mục tiêu và trạng thái

- **Tên tính năng / task ID**: `style-dialog-ui-perf-20261018` / Module: `desktop` / Owner: `antigravity`.
- **Người dùng cần hoàn thành**: Chỉnh sửa màu nguồn (Fill, Stroke, Drop Shadow, Spot, CMYK, RGB, Grayscale) và độ dày viền (mm) thông qua hộp thoại siêu nhỏ gọn (480×272), không còn khoảng trắng thừa, thao tác kéo màu và di chuyển cửa sổ hoàn toàn ổn định và mượt mà.
- **Vấn đề đã khắc phục triệt để**:
  1. **Khoảng trắng thừa**: Cột phải đã được xếp động (`layout_right_column`): các kênh màu, Opacity, Viền và Overprint tự động kéo lên khít nhau tùy theo chế độ xem (3 hàng cho HSB/HSL/RGB, 4 hàng cho CMYK, 1 hàng cho Grayscale). Chiều cao dialog thu gọn xuống chỉ còn 272 px, loại bỏ hoàn toàn các khoảng trống thừa.
  2. **Bị trôi màu & nhảy về đỏ khi di chuyển con trỏ**: Lưu trạng thái HSV liên tục (`current_hue`, `current_sat`, `current_val`). Kéo trên Plane giữ nguyên Hue chính xác; kéo Hue giữ nguyên Saturation/Value. Khi chạm biên đen hoặc xám ($S=0$ hoặc $V=0$), Hue được bảo toàn qua `fallback_hue`, không bao giờ bị nhảy về đỏ $0^\circ$.
  3. **Khó kiểm soát khi di chuyển cửa sổ**: Hỗ trợ `WM_NCHITTEST` trả về `HTCAPTION` khi bấm vào thanh header trên cùng (giữa các combobox) hoặc thanh tiêu đề, giúp người dùng có thể kéo thả cửa sổ từ bất kỳ vị trí trống nào ở phần đầu hộp thoại.
  4. **Tăng tốc DWM**: Loại bỏ `SetWindowRgn` giúp DWM tăng tốc phần cứng, di chuyển cửa sổ 60/120+ FPS không độ trễ.
  5. **Bộ đệm bitmap**: `cached_plane` và `cached_hue_pixels` loại bỏ việc cấp phát và tính toán lại 66.600 pixel float trong mỗi lần `WM_PAINT`.

## 2. Bố cục kích thước chi tiết (Ultra-Compact Layout)

- **Kích thước vùng làm việc (Client Area)**: 480 × 272 DIP (giảm 72% diện tích so với bản gốc 760×610).
- **Hàng trên (y = 10..32)**:
  - Model: Combo `86, 10, 140, 160` (RGB, CMYK, Spot, Grayscale).
  - Viewer: Combo `330, 10, 138, 160` (HSB, HSL, RGB, CMYK).
- **Cột trái (x = 12..226)**:
  - Color Plane: `12, 38, 226, 178` (214 × 140 px, viền sắc nét, con trỏ tròn 2 lớp tương phản).
  - Hue Bar: `12, 184, 226, 198` (214 × 14 px, con trỏ định vị trắng-đen).
  - Swatch so sánh: `Gốc` (`48, 204, 52, 24`) và `Mới` (`144, 204, 82, 24`).
- **Cột phải (x = 246..468)**:
  - Các hàng kênh màu: Nhãn x=246 (rộng 94), ô nhập x=344 (rộng 124, cao 20, bước hàng 25px).
  - Độ đục / Opacity (%): Tự động đặt ngay dưới kênh màu cuối cùng (`y = 38 + active_rows * 25`).
  - Độ dày viền (mm): Đặt ngay dưới Opacity (nếu là Stroke).
  - Checkbox Overprint: Đặt ngay dưới Viền (hoặc Opacity).
- **Hàng đáy (y = 236..268)**:
  - Hint ghi chú: x=12, y=238, w=268, h=26, chữ nhỏ gọn gàng.
  - Nút bấm: `Áp dụng / Apply` (x=290, y=236, w=84, h=26, màu Accent) và `Hủy / Cancel` (x=384, y=236, w=84, h=26).

## 3. Kiểm thử và nghiệm thu

- Unit tests: `test_pikdraw_style_dialog` kiểm tra tính toán HSV/HSL, bảo toàn hue khi về đen/xám, định dạng số và nhãn đa ngôn ngữ.
- Tích hợp: `test_pikdraw_desktop_shell` đảm bảo tương thích hoàn toàn với luồng chọn màu và Undo/Redo của desktop shell.
