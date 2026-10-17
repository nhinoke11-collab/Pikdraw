# Align, Distribute và Shaping theo ngữ cảnh đối tượng

## 1. Mục tiêu và trạng thái

- **Task:** `align-distribute-panel-followup-20261017` / Desktop; geometry ordering regression: `align-distribute-geometry-ordering-20261017`.
- **Mục tiêu:** căn chỉnh và phân bổ theo cạnh/tâm/khoảng hở; mở bảng thao tác đầy đủ khi cần mà không biến Properties mặc định thành lưới lệnh; Shaping chỉ hiện khi cặp đối tượng hợp lệ.
- **Runtime:** Win32/GDI. Thanh context vẫn dùng icon GDI theo asset SVG; bảng Align dùng các nút chữ ngắn trong dock Properties hiện có.
- **Năng lực:** sáu kiểu Align theo selection/key object/page; Center to Page; tám kiểu Distribute (khoảng hở, tâm và cạnh theo hai trục), gồm khoảng hở tự động và nhập khoảng hở cố định theo mm. Mỗi thao tác hình học là transaction có Undo.
- **Giới hạn hình học khác:** Boolean chỉ nhận đúng hai path hình chữ nhật song song trục sau transform; không có Boolean path tổng quát, Boundary hoặc CNC Auto-Weld tổng quát.
- **Nguồn quyết định:** `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md`, ADR 0008. Panel tái dùng Inspector/Properties để giữ dock hiện tại, không tạo cửa sổ nổi.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định cụ thể |
| --- | --- |
| Menu chính | `Object > Align & Distribute`: sáu Align, Center to Page và tám Distribute. `Object > Shaping`: Weld/Union, Difference, Intersect, Exclude, Reverse Difference và Keep Source/Target. `Window > Align & Distribute Panel` mở/focus vùng Align trong Inspector. |
| Action ID | Object: `ObjectAlignLeft/Center/Right/Top/Middle/Bottom`, `ObjectCenterPage`, `ObjectDistributeHorizontal/Vertical`, `ObjectDistributeLeftEdges/CentersHorizontal/RightEdges/TopEdges/CentersVertical/BottomEdges`. Mở bảng: `WindowAlignDistributePanel`. Boolean dùng các action IDs đang có. |
| Loại thao tác | Align/Distribute/Shaping là lệnh một lần qua `DocumentWorkflow`; đổi mốc căn và nhập gap chỉ sửa trạng thái app, không ghi lên tài liệu. |
| Standard toolbar | Không áp dụng: các lệnh phụ thuộc số lượng/capability của selection và đã có menu/property bar. |
| Toolbox / flyout | Không áp dụng: đây không phải mode vẽ tương tác liên tục trên canvas. |
| Property bar | Giữ cụm icon ngữ cảnh gọn cho sáu Align, Center Page, hai lệnh gap tự động ngang/dọc, Group và các Boolean hợp lệ. Các mode phụ dùng menu/panel; không thêm một hàng property bar. |
| Properties | Mặc định là thông tin đối tượng. Trong Inspector có ba chế độ nhỏ `Objects`, `Style`, `Align`; `Align` hiển thị theo yêu cầu và dùng lại dock. Hệ lệnh đầy đủ không xuất hiện mặc định trong phần Properties đối tượng. |
| Context menu / shortcut | Không có context menu chuột phải trên canvas. `Ctrl+Shift+A` mở `Window > Align & Distribute Panel`; các alias khác giữ binding hiện có: P căn giữa trang, Shift+L/C/R/T/E/B cho Align tương ứng. Không gán shortcut cho từng kiểu Distribute. |
| Dialog/panel mới | Không tạo cửa sổ/dock riêng. Panel Align là view tùy chọn trong Inspector, mở từ `Window` hoặc `Ctrl+Shift+A`; bấm `Objects` quay lại Properties. |

## 3. Trường thuộc tính

| ID / label EN + VI | Bar / Properties | Kiểu, unit, range, default | Scope và binding | Khi nào enable / Mixed | Preview, commit, validation |
| --- | --- | --- | --- | --- | --- |
| `distribute_spacing_mm` / Fixed gap (mm) · Khoảng hở cố định (mm) | Inspector > Align | Số thập phân, mm, 0–40,000 mm, mặc định 10 mm | App UI state; không ghi vào tài liệu hay thay đổi khi đổi selection | Có thể nhập mọi lúc; lệnh Áp ngang/dọc cần từ 2 đối tượng chỉnh sửa được | Nhấp field, gõ số, Enter xác nhận; dấu phẩy thập phân được chuẩn hóa; Esc hủy; giá trị âm, không hữu hạn hoặc vượt giới hạn bị từ chối và giữ draft để sửa. Áp dụng H/V là transaction; Ctrl+Z hoàn tác. |
| Align reference / Mốc căn | Inspector > Align | `Selection`, `Key Object`, `Page`; mặc định `Selection` | App UI state | Key Object yêu cầu ít nhất 2 đối tượng; Key Object là đối tượng chọn cuối | Đổi mốc không sửa document. Sáu nút Align chạy transaction khi khả dụng. |

Các giá trị trên property bar và panel cùng gọi `DocumentWorkflow`; panel không tạo nguồn dữ liệu thứ hai. Align theo selection/key yêu cầu ít nhất hai đối tượng có bounds chỉnh sửa được; căn theo page và Center to Page có thể dùng một. Phân bổ cạnh/tâm cần ít nhất hai; chia khoảng hở tự động cần ít nhất ba; khoảng hở cố định cần ít nhất hai. Nút không hợp lệ được disable và hiển thị màu muted, không tạo kết quả giả.

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Menu/tool | Property bar | Properties | Hint / lý do |
| --- | --- | --- | --- | --- |
| Không có tài liệu/selection | Lệnh Object mờ; Window vẫn mở panel | Không có lệnh selection | Objects mặc định; Align cho biết chưa đủ selection | Chọn đối tượng trước |
| Một đối tượng | Center to Page bật; Align theo Page bật; Distribute và Key Object tắt | Center Page nếu capability hiện có | Panel Align vẫn mở được, lệnh thiếu đối tượng bị disable | Cạnh/tâm và gap cần ít nhất 2; auto gap cần 3 |
| Hai đối tượng hợp lệ | Align và các phân bổ cạnh/tâm bật; auto gap tắt; fixed gap H/V bật | Align, Center Page, Group/Boolean theo capability | Objects vẫn gọn; Align mở theo yêu cầu | Fixed gap yêu cầu 2+, equal gap cần chuỗi 3+ |
| Ba đối tượng trở lên | Tám lệnh Distribute theo capability | Hai auto-gap được giữ ở context bar; Shaping theo đúng cặp | Panel hiển thị đủ Align và Distribute | Ctrl+Z hoàn tác một thao tác |
| Locked/read-only hoặc bounds không hợp lệ | Lệnh tương ứng mờ | Nút không đủ điều kiện ẩn | Có thể xem Properties; không giả giá trị chỉnh được | Mở khóa/chọn đối tượng hỗ trợ |
| Đang nhập gap | Lệnh panel khác commit theo policy click hiện hành | Không làm đổi draft tool khác | Field có focus highlight; Enter áp dụng giá trị, Esc hủy | Gap phải là số mm không âm trong giới hạn |
| Busy/error/unsupported | Không có worker mới trong các lệnh này | — | Không giả success | Thao tác lỗi không phát sinh transaction |

## 5. Icon, tên và hướng dẫn

| Nội dung | EN / VI hoặc tham chiếu |
| --- | --- |
| Icon SVG | `apps/desktop/resources/icons/object-context-actions.svg`: Align, Center Page, auto gap H/V, Group và Shaping tiếp tục dùng symbol hiện có, tự vẽ trong repo. Các mode bổ sung trong panel/menu dùng nhãn chữ; không tạo icon mới khó phân biệt giữa cạnh/tâm. |
| Label ngắn | Dock: `Objects`, `Style`, `Align` / `Đối tượng`, `Kiểu`, `Căn chỉnh`. Panel có `Selection`, `Key Object`, `Page`, `Center Page`; các nút Distribute ghi rõ `Equal gap`, `Centers`, `Left edges`… / `Khoảng hở`, `Tâm`, `Mép trái`… |
| Accessible name | Nút trong GDI shell có nhãn chữ nhìn thấy; UI Automation/MSAA cho toàn shell chưa được cung cấp và chưa được review. |
| Tooltip + focus help | Hover panel giải thích mốc căn, hướng phân bổ, số đối tượng tối thiểu, gap mm và `Ctrl+Shift+A`. Center Page giải thích dịch toàn selection như một cụm. |
| Status hint | Khi mở: “Bảng Căn chỉnh & Phân bổ · chọn mốc rồi chọn lệnh · Ctrl+Z để hoàn tác” / “Align & Distribute · choose a reference and operation · Ctrl+Z to undo”. Khi thao tác thành công, status nêu lệnh và Undo. |
| Disabled/error | Equal gap tự động cần 3 đối tượng; các kiểu cạnh/tâm và fixed gap cần 2; Key Object cần 2. Gap âm, vô hạn hoặc trên 40,000 mm bị từ chối. |
| Help đầy đủ | `Window > Align & Distribute Panel` hoặc `Ctrl+Shift+A`; chọn mốc căn, chọn Align/Distribute; nhập Fixed gap (mm) rồi Apply Horizontal/Vertical; Ctrl+Z để hoàn tác. |

## 6. Hành vi và bằng chứng nhận

- **AC-ALIGN-01:** Given 3 rectangle khác kích thước, When áp dụng Align/Center Page hoặc Equal gap, Then bounds đúng theo reference/span; undo khôi phục vị trí.
- **AC-ALIGN-02:** Given dock ở History, When gọi `Ctrl+Shift+A`/lệnh Window, Then Inspector được focus ở chế độ Align; Objects/Style vẫn chuyển được và không nhét action grid vào Properties mặc định.
- **AC-ALIGN-03:** Given 3 đối tượng, When click Horizontal Equal gap trong panel, Then khoảng hở bằng nhau; nhập `12,5` mm rồi Apply Vertical phân bổ gap 12.5 mm; giá trị âm bị từ chối; mỗi apply có một bước Undo.
- **AC-DIST-01:** Given đối tượng có kích thước khác nhau, When phân bổ theo tâm, cạnh phải hoặc cạnh dưới, Then thuật toán sắp xếp theo chính tọa độ đó và các tọa độ đích cách đều. Regression: `distribution_orders_items_by_the_distributed_coordinate`.
- **AC-CTX-01–04:** Kiểm capability theo số lượng/cặp, từ chối Boolean không hỗ trợ, giữ transform và undo như task context actions.
- **Kiểm tra native:** `test_align_and_distribute_tools`, `test_contextual_object_action_capabilities`, `test_solid_tools_boolean_knife_and_context_actions`, `test_compact_icons_and_hover_tooltips`, `distribution_orders_items_by_the_distributed_coordinate`.
- **Giới hạn review:** CTest không chứng minh UI Automation, tab order, màn hình hẹp, DPI 125–200% hay mọi theme; những cấu hình này vẫn cần review thủ công riêng.
