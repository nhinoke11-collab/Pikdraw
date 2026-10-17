# Nhóm công cụ vẽ và Polygon

Ngày 17/10/2026. Đặc tả UI và hành vi cho runtime Win32/GDI đã nối vào shell; build Debug đã qua, nghiệm thu tương tác trực tiếp còn mở.
Đối chiếu [chuẩn UX/UI](../ui-ux-standard.md), [workspace](../../architecture/workspace-layout.md)
và [phiếu tính năng](../feature-ui-template.md).

## Mục tiêu và phạm vi

Gom các công cụ họ hàng vào flyout trong toolbox trái để tiết kiệm chỗ mà vẫn giữ thao tác bằng chuột/bàn phím.
Thêm Polygon tạo path khép kín đều có số cạnh cấu hình được. Hình vuông, hình tròn và origin giữa dùng modifier
nhất quán cho thao tác kéo shape. Dữ liệu hình cuối vẫn là path vector qua command của document.

Polygon lưu số cạnh vào hình học path lúc tạo; đổi thiết lập chỉ áp dụng cho lần tạo tiếp theo, không âm thầm
thay đổi polygon đã có. Chỉnh từng đỉnh dùng công cụ node khi capability đó được triển khai. Pen draft và quy tắc
hoàn tất/hủy hiện có được giữ nguyên khi truy cập Pen từ flyout.

## Menu, action và các bề mặt

| Mục | Quyết định |
| --- | --- |
| Quick Shapes group | Toolbox trái; giữ slot chính cho tool dùng gần đây. Nửa mũi tên mở flyout `Rectangle`, `Ellipse`, `Polygon`; focus bàn phím mở bằng `Alt+Down`, phím mũi tên duyệt, Enter chọn. |
| Path Drawing group | Toolbox trái; flyout `Line`, `Polyline`, `Pen`, dùng cùng cách mở và trạng thái nhóm. |
| Các tool khác | Select, Pan, Text và Knife tiếp tục ở slot riêng. Flyout không biến chúng thành một nhóm không liên quan. |
| Menu chính | `Tools > Rectangle Tool`, `Tools > Ellipse Tool`, `Tools > Polygon Tool`; `Tools > Line Tool`, `Tools > Polyline Tool`, `Tools > Pen Tool`. Menu dùng action của từng tool, không tạo lệnh sửa dữ liệu thứ hai. |
| Action IDs | Dùng IDs hiện có `ToolRectangle`, `ToolEllipse`, `ToolLine`, `ToolPolyline`, `ToolPen`; thêm `ToolPolygon`. `QuickShapes` và `PathDrawing` là IDs nhóm giao diện nếu shell cần định danh control, không phải document commands. |
| Standard toolbar | Không thêm bản sao. Các công cụ vẽ nằm trong toolbox. |
| Property bar | Khi Polygon active, có `Sides` để đặt nhanh; Rectangle/Ellipse chỉ hiện các tùy chọn cần cho tool đang active. |
| Properties | Bảng phải có section `Polygon` khi tool active: Sides và tùy chọn tạo polygon. Dùng cùng state/validation với field Sides trên bar. Mở Properties không làm đổi mặc định hoặc tạo Undo. |
| Dialog/panel mới | Không áp dụng; dùng toolbox, property bar và Properties đang có. |

## Field, phạm vi và modifier

| Field | Bar / Properties | Kiểu và miền | Scope/commit |
| --- | --- | --- | --- |
| Sides | Bar và `Properties > Polygon` | Số nguyên, 3–256, mặc định phiên làm việc là 6; từ chối giá trị ngoài miền và báo lý do | Tool preference; cùng giá trị điều khiển preview và polygon tạo sau đó; không đổi path đã commit, không tạo Undo |
| Rotation | `Properties > Polygon` (advanced) | Góc theo độ; mặc định đặt đỉnh đầu lên trên (−90°) | Tool preference; điều khiển hướng preview/path nguồn và chỉ áp dụng cho polygon tạo sau đó |
| Equal extents | Modifier Shift, hiện rõ trong hint | Boolean theo trạng thái phím | Draft hiện tại; không đổi preference |
| From center | Modifier Ctrl, hiện rõ trong hint | Boolean theo trạng thái phím | Draft hiện tại; không đổi preference |

Trong thao tác Rectangle/Ellipse, Shift khóa hai extent bằng nhau để tạo square/circle; Ctrl lấy điểm nhấn đầu làm
tâm; kết hợp cả hai cho square/circle từ tâm. Mọi tính toán chạy trong tọa độ document và áp dụng cùng một lần
cho preview lẫn pointer-up, kể cả khi release không có event move cuối.

Polygon luôn đều: số cạnh bằng `Sides` và các đỉnh cách đều quanh circumcircle. Drag thường xác định vùng bao,
Ctrl đổi điểm neo thành tâm và drag distance thành bán kính; Shift khóa hướng đỉnh theo bước 15° như hướng Polar
hiện có. Kết hợp Ctrl+Shift tạo polygon đều đặt tâm tại điểm đầu với hướng đỉnh được khóa. Sides/Rotation áp dụng
cho draft đang mở qua preview; Undo chỉ xuất hiện sau command tạo object thành công.

Phím Ctrl/Shift của Pen giữ semantics theo profile đã cấu hình cho tay nắm/khóa hướng. Mở flyout, chọn một nhóm,
focus hoặc chuyển qua một path tool không tự kết thúc hay xóa Pen draft; chỉ quy tắc finish/cancel hiện có được dùng.

## Icon, label và trợ giúp

| Thành phần | Nội dung |
| --- | --- |
| Icon | Tái dùng icons hiện có cho Rectangle, Ellipse, Line, Polyline, Pen. Thêm SVG `tool-polygon.svg` có ngũ giác outline; một mũi tên nhỏ riêng báo flyout. |
| Label | `Quick Shapes` / `Hình cơ bản`; `Path Drawing` / `Đường vẽ`; item `Polygon` / `Đa giác`. Catalog IDs ổn định, UI English mặc định và Vietnamese tiếp theo. |
| Tooltip/focus | Tên nhóm/tool, shortcut chỉ khi đã đăng ký; polygon ghi `Draw a regular polygon. Set sides in the property bar or Properties.` |
| Status hint | Shape: `Drag to draw · Shift: equal sides · Ctrl: from center`. Polygon: `Drag to size · Shift: 15° angle · Ctrl: center`. Pen hint vẫn lấy từ trạng thái Pen thực. |
| Help | Không thêm Help action mới vì mục Help Topics hiện chưa hỗ trợ; tooltip/focus hint, status hint và validation của field hướng dẫn thao tác cùng giới hạn 256 cạnh. |
| Unsupported/error | Sides ngoài 3–256 báo cạnh field và không tạo path; draft stale/locked theo lỗi và chính sách shell/document hiện hành. |

## Trạng thái và nghiệm thu thủ công

| Ca | Given / When / Then |
| --- | --- |
| AC-1, group | Given toolbox chưa focus, When mở flyout Shapes hoặc Path Drawing bằng chuột/bàn phím, Then các tool đúng nhóm xuất hiện, tool hiện hành được nhận biết, Enter chọn được item và mở nhóm không làm đổi document/Undo. |
| AC-2, constraints | Given Rectangle hoặc Ellipse active, When kéo tự do rồi thử Shift, Ctrl và tổ hợp, Then lần lượt thu được rect/ellipse tự do, square/circle, shape từ tâm và square/circle từ tâm; preview khớp path đã commit. |
| AC-3, polygon | Given Polygon active với `Sides=6`, When kéo tạo rồi đổi sides lần lượt thành 3 và 256, Then mỗi path đóng, có đúng N đỉnh và cạnh đều; giá trị 2/257 bị từ chối, document và Undo không đổi. |
| AC-4, synchronized fields | Given Polygon active và hai bề mặt khả dụng, When sửa Sides ở bar hoặc Properties, Then field kia và preview của lần kéo kế tiếp phản ánh cùng giá trị; mở section không thêm Undo. |
| AC-5, Pen draft | Given Pen draft đang có điểm hoặc tay nắm, When mở flyout rồi đổi focus trong cùng group, Then draft/handles vẫn giữ; finish/cancel tuân theo profile và hành vi hiện có. |

Các ca trên là tiêu chí bàn giao UI. Shell Win32/GDI đã nối và build Debug đã qua; AC-1–AC-5, DPI,
accessibility, cửa sổ hẹp và rà soát theme vẫn cần nghiệm thu trực tiếp.
