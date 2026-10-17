# Phiếu UI: Tổ chức thanh menu chính

Phiếu theo [chuẩn UX/UI](../ui-ux-standard.md), [bố cục workspace](../../architecture/workspace-layout.md)
và [mẫu feature](../feature-ui-template.md). Ảnh CorelDRAW người dùng gửi là tham chiếu cách phân nhóm lệnh;
PikDraw giữ theme và control đang có.

## 1. Mục tiêu và capability

- Task: `desktop-main-menu-groups-20261016`, module desktop, owner Codex.
- Thay hàng nút lệnh New/Open/Save/Undo/Redo bằng menu cấp một `File`, `Edit`, `View`, `Layout`,
  `Object`, `Effects`, `Bitmaps`, `Text`, `Tools`, `Window`, `Settings`, `Help`.
- Menu tái dùng các handler Win32 hiện có. `MenuActionId` là ID nội bộ của menu, được nối trực tiếp tới
  hành vi desktop; chưa phải registry action dùng chung của toàn app.
- Lệnh hoạt động gồm New/Open/Save/Save As/Export SVG/Exit; Undo/Redo/Delete/Duplicate/Select All/Deselect;
  zoom và guideline; xoay hướng, preset A4/A3/Banner; Group/Ungroup/Align/Boolean/Convert to Curves;
  Text actions; các tool đã có; Properties; Preferences, theme và ngôn ngữ.
- Lệnh chưa được hỗ trợ thật (ví dụ Cut/Copy/Paste, snap toggles, Page Setup, Node, Effects, Bitmap tools,
  fullscreen, Help) hiển thị disabled. Không gắn shortcut giả và handler không báo thành công giả.

## 2. Bề mặt và tương tác

| Mục | Quyết định triển khai |
| --- | --- |
| Menu chính | 12 nhóm trên hàng trên cùng, theo thứ tự ở trên. |
| Dropdown | Click nhóm để mở; rê sang nhóm khác để đổi; click lệnh chạy handler; click ngoài hoặc Escape đóng. |
| Bàn phím | F10 mở/đóng menu File; mũi tên lên/xuống chuyển qua lệnh khả dụng, trái/phải đổi nhóm, Enter/Space chạy, Escape đóng. |
| Action ID | `MenuActionId` trong `native_window_menus.hpp`; lệnh khả dụng gọi `execute_menu_action` và các API hiện hữu. |
| Standard toolbar | Không thêm nút văn bản dài; các lệnh New/Open/Save/Undo/Redo nằm trong menu tương ứng. |
| Toolbox / property bar | Giữ các vùng hiện hành; menu không dựng toolbox/ribbon riêng. |
| Properties | `Window > Properties` mở inspector hiện có. |
| Shortcut | Chỉ hiển thị shortcut đã có handler và đối chiếu với xử lý bàn phím hiện hành. |
| Dialog mới | Không tạo; dùng dialog/panel hiện có cho New/Open/Save/Export/Preferences. |

## 3. Nhóm lệnh đang hiển thị

| Nhóm | Lệnh khả dụng | Disabled / ghi chú |
| --- | --- | --- |
| File | New, Open, Save, Save As, Export SVG, Exit | Document Properties |
| Edit | Undo, Redo, Delete, Duplicate, Select All, Deselect | Cut, Copy, Paste |
| View | Zoom In/Out/100%, Fit Page, Toggle/Clear Guidelines | Snap to Grid/Objects |
| Layout | Landscape/Portrait, A4 210×297, A3 297×420, Banner 3000×1000 mm | Page Setup |
| Object | Group, Ungroup, Convert to Curves, Align, Union, Difference, Intersection | — |
| Effects | — | Contour, Drop Shadow, Extrude, Bevel, Clear Effect (M2/unsupported) |
| Bitmaps | — | Convert, Edit, Trace, Color Mask (unsupported) |
| Text | Edit Text, Bold, Italic, Convert to Curves | — |
| Tools | Pick, Rectangle, Ellipse, Text, Knife, Line, Polyline | Node |
| Window | Properties Inspector | Tabs, Fullscreen, Reset Workspace |
| Settings | Preferences, Dark/Light theme, Vietnamese/English | — |
| Help | — | Help topics, shortcuts reference, About |

Labels/shortcuts nằm trong menu catalog; locale theo `UILanguage` hiện hành. Nhãn được rút gọn trong bề rộng
hạn chế bằng ellipsis. Chưa có help chi tiết cho các mục disabled.

## 4. Hành vi và giới hạn bàn giao

- Chọn menu hoặc chạy command không thay đổi document trừ khi handler hiện hành thực thi command; Undo/Redo
  giữ hành vi chọn lại như shortcut hiện có.
- Khi mở menu, canvas không nhận click nằm trong dropdown; click ngoài đóng menu và tiếp tục xử lý vùng được click.
- Lệnh disabled không có tác dụng; hover giải thích `Not available in this version` / `Chưa có trong phiên bản này`.
  Tạo/triển khai những lệnh đó cần task riêng có UI spec và capability thật.
- Review runtime cần kiểm các nhóm ở English/Vietnamese, vùng cửa sổ hẹp, F10, phím mũi tên, Enter/Space,
  Escape/click ngoài, các handler chính và xác nhận không còn hàng nút cũ.
- Build/test/screenshot chưa được khẳng định trong phiếu; ghi kết quả chỉ sau khi kiểm trên app canonical.
