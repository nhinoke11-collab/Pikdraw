# Phiếu UI: Scale và xoay selection bằng chuột

Đọc theo [chuẩn UX/UI](../ui-ux-standard.md), [bố cục workspace](../../architecture/workspace-layout.md)
và [mẫu phiếu](../feature-ui-template.md). Đây là thao tác Pick trên canvas, không tạo bar hoặc panel mới.

## 1. Mục tiêu và trạng thái

- Tính năng / task: `selection-scale-rotation-20261016` / desktop + vector / Codex.
- Người dùng chọn một hay nhiều đối tượng rồi kéo tay nắm để thay đổi kích thước hoặc góc xoay.
- Scale dùng tám tay nắm quanh khung bao; xoay dùng tay nắm tròn ở giữa phía trên và một đường nối ngắn.
- Preview thuộc `TransformGesture`; khi thả chuột, các đối tượng được cập nhật bằng một transaction Undo. Escape hủy gesture; snapshot/revision đổi thì hủy an toàn.
- Phạm vi chỉ áp dụng đối tượng được `editable_target` chấp nhận. Kích thước âm không dùng làm dữ liệu source; có thể lật qua điểm neo khi tay nắm vượt qua điểm đối diện. Scale về gần 0 bị từ chối.
- Nhóm được thao tác theo selection hiện có; text giữ source Unicode/font. Rotation biến đổi hình học quanh tâm bounding selection, không raster hóa.

## 2. Tìm thấy tính năng ở đâu

| Mục | Quyết định |
| --- | --- |
| Menu chính | `Object` là nhóm chức năng. Đây là gesture liên tục trên Pick tool, nên điểm vào chính là khung selection trên canvas; không tạo menu command rỗng chỉ để gọi lại handle. Nếu sau này có Rotate/Scale số hoặc lệnh rời, chúng thuộc `Object > Transform`. |
| Action/command ID | Không thêm action ID cho một handle canvas. Commit dùng cùng `TransformGesture` / command `Transform Objects`, chung undo và stale-document guard. |
| Loại thao tác | Gesture preview → thả chuột commit một undo unit; Escape hủy; click không kéo không đổi tài liệu. |
| Standard toolbar | Không thêm nút; tái dùng Pick tool hiện có. |
| Toolbox / flyout | `Tools > Pick` (tool hiện có). Tám handle vuông dùng scale; handle tròn ở giữa phía trên dùng rotate. |
| Property bar | Tái dùng X/Y/W/H và toggle khóa tỉ lệ hiện tại. Angle tạm thời hiển thị ở status bar trong lúc xoay; chưa thêm ô góc số trong phạm vi này. |
| Properties | Tái dùng section `Transform` cho X/Y/W/H và khóa tỉ lệ; không tạo section/panel thứ hai. |
| Context menu / shortcut | Không có shortcut mới; Escape hủy gesture theo luồng canvas hiện tại. |
| Dialog/panel mới | Không. |

## 3. Trường thuộc tính

| ID / nhãn | Bề mặt | Kiểu / đơn vị / miền | Scope | Enable / Mixed | Preview, commit, validation |
| --- | --- | --- | --- | --- | --- |
| X, Y | Property bar / Transform | Số thực, mm | Selection bounding box | Khi selection có thể sửa | Commit theo field hiện hữu |
| W, H | Property bar / Transform | Số thực, mm; > 0, trong giới hạn object | Với một object: object đó. Với nhiều object: field số hiện vẫn lấy object đầu tiên; chưa phải kích thước bounding selection | Khi object đầu tiên có bounds hợp lệ | Commit theo field hiện hữu; nếu lock bật thì field còn lại đổi theo tỉ lệ |
| Lock proportions | Property bar toggle | Boolean | Trạng thái UI phiên hiện tại | Luôn có trong object mode | Khi bật, cạnh và góc scale giữ tỉ lệ quanh anchor đối diện; khi tắt, cạnh scale một trục và góc scale tự do. Shift tạm thời ép tỉ lệ khi kéo cả cạnh/góc |
| Rotation angle | Status hint trong gesture | Độ, delta so với lúc bắt đầu gesture | Draft selection transform | Chỉ khi đang kéo rotate handle | Preview; Shift làm tròn delta theo 15°; thả commit, Escape hủy |

Kéo handle tác động cả selection theo bounding box và giữ anchor đối diện. Field số X/Y/W/H hiện đọc object đầu tiên khi chọn nhiều đối tượng; không coi các field đó là bounding selection. Handle góc lấy hướng kéo lớn hơn để suy ra scale đồng nhất. Handle cạnh khi khóa tỉ lệ bật đổi cả hai trục quanh anchor đối diện.

## 4. Ma trận ngữ cảnh

| Ngữ cảnh | Menu/tool | Property bar | Properties | Hint |
| --- | --- | --- | --- | --- |
| Không có tài liệu | Không áp dụng | Không có selection | Không có selection | Không hiện handles |
| Có tài liệu, không selection | Pick | Page fields hiện hành | Page/document | Không hiện handles |
| Một object editable | Pick | X/Y/W/H và lock | Transform | Kéo handle vuông để scale; kéo handle tròn để rotate |
| Nhiều object | Pick | Handle dùng bounding selection; field số còn đọc object đầu tiên | Transform theo selection hiện hành | Một gesture biến đổi mọi object được chọn; số liệu X/Y/W/H chưa đại diện bbox |
| Locked/read-only/unsupported target | Pick | Không ghi giá trị | Giữ state hiện hành | Begin gesture bị từ chối; không ghi dữ liệu một phần |
| Đang nhập số/draft | Field đang focus được ưu tiên | Giữ editor hiện hành | Giữ editor hiện hành | Handle không commit field thay người dùng |
| Busy/cancel/stale | Pick | Không đổi | Không đổi | Escape hủy; stale revision báo hủy an toàn |

## 5. Icon, tên và hướng dẫn

| Nội dung | Quyết định |
| --- | --- |
| SVG icon | Không thêm icon toolbar/toolbox. Rotation affordance là handle hình học vẽ bằng GDI: vòng tròn + mũi chỉ hướng + stalk; handle scale là ô vuông. |
| Label ngắn | Không thêm button/label trên canvas. |
| Accessible name | `Rotate selection` / `Xoay vùng chọn`; scale handles theo vị trí cạnh/góc. Hiện runtime vẫn là điều khiển chuột, chưa có focus/tab hoặc keyboard transform handle. |
| Tooltip + focus help | Hover rotation handle: `Rotate selection · drag; Shift snaps to 15°` / `Xoay vùng chọn · kéo chuột; Shift khóa bước 15°`. Handle scale: `Resize selection` / `Đổi kích thước vùng chọn`. Focus tooltip chưa áp dụng vì canvas handles chưa vào tab order. |
| Status hint | Khi xoay: hiện độ xoay hiện tại và `Shift: 15°`; khi scale đang kéo: hiện W × H preview và trạng thái khóa/tự do. Tooltip handle gợi ý thao tác kéo. |
| Error/disabled | Selection rỗng hoặc target không sửa được: handle không hiện/không bắt chuột. Gesture stale: thông báo hủy an toàn. |
| Help đầy đủ | Pick tool → chọn object → kéo handle vuông trái/phải/trên/dưới/góc để scale; dùng lock/Shift để giữ tỉ lệ; kéo vòng tròn phía trên để xoay; giữ Shift để snap 15°; thả để commit, Ctrl+Z để undo, Escape để hủy. |

## 6. Hành vi và bằng chứng nhận

- Preview không sửa snapshot; thả chuột commit một `Transform Objects` undo unit.
- Escape hủy, stale epoch/revision không commit; một thao tác selection không sửa default object hoặc production data.
- Lock hiện có áp dụng cho corner và edge scale. Shift tạm thời khóa tỉ lệ khi kéo scale và snap bước góc khi kéo rotate. Numeric property fields hiện chưa chuyển sang bounding selection khi chọn nhiều object.
- Property bar/Properties vẫn là bề mặt số; keyboard/focus access cho canvas handle là giới hạn còn lại.
- Không thêm phím tắt hoặc panel. GDI handle phải thấy được ở light/dark và nằm trong vùng canvas; status bar dùng nhãn ngắn.
- Kiểm chứng cần có: build app canonical, thử đủ 8 handle, bật/tắt lock, Shift, rotate clockwise/counter-clockwise, Escape, Ctrl+Z, và selection nhiều object. Test source native hiện đã bị dọn theo yêu cầu trước đó nên không chạy CTest khi chưa khôi phục.
- Tự kiểm placement: không thêm một menu/bar riêng cho thao tác drag; Object là nhóm nghiệp vụ, Pick canvas là entry point; các handle và X/Y/W/H chia sẻ selection và undo semantics.
