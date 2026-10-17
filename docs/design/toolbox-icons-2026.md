# Thiết Kế Lại Toàn Bộ Icon Toolbox & Chuẩn Bố Cục 2026 (PikDraw Desktop)

Ngày hoàn thiện: 17/10/2026.  
Mô-đun: `apps/desktop` · Trạng thái: Đã tích hợp và kiểm nghiệm thực tế.

---

## 1. Phân Tích Nguyên Nhân Gốc Rễ

### 1.1. Nguyên nhân icon bị nhòe vỡ, thô kệch và rẻ tiền
- **GDI 1-bit Rasterization**: Trước đây, các icon trong `native_window_paint.cpp` được vẽ bằng các lệnh đồ họa GDI cổ điển (`MoveToEx`, `LineTo`, `Ellipse`, `Rectangle`) kết hợp với bút `CreatePen(PS_SOLID, 2, color)`.
- GDI không có cơ chế **khử răng cưa (Anti-Aliasing)**. Thuật toán Bresenham nguyên bản chỉ tô các pixel số nguyên 0/1, khiến mọi đường chéo (mũi tên chọn, đoạn thẳng, ngòi bút, dao cắt) bị gãy bậc thang (aliasing/jaggies) thô ráp.
- Khi vẽ đường chéo với nét dày 2px trong GDI, các điểm giao nhau bị méo mó, biến dạng, khiến hình ảnh trông như đồ họa thập niên 1990 trên màn hình độ phân giải cao.

### 1.2. Nguyên nhân bố cục nút bị lệch (Layout Misalignment)
- **Lệch tâm quang học**: Dải công cụ có chiều rộng `tool_w = 52px`. Mỗi nút bấm chiếm khung chữ nhật `rc_tb = { 6, ty, tool_w - 6, ty + 36 }`, tức chiều ngang từ `x = 6` đến `x = 46` (độ rộng 40px). Tâm ngang chính xác của nút phải là `cx = (6 + 46) / 2 = 26px`. Tuy nhiên, mã nguồn cũ hardcode tọa độ vẽ icon tại `center_x = 22px`, đẩy toàn bộ icon lệch 4px về bên trái.
- **Ký hiệu mở rộng (Flyout Arrow) chèn ép**: Trước đây, dấu mũi tên mở flyout cho các nhóm công cụ (Hình cơ bản, Đường vẽ, Diện mạo) được vẽ tại `arrow_x = 43, arrow_y = ty + 18`. Vị trí này nằm lơ lửng ngay chính giữa mép phải nút, đè sát vào viền và va chạm với không gian của icon, tạo cảm giác nút bị xô lệch, méo mó.

### 1.3. Vấn đề icon Pen Tool chưa chuẩn
- Icon Pen Tool cũ (tool 8) được mô tả bằng một đa giác 4 điểm xiên xẹo kèm 2 nét gạch chéo rời rạc, không thể nhận diện được hình dáng công cụ bút vẽ đường cong Bezier.

---

## 2. Giải Pháp Thiết Kế & Kiến Trúc Kỹ Thuật 2026

### 2.1. Vector Anti-Aliasing Engine (GDI+ Dynamic Loader)
- Tích hợp bộ nạp động GDI+ (`GdiplusApi`) thông qua `LoadLibraryW(L"gdiplus.dll")` và `GetProcAddress`. 
- Cấu hình chế độ vẽ đồ họa vector chất lượng cao:
  - `kSmoothingModeAntiAlias (4)`: Khử răng cưa subpixel toàn diện cho mọi đường cong và đường chéo.
  - `kPixelOffsetModeHalf (4)`: Căn chỉnh nửa pixel giúp nét vẽ đạt độ sắc nét quang học tối đa.
  - `kLineCapRound (2)` và `kLineJoinRound (2)`: Đầu mút và khớp nối được bo tròn mềm mại, loại bỏ hoàn toàn các góc nhọn thô cứng.
  - Độ dày nét đồng nhất chuẩn thiết kế 2026: `1.55px` thanh thoát, tinh tế.
- **Tính tự chủ và an toàn**: Nạp động thư viện chuẩn Windows đảm bảo không gây xung đột liên kết, không yêu cầu thay đổi `CMakeLists.txt`, đồng thời có tầng fallback GDI tự động nếu môi trường đặc biệt không có GDI+.

### 2.2. Chuẩn Hóa Bố Cục Nút Bấm & Ký Hiệu Flyout
- **Tâm quang học tuyệt đối**: Toàn bộ icon được định vị chính xác tại `btn_cx = 26px` và `btn_cy = ty + 18px`.
- **Flyout Indicator chuẩn chuyên nghiệp**:
  - Áp dụng chuẩn thiết kế của các phần mềm đồ họa hàng đầu thế giới (Adobe Illustrator, Photoshop, CorelDRAW, Affinity Designer 2026).
  - Ký hiệu mở nhóm công cụ là một **tam giác đặc góc dưới-phải (Bottom-Right Corner Triangle)** kích thước 4x4px đặt tại `(rc_tb.right - 5, rc_tb.bottom - 5)`.
  - Tam giác nằm gọn ở góc dưới, hoàn toàn không xâm lấn không gian của icon chính giữa, giúp nút bấm thoáng đãng, sang trọng và chuẩn mực.

---

## 3. Chi Tiết Thiết Kế Hệ Thống Icon 2026

| Mã Tool | Tên Công Cụ | Thiết Kế Cũ (GDI Aliased) | Thiết Kế Mới 2026 (Subpixel Vector AA) |
|---|---|---|---|
| **0** | **Selection (Chọn)** | Con trỏ đa giác thô, đuôi răng cưa | Mũi tên con trỏ vector 45° thanh thoát, notch góc sắc nét, thân nghiêng tinh tế. |
| **1** | **Rectangle (Chữ nhật)** | Khung chữ nhật sắc cạnh thô | Hình chữ nhật bo góc nhẹ 2.2px (squircle hiện đại), nét 1.55px mềm mại. |
| **2** | **Ellipse (Elip)** | Hình elip pixel vỡ viền | Đường tròn vector đường kính 14px khử răng cưa mượt mà, cân xứng tuyệt đối. |
| **3** | **Hand / Pan (Bàn tay)** | Găng tay pixel vuông vức 1995 | Bàn tay mở vector công thái học, ngón tay thon gọn, đường cong lòng bàn tay mượt mà. |
| **4** | **Text (Văn bản)** | Chữ 'A' thô sơ 3 nét | Chữ **'T'** typographic sang trọng với chân serif và thanh ngang cân đối. |
| **5** | **Knife (Dao cắt)** | Hình thang và vạch cắt vụng về | Dao cắt thủ công vector (craft scalpel) 45° với lưỡi dao vát và đường rãnh cắt dẫn hướng. |
| **6** | **Line (Đoạn thẳng)** | Nét chéo pixel với 2 đốm tròn méo | Đoạn thẳng 45° chuẩn xác kết thúc bằng 2 điểm neo (vector anchor nodes) 3.6px. |
| **7** | **Polyline (Gấp khúc)** | Ziczac 4 điểm đốm thô | Đường gấp khúc 3 điểm neo vector với khớp nối tròn và các node điều khiển. |
| **8** | **Pen Tool (Bút Bezier)** | Khối đa giác xiên xẹo không rõ hình | **Ngòi bút máy (Fountain Pen Nib)** chuẩn mực quốc tế: mũi nhọn 45°, rãnh dẫn mực, lỗ thở (breather hole) tròn ở tâm, vai cong mở rộng và đai cổ bút. |
| **9** | **Polygon (Đa giác)** | Ngôi sao / đa giác nhấp nhô | Hình lục giác đều (Hexagon) hình học chuẩn xác, góc bo nhẹ chống gãy nét. |
| **10** | **Appearance (Diện mạo)** | 3 vạch ngang với 3 hình tròn lệch | Bộ 3 thanh trượt điều khiển (equalizer/filters) với ray trượt thanh mảnh và nút kéo tròn. |
| **11** | **Gaussian Blur (Mờ)** | 3 vòng tròn đồng tâm thô | Điểm hội tụ ở tâm lan tỏa ra các vòng sóng mờ mềm mại với độ mờ chuyển tiếp. |
| **12** | **Drop Shadow (Đổ bóng)** | 2 khối chữ nhật lồng nhau thô | Khối đối tượng phía trước với lớp bóng đổ mờ nổi (elevation shadow) phía sau. |

---

## 4. Bằng Chứng Nghiệm Thu (Evidence & Verification)

Bộ kiểm thử tự động của Desktop Shell (`test_pikdraw_desktop_shell`, `test_pikdraw_pen_native_shell`, `test_pikdraw_drawing_tool_ui`) kiểm tra trực tiếp icon rendering, trạng thái active/flyout và vượt qua 100%. Không lưu trữ ảnh chụp tạm thời trong kho mã nguồn.
