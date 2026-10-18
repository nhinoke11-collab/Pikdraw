# Phiếu UI: Node Edit Tool

## 1. Mục tiêu và trạng thái

- **Tính năng / task:** `connect-node-tool-ui-20261018` · module `desktop`.
- **Mục tiêu:** giúp người dùng tìm và mở Node tool qua menu/toolbox, kéo node trên canvas, nhập chính xác tọa độ của một node và chỉnh mode node bằng các lệnh đã có.
- **Capability hiện có:** controller Node thao tác trên editable path; hỗ trợ chọn/kéo node, thêm/xóa/tách/nối/đóng/đảo path, mode Cusp/Smooth/Symmetric và chuyển Line/Cubic Bézier. `set_selected_node_position` dùng `MovePathNodeCommand` nên tọa độ có một bước Undo.
- **Giới hạn:** chỉ chỉnh một node bằng X/Y; nhiều node hiện Mixed và khóa hai trường tọa độ. Chưa có nhập tọa độ tay nắm Bézier, chọn/chỉnh node ID trong danh sách, thêm lệnh Corner/AutoSmooth hoặc sửa path không có authoring source.
- **Nguồn thiết kế:** `docs/design/ui-ux-standard.md`, `docs/architecture/workspace-layout.md`, audit B1 trong `docs/reviews/ui-logic-gap-audit-20261018.md`, `NodeToolController` và các Node tool tests. Dùng Tools cho lệnh đổi tool; đưa Node vào flyout Path Drawing và tái dùng Inspector bên phải.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định cụ thể |
| --- | --- |
| Menu chính | `Tools > Node Edit Tool`; English/Vietnamese label hiện có, bỏ trạng thái disabled và nối cùng `set_active_tool(11)`. |
| Action ID | `action.tool.node_edit`; ID ổn định hiện có trong dispatcher. |
| Loại thao tác | Tool mode; không tạo command khi mở/chuyển tool. Kéo node hoặc sửa tọa độ tạo command khi hoàn tất. |
| Standard toolbar | Không áp dụng: Node là công cụ thao tác trực tiếp trên canvas, thuộc toolbox. |
| Toolbox / flyout | `Toolbox > Path Drawing > Node`; nằm sau Line, Polyline, Pen. Icon là glyph node/segment vẽ bằng painter icon native hiện có; tên đọc được là `Node`. |
| Property bar | Giữ nhanh các lệnh đang có: Add/Delete, Break/Join/Close, Cusp/Smooth/Symmetric, Convert Line/Curve, Reverse. Không thêm tọa độ vào bar để tránh tràn bar khi cửa sổ hẹp. |
| Properties | Inspector > Objects khi Node active: số node chọn, mode hiện tại, X/Y theo mm và ba nút mode. X/Y chỉ sửa được khi chọn đúng một node; nút mode sửa toàn bộ node đang chọn. |
| Mở bảng thuộc tính | Chọn Node từ menu/toolbox/F10 tự chuyển Inspector về Objects, đóng subsection Style/Align; người dùng vẫn có thể mở lại tab Inspector bằng `Window > Properties`. |
| Context menu / shortcut | F10 hiện có vẫn mở Node; không gán shortcut mới. Menu/toolbox/F10 cùng gọi một tool và state. |
| Dialog/panel | Không tạo mới; dùng dock Properties hiện có. |

## 3. Trường thuộc tính

| ID / label EN + VI | Bề mặt | Kiểu, đơn vị, miền, mặc định | Scope / binding / Mixed | Validation, commit |
| --- | --- | --- | --- | --- |
| Node count / Số node | Properties summary | Số nguyên ≥0; chỉ đọc | Selection node hiện tại; không có selection thì 0 | Cập nhật theo selection, không tạo Undo |
| Node mode / Chế độ node | Properties summary + bar | Corner, Cusp, Smooth, Symmetric, Auto Smooth; ban đầu theo node | Selection; nhiều mode khác nhau hiển thị Mixed | Cusp/Smooth/Symmetric gọi cùng controller command; Mixed không giả một giá trị |
| X / Tọa độ X | Properties | Số thập phân, mm, −20.000..20.000 mm; giá trị hiện tại luôn đọc từ node được chọn | Chỉ single-node selection; nhiều node/không có node thì disabled và hiện Mixed/chọn node | Khi focus, draft trống để gõ thay thế; dot hoặc comma thập phân; Enter commit, Esc hủy, Tab sang Y; giá trị ngoài miền/không hợp lệ được giữ ở draft và báo lỗi |
| Y / Tọa độ Y | Properties | Số thập phân, mm, −20.000..20.000 mm; giá trị hiện tại luôn đọc từ node được chọn | Như X | Draft trống khi focus; Enter commit một `MovePathNodeCommand`; Tab quay lại X; Ctrl+Z hoàn tác |

- Bar không có bản sao của X/Y; chỉ phần mode/lệnh nhanh nằm trên bar. Properties và numeric editor chia sẻ `NumericFieldId::ObjectX/ObjectY` hiện có trong ngữ cảnh Node, không thêm state/enum mới vào header shell.
- Đổi selection khỏi một node khi đang nhập sẽ commit giá trị hợp lệ cho node cũ trước khi tiếp tục; số sai vẫn giữ field mở để người dùng sửa hoặc Esc hủy.
- Tab từ X chuyển Y, Tab từ Y quay về X; phím canvas không được lấy khi draft numeric đang nhập.

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Menu/tool | Property bar | Properties | Hint / trạng thái |
| --- | --- | --- | --- | --- |
| Không có tài liệu | Không chạy nếu không có tab | Không áp dụng | Trạng thái trống | Không tạo node/path giả |
| Có tài liệu, không selection | Menu, Path Drawing flyout và F10 mở Node | Lệnh node hiện nhưng không khả dụng theo selection | Node count 0; X/Y disabled | `Chọn Path có thể chỉnh sửa, rồi chọn node` |
| Một editable Path, chưa chọn node | Node active | Lệnh tool sẵn sàng nhưng thao tác node disabled | X/Y disabled; mode chưa có giá trị | Hint chọn node trên canvas |
| Một node được chọn | Node active | Add/Delete/Break/Join/Close, mode và segment actions theo controller | X/Y editable, mode hiển thị chính xác | Kéo node hoặc nhập X/Y mm |
| Nhiều node / mixed mode | Node active | Mode buttons áp dụng cho toàn bộ node đã chọn; thao tác cần 1/2 node chỉ thành công theo controller | X/Y disabled/Mixed; count rõ; mode Mixed nếu cần | Không tự lấy tọa độ node đầu tiên |
| Path không editable/locked hoặc tool mất target | Tool không tự sửa | Lệnh không phù hợp không commit | X/Y disabled | Status báo cần Path có authoring source và không khóa |
| Đang draft X/Y hoặc IME | Numeric field giữ focus | Shortcut canvas tạm nhường phím | Field focus nhìn thấy caret | Enter commit; Tab đổi X/Y; Esc hủy |
| Error / mode unsupported | Cùng Node tool | Không phát sinh command | Giá trị nguồn không đổi | Báo miền hoặc selection không còn hợp lệ |

## 5. Icon, tên và hướng dẫn

| Nội dung | English / Tiếng Việt |
| --- | --- |
| Icon | Glyph Node riêng trong painter icon native hiện tại: đoạn path với anchor nodes; không dùng chữ cái/emoji và không xung đột các pseudo-icon effects `11–14`. Không thêm asset SVG rời vì NativeWindow chưa có SVG icon renderer. |
| Label | `Node Edit Tool` / `Chỉnh sửa điểm (Node)` trong Tools; `Node` / `Node` trong flyout. |
| Accessible name | Nhãn menu/flyout được vẽ bằng text và có hit region bàn phím tương ứng. |
| Tooltip/focus help | `Node · click to select, drag to move` / `Node · nhấp để chọn, kéo để di chuyển`; Properties mô tả X/Y và giới hạn single selection. |
| Status hint | Mở tool: chọn Path/node; chọn: kéo node hoặc tay nắm; numeric: `X/Y (mm) · Enter áp dụng · Tab đổi trục · Esc hủy · Ctrl+Z hoàn tác`. |
| Error/disabled reason | Chỉ nhập X/Y với đúng một node; mode áp dụng cho toàn bộ node đã chọn; Path khóa/không có authoring source không được sửa. |
| Help đầy đủ | Phiếu này; chưa có F1/help browser binding mới. |

## 6. Hành vi và bằng chứng nhận

- Mở `Tools > Node Edit Tool`, chọn Path Drawing > Node hoặc nhấn F10; tool active không đổi document revision. Node tool tự chọn target Path tương thích đầu tiên nếu chưa có selection theo controller hiện tại.
- Nhấp node trên canvas để chọn; kéo anchor/handle thao tác theo Node controller. Lệnh bar và nút mode trong Properties cùng gọi controller; mỗi lần áp dụng là command/Undo như API hiện tại.
- Nhấp X/Y giữ tọa độ hiện tại (mm) nhìn thấy trong Properties và mở draft trống để nhập giá trị thay thế. Enter xác thực miền hữu hạn ±20.000 mm, gọi `set_selected_node_position`; Esc bỏ draft; Ctrl+Z hoàn tác thao tác đã commit.
- Properties chỉ được hiện trong Inspector hiện có; chuyển/focus dock không sửa artwork. Mode, count và coordinates được đọc lại từ Node controller sau selection/document update.
- Cửa sổ hẹp: tọa độ nằm trong dock, không làm property bar dài hơn; flyout Path Drawing cao hơn một hàng. Keyboard navigation phải đi được qua 4 item Line/Polyline/Pen/Node bằng ↑/↓, Enter/Space và Esc.
- Kiểm tra mục tiêu: `test_pikdraw_drawing_tool_ui` xác nhận menu/action route, toolbox mouse + keyboard route, single-node X/Y commit/cancel/range, mode command và Undo; `test_pikdraw_desktop_shell` regression hiện có.
- Đã chụp và kiểm tra Node flyout cùng Properties ở cửa sổ 1440×900 (client 1424×861, 100% DPI); sửa chồng lấn nội dung Inspector được phát hiện qua ảnh. 1366×768, 1920×1080 và scale 125/150/200% vẫn chưa kiểm trực quan; không suy ra UX/accessibility ở các cấu hình đó từ harness.
