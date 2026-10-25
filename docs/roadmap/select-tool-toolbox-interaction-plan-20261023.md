# Kế hoạch tinh chỉnh Select và tương tác toolbox

Ngày: 23/10/2026. Trạng thái: **đã triển khai phần tương tác nền; acceptance hiển thị còn mở**.
Phạm vi: Select/Pick, chuyển công cụ, focus, flyout, cursor và ngữ cảnh Properties trên Win32 Native Shell.
Thực hiện trực tiếp tại `C:/Users/CK ART STAGE/Downloads/PikDraw`; giữ các local edits hiện có.

## Bổ sung Page/Artboard và vùng đáy editor

Theo yêu cầu bổ sung ngày 23/10/2026 và hai ảnh tham chiếu, phạm vi được mở rộng bằng
[kế hoạch đại tu Page/Artboard](page-artboard-workspace-overhaul-plan-20261023.md):

- Settings chọn giao diện làm việc theo trang hoặc tổng quan Artboard; canvas/pasteboard rộng, có pan/zoom.
- Navigator dưới canvas có thêm/xóa/nhân đôi, chọn và sắp thứ tự trang/artboard; đồng bộ Properties và menu.
- Page/scene/Artboard, camera, selection, tools, lưu/mở lại, import/export dùng context và định danh rõ ràng.
- Bỏ Theme, GPU và tọa độ con trỏ khỏi status bar, bao gồm tooltip/focus/hit target cũ; giữ nhập tọa độ chính xác trong Properties.
- Select P0 là điều kiện đầu tiên; triển khai nền layout/context trước thao tác Artboard. Không nghiệm thu Artboard chỉ từ việc vẽ nhiều khung trang.

Đây là phần bổ sung của cùng kế hoạch, không thay thế các bước sửa Select/toolbox bên dưới.

## Bổ sung Snap, Smart Guides và khoảng cách thông minh

[Kế hoạch đại tu Snap/Smart Guides](snap-smart-guides-overhaul-plan-20261023.md) bổ sung yêu cầu ngày 23/10/2026:
bắt góc/cạnh/tâm, căn hàng trên/dưới/trái/phải, tiếp xúc cạnh, khoảng hở đều, đường gióng ảo và bảng `Snap To`.
Select kéo object phải dùng cùng solver với preview/commit; không chỉ hiển thị vạch gióng trên tọa độ chưa được snap.
Thực hiện cấu hình/Snap Select trước; tích hợp Artboard sau nền context/transform của kế hoạch Page/Artboard.
Phần này kế thừa OSNAP hiện có, không chờ hoàn tất mọi thuật toán CAD mới và không dựng thêm một hệ snap độc lập.

## 1. Kết luận kiểm tra

Hiện tượng người dùng mô tả có một nguyên nhân trực tiếp trong mã, không chỉ là dự đoán về vị trí chuột:

1. `previous_drawing_tool_` khởi tạo bằng `1` — Rectangle.
2. Trong `handle_canvas_mouse_up`, Select nhận click vùng trống sẽ bỏ chọn rồi gọi
   `set_active_tool(previous_drawing_tool_)` nếu giá trị lớn hơn 0.
3. Rectangle/Ellipse/Polygon thuộc Quick Shapes, nên nhóm này trở thành active dù không có click toolbox.
4. Sau khi vẽ shape, shell lưu tool vừa vẽ, tự chuyển Pick; click trống sau đó lại khôi phục tool ấy.
5. Test hiện có **cố ý yêu cầu** hành vi tự khôi phục; cần đổi expectation cùng bản sửa.

Luồng điển hình: `Ellipse → vẽ hình → Pick → click trống → Ellipse/Quick Shapes`.
Ngay cả khi bắt đầu bằng Pick, giá trị mặc định Rectangle cũng có thể làm click trống kích hoạt Quick Shapes.
Đây là kết luận từ source và test hiện hành; chưa phải bằng chứng tái hiện bằng chuột trên app đang mở.

### Bằng chứng và các vấn đề liên quan

Các số dòng sau dùng cho source đang kiểm tra, có thể thay đổi sau triển khai:

| Mức | Vấn đề | Bằng chứng | Hệ quả / hướng xử lý |
| --- | --- | --- | --- |
| P0, xác định trong mã | Click trống tự đổi tool | `apps/desktop/src/shell/native_window_selection.cpp:859–876`; mặc định tại `apps/desktop/include/pikdraw/desktop/shell/native_window.hpp:990` | Bỏ restore khỏi thao tác deselect; Select được giữ cho tới yêu cầu chuyển tool rõ ràng. |
| P0, expectation sai | Test bảo vệ hành vi gây lỗi | `apps/desktop/tests/test_desktop_shell_settings.cpp:1069–1141`, đặc biệt `1120–1131` | Đổi test thành click trống vẫn Pick; giữ các kiểm tra vẽ shape và guideline có ích. |
| P1, xác định trong mã | Tab nhận focus theo nhóm thay vì toàn toolbox | `native_window_menu_actions.cpp:1160–1288`; Tab lần đầu chọn nhóm 1; Enter/Space mở nhóm | Điều hướng bằng slot thực, bao gồm Pick/Node; chỉ mở nhóm khi focus thật sự ở nhóm đó. |
| P1, cần ca tái hiện | Focus nhóm còn giữ khi thao tác canvas | Canvas down không xóa `toolbox_keyboard_focus_`; `set_active_tool` không đồng bộ focus; paint dùng cờ này tại `native_window_paint_docks.cpp:133` | Test `Tab → V → click canvas → Enter`; phân biệt focus nét đứt với active và hover. |
| P1, xác định trong mã | Test Space và message loop khác nhau | `native_window_events.cpp:373–391` nhận Space rồi return; `native_window_shortcuts.cpp:747–763` lại toggle Pick/tool; test gọi trực tiếp `handle_key_down(VK_SPACE)` | Kiểm tra bằng chuỗi `WM_KEYDOWN/UP`; một bộ quy tắc áp dụng cho cả shell và test. Không kết luận Space toggle chạy thật từ test hiện có. |
| P1, xác định trong mã | Chuyển tool có side effect khác nhau theo điểm gọi | Toolbox/menu gọi `cancel_effect_draft()` trước `set_active_tool`; action shortcut Select gọi thẳng setter; setter có thể từ chối vì field sai hoặc Pen một điểm | Dùng chung quy trình prepare/accept/apply; không hủy draft trước khi biết chuyển được. |
| P1, xác định trong mã | Vùng bấm rộng hơn nút vẽ | Paint nút cao 36 DIP, bước hàng 42; click lấy toàn hàng bằng phép chia tại `native_window_chrome_clicks.cpp:594–610` | Khoảng trống 6 DIP vẫn có thể kích hoạt tool. Paint, hit test, tooltip và focus dùng cùng hình học. |
| P1, xác định trong mã | Node alias trong Path Drawing lẫn định danh icon/tool | Setter nhớ Node `11` vào `path_drawing_last_tool_`; paint lấy số đó làm icon, trong khi Node slot dùng icon `15` | Registry tách ToolId, SlotId, IconId; Node alias cùng một action; không dùng số tool làm số icon. |
| P2, xác định trong mã | Nhiều kiểu “active” | Blur/Shadow tô active theo việc selection có effect; Node/Contour/Gradient còn xét session active | Tách công cụ đang dùng, effect đang có, hover và focus. Một chỉ báo active chính. |
| P2, xác định trong mã | Cursor có hai đường quyết định | `canvas_cursor_at()` phân biệt handle/vùng chrome; `WM_SETCURSOR` dùng `active_tool_` chung cho toàn HTCLIENT | Quyết định cursor từ cùng kết quả hit test; ngoài canvas không mang cursor vẽ/resize. |

Không tìm thấy logic hover Quick Shapes tự gọi `set_active_tool` trong đoạn pointer move đã kiểm tra.
Không quy lỗi cho DPI trước khi tái hiện: message loop đã gọi `logical_pixel` khi nhận tọa độ chuột.
Vẫn phải kiểm biên làm tròn DPI, resize và các flyout neo theo hàng cũ.

## 2. Tham khảo CorelDRAW và Illustrator

Đã tra tài liệu chính thức ngày 23/10/2026:

| Nguồn | Điểm tham khảo | Áp dụng cho PikDraw |
| --- | --- | --- |
| [CorelDRAW — Pick tools](https://learn.corel.com/tutorials/coreldraw-pick-tools/) | Pick phục vụ chọn, di chuyển và biến đổi; có thao tác số chính xác trên property bar | Select là công cụ làm việc ổn định; giữ selection để chuyển tiếp Node, appearance, dimension, transform. |
| [CorelDRAW — Toolbox](https://product.corel.com/help/CorelDRAW/540229932/Main/EN/Documentation/CorelDRAW-Exploring-the-toolbox.html) | Nhóm tool liên quan có mũi tên mở flyout | Tách vùng kích hoạt tool và vùng mở nhóm; mở nhóm không sửa tài liệu. Đây là trang trợ giúp đời cũ, chỉ tham khảo nguyên tắc nhóm. |
| [Illustrator — Tools](https://helpx.adobe.com/illustrator/using/tools-in-illustrator.html) | Selection V khác Direct Selection A; giữ icon để xem tool ẩn | Phân biệt chọn object và chỉnh node; hỗ trợ nhấn giữ mở nhóm mà không vô tình kích hoạt item. |
| [Illustrator — Keyboard shortcuts](https://helpx.adobe.com/illustrator/using/default-keyboard-shortcuts.html) | Space dùng Hand ngoài chế độ nhập chữ; selection và modifier có ngữ cảnh | Phím tạm thuộc vùng canvas, phải nhả khi mất focus. Binding cụ thể lấy từ profile và catalog đang dùng. |

Các quy tắc giữ Select khi click trống, thứ tự Escape và chính sách draft dưới đây là **quyết định thiết kế PikDraw**.
Không gắn nhãn “Corel parity” hay “Illustrator parity” cho toàn bộ hành vi. Không lấy comment “CorelDRAW Flow” trong test làm oracle.
Không tự đổi A từ Text sang Node hoặc đổi toàn bộ shortcut hiện có chỉ vì Illustrator dùng phím khác.

## 3. Hợp đồng tương tác đích

### 3.1. Select giữ quyền điều khiển rõ ràng

| Input / trạng thái | Kết quả yêu cầu |
| --- | --- |
| Click Pick, menu Pick hoặc action Select | Chuyển Pick nếu phiên hiện tại cho phép; giữ selection hợp lệ; đóng flyout sau khi chấp nhận. |
| Pick click object | Chọn object theo scope hiện tại; không đổi tool. |
| Pick click trống | Bỏ chọn, xóa pivot/rotate context khi cần; vẫn Pick; không thay document revision hay Undo. |
| Pick kéo trống | Marquee; giữ Window trái→phải và Crossing phải→trái hiện có; không tạo shape. |
| Shift/Ctrl khi Pick | Giữ Add/Toggle hiện có, không lấn phím constraint của tool khác; mọi lối vào cùng semantics. |
| Hover object/toolbox/flyout | Chỉ đổi hover/cursor/help; không đổi tool, remembered tool, selection hoặc history. |
| Mở/đóng flyout | Chỉ đổi UI; Escape đóng lớp đang mở; không kết thúc Pen/effect draft. |
| Yêu cầu tiếp tục tool trước | Chỉ qua action rõ ràng `Resume Previous Tool` hoặc binding của profile; không qua deselect. |
| Không có tool trước hợp lệ | Resume bị disable; Pick vẫn hoạt động. Không mặc định chuyển Rectangle. |
| Tool đổi thất bại | Tool, selection, remembered tool và draft cũ được giữ; lỗi nằm gần field hoặc status. |

### 3.2. Sau khi tạo hình và nhập nhanh kích thước

- P0 giữ luồng đang có: tạo shape thành công → chọn object mới → Pick → nhập nhanh kích thước.
  **Click trống chỉ kết thúc ngữ cảnh nhập nhanh/bỏ chọn; không phục hồi shape tool.**
- P2 thêm tùy chọn app `AfterCreate = SelectCreated | KeepDrawing`, mặc định `SelectCreated` để giữ workflow hiện có.
- `KeepDrawing`: object vừa tạo vẫn được chọn; tool vẽ giữ active, lần kéo kế tiếp tạo hình mới khi không ở ngữ cảnh chỉnh sửa.
  Nhập nhanh kích thước vẫn tác động đúng ID object vừa tạo, không biến thành kích thước mặc định toàn cục.
- Enter/Escape của nhập nhanh phải giữ semantics hiện có đã kiểm chứng; Escape buffer không được xóa object đã commit.
- Đổi tùy chọn không sửa object cũ, không tạo Undo; lưu trong app settings với default/migration rõ ràng.
- Profile shortcut và `AfterCreate` là hai setting riêng; không âm thầm đổi workflow khi nâng cấp hoặc đổi profile.

### 3.3. Chuyển qua lại giữa các công cụ

| Công cụ / ngữ cảnh | Select → công cụ | Công cụ → Select / công cụ khác |
| --- | --- | --- |
| Rectangle/Ellipse/Polygon | Giữ selection; phân biệt handle sửa selection và vùng bắt đầu vẽ | Drag chưa commit bị hủy khi đổi tool; object đã commit và nhập nhanh còn hợp lệ được giữ. |
| Line/Polyline | Khởi tạo tool qua session hiện có | Chuyển vĩnh viễn hủy draft theo policy hiện có, có hint; pan tạm chỉ suspend. Không tự tạo đoạn cuối. |
| Pen | Kích hoạt session với selection/source hợp lệ | Giữ bảo vệ hiện có: 0 điểm có thể rời; 1 điểm chặn và chỉ cách Enter/Esc; từ 2 điểm finish-open đúng một command hoặc báo lỗi rồi giữ Pen. Mở flyout/focus không finish. |
| Node | Target object được chọn nếu capability hỗ trợ | Giữ object selection; node/handle draft hủy khi đổi vĩnh viễn; không tự chuyển geometry thành kiểu khác. |
| Text | Vào text edit bằng thao tác rõ ràng | Commit composition và text hợp lệ đúng một lần; input chữ/IME sở hữu V, Space, Enter, Tab; không ăn mất ký tự để đổi tool. |
| Pan | Giữ selection, camera và tool trước | Pan tạm không gọi setter phá phiên; nhả phím/chuột hoặc mất focus kết thúc pan. Tool Pan được chọn trực tiếp vẫn là tool thường. |
| Gradient/Contour | Dùng selection hợp lệ; thiếu capability thì báo lý do | Hủy preview chưa apply theo policy tool khi đổi vĩnh viễn; giữ nguồn đã apply. Không tạo effect mới chỉ vì chọn lại tool. |
| Blur/Shadow | Tái chọn node hiện có theo capability; action thêm effect phân biệt action sửa | Giữ stable effect ID; không tạo node thứ hai; hủy draft chỉ sau khi chuyển được chấp nhận. |
| Dimension | Giữ selection làm tham chiếu khi tool hỗ trợ | Hủy bước chưa hoàn tất; giữ annotation đã commit và liên kết hợp lệ. |
| Knife | Lựa chọn đầu vào theo capability hiện hành | Hủy đường cắt chưa commit; không cắt object vì click toolbox hoặc pointer-up đến trễ. |
| PowerClip / group isolation | Tool nhận cùng scope đang chỉnh | Pick vẫn dùng `controller.pick` và `on_resolved_pointer_down`; không chọn object ngoài scope hay thoát isolation ngầm. |
| Layers / palette / Properties | Tác động đúng selection đã xác định | Click panel không tự chọn shape tool; đổi selection làm preview cũ hết hiệu lực theo policy của tool. |

Chuẩn chung: UI thuần không tạo Undo; gesture có sửa dữ liệu tạo đúng một command/transaction.
Khi thao tác chuyển tool hoàn tất Pen/Text thì command đó có thể tạo Undo; phải kiểm riêng với thao tác UI thuần.

### 3.4. Phím, focus, capture và Escape

1. Thứ tự sở hữu input: modal/menu/flyout đang sở hữu focus → IME/text/numeric → gesture đang capture → tool canvas → action shortcut.
   Mỗi event có một chủ sở hữu; event đã consumed không chạy tiếp handler khác.
2. Tab tại input theo control hiện hành; Tab tại canvas đưa focus tới **slot active thật** hoặc Pick.
   Trong toolbox dùng Up/Down duyệt các slot đang hiện, Alt+Down mở nhóm được focus, Enter/Space xác nhận.
   Tab/Shift+Tab đi tiếp giữa các vùng workspace; không vòng lặp chỉ ba nhóm.
3. Click canvas hoặc action chọn tool vĩnh viễn trả focus logic về canvas; mở nhóm bằng chuột giữ anchor rõ ràng.
   Focus là viền nét đứt; active là nền/viền tool; hover chỉ là trạng thái trỏ.
4. Corel-like profile đề xuất: Space tap toggle Pick/tool trước **chỉ khi canvas idle**, có tool trước thật;
   Space+drag pan tạm và không toggle khi nhả. Đây là lựa chọn PikDraw, chưa xác nhận tương đương mọi phiên bản Corel.
5. Illustrator-like profile: Space giữ để pan, nhả trở lại phiên cũ; Space tap không khôi phục tool vẽ.
   Profile PikDraw giữ Space+drag pan hiện có; Resume dùng action riêng, binding cấu hình được.
6. Toggle chỉ xét một chu kỳ down/up, bỏ auto-repeat; không toggle trong transform, draft đang kéo, IME hay input.
   Không dùng Ctrl đơn để tạm Pick vì đang phục vụ Toggle, vẽ từ tâm và constraint Pen.
7. Escape xử lý lớp trên cùng: popup → field draft → gesture preview → session draft → selection khi tool idle.
   Mỗi lần Esc xử lý một lớp; không đồng thời deselect, đổi tool và hủy effect khác.
8. `WM_KILLFOCUS`, `WM_CANCELMODE`, capture loss, DPI change, đổi tab/page/document phải nhả transient/capture;
   giữ/hủy draft theo tool, kiểm stamp trước resume. Pointer-up cũ không được commit vào document/tool mới.

Không sao chép nguyên `InputRouter` rồi giữ thêm một bộ Space trong shell: dùng adapter profile để gọi chung router,
hoặc mở rộng API nhỏ khi thiếu scope Toolbox. Kiểm các consumer hiện có trước khi thay contract module.
Selection-only không đổi revision; nếu sửa document trong lúc phiên tạm dừng, phải revalidate/rebase theo API có thật,
hoặc báo stale và hủy preview an toàn; không chỉ ghi lại revision mới để bỏ qua bảo vệ.

## 4. Toolbox, cursor và Properties

### 4.1. Hình học và trạng thái

- Giữ các slot hiện có: Pick, Node, Quick Shapes, Path Drawing, Pan, Text, Knife, Blur, Shadow, Contour, Gradient, Dimension.
- Một mô tả layout theo DIP cung cấp rect nút, rect mũi tên, item flyout, customize, focus và tooltip.
  Rect không giao nhau; khoảng hở giữa nút là NoHit; không dùng cả hàng 42 DIP làm nút 36 DIP.
- Không coi toàn mép phải nút là mũi tên nếu marker chỉ ở góc dưới. Vùng mở nhóm phải được vẽ rõ,
  đủ dễ bấm và nằm trong nút; tránh đổi chiều rộng cả toolbox ngoài scope.
- Click thân group chọn tool dùng gần nhất đã xác nhận; click mũi tên mở nhóm.
  Nhấn giữ thân group khoảng 400 ms là giá trị khởi đầu cần nghiệm thu; timer mở nhóm phải hủy khi release/leave/focus loss.
- Click thường và nhấn giữ phân biệt tại release: không kích hoạt tool ở down rồi mới mở nhóm.
  Release lần đầu sau nhấn giữ chỉ mở nhóm; cần click/Enter item rõ ràng để chọn, tránh chọn item đang nằm dưới con trỏ.
- Khi flyout mở, click canvas đầu tiên đóng flyout và được consume; click Pick/slot khác có chủ đích được xử lý một lần.
  Header, viền và vùng trống của popup không được xuyên xuống canvas.
- Flyout neo theo slot đang hiện, clamp trong client workspace; resize/ẩn anchor đóng hoặc tính lại popup.
  Loại nhánh Appearance neo cứng `row=6`; không mở nhóm cho slot ẩn.
- Node trong Path Drawing được giữ như alias; icon và action lấy từ registry Node, active chính hiển thị ở slot Node.
  Node alias không ghi đè `lastPathDrawingTool` của Line/Polyline/Pen.
- Blur/Shadow là action hoặc ngữ cảnh effect, không giả làm active tool chỉ vì object có effect.
  Dùng chỉ báo “có hiệu ứng” riêng nếu cần; không dùng cùng style active của Pick.
- Cursor lấy từ hit target hiện hành: chrome arrow → tool/handle phù hợp trên canvas → gesture capture.
  `WM_SETCURSOR`, hover, tooltip và click phải thống nhất; hết hover trả cursor/hint tool thật.

### 4.2. Phiếu UI tích hợp trong kế hoạch

Đối chiếu [chuẩn UX/UI](../design/ui-ux-standard.md), [workspace](../architecture/workspace-layout.md),
[nhóm công cụ vẽ](../design/features/drawing-toolbox-groups.md) và [nghiệm thu](../quality/feature-acceptance.md).

| Bề mặt | Quyết định |
| --- | --- |
| Menu/action chính | Giữ `Tools > Pick Tool (Selection)` / `action.tool.select`; Node dùng `action.tool.node_edit`; tool khác dùng action đã đăng ký. Thêm `Tools > Resume Previous Tool` / `action.tool.resume_previous`, disabled khi chưa có tool trước. |
| Toolbox | Pick luôn hiện ở slot đầu; Node riêng; Quick Shapes/Path Drawing giữ nhóm hiện có và alias tương thích. Mọi lối vào qua cùng transition. |
| Standard toolbar | Không thêm bản sao nút Pick/Resume. |
| Property bar | Pick ưu tiên X, Y, W, H và nút mở Properties, trong giới hạn 3–5 controls; tận dụng field hiện có, không thêm hàng bar. Không selection thì không hiện giá trị của lần chọn cũ như đang áp dụng. |
| Properties | `Object > Properties` / `action.object.properties.focus` mở/focus Transform và các section phù hợp. `Window > Properties` tiếp tục mở dock khi bị đóng. Không tạo bảng Properties thứ hai. |
| Giá trị chi tiết | Rotation, reference point, aspect lock, appearance và tùy chọn selection đã hỗ trợ nằm trong section tương ứng; dùng chung validation/command với bar, giữ đơn vị trang và trạng thái Mixed/Disabled/Locked. |
| Tùy chọn tương tác | `Settings > Preferences > Tools > Selection & Switching` trong dialog hiện có; `AfterCreate`, profile/binding liên quan. App preference, không document command; default/migration nêu ở mục 3. |
| Icon SVG | Tái dùng asset Pick/Node/shape/Pan hiện có; registry ánh xạ asset ID độc lập ToolId. Marker nhóm riêng. Nếu icon native chưa dùng cùng asset SVG phải nối asset và painter trong scope, không chỉ thêm SVG không được render. |
| Label | `Chọn` / `Pick`; `Tiếp tục công cụ trước` / `Resume Previous Tool`; nhóm `Hình cơ bản` / `Quick Shapes`. Shortcut hiển thị theo binding thực. |
| Tooltip/focus help | Hai dòng: tên + shortcut; tác dụng ngắn. Pick: “Chọn và biến đổi đối tượng. Nhấp vùng trống để bỏ chọn.” Mũi tên: “Mở nhóm công cụ”, không gọi là Rectangle action. |
| Status hint | Pick: “Nhấp để chọn · Shift: chọn thêm · Kéo trống: quét chọn”; khi lỗi transition giữ thông báo lỗi ưu tiên hơn hover hint. Không gợi ý tự tiếp tục shape sau deselect. |
| Help | Ghi 3–5 bước sử dụng trong tài liệu nhóm tool hiện có; tooltip/focus và status hướng dẫn ngay trong app. Chỉ gắn F1 nếu help action thật sự đã đăng ký. |
| Không áp dụng | Không thêm menu cấp cao, panel nổi, thiết kế HTML, tool chưa có capability hoặc control export/import mới. |

## 5. Thiết kế triển khai và đường dẫn

### 5.1. Một nguồn trạng thái và một quy trình chuyển tool

State tối thiểu: `activeTool`, `focusedSurface/slot`, `openFlyout`, `hoverTarget`, `lastToolByGroup`,
`previousExplicitTool` dạng optional, `transientOverride`, `interactionGeneration`.
Selection và Undo vẫn thuộc controller/document hiện có; không tạo một bản selection/history riêng trong shell.

`request_tool_transition(target, cause)` trả kết quả `Changed | Unchanged | Blocked` và lý do:

1. Resolve action/ToolId và capability, lấy document/tab/epoch/revision hiện tại.
2. Prepare field/text/draft đang sở hữu input. Validate trước; không hủy effect rồi mới phát hiện Pen/field chặn.
3. Thực hiện finish/cancel được policy cho phép; nếu có command, commit qua document writer.
4. Activate target; chỉ publish active state, remembered tool, UI và generation sau kết quả thành công.
   Thiếu target/capability được kiểm trước; lỗi activation bất ngờ phải có recovery rõ ràng, không UI báo active giả.
5. Từ kết quả thống nhất cập nhật mode controller, focus, bar, Properties, cursor, status và repaint.

`cause` phân biệt Toolbox/Menu/Shortcut/Resume/AfterCreate/TemporaryPan/Recovery.
Đổi selection, hover, mở Properties và đóng flyout **không được phát yêu cầu resume**.
Transient pan đi qua suspend/resume của tool, không chạy deactivation của một chuyển tool vĩnh viễn.

### 5.2. Bản đồ file

| Miền | Đường dẫn cần sửa / bổ sung | Nội dung và giới hạn |
| --- | --- | --- |
| P0 Select | `apps/desktop/src/shell/native_window_selection.cpp` | Bỏ restore khi click trống; giữ picker scope, marquee, pivot, transform, quick dimensions. |
| Transition | `apps/desktop/src/shell/native_window_settings.cpp`; mới `native_window_tool_interaction.hpp/.cpp` cùng thư mục | Tách lifecycle tool khỏi settings; typed state/transition result; không tạo state machine thứ hai trong module. |
| Header | `apps/desktop/include/pikdraw/desktop/shell/native_window.hpp` | Khai báo/state gọn hoặc pImpl, giữ public API tương thích trong thời gian chuyển; không rải magic number mới. |
| Toolbox layout | `apps/desktop/src/shell/native_window_toolbox.hpp/.cpp` | Registry Tool/Slot/Icon, rect chung, focus, group/alias và timer nhấn giữ. |
| Routing | `native_window_events.cpp`, `native_window_pointer_dispatch.cpp`, `native_window_chrome_clicks.cpp`, `native_window_shortcuts.cpp`, `native_window_menu_actions.cpp`, `native_window_shortcuts_settings.cpp` | Một owner cho input; mọi action chuyển tool dùng chung transition; xử lý popup trước canvas/pan. |
| Hiển thị | `native_window_paint_docks.cpp`, `native_window_tooltips_guides.cpp`, `native_window_selection.cpp` | Active/focus/hover/cursor/hint dùng registry và hit target chung. |
| Input module | `modules/vector/include/pikdraw/vector/interaction/input_router.hpp`, `modules/vector/src/interaction/input_router.cpp` | Chỉ mở rộng nếu shell cần scope/profile mà API hiện tại chưa biểu đạt; core/vector không import apps/desktop. |
| Tool consumers | `apps/desktop/src/tools/*_tool_controller.*`, `apps/desktop/src/shell/native_window_text_input.cpp`, workflow effect/quick dimensions hiện có | Chạm đúng adapter cần suspend/validate; không viết lại thuật toán Pen/Node/Contour/Text. |
| Action/UI settings | `apps/desktop/include/pikdraw/desktop/actions/action_id.hpp`, `native_window_menus.hpp`, settings presenter/store hiện có | Resume ID, enable state, cấu hình AfterCreate; dùng một catalog cho shortcut label và thực thi. |
| Test/build/docs | Các test hiện có ở mục 7; `apps/desktop/CMakeLists.txt`; `docs/design/features/drawing-toolbox-groups.md` | Giữ target cũ; chỉ đăng ký source mới nếu tách theo domain; cập nhật expectation và hướng dẫn sau runtime pass. |

Các file shell đã kiểm đều dưới 2.000 dòng; không cần tái cấu trúc toàn bộ NativeWindow để sửa P0.
Không nối tính năng vào file vượt giới hạn; nếu test gần trần thì tách translation unit theo domain trong **cùng target**.
Không tạo harness task/claim/lease/handoff; không reset/stash/commit chung các local edits của công việc khác.

## 6. Trình tự thực hiện

| Bước | Việc thực hiện | Điều kiện xong |
| --- | --- | --- |
| P0 — chặn tự đổi tool | Sửa deselect; tool trước dùng optional/no fallback Rectangle ở đường Resume; cập nhật regression cũ. Giữ AfterCreate và quick dimensions hiện có. | Pick click trống với tool trước bất kỳ vẫn Pick; không tạo object/history; shape creation và guideline không hồi quy. |
| P1a — lifecycle chung | Transition result, resolve capability, field/draft guard; chuyển menu/toolbox/action/shortcut sang chung pipeline. | Chuyển cùng source/target qua mọi lối vào cho cùng selection, draft, revision, status và enable state. |
| P1b — toolbox/focus | Layout/hit test dùng chung; slot focus thật; alias/icon; consume popup; đóng focus cũ khi về canvas. | Tab/V/canvas/Enter không mở Quick Shapes ngoài ý định; click gap/biên/hover không đổi tool. |
| P2a — input/cursor | Nối profile Space với message loop; transient/capture lifecycle; cursor một nguồn. | Test qua HWND khớp hành vi helper; không mất Pen/Text input, không kẹt Pan/resize sau Alt+Tab. |
| P2b — context/tùy chọn | Đồng bộ property bar/Properties, Resume và AfterCreate; thử luồng liên công cụ. | Không field cũ/target cũ; giá trị bar/dock và Undo giống nhau; tạo liên tiếp và quick dimensions hoạt động ở cả hai chế độ. |
| P3 — nghiệm thu app | Build app chuẩn, chạy test đúng phạm vi, thử visible UI/DPI/narrow, dọn output tạm. | AC bên dưới có kết quả thực tế; còn case chưa chạy phải ghi rõ, không đánh dấu UX hoàn tất từ build/test helper. |

P0 có thể bàn giao riêng khi đã qua gate của P0; toàn bộ yêu cầu tương tác toolbox chỉ hoàn tất sau P1–P3.
Mỗi bước kiểm focused tests trước, mở rộng khi đổi shared contract hoặc xuất hiện hồi quy.

## 7. Nghiệm thu và kiểm tra

Mọi ca có cùng oracle bổ sung: active tool/focus đúng; selection ID/scope đúng; document/revision/history chỉ đổi khi có command hợp lệ;
draft preview không commit hai lần; no-op không thêm Undo. Cho pure UI chỉ kiểm history/revision sau khi field/text đang pending đã được xử lý rõ.

| AC | Given | When | Then / Verify |
| --- | --- | --- | --- |
| ST-01 | App/document mới ở Pick, không có tool trước | Click trống hai lần | Vẫn Pick, selection rỗng, không tạo Rectangle; shell pointer down/up regression. |
| ST-02 | Vừa tạo Ellipse/Polygon, app đang Pick | Click trống rồi kéo marquee | Bỏ chọn, vẫn Pick; không có shape mới; sửa test auto-resume hiện có. |
| ST-03 | Bất kỳ tool trước hợp lệ, đã chọn Pick bằng toolbox/menu/V/custom binding | Click trống | Kết quả giống nhau ở mọi entry point; kiểm command-dispatch và đường pointer thật. |
| ST-04 | Pick hoặc tool vẽ đang idle | Hover mọi slot, gap, marker, header flyout | Không đổi tool/selection/remembered tool; click gap không activate; screenshot + hit boundary tests. |
| ST-05 | Canvas có focus, Pick active | Tab; V; click canvas; Enter/Space | Không bật Quick Shapes; Enter tới owner hiện hành, Space theo profile; kiểm focus state và HWND messages. |
| ST-06 | Focus toolbox | Up/Down, Alt+Down, Esc, Tab/Shift+Tab; ẩn Node/shape group | Đi theo slot hiện; group ẩn không mở; focus quay đúng vùng; alias Node icon đúng. |
| ST-07 | Flyout mở bên trên artwork | Click header/viền/trống, rồi click canvas; click Pick | Popup consume đúng; canvas không nhận một thao tác vẽ/chọn xuyên; Pick chỉ activate một lần. |
| ST-08 | Tool group idle | Click ngắn và nhấn giữ, kéo ra ngoài rồi release | Click ngắn chọn đúng last tool; nhấn giữ chỉ mở nhóm; release không tự chọn item; timer hết hiệu lực khi mất focus. |
| ST-09 | Field số sai, hoặc Pen chỉ một điểm | Đổi tool bằng menu/toolbox/shortcut | Bị chặn đồng nhất; tool/focus/draft/effect chưa mất; có lý do rõ, không báo active tool đích. |
| ST-10 | Pen đủ điểm/Line/Polyline draft | Mở flyout; pan tạm; chuyển Pick vĩnh viễn | Browse/pan giữ draft; chuyển vĩnh viễn theo policy mục 3.3; finish Pen một command; failure giữ phiên. |
| ST-11 | Đang sửa chữ và đang composition tiếng Việt | Gõ V/R/E/Space/Tab/Enter, đổi tool rồi Escape | Không mất chữ/IME; tool shortcut không chạy khi input sở hữu; text commit đúng một lần. |
| ST-12 | Profile tương ứng, canvas idle | Space down/repeat/up; Space+drag; Alt+Tab lúc giữ | Tap/hold theo profile, không toggle nhiều lần; không kẹt Pan, không đổi remembered tool vì pan. |
| ST-13 | Select có object/multi-selection | Move/resize/rotate rồi Esc/Undo; Ctrl/Shift chọn và marquee | Geometry preview/commit, Add/Toggle, Window/Crossing và một Undo đúng như hiện có; active vẫn Select. |
| ST-14 | Chọn object rồi vào Node/Gradient/Contour/Dimension/Knife | Sửa một gesture, hủy gesture khác, quay lại Pick và chọn object khác | Selection/target đồng bộ; không dùng ID cũ hoặc commit preview sai tool; không flatten. |
| ST-15 | Object có Shadow/Blur với stable node ID | Bỏ chọn, chọn lại, vào effect, chuyển tool bị chặn/được chấp nhận | Không nhân node, không mất tham số; effect indicator không làm nhiều tool active; mở Properties không thêm Undo. |
| ST-16 | Group lồng/PowerClip đang edit contents | Chọn child, đổi Node/Pick, click trống | Scope/picker/event dispatch giữ đúng; không chọn root hay object ngoài clip do chuyển tool. |
| ST-17 | Gesture đang capture hoặc phiên đang suspend | Capture loss, CancelMode, đổi document/page/tab, DPI; pointer-up cũ | Không commit vào phiên mới; nhả capture/transient; stale bị từ chối; document cũ/new không hỏng. |
| ST-18 | Pick và dock Properties đóng/mở; selection none/single/mixed/locked | Mở Properties, sửa X/Y/W/H ở bar rồi dock | Giá trị/đơn vị/validation giống nhau; mỗi sửa hợp lệ một Undo; mở panel không sửa dữ liệu; None/Mixed/Locked rõ. |
| ST-19 | Cả hai chế độ AfterCreate | Tạo liên tiếp, nhập nhanh kích thước, Escape, Undo | Đúng active tool, đúng ID nhập nhanh; không mất chức năng nhập kích thước; object đã commit không bị xóa khi hủy buffer. |
| ST-20 | 100/125/150/200% DPI; cửa sổ 1280×720 và 1366×768, kích thước lớn | Resize, đổi monitor, mở flyout/cuộn toolbox nếu thiếu chỗ | Paint/hit/focus/cursor khớp; Pick và nhóm cuối truy cập được; popup không tràn màn hình; light/dark và focus nhìn rõ. |

Mở rộng test hiện có:

- `apps/desktop/tests/test_desktop_shell_settings.cpp`: sửa test auto-resume, thêm P0/focus/AfterCreate.
- `apps/desktop/tests/test_desktop_shell.cpp`: toolbox layout/slot/entry-point, cursor, HWND message sequence.
- `apps/desktop/tests/test_pen_native_shell.cpp`, `test_gradient_native_shell.cpp`, `test_effect_draft.cpp`: draft/target/reselect cần thiết.
- `modules/vector/tests/interaction/test_input_router.cpp`, `test_tool_session.cpp`: repeat, focus, transient chỉ khi module API thay đổi.
- `modules/vector/tests/selection/test_selection.cpp`, `tests/gestures/test_gestures.cpp`: scope/gesture nếu contract bị chạm.

Test message loop phải có HWND test và bơm message đúng; gửi đủ key down/up, repeat bit, pointer down/move/up,
modifier/focus/capture khi cần. Gọi `handle_key_down` trực tiếp vẫn hữu ích cho logic cục bộ nhưng không thay kiểm Win32 dispatch.
Visible UI xác minh cả mũi tên cursor, highlight, focus, property bar và click thực; test offscreen không thay phần này.

### Lệnh dự kiến sau triển khai — chưa chạy trong lượt lập kế hoạch

```powershell
# P0 và tích hợp shell
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_desktop_shell -TestRegex '^test_pikdraw_desktop_shell$'

# Khi chạm input/session/selection/gesture của vector
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_vector_input_router,test_pikdraw_vector_tool_session,test_pikdraw_vector_selection,test_pikdraw_vector_gestures -TestRegex '^test_pikdraw_vector_(input_router|tool_session|selection|gestures)$'

# Luồng Pen/Gradient/effect nếu bị ảnh hưởng
.\tooling\native\build.ps1 -Test -TestTarget test_pikdraw_pen_native_shell,test_pikdraw_gradient_native_shell,test_pikdraw_effect_draft -TestRegex '^test_pikdraw_(pen_native_shell|gradient_native_shell|effect_draft)$'

# App chuẩn để nghiệm thu tương tác hiển thị
.\tooling\native\build.ps1 -Target pikdraw
```

Các target trên đã đối chiếu CMake hiện tại. Chọn subset đúng phần đã sửa, không tự chạy full suite cho mọi bước.
Không ghi đè executable đang dùng; khi link bị process giữ thì báo trạng thái và xử lý theo ngữ cảnh người dùng.
Không dùng `-Force`/`-NoLock` để vượt build lock. Kết quả build phải có final exit; binary cũ không là bằng chứng sau sửa.

## 8. Ranh giới tài nguyên vector và tương thích

Đã đọc [hướng dẫn vector](../architecture/vector-resource-interoperability.md),
[C8](../../contracts/c8-import-fidelity.md), [C9](../../contracts/c9-appearance-effects.md),
`modules/interop/README.md` và capability matrix hiện tại. Kế hoạch không đổi parser, worker, format mapping hoặc capability.
C8 vẫn là hợp đồng đề xuất; không giả rằng mọi provenance/report trong đó đã có runtime đầy đủ.

| Format/representation hiện có để đối chiếu | Cách Select dùng | Giới hạn trong kế hoạch |
| --- | --- | --- |
| SVG subset qua SVG adapter; DXF 2D subset qua DXF adapter | Dùng ID/hierarchy/hit test của object native đã host validate | Không thêm version hoặc feature import. |
| PDF qua PDFium page-set subset | Native Path/Text/Image đã materialize dùng semantics tương ứng; reference chọn theo capability thật | Không gọi reference là node-editable; không tự materialize khi chuyển Select/Node. |
| AI qua representation PDF-compatible | Như PDF sau normalize | Không suy ra private Illustrator data/artboard/effect đã hỗ trợ. |
| CDR RIFF/ZIP qua libcdr 0.1.9, normalize SVG | Dùng object/group/clip native đã được adapter giữ và host nhận | Không đổi parser/version support hoặc tuyên bố toàn bộ CDR editable. |
| EPS DSC allowlist; DOS EPS qua Ghostscript/PDF khi khả dụng | Dùng model đã normalize | Không thêm operator, text/effect fidelity hoặc phụ thuộc mới. |

Adapter vẫn riêng theo format/representation. Sau normalize, Select/command/edit/render dùng chung model của PikDraw
ở những capability đã có; không ép các file qua một converter chung để đạt tương tác giống nhau.
Regression chính dùng object native tạo trong test. Chỉ dùng fixture import sẵn có khi cần bắt lỗi group/clip/reference;
không tải tài nguyên trên mạng cho task này.

Tách kết quả: xem/render, editability, metadata nguồn, export round-trip và sản xuất là các trục riêng.
Việc Select/move được một object không xác nhận fidelity nguồn, round-trip hoặc khả năng chạy CNC.
Giữ stable object/effect IDs, source colors/ICC/metadata và Undo; không sửa C8/C9 hoặc renderer để che lỗi routing.

## 9. Kết quả của lượt lập kế hoạch

- Đã kiểm source shell, controller/input router, test auto-resume, registry/dependency và tài liệu UI liên quan.
- `.\tooling\pikdraw.ps1 doctor`: exit 0, `ok=true`, 18 modules, không có issue.
- Có process app chuẩn đang chạy; không điều khiển/đóng app đó và chưa nghiệm thu chuột trực tiếp.
- Chỉ thêm kế hoạch này; chưa sửa source/test runtime, chưa build/CTest và chưa commit.
- Điều kiện hoàn thành triển khai: P0 hết tự chuyển tool; các entry point dùng cùng policy;
  các AC phù hợp phạm vi có evidence, bao gồm message loop và visible UI, không chỉ test helper xanh.

## 10. Trạng thái triển khai — 2026-10-23

Đã nối các luồng nền và giao diện theo hợp đồng ở trên:

- Click vùng trống trong Pick chỉ bỏ chọn; không khôi phục Quick Shapes. Hit-test không nhận vùng
  đệm giữa hàng; focus bàn phím đi qua slot thật; flyout neo theo hàng; alias icon Node tách khỏi ToolId.
- Space-pan là thao tác tạm qua key-down/key-up. Chuyển tool dùng chung setter; transition bị chặn giữ
  draft và tool hiện tại. Cursor canvas lấy từ hit-test pointer.
- Artboard là slot Toolbox thứ 13, có tool Shift+O, kéo tạo/move/resize, overlay/handles, property bar
  X/Y/W/H và dock đổi tên, chỉnh bounds, thêm/nhân đôi/xóa/sắp thứ tự/xuất. Numeric field giữ
  ArtboardId và sửa bằng command có Undo.
- Snap toolbar là split-button: thân đổi master, mũi tên mở Snap To. Popup, View toggles và Properties
  cùng đọc/ghi `DrawingToolController`/Drawing Preferences; Properties có candidate engine hỗ trợ,
  bán kính acquire/release DIP và policy target. Các setting không làm dirty document.

Kiểm chứng native subset ngày 2026-10-23: 12/12 pass gồm `test_pikdraw_desktop_shell`,
`test_pikdraw_artboard_tool_ui`, `test_pikdraw_artboard_snap_ui`, `test_pikdraw_snap_properties_ui`,
`test_pikdraw_quick_shape_snap_ui`, `test_pikdraw_drawing_tool_controller`, `test_pikdraw_drawing_tool_ui`,
`test_pikdraw_snap_status_controller`, `test_pikdraw_vector_snap`, `test_pikdraw_pen_native_shell`,
`test_pikdraw_editor_tools` và `test_pikdraw_node_tool_controller`. Integration tests xác nhận resize
Select giữ cạnh neo, Quick Shapes commit endpoint đã bắt, Pen bắt endpoint object, Artboard resize bắt
mép object và Snap Properties lưu/khôi phục cấu hình. Đây là HWND/shell regression, chưa phải nghiệm thu
pixel/chuột trực quan ở nhiều DPI.

Chưa xác nhận pixel/chuột trực quan trên canonical app, mọi DPI/độ phân giải, IME/Alt+Tab, capture loss,
hoặc toàn bộ luồng Node/Gradient/Contour/Dimension. AC ST-09–ST-20 còn phần chưa chạy vẫn là gate;
không suy từ subset pass thành parity toàn bộ CorelDRAW/Illustrator.
