# Bố cục Workspace và Design System PikDraw 2026

Tài liệu này xác định đặc tả giao diện desktop, hệ thống Design Tokens và cấu trúc layout chuẩn cho PikDraw.
Bản thiết kế chuẩn tương tác (canonical visual reference) được lưu trữ tại [ui_design_2026.html](ui-reference/ui_design_2026.html), [ui_light_mode.html](ui-reference/ui_light_mode.html) và [ui_english_2026.html](ui-reference/ui_english_2026.html). Bản vẽ này thay thế toàn diện bản phác thảo SVG trước đây và là kim chỉ nam bắt buộc cho mọi agent triển khai UI tại tác vụ `ps05-native-preview` và `ps13-vertical-slice`.

---

## 1. Khung Bố Cục Chuẩn (2026 Cyber Layout)

```text
┌──────────────────────────────────────────────────────────────────────────────────┐
│ TOPBAR (38px): [PK PRO 2026] [Tên file · pulse] [Menus] [⌘K Search] [Pill Tabs] [Theme/Settings/Xuất] │
├──────────────────────────────────────────────────────────────────────────────────┤
│ CONTEXT RIBBON (34px): [Target Indicator] [X, Y mm] [🔒] [W, H mm] [Align/Boolean]│
├──────┬───────────────────────────────────────────────────────────┬───────────────┤
│ Tool │ Tab tài liệu · Thước đo mm ngang                          │ DOCK PHẢI     │
│ Strip├───────────────────────────────────────────────────────────┤ (280–320 px)  │
│ 48px │ Thước │                                                   │               │
│      │ dọc   │                     CANVAS / VIEWPORT             │ • Inspector   │
│ Select│       │            Chiếm 65–75% không gian làm việc       │   Thuộc tính  │
│ Node │       │                 Lưới chấm hạt 24×24px             │   Typography  │
│ Pen  │       │                                                   │   Tô/Viền/Hiệu ứng│
│ Shape│       │                                                   │               │
│ Text │       │                                                   │ • Layers &    │
│ Cut  │       │                                                   │   Assets      │
├──────┴───────┴───────────────────────────────────────────────────┴───────────────┤
│ STATUS BAR (26px): Đối tượng chọn · Tọa độ chuột mm · ICC Profile · Preflight · Zoom 25%│
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Hệ Thống Design Tokens (Bám sát `ui_design_2026.html`)

Mọi file Qt Style Sheets (`.qss`) và form Qt Designer (`.ui`) trong `apps/desktop/` phải áp dụng nghiêm ngặt các mã màu và kích thước sau:

### 2.1. Cyber Dark Palette (Chế độ Tối mặc định)
- **Nền chính ứng dụng (App Background)**: `#070b10`
- **Thanh Topbar / Header**: `#0c131d`
- **Thanh Context Ribbon**: `#0e1622`
- **Thanh công cụ trái (Tool Strip)**: `#0c131d`
- **Khung Dock phải (Inspector & Layers)**: `#0c131d`
- **Viền công nghệ mảnh (Hairline Tech Border)**: `rgba(255, 255, 255, 0.08)` hoặc `#1d2636`
- **Ô nhập liệu (Input Field)**: Nền `rgba(10, 16, 24, 0.7)`, viền `rgba(255, 255, 255, 0.09)`, hover/focus viền `rgba(0, 242, 195, 0.45)` nền `rgba(12, 20, 30, 0.95)`
- **Màu điểm nhấn (Cyber Accent Teal)**:
  - `DEFAULT`: `#00f2c3`
  - `HOVER`: `#00d2aa`
  - `LIGHT`: `#00a884`
  - `DIM GLOW`: `rgba(0, 242, 195, 0.12)`
  - Active tool shadow: `box-shadow: 0 0 14px rgba(0, 242, 195, 0.25)`
- **Văn bản**:
  - Tiêu chuẩn: `#e2e8f0` hoặc `#ffffff`
  - Mờ (Muted/Secondary): `rgba(255, 255, 255, 0.6)`
  - Chú thích nhỏ: `rgba(255, 255, 255, 0.4)`
- **Lưới Canvas (Dark Canvas Grid)**: Nền `#070c12`, lưới chấm tròn radial 24×24px với màu chấm `rgba(255, 255, 255, 0.14)`.

### 2.2. Light Mode Palette (Chế độ Sáng)
- **Nền chính**: `#ffffff` / `#f8fafc`
- **Panels & Docks**: `#f1f5f9` / `#e8ecf2`
- **Viền mảnh**: `rgba(0, 0, 0, 0.08)` / `#dcdfe6`
- **Màu điểm nhấn (Emerald Accent)**: `#00876a` / `#00a884`
- **Lưới Canvas**: Nền `#e8ecf2`, lưới chấm `rgba(0, 0, 0, 0.12)`.

### 2.3. Typography Chuẩn
- **Nhãn UI, Menu, Tiêu đề**: Font `Plus Jakarta Sans`, `-apple-system`, `Segoe UI`, `Roboto`.
- **Số liệu kỹ thuật, Tọa độ, Thước đo**: Font `JetBrains Mono`, `monospace` (`font-mono-num` với tính năng `tnum` tabular numbers).

---

## 3. Quy Cách Từng Thành Phần Chi Tiết

### 3.1. Topbar / Titlebar (Chiều cao cố định 38px)
1. **Logo & Brand**:
   - Icon vuông 24×24px bo góc `rounded-md` với nền gradient từ `#00f2c3` sang `#0ea5e9`, chữ `PK` đậm màu đen `#070b10`.
   - Nhãn thương hiệu: `PikDraw` kèm badge `PRO 2026` nền `#00f2c3/15`, chữ `#00f2c3`, viền `#00f2c3/30`.
2. **File Title & Trạng thái lưu**:
   - Tên tệp (vd: `Bien_Hieu_An_Phat.pik`) kèm icon file SVG màu `#00f2c3`.
   - Đèn LED tròn xanh `#00f2c3` hiệu ứng `animate-pulse` hiển thị trạng thái "Đã lưu tức thì".
3. **Thanh menu**: `Tệp`, `Sửa`, `Đối tượng`, `Thi công`, `Plugin` với kích thước 11px, hover đổi màu nhẹ.
4. **Spotlight Command Search (`Ctrl+K` / `⌘K`)**:
   - Hộp tìm kiếm bo tròn trung tâm, chiều cao 26px, chiều rộng tối đa 300px.
   - Placeholder: `"Tìm nhanh lệnh, công cụ..."` kèm phím tắt `⌘K`.
5. **Workspace Switcher dạng viên thuốc (Pill Tabs)**:
   - Cụm pill bo góc gồm 4 nút: `Vector` (có chấm tròn xanh active), `In ấn`, `Thi công`, `3D`.
   - Nhấn chuyển tab chỉ đổi bộ công cụ và panel, giữ nguyên tài liệu, selection và lịch sử undo/redo.
6. **Thao tác nhanh**:
   - Nút chuyển giao diện Sáng / Tối (Sun / Moon icon).
   - Nút mở Cài đặt (Settings Modal).
   - Nút `Xuất file` màu xanh gradient nổi bật.
   - Nút điều khiển cửa sổ (`─`, `□`, `✕`).

### 3.2. Context Tool Ribbon (Chiều cao cố định 34px)
- **Target Indicator**: Khối nhỏ bên trái hiển thị loại đối tượng đang chọn (vd: `Văn bản nghệ thuật (Group #402)`).
- **Geometry Matrix (Hệ thống nhập tọa độ hình học)**:
  - Các ô nhập: `X`, `Y`, nút Khóa tỷ lệ (Lock aspect ratio), `W`, `H`.
  - Hiển thị đơn vị thực tế (mm) với font số `JetBrains Mono`.
- **Căn chỉnh & Boolean**: Các nút Căn lề trái/giữa/phải, Phân bố đều, và menu dropdown Boolean (Hợp, Giao, Trừ, Chia).

### 3.3. Tool Strip Trái (Chiều rộng 48–56px)
- Chứa các nút công cụ chính (Selection, Direct Select/Node, Pen, Shapes: Rectangle/Ellipse, Text, Contour/Cut, Dimension, Zoom/Pan).
- Nút active có viền sáng và glow xanh Cyber Teal `#00f2c3`.

### 3.4. Canvas Viewport Trung Tâm (Chiếm 65–75% màn hình)
- Thước đo mm ngang và dọc chính xác.
- Vùng canvas hiển thị trang in/biển hiệu với khung viền selection màu xanh dương `#2788df` và 8 điểm mút neo (handles).
- Đường kích thước kỹ thuật hiển thị mũi tên hai chiều và thông số (vd: `3000 mm · 1:10`).

### 3.5. Inspector Dock Phải (Chiều rộng 280–320px)
- **Khu vực Thuộc tính**:
  - Thông số Font (Family, Size pt, Tracking, Line height).
  - Khối màu Tô (Fill) và Viền (Stroke) trực quan (kèm ô gạch chéo đỏ khi không có viền).
  - Danh sách Hiệu ứng sống (Non-destructive editable effects).
  - Nút chuyển Outline giữ nguyên bản chữ nguồn.
- **Khu vực Layers & Assets**:
  - Cây đối tượng phân cấp (Trang 1 -> Nền biển -> Dòng mô tả -> AN PHÁT -> Đường sản xuất).
  - Trạng thái khóa/ẩn hiện từng layer.

### 3.6. Status Bar Đáy (Chiều cao 26px)
- Hiển thị số lượng đối tượng đang chọn (`1 đối tượng · chữ sống`).
- Tọa độ chuột trực tiếp theo mm (`X: 100.00 · Y: 150.00 mm`).
- Profile màu hiện hành (`Màu: ICC profile mẫu`).
- Trạng thái Preflight (`Preflight: cần chạy` hoặc cờ xanh).
- Thanh trượt điều khiển Zoom (`25% − ▬ +`).

---

## 4. Hướng Dẫn Kỹ Thuật Cho Agent Triển Khai

Khi nhận task liên quan đến UI (`ps05-native-preview` và `ps13-vertical-slice`):
1. **Đọc tệp tham chiếu gốc**: Mở xem mã nguồn và giao diện tại [docs/architecture/ui-reference/ui_design_2026.html](ui-reference/ui_design_2026.html).
2. **Tuân thủ QSS Design Tokens**: Không tự ý bịa mã màu hoặc dùng màu xám mặc định của Qt. Mọi stylesheet phải sử dụng bảng mã màu mục 2.
3. **Cơ chế nạp động trong dev**: Tách biệt `.ui` forms trong `apps/desktop/ui/` và `.qss` styles trong `apps/desktop/themes/` để hỗ trợ tính năng **Reload UI (`F5`)** mà không cần build lại C++.
4. **Không đưa WebAssembly hoặc HTML runtime vào app native**: HTML chỉ là bản vẽ tham chiếu; runtime đích thực là **C++20 Qt Widgets + Skia**.
