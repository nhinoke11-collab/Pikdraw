# Phiếu thiết kế UI: Bảng Quản lý Layer và Đối tượng (Layers & Objects Docker)

**Task ID**: `layers-objects-panel-20261018`  
**Module**: `desktop` / `document`  
**Owner**: `antigravity`  
**Quyết định thiết kế**: Tự động tổng hợp và quyết định dựa trên chuẩn `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md` và khảo sát chuẩn Adobe Illustrator & CorelDRAW.

---

## 1. Mục tiêu và trạng thái

- **Tên tính năng / task ID / module / owner**:
  - Tên: Bảng Quản lý Lớp & Đối tượng trực quan (Hierarchical Layers & Objects Panel).
  - Task ID: `layers-objects-panel-20261018`.
  - Module: `apps/desktop` (Shell UI) & `core/document` (Commands & Model).
  - Owner: `antigravity`.
- **Người dùng cần hoàn thành việc gì, đầu vào và kết quả nhìn thấy**:
  - Người dùng cần quản lý cấu trúc cây phân cấp của tài liệu (Layer -> Group -> Object), bật/tắt hiển thị (Eye), khóa chỉnh sửa (Lock), bật/tắt xuất sản xuất (Print/CNC), đổi tên, chọn đối tượng từ bảng và sắp xếp lại thứ tự Z-order.
  - Kết quả: Bảng Layers ở Dock phải (Tab 1) hiển thị cấu trúc cây sắc nét, có các biểu tượng trực quan, hỗ trợ tìm kiếm/lọc, đồng bộ 2 chiều với Canvas và hoàn tác (Undo/Redo) đầy đủ.
- **Capability hiện có; phần dự kiến; nội dung không hỗ trợ cần báo**:
  - Đã có: `Page` chứa `std::vector<Layer> layers;`, `Layer` có `visible`, `locked`, `object_ids`. Selection quản lý danh sách `ObjectId`.
  - Phần triển khai: Hệ thống lệnh `layer_commands.hpp`, virtual tree list rendering trong dock, hit-testing các icon Eye/Lock/Print/Select/Expand, tìm kiếm theo tên/ID, lọc theo loại đối tượng, các nút footer thao tác nhanh.
  - Nội dung chưa hỗ trợ: Kéo thả layer giữa các cửa sổ ứng dụng khác nhau (không áp dụng).
- **Tài liệu/source snapshot được dùng**:
  - `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md`, `contracts/c2-document.md`, `layers-upgrade-specification.md`.

---

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định cụ thể |
| :--- | :--- |
| Menu chính | `Window > Layers` (hoặc `Cửa sổ > Lớp đối tượng`) để mở/focus tab Layers ở Dock phải. |
| Action/command ID | `action.window.layers_tab`, `command.layer.add`, `command.layer.remove`, `command.layer.set_visibility`, `command.layer.set_locked`, `command.layer.reorder`. |
| Loại thao tác | Dock panel tích hợp trong right dock tab (Tab 1: `Layers` / `Lớp`). |
| Standard toolbar | Không đặt icon riêng để tránh quá tải toolbar; mở qua Tab hoặc menu `Window`. |
| Toolbox / flyout | Không áp dụng (đây là dock quản trị đối tượng, không phải canvas drawing tool). |
| Property bar | Không thay thế property bar; phản ánh selection đồng bộ. |
| Properties | Tab 0 là Inspector/Properties; Tab 1 là Layers. Hai tab cạnh nhau trong cùng hệ dock rộng 280px. |
| Mở bảng thuộc tính | Click vào Tab "Layers" (vị trí `x: w - 186` đến `w - 93`, `y: 102..132`) hoặc menu `Window > Layers` (Phím tắt: `F7` theo chuẩn Illustrator / `Ctrl + F7` theo chuẩn Corel). |
| Context menu / shortcut | Phím tắt: `F7` để toggle tab Layers. Click phải trên row hiện Context Menu: Đổi tên, Khóa, Ẩn, Xóa, Lên trên, Xuống dưới. |
| Dialog/panel mới | Tái dùng Tab 1 có sẵn trong right dock (`active_dock_tab_ == 1`), không mở thêm cửa sổ popup riêng. |

---

## 3. Trường thuộc tính & Thao tác

| ID / label EN + VI | Bar / Properties / Dock | Kiểu, unit, default | Scope và source binding | Khi nào enable / Mixed | Preview, commit, validation |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `layer.search` / Tìm đối tượng | Dock Header | Text input string | Lọc danh sách hiển thị | Luôn enable | Lọc tức thì theo thời gian thực khi gõ |
| `layer.filter` / Lọc loại | Dock Header | Segmented 5 nút | Bộ lọc (All, Path, Text, Image, Group) | Luôn enable | Lọc tức thì |
| `item.visibility` / Ẩn hiện | Cột Eye trong Row | Boolean toggle, true | Thuộc tính `visible` của Layer/Object | Luôn enable | Commit ngay bằng Command có Undo |
| `item.locked` / Khóa | Cột Lock trong Row | Boolean toggle, false | Thuộc tính `locked` của Layer/Object | Luôn enable | Commit ngay bằng Command có Undo |
| `item.printable` / Xuất CNC | Cột Print trong Row | Boolean toggle, true | Thuộc tính `printable` của Layer | Luôn enable | Commit ngay bằng Command có Undo |
| `item.name` / Tên đối tượng | Cột Label trong Row | String UTF-8 | Tên hiển thị của Layer/Object | Luôn enable | Double-click sửa, Enter commit |

---

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Tab Layers | Thanh tìm kiếm & lọc | Danh sách Tree | Thanh chân dock (Footer) | Hint / Trạng thái |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Tài liệu rỗng | Hiện bình thường | Disable | Hiện "Chưa có đối tượng" | Chỉ bật nút "+ Lớp" | Trạng thái sẵn sàng tạo mới |
| Có đối tượng, không chọn | Hiện cây đầy đủ | Enable | Các row hiển thị bình thường | Bật "+ Lớp", tắt Up/Down/Delete | Hint: "Chọn đối tượng để chỉnh sửa" |
| Đang chọn 1 đối tượng | Tự cuộn đến row | Enable | Row được highlight, proxy ■ sáng | Bật đầy đủ nút thao tác | Hiện tên đối tượng và mã ID |
| Đang chọn nhiều đối tượng | Highlight các row | Enable | Các row được chọn sáng | Bật Xóa, Nhóm, Di chuyển | Hiện số lượng đối tượng đang chọn |
| Đối tượng bị khóa | Icon Khóa màu cam | Enable | Không thể chọn/sửa trên canvas | Nút Xóa/Up/Down bị disable | Hint: "Đối tượng đang bị khóa" |

---

## 5. Icon, tên và hướng dẫn

- **Icon**:
  - Mắt `👁` (Draw via GDI Arc/Ellipse/Pupil hoặc Polyline)
  - Khóa `🔒` (Draw via GDI Rect body + Arc shackle)
  - Máy in / Xuất `🖨` (Draw via GDI Box + Paper feed)
  - Loại đối tượng:
    - Path/Curve: Icon hình bút Pen / đường cong bezier
    - Text: Icon chữ `T`
    - Image: Icon khung ảnh núi non
    - Group: Icon thư mục Folder `📁`
- **Tên & Hướng dẫn (EN/VI)**:
  - Header: `LAYERS & OBJECTS` / `LỚP & ĐỐI TƯỢNG`
  - Search placeholder: `Search objects (#ID)...` / `Tìm kiếm (#ID)...`
  - Empty state: `The document contains no objects.` / `Tài liệu chưa có đối tượng nào.`
  - Footer buttons: `+ Layer`, `Group`, `Up ▲`, `Down ▼`, `Delete 🗑`.
