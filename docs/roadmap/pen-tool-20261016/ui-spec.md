# Phiếu UI — Pen và Node

Task thiết kế `pen-tool-plan-20261016`; owner `session-pen-plan-20261016`; module đích vector/desktop.
**Đặc tả đích, chưa có controls/bindings/runtime acceptance.** Agent đã tự chọn và tự kiểm theo
[chuẩn UI](../../design/ui-ux-standard.md), [template](../../design/feature-ui-template.md) và
[workspace](../../architecture/workspace-layout.md). Không cần người dùng điền hay duyệt layout thường lệ.
Baseline: [audit](audit.md). Reviewer implementation chưa được giao; mọi kết quả native còn pending.

## 1. Mục tiêu và quyết định

Người thiết kế tạo path mở/đóng có đoạn thẳng và cubic, chỉnh node và nhập kích thước khi muốn.
Mặc định mouse-first, chế độ Precision chỉ là tùy chọn hiển thị/nhập; không đổi loại nguồn.
English mặc định, Vietnamese qua message catalog. Tất cả số dùng chung một draft model;
bar/Properties/HUD không có ba giá trị khác nhau. Chưa có solver ràng buộc bền vững sau vẽ.

## 2. Tìm thấy tính năng

| Bề mặt | Quyết định và action |
| --- | --- |
| Menu chính | `Tools > Pen`, `Tools > Node`; cùng nhóm công cụ trực tiếp đã có, không dựng top-level menu mới |
| Action có sẵn | `action.tool.pen`, `action.tool.node_edit`: ID đã có, handler/enable state cần P07/P11 |
| Lệnh path | `Object > Paths > Add Node / Delete Node / Break / Join / Reverse / Open / Close`; submenu và IDs mới ở P01, dùng chung với context menu |
| IDs đề xuất | `action.pen.finish`, `.cancel_draft`, `.adjust_handles`, `.resume`, `.auto_add_delete`; `action.path.add_node`, `.delete_node`, `.break`, `.join_bridge`, `.merge_nodes`, `.reverse`, `.open`, `.close`, `.convert_line`, `.convert_cubic` |
| Mode/toggle | Pen/Node là tool; node kind/input reference là mode; auto add/delete, Precision là toggle; topology là action một lần |
| Toolbox | Pen trong flyout nhóm Line/Polyline/Pen; Node ngay dưới Pick. Giữ một cột, active tool nhớ trong nhóm. Flyout mở bằng click/keyboard, không chỉ long-press/hover |
| Standard toolbar | Không thêm bộ nút Pen riêng. Giữ new/open/save/undo/redo như hiện có |
| Context property bar | Target `Draft` hoặc `Selected node` → input mode + L/θ hoặc X/Y → stroke → node kind/Finish. Overflow cho option phụ |
| Properties hiện có | Sections `Geometry`, `Nodes & Handles`, `Appearance > Stroke`, `Drawing Aids`; tái dùng panel, không tạo docker Pen riêng |
| Context menu | Finish, Close, Cancel Draft, Adjust Handles; endpoint có Resume; node/segment có các action hợp lệ cùng IDs với menu |
| Shortcut | P cho Pen, F2 cho Node là binding đề xuất, chỉ công bố sau P01/P07 kiểm xung đột. L/Shift+L hiện có không đổi. F10/F8/Shift+F2 không chiếm để làm hướng/HUD |
| Cấu hình | `Settings > Preferences > Drawing` cho profile/defaults; `View > Snap` dùng action snap chung. F3 giữ hành vi thực tế đến khi migration được kiểm chứng |
| Help | `Help > Tools > Pen` và `Help > Tools > Node`; thêm handler/help resource trong delivery UI; F1 chỉ hiển thị khi thực sự nối |
| Panel/dialog mới | Không có; nhập số dùng editor/HUD của phiên và property bar. HUD chỉ khi nhập/chế độ kỹ thuật; không thêm toolbar nổi mặc định |

## 3. Fields và bindings

Các ID `field.pen.*`, `field.node.*`, `pref.pen.*` dưới đây là **đề xuất**; nguồn message
`ui.pen.*`, `ui.node.*`, `help.pen`, `help.node`. Không hardcode chuỗi EN/VI mới trong paint code.
Miền tọa độ kế thừa geometry hiện hành ±1.000.000 mm; đơn vị hiển thị theo tài liệu, không round source.
`θ` là độ CCW; chấp nhận deg/rad. `L/H` bắt buộc finite, kết quả trong miền nguồn.

| ID / EN — VI | Vị trí | Kiểu, mặc định, miền | Scope/source binding | Enable / mixed | Preview, commit, lỗi |
| --- | --- | --- | --- | --- | --- |
| pref.pen.precision / Precision — Chính xác | Bar, Drawing Aids | Bool false | App/tool preference | Có document | Chỉ đổi view, không undo |
| field.pen.input_mode / Polar, XY, Delta XY — Cực, XY, ΔXY | Bar, Geometry | Enum Polar | Draft candidate | Chờ anchor/điểm kế | Chuyển view cùng candidate, giữ locks rõ ràng |
| field.pen.length / Length — Dài | Bar, HUD | mm, chưa khóa; L>0 cho đoạn thẳng | Draft endpoint chord | Sau anchor; chỉ một candidate | Gõ preview, Enter đặt anchor; âm/zero không commit đoạn thẳng |
| field.pen.angle / Angle — Góc | Bar, HUD | deg, chưa khóa, normalize [0,360) | Draft direction lock | L hoặc pointer xác định hướng | Giữ lock; lỗi không thoát ô |
| field.pen.reference / Reference — Tham chiếu | Bar, Geometry | World X mặc định; World Y / Frame X / Previous segment | Draft angle display adapter | Previous chỉ khi có tangent hợp lệ | Đổi hiển thị không xoay hình/khóa world hiện có |
| field.pen.x / X | Bar, Geometry | mm, candidate world X | Draft absolute coordinate | Không doc disabled | Preview; Enter cả cặp nguyên tử |
| field.pen.y / Y | Bar, Geometry | mm, candidate world Y | Draft absolute coordinate | Như X | Không tự đảo Y theo screen |
| field.pen.dx / Delta X — ΔX | Bar, Geometry | mm, candidate−anchor theo frame | Draft displacement | Sau anchor | Chấp nhận âm; kiểm cả endpoint |
| field.pen.dy / Delta Y — ΔY | Bar, Geometry | mm, candidate−anchor theo frame | Draft displacement | Sau anchor | Chấp nhận âm; không thay origin document |
| field.pen.lock_length / Lock length — Khóa dài | Cạnh L | Bool false | Candidate lock | Có L hợp lệ | Enter đặt anchor xóa lock trừ repeat chủ động |
| field.pen.lock_angle / Lock angle — Khóa góc | Cạnh θ | Bool false | Candidate world direction | Có hướng hợp lệ | Numeric ưu tiên snap/modifier |
| pref.pen.repeat / Repeat — Lặp | Drawing Aids | Mask length/angle, mặc định off | Tool preference, bản sao locks cho điểm kế | Sau điểm đầu | Chỉ báo rõ; cancel/đổi document xóa locks của phiên |
| field.node.kind / Node type — Loại node | Bar, Nodes & Handles | Corner/Cusp/Smooth/Symmetric | Node draft hoặc selection | Multi khác loại hiện Mixed | Preview conversion; một undo unit khi Apply |
| field.node.x / X | Bar, Geometry | mm, world coordinate | Một selected anchor | Multi hiện Mixed, không ghi chung XY | Apply move giữ handle offset, kiểm transform |
| field.node.y / Y | Bar, Geometry | mm, world coordinate | Một selected anchor | Như X | Không chọn node đầu làm giá trị giả |
| field.node.handle_side / In, Out — Vào, Ra | Nodes & Handles | Enum Out | Active handle target | Một node hợp lệ | Chọn handle không sửa nguồn |
| field.node.handle_length / Handle length — Dài tay nắm | Bar khi HandleAdjust, Properties | H≥0 mm; unset nếu absent | Active handle | Smooth zero bị chặn/đổi mode rõ ràng | Enter confirm handle; không đặt anchor mới |
| field.node.handle_angle / Handle angle — Góc tay nắm | Bar khi HandleAdjust, Properties | θ deg theo reference | Active handle | Không có hướng khi H=0 | Cập nhật đối diện theo node mode |
| field.pen.stroke_width / Stroke width — Rộng nét | Bar, Appearance | mm theo creation default hiện có, miền style hiện hành | Draft/new-object hoặc selection phải ghi target | Mixed hiện Mixed | Draft style chưa tạo revision; selection qua atomic style command |
| pref.pen.auto_add_delete / Auto nodes — Tự thêm/xóa | Drawing Aids | Bool false | Tool preference | Chỉ bật khi P10/P11 đạt | Cursor +/− trước click; không xóa do hover |
| field.pen.direction / Free, Ortho, Polar — Tự do, Vuông góc, Cực | Drawing Aids/Snap chung | Enum Free, step 45° | Tool effective preference | Capability hiện có | Không tăng revision; không xóa numeric locks |
| field.pen.snap / Snap — Bắt điểm | Drawing Aids/Snap chung | Mask kế thừa D07 | Tool effective preference | Loại chưa có disabled + lý do | Một config generation, giữ mask khi tắt tổng |

Stroke color tái dùng swatch/control hiện có, giữ RGB/CMYK/spot/ICC/overprint. Chọn default
chỉ sửa new-object/draft; không bôi lại selection vô tình. Bar và Properties subscribe cùng binding;
đổi node/selection cập nhật cả hai, không auto-apply text chưa valid sang target mới.

Cửa sổ hẹp giữ input đang focus, L/θ hoặc X/Y và Finish; node kind, repeat, stroke advanced
vào overflow có tên và keyboard access. Không thu font/cắt unit. Properties luôn là đường thay thế.

## 4. Ma trận ngữ cảnh

| Context | Menu/tool | Bar | Properties | Hint |
| --- | --- | --- | --- | --- |
| Không document | Pen/Node disabled | Không giả số draft | Empty | Open or create a document / Mở hoặc tạo bản vẽ |
| Doc writable, không selection | Pen enabled; Node chờ chọn path | Creation defaults | Drawing Aids/Stroke | Click to start / Nhấp để bắt đầu |
| Một path mở | Pen có Resume ở endpoint, Node enabled | Target rõ | Path/Node | Click endpoint to continue / Nhấp đầu mút để tiếp tục |
| Một node/handle | Action theo loại | XY hoặc H/θ | Nodes & Handles | Move node / Di chuyển node |
| Nhiều node/đối tượng | Type/style chung; join chỉ đúng 2 endpoints | Mixed khi khác | Không lấy node đầu thay tất cả | Chỉnh multi bằng delta/nudge; absolute XY disabled |
| Locked/hidden/read-only/không phải path | Không sửa; resume disabled | Read-only/disabled | Lý do | Unlock/select an editable path / Mở khóa/chọn path sửa được |
| Draft + numeric/IME | Không gọi shortcut canvas | Field có caret/error/focus | Cùng target draft | Enter places point / Enter đặt điểm |
| Gesture/busy | Capability nặng có pending; không hiện snap chắc chắn | Giá trị preview | Không auto-apply | Computing… / Đang tính… |
| Error/cancel/stale | Không half-commit | Giữ text lỗi hoặc rollback theo phase | Không đổi source | Resolve value / Sửa giá trị; stale cho retry/cancel |

## 5. Icon, nhãn và trợ giúp

Asset đề xuất dưới `apps/desktop/resources/icons/` (chưa tạo): `tool-pen.svg`, `tool-node.svg`,
`node-cusp.svg`, `node-smooth.svg`, `node-symmetric.svg`, `path-close.svg`, `handle-split.svg`.
P07/P11 kiểm hệ asset hiện có rồi dùng chung viewBox 24×24, glyph 16/20 DIP, nét theo theme;
vẽ SVG gốc cho PikDraw, ghi provenance/license theo repo, không chép icon Corel/Adobe.
Normal/hover/active/disabled/focus; active/focus có hình viền, không chỉ màu.
Corner vuông, Smooth tròn, Symmetric thoi; Cusp có marker độc lập; In/Out có ký hiệu và accessible name.

| Message | EN | VI |
| --- | --- | --- |
| ui.pen.label | Pen | Bút Pen |
| ui.node.label | Node | Node |
| ui.pen.tooltip | Pen — Draw straight and curved paths. | Bút Pen — Vẽ path thẳng và cong. |
| ui.pen.start | Click to start. Drag to create handles. | Nhấp bắt đầu. Kéo để tạo tay nắm. |
| ui.pen.next | Click next point. Enter finishes. | Nhấp điểm tiếp. Enter kết thúc. |
| ui.pen.numeric | Enter places point. Tab changes field. | Enter đặt điểm. Tab đổi ô. |
| ui.pen.handle | Enter confirms handle. Esc restores it. | Enter xác nhận tay nắm. Esc khôi phục. |
| ui.pen.conflict | Values conflict. Edit or unlock a field. | Số liệu xung đột. Sửa hoặc bỏ khóa một ô. |
| ui.pen.stale | Drawing changed. Retry or cancel this draft. | Bản vẽ đã đổi. Thử lại hoặc hủy bản nháp. |
| ui.pen.unsupported | This snap type is not available yet. | Kiểu bắt điểm này chưa khả dụng. |
| ui.pen.finish | Finish | Kết thúc |
| ui.pen.cancel | Cancel Draft | Hủy nháp |
| ui.pen.adjust | Adjust Handles | Chỉnh tay nắm |

Accessible names dùng label đầy đủ, unit/reference/lock đọc được. Tooltip có cả hover/focus;
phím chỉ thêm sau đăng ký và trace xác nhận. Status theo phase và effective profile,
không chứa ID/class/backend. Help nhận ví dụ 4 bước ở README và quy tắc Esc/undo trong
[behavior](behavior-and-precision.md), gồm cả giới hạn L dây cung và chưa có persistent constraints.

## 6. Lifecycle, accessibility và tự kiểm

Numeric editor có caret/select/copy/paste, Tab/Shift+Tab, Escape và IME; canvas không chiếm E/R/P
trong `deg/rad`, text hoặc composition. Chuyển focus vào bar/Properties giữ draft và target;
click canvas ngoài control nhập hợp lệ đặt đúng một điểm. Close dock không commit.
Đổi tool/tab/đóng file theo ToolSession, không mất draft âm thầm. Xem bảng phase trong behavior.

UI mới dùng 24×24 DIP tối thiểu cho icon hit target, theo density chung; một property bar,
Properties 280–340 DIP đích và overflow. P08/P12 kiểm 1366×768, 1920×1080;
100/125/150/200% trên thiết bị có sẵn, light/dark/high contrast và keyboard-only.
Không suy ra accessibility native từ mockup. Bộ tài liệu không kèm mockup vì wireframe workspace
đã đủ xác định placement; evidence bắt buộc khi code là ảnh/trace trên executable chuẩn.

Tự kiểm đã hoàn tất ở mức đặc tả: menu/ID, surfaces, từng field/scope/unit/state,
mixed/locked/empty/error, icon/provenance, short labels, tooltip/focus/status/help,
cancel/undo, numeric/IME, overflow và localization đều có quyết định cụ thể.
Build/native/DPI/keyboard/reviewer acceptance: **chưa chạy**; không ghi UI implemented.
