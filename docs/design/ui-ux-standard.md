# Chuẩn UX/UI của editor PikDraw

Ngày hiệu lực: 16/10/2026. **Quy tắc thiết kế và review; chưa phải giao diện đã triển khai.**
Áp dụng khi thêm tính năng, sửa luồng thao tác hoặc tiếp nhận code UI từ nguồn cũ.
Quyết định: [ADR 0008](../decisions/0008-editor-ui-organization.md).

## 1. Một nguồn quy tắc

PikDraw là editor desktop phục vụ thiết kế và sản xuất. Canvas và thao tác chính xác được ưu tiên.
Mỗi tính năng phải có [phiếu UI](feature-ui-template.md) đủ nội dung trước khi code phần người dùng nhìn thấy.
**Agent triển khai chịu trách nhiệm tự phân tích, tự điền phiếu và tự quyết định thiết kế trong phạm vi task.**
Agent đọc yêu cầu, xem luồng/control hiện có, chọn menu/bar/Properties/icon/help phù hợp, ghi lý do ngắn,
tự kiểm theo chuẩn rồi tiếp tục code. Không gửi mẫu trống cho người dùng, không yêu cầu họ chọn từng vị trí,
không chờ người dùng hoặc coordinator ký duyệt các lựa chọn thường lệ trước khi làm.
Phiếu là bản ghi quyết định của agent để triển khai và review; không phải form xin phép.
Coordinator/reviewer vẫn kiểm chất lượng trong quy trình bàn giao; quy tắc scope/contract hiện hành vẫn áp dụng.
Chỉ làm rõ với người dùng khi thiếu yêu cầu trọng yếu mà không thể suy ra hợp lý hoặc cần mở rộng phạm vi chưa được giao;
không lấy việc chưa có phiếu/nhãn/icon do người dùng cung cấp làm blocker.
Tính năng core không có UI ghi rõ lý do và điểm tích hợp dự kiến.

Thứ tự đọc: AGENTS và invariants → tài liệu này → [bố cục workspace](../architecture/workspace-layout.md)
→ phiếu của tính năng. Phiếu không tự thay chuẩn chung; thay menu cấp cao, hệ icon hoặc bố cục chung cần task có scope.
HTML, SVG và ảnh cũ là tài liệu tham khảo lịch sử, không được dùng để phủ định chuẩn này.
Không sao chép toàn bộ hướng dẫn sang từng roadmap; liên kết đến mục chuẩn và ghi phần riêng của tính năng.

## 2. Bài học từ ảnh tham chiếu

Ba ảnh CorelDRAW người dùng cung cấp cho thấy: menu và thanh lệnh nằm trên; thanh thuộc tính đổi theo ngữ cảnh;
toolbox dọc trái; canvas ở giữa; Properties chia mục bên phải; palette ngoài cùng; hướng dẫn ngắn ở đáy.
PikDraw dùng cách phân chia trách nhiệm này. Không sao chép thương hiệu, icon độc quyền hoặc mọi tính năng của Corel/Adobe.

Tài liệu chính thức của [CorelDRAW](https://product.corel.com/help/CorelDRAW/Documentation-Windows/CorelDRAW-en/CorelDRAW-CorelDRAW-application-window.html)
phân biệt menu, toolbar, property bar và docker. [Illustrator Control panel](https://helpx.adobe.com/illustrator/desktop/get-started/learn-the-basics/control-panel-overview.html)
và [Properties](https://helpx.adobe.com/illustrator/desktop/get-started/learn-the-basics/properties-panel-overview.html)
cũng tổ chức điều khiển theo công cụ/selection. Đây là tham khảo về tổ chức; các quy tắc cụ thể dưới đây là quyết định của PikDraw.

## 3. Phân loại menu trước khi thêm nút

Tên dưới đây là nhãn tiếng Anh chuẩn; bản tiếng Việt lấy từ catalog. Đây là bản đồ đích,
không phải yêu cầu hiện ngay mọi menu hoặc tính năng chưa làm. Menu rỗng không được dựng để đủ danh sách.

| Menu | Nội dung thuộc nhóm | Không đặt vào nhóm này |
| --- | --- | --- |
| File | New, Open, Save, Import, Export, Print; thiết lập màu riêng của tài liệu | Theme, thao tác biến đổi đối tượng |
| Edit | Undo/Redo, clipboard, Duplicate, Delete, thao tác chọn | Mọi lệnh chỉ vì chưa biết xếp đâu |
| View | Zoom, Pan, chế độ hiển thị, thước/lưới/guide visibility, bật/tắt Snap | Thay đổi geometry nguồn để tạo hiệu ứng nhìn |
| Layout | Trang/artboard, khổ/hướng trang, đơn vị hiển thị của tài liệu, guide và bố cục trang | Theme, ngôn ngữ của app |
| Object | Transform, Align/Distribute, Group, Order, Lock, path operations, thuộc tính đối tượng | Preferences toàn ứng dụng |
| Effects | Hiệu ứng vector/appearance có tham số, ví dụ Contour hoặc Shadow | Crop/resize pixel của ảnh |
| Bitmaps | Crop/resize/adjust ảnh, trace bitmap, chuyển đổi sang bitmap có chủ đích | Fill/stroke của đường vector |
| Text | Font, paragraph, text frame, text on path, chuyển chữ thành outline có thông báo | Ngôn ngữ giao diện |
| Tools | Công cụ đo, drawing aids, tiện ích tác nghiệp và công cụ sản xuất đã có capability | Tất cả chức năng của một module kỹ thuật |
| Settings | Preferences của app: UI, language, shortcuts, mặc định công cụ, preview thiết bị | Thuộc tính của selection đang chọn |
| Window | Mở/đóng Properties, Layers, panel khác; bố trí/reset workspace | Chỉnh sửa dữ liệu tài liệu |
| Help | Hướng dẫn công cụ, tra cứu phím tắt, trợ giúp và About | Diagnostics kỹ thuật trong luồng vẽ thường ngày |

Một action có **một đường dẫn menu chính**. Toolbar, toolbox, context menu, Properties và shortcut
có thể là lối tắt đến cùng command/action ID, cùng enable state và cùng Undo semantics.
Action mở/focus panel khác action sửa giá trị: các field ở bar/Properties dùng chung command sửa;
menu mở Properties chỉ điều hướng đến các field đó, không tự tạo transaction chỉnh sửa.
Ghi đầy đủ đường dẫn, ví dụ `Effects > Contour…`, không chỉ ghi “thêm vào menu”.
Không tự thêm menu cấp cao theo từng feature, tên module, tên agent hoặc tên engine.
Không có hiệu ứng hay bitmap capability thì không dựng menu/nút giả. Nếu phải giữ entry để khám phá,
disable với lý do cụ thể và đường trợ giúp; không có handler rỗng hoặc báo thành công giả.

Phân biệt phạm vi: `Layout > Page Setup…` sửa trang; `Settings > Preferences…` sửa app;
`Object > Properties` tập trung bảng thuộc tính selection; `Window > Properties` bật/tắt bảng đó.
Cùng một panel, không tạo hai bản Properties. Tên `Inspector` trong code chỉ cùng bề mặt này.

## 4. Mỗi bề mặt có một nhiệm vụ

| Bề mặt | Đặt gì ở đây | Giới hạn |
| --- | --- | --- |
| Menu bar | Danh mục lệnh đầy đủ và ổn định | Không nhét ô nhập, hướng dẫn dài, badge trạng thái |
| Standard toolbar | Lệnh dùng thường xuyên: new/open/save, undo/redo, zoom… bằng icon gọn | Không biến thành hàng nút chứa mọi feature |
| Property bar phía trên | Vài thuộc tính hay sửa nhất của tool/selection, ô số có đơn vị | Thay nội dung theo ngữ cảnh, không thêm hàng bar mới mỗi tính năng |
| Toolbox trái | Công cụ tương tác trực tiếp trên canvas: Pick, Node, Pen, Shape, Text, Zoom/Pan… | Nhóm công cụ họ hàng bằng flyout; Export/Settings không phải drawing tool |
| Properties phải | Thuộc tính chi tiết theo nhóm, mở dần phần nâng cao | Không lặp nguyên một form dài trên thanh trên và trong dock |
| Panel chuyên dụng | Layers, Assets hoặc workflow đủ phức tạp có nhu cầu thật | Mở từ Window, tái dùng tab/dock; không tự mở thêm cửa sổ cho mỗi action |
| Dialog | Quyết định cần hoàn tất, thao tác file hoặc cấu hình phức tạp ít dùng | Có Apply/OK/Cancel đúng nghĩa; không dùng dialog cho từng đổi stroke width |
| Status bar đáy | Gợi ý thao tác/phím bổ trợ và thông tin selection, tọa độ, màu/zoom khi có | Không đặt form chỉnh sửa chính hoặc đoạn hướng dẫn dài |
| Tooltip/Help | Giải nghĩa icon, phím tắt; hướng dẫn sâu khi người dùng cần | Không dùng hover làm cách duy nhất để tìm và thao tác lệnh |

Tính năng mới trước hết tái dùng bề mặt đang có. Khi cần panel/dialog/bar mới trong phạm vi được giao,
agent tự đánh giá và ghi trong phiếu vì sao menu + property bar + Properties hiện tại không đáp ứng được,
rồi thực hiện theo chuẩn; reviewer kiểm lý do và kết quả khi review. Đổi khung chung phải nằm trong yêu cầu người dùng, giữ local edits khác.
Không đặt toolbar nổi hoặc bảng hướng dẫn mặc định đè lên artwork.

## 5. Property bar và Properties theo ngữ cảnh

**Công cụ/chức năng trong editor có nhiều thuộc tính, tùy chọn phụ hoặc thiết lập nâng cao bắt buộc có bảng Properties chi tiết.**
Property bar chỉ đưa ra các thiết lập quan trọng, thường dùng để thao tác nhanh; không thay thế bảng chi tiết.
Properties phải chứa đầy đủ thuộc tính và thiết lập của chức năng đã được hỗ trợ, chia nhóm rõ ràng;
phần ít dùng có thể thu gọn trong section nâng cao. Không lấy overflow hoặc các dialog rời cho từng tham số
làm nơi duy nhất truy cập phần thiết lập còn lại. Tái dùng Properties bên phải; nếu workflow cần tab/panel
thuộc tính chuyên dụng, đặt trong cùng hệ dock và tuân thủ quy tắc này, không nhân đôi form.

### 5.1. Quy tắc nghiêm ngặt chống nhồi nhét Property bar (Anti-Overcrowding Rules)

Property bar phía trên có diện tích chiều ngang hữu hạn (đặc biệt ở độ phân giải phổ thông 1366×768 và 1280×720). Mọi agent khi thiết kế UI bắt buộc phải tuân thủ nghiêm ngặt 4 điều cấm sau:

1. **Giới hạn tối đa 3–5 controls trọng yếu**: Property bar chỉ dành riêng cho 3–5 thiết lập quan trọng nhất, có tần suất người dùng điều chỉnh thường xuyên (>80% thời lượng thao tác). Tất cả các tham số cấu hình phụ, tham số dung sai (tolerance), miter limit, tùy chọn chuyên sâu bắt buộc phải đưa vào bảng Properties bên phải.
2. **Tuyệt đối cấm chuỗi nút hành động dài trên Property bar**: Không được xếp một hàng nút hành động nối đuôi nhau như `[✔ Áp dụng] [⚲ Rã viền] [✕ Xóa] [↺ Hủy]` trên thanh trên. Các nút hành động này phải nằm trong cụm **Hành động (Action Group)** của Bảng Properties bên phải, hoặc kích hoạt qua phím bấm thuận tiện (`Enter`, `Esc`, `Ctrl+K`).
3. **Tuyệt đối cấm in câu văn hướng dẫn dài lên Property bar**: Không dùng `DrawTextW` để in các chuỗi văn bản gợi ý 50–100 ký tự (như *"Bo góc nhanh: tròn, lõm, vát · Khóa 4 góc đồng bộ · Shift tạo hình vuông..."*) lên Property bar làm chiếm chỗ và tràn giao diện. Toàn bộ câu gợi ý thao tác và phím bổ trợ phải đưa xuống **Status bar ở đáy (Botbar)** hoặc hiển thị qua **Tooltip khi hover chuột**.
4. **Bắt buộc có nút liên kết nhanh `[⚙ Thuộc tính...]`**: Ở cuối cụm controls trên Property bar, luôn bố trí một nút icon gọn `[⚙]` hoặc `[Thuộc tính ▸]` để người dùng bấm vào là mở/focus ngay vào section tương ứng trên Bảng Properties bên phải.

### 5.2. Chuẩn tương tác của Bảng Properties bên phải (Properties Dock Interactive Standard)

Bảng Properties ở dock bên phải là nơi tập trung đầy đủ 100% thuộc tính và thiết lập của đối tượng hoặc công cụ đang chọn.

1. **Tuyệt đối cấm biến Properties thành bảng text tĩnh**: Không được chỉ dùng `DrawTextW` ghép chuỗi thông tin (như `[Bo góc: Tròn] TL: 0 mm...`) mà không có ô nhập liệu hay nút bấm. Bảng Properties sinh ra là để người dùng xem VÀ chỉnh sửa trực tiếp.
2. **Tương tác hoàn chỉnh (100% Interactive)**: Mọi thông số hiển thị phải có thành phần điều khiển tương ứng:
   - Các kích thước, tọa độ, khoảng cách, số bước, dung sai: bắt buộc dùng ô nhập số có đơn vị (`NumericFieldId`), hỗ trợ click nhập số, Enter/Tab, cuộn chuột.
   - Các chế độ/loại (ví dụ kiểu bo góc, kiểu mối nối, hướng contour): bắt buộc dùng các nút toggle/segmented buttons trực quan kèm icon SVG hoặc ký hiệu chuẩn.
   - Các tùy chọn bật/tắt (Khóa tỉ lệ, Khóa 4 góc đồng bộ): dùng nút toggle/checkbox có phản hồi trực quan trạng thái Active/Inactive rõ ràng.
3. **Cấu trúc phân tầng chuẩn của mỗi Section trong Properties**:
   - **Header**: Tên công cụ / đối tượng đang chọn + Badge tóm tắt trạng thái ngắn gọn.
   - **Geometry / Transform**: X, Y, W, H, Góc xoay (nếu là đối tượng vẽ).
   - **Thiết lập chuyên biệt (Feature Settings)**: Toàn bộ tham số cấu hình chính và phụ của chức năng, có nhãn rõ ràng và canh lề chuẩn.
   - **Khối nút Hành động (Action Group)**: Nằm ở phần dưới cùng của section, bố trí dạng các nút bấm rõ ràng (như `[✔ Áp dụng (Enter)]`, `[⚲ Rã đối tượng (Ctrl+K)]`, `[↺ Đặt lại]`, `[✕ Xóa]`).

Luôn có đường mở/focus đúng section của chức năng từ menu hoặc nút mở rộng có tên rõ trên property bar,
kể cả khi bảng đang đóng hoặc cửa sổ hẹp. “Luôn có” nghĩa là luôn truy cập được khi ngữ cảnh phù hợp,
không bắt bảng luôn mở hay tự bật lại sau khi người dùng đóng. Bảng chi tiết và bar dùng chung giá trị,
scope, validation, enable/mixed state và command/Undo; sửa ở một nơi phải cập nhật nơi còn lại.
Chỉ mở/đóng bảng, đổi section hoặc focus không làm đổi thiết lập hay tạo Undo.

Phiếu UI phải xác định nhóm thiết lập nhanh, toàn bộ nhóm chi tiết và đường mở bảng. Chức năng đơn giản
không có tùy chọn phụ có thể ghi Properties không áp dụng với lý do cụ thể; chức năng nhiều tùy chọn
không được miễn chỉ vì đã có control trên bar. Preferences toàn app và dialog file/format vẫn theo scope ở mục 3–4.

Property bar giữ thứ tự nhóm ổn định: target → geometry → appearance → tùy chọn riêng của tool.
Không buộc công cụ không liên quan phải có đủ mọi nhóm. Những ô thường dùng được giữ gần vị trí quen thuộc;
các tùy chọn ít dùng chuyển vào mục chi tiết/overflow, vẫn truy cập được từ menu hoặc Properties.

| Ngữ cảnh | Thanh thuộc tính nhanh | Properties chi tiết |
| --- | --- | --- |
| Chưa có tài liệu | Chỉ lệnh tạo/mở thích hợp | Trạng thái trống ngắn, không có giá trị tài liệu giả |
| Có tài liệu, không selection | Khổ/hướng trang, đơn vị hoặc drawing defaults được ghi nhãn rõ | Page/Document và mặc định cho đối tượng mới; không giả là đang sửa selection |
| Pick, một đối tượng | X/Y, W/H, khóa tỉ lệ, góc; fill/stroke quick controls phù hợp | Transform, Appearance, thông số riêng của loại đối tượng |
| Nhiều đối tượng | Bounding selection và các thuộc tính chung có thể sửa | Shared properties; giá trị khác nhau hiện Mixed/—, không giả 0 hoặc tự chọn một giá trị |
| Line/Polyline/Pen | Stroke width, kiểu đường, tham số/drawing aids có hiệu lực | Stroke và tùy chọn tool; trường nhập dài/góc theo thiết kế ToolSession đã nhận |
| Text | Font, size, style/alignment thường dùng | Character, Paragraph, Text frame; giữ Unicode/font/source text |
| Bitmap | Kích thước/transform và quick actions hợp lệ | Image, crop, link/embed, thông số pixel/profile đã hỗ trợ |

Nội dung bảng là định hướng placement, không chứng nhận các control đã chạy. Tool session đang nhập số
được ưu tiên theo đặc tả tính năng; không tự nhân đôi HUD và property bar thành hai nguồn giá trị độc lập.

Properties chia section ngắn: **Transform**, **Appearance** (Fill/Stroke/Opacity), phần đặc thù và phần nâng cao.
Ví dụ Stroke gồm màu, width + đơn vị, dash, cap/join; miter limit, position và arrowheads ở phần mở rộng
khi capability có thật. Dùng icon lựa chọn cap/join và mẫu line trực quan; tên đọc được xuất hiện qua tooltip/focus.
Không tạo một nút dài để mô tả toàn bộ tác dụng của section.

Mỗi field phải chỉ rõ đơn vị, miền hợp lệ, giá trị ban đầu, scope (app/document/new-object/selection/draft),
đối tượng áp dụng và lúc commit. Field không phù hợp bị disable với lý do hoặc ẩn theo ngữ cảnh;
không tự sửa source khi chỉ chuyển tool, chuyển tab hoặc mở panel. Lock, read-only, mixed selection,
busy và unsupported phải phân biệt được. Thay đổi selection trong lúc có bản nháp phải theo policy hủy/giữ rõ ràng.

### 5.3. Ưu tiên chuột, thanh kéo và xem trước trên canvas

Với tham số liên tục người dùng thường điều chỉnh bằng cảm nhận thị giác (độ mờ, bán kính, cường độ,
khoảng cách, góc), ưu tiên thanh kéo hoặc tay nắm trực tiếp trên canvas. Luôn có ô số và đơn vị bên cạnh
để nhập chính xác; thanh kéo không thay thế khả năng nhập số. Các tham số rời rạc dùng nút chọn hoặc
danh sách thay vì một thanh kéo giả. Preview của thao tác trực quan **bật mặc định** khi renderer hỗ trợ.

- **Dễ bắt bằng chuột:** track đủ dài để chỉnh được giá trị; thumb, trạng thái hover/pressed và vị trí hiện tại
  phải nhìn rõ. Vùng bấm thumb tối thiểu theo token chuột trong [bố cục workspace](../architecture/workspace-layout.md)
  (24×24 DIP, ưu tiên 28–32 DIP), có thể lớn hơn hình vẽ thumb; paint/hit dùng cùng rect DIP và kiểm ở
  nhiều mức DPI. Nhấn track đặt giá trị và bắt đầu kéo; khi đã capture, kéo ra ngoài track vẫn điều chỉnh
  trong miền hợp lệ. Không để label, ô số, scrollbar hoặc vùng kéo dock cướp thao tác của slider.
- **Một nguồn giá trị:** slider, ô số, preset, tay nắm canvas, Property bar và Properties đọc/ghi cùng
  trạng thái bản nháp và cùng validation. Giá trị hiển thị phải có đơn vị; nếu multi-selection khác giá trị,
  hiện `Mixed` thay vì một số giả. Mở/focus panel hoặc hover không sửa document.
- **Canvas phản hồi trong lúc kéo:** mỗi thay đổi hợp lệ cập nhật preview của đúng đối tượng và tham số;
  thanh kéo/số phản hồi ngay ở frame UI kế tiếp. Gộp các sự kiện pointer nhanh, ưu tiên mẫu mới nhất;
  không render đồng bộ nặng trong từng `WM_MOUSEMOVE`. Bản xem trước có định danh thế hệ/revision để kết quả
  cũ không xuất hiện sau kết quả mới, đổi selection, đổi tab hoặc hủy.
- **Không giả đồng bộ:** khi canvas chưa kịp hiển thị giá trị mới, giữ frame hợp lệ gần nhất và báo ngắn
  `Đang cập nhật`; nếu cần, cho biết giá trị của frame đang thấy. Chỉ báo `Đã đồng bộ` khi frame đã trình bày
  đúng giá trị đang kéo. Preview giảm chất lượng tạm thời được phép khi giữ cùng semantics/màu; sau khi thả
  phải dựng frame cuối ở chất lượng đúng. Không dùng preview thấp làm dữ liệu xuất hoặc in.
- **Commit rõ ràng:** một lượt kéo từ mouse down đến mouse up là một thao tác và tối đa một bước Undo nếu
  giá trị đã đổi. Esc/Cancel hoặc mất capture do đóng/chuyển document hủy draft và trả canvas về trạng thái
  trước kéo; click không đổi không tăng revision. Tính toán nặng có cancellation, work/memory quota và
  progress, không giữ UI event loop chờ render.
- **Nghiệm thu nhìn thấy:** phiếu feature ghi miền slider, miền nhập số, mapping (tuyến tính hay phi tuyến),
  hành vi click/drag/Shift/Esc, trạng thái busy và mục tiêu độ trễ. Đo pointer-to-present p50/p95 trên
  NativeWindow thật với fixture công bố; kiểm chuột, DPI, CPU/D2D/WARP, drag nhanh và cancel. Build,
  snapshot/offscreen hoặc số FPS suy từ CPU không chứng minh canvas đã theo kịp chuột.

Quy tắc này áp dụng cho hiệu ứng, gradient, raster adjustment và mọi control trực quan tương tự; công cụ
không thể preview trong budget phải nêu giới hạn và trạng thái chờ ngay trong UI. Ví dụ triển khai chi tiết:
[kế hoạch đại tu Hiệu ứng](features/effects-system-overhaul.md).

## 6. Icon SVG, nhãn ngắn và trạng thái

- Toolbar/toolbox ưu tiên icon SVG từ hệ icon thống nhất; không dùng emoji hoặc một chữ cái thay hình công cụ.
  Tái dùng icon đã có; icon mới phải có asset ID, provenance/license và nghĩa rõ. Task tài liệu chỉ ghi đề xuất asset.
- Một nghĩa dùng cùng icon ở mọi bề mặt; cùng một icon không chỉ hai action khác nhau. Dùng cùng viewBox,
  kích thước và độ dày nét trong một bộ. Nét/màu thích ứng light/dark/high contrast; không bê icon thương hiệu từ ảnh mẫu.
- Nút toolbar không xuống dòng, không chứa câu hướng dẫn. Khi cần chữ, dùng động từ hoặc tên ngắn khoảng 1–3 từ
  như `Export`, `Apply`, `Reset`; đây là mục tiêu biên tập, không cắt nghĩa hoặc cắt chữ để đạt số từ.
- Menu, tên section, nhãn ô số và nút quyết định trong dialog vẫn cần chữ rõ. Không thay mọi label bằng icon
  nếu khiến người dùng phải đoán. Giữ tên có nghĩa cho screen reader và điều khiển bằng bàn phím.
- Mỗi icon tương tác có tên truy cập được, tooltip khi hover **và focus**, phím tắt thực sự đã đăng ký nếu có,
  cùng trạng thái normal/hover/pressed/active/disabled/focus/busy phù hợp. Tên action không đổi tùy nơi gọi.
- Toggle/mode cho biết đang bật; action một lần không giả là toggle. Focus không chỉ bằng đổi màu;
  None, Mixed, Disabled, Error và Busy không được dùng cùng một hình/nhãn.
- Dấu `…` trên menu dùng cho action cần thêm lựa chọn trước khi thực hiện. Nút `…` mở rộng phải có tên truy cập
  cụ thể, ví dụ “More stroke options”, không đặt nhiều nút ba chấm vô danh.

Không bắt người dùng đọc câu trên button để hiểu tool. Không rút ngắn thông số đến mức mất đơn vị;
ví dụ `0.20 mm` là ô số có đơn vị, không phải button “Nhấn để thay đổi độ dày đường viền…”.

## 7. Hướng dẫn sử dụng xuất hiện đúng chỗ

| Lớp trợ giúp | Nội dung | Ví dụ nội dung, cần khớp hành vi thật |
| --- | --- | --- |
| Tooltip | Tên action, shortcut hợp lệ, một câu tác dụng nếu cần | `Stroke width — Change the source outline width.` |
| Status hint | Một câu ngắn về bước hiện tại và modifier cần nhất | `Drag to draw. Esc cancels.` |
| Trạng thái lựa chọn | Loại/số đối tượng; thông số đủ dùng | `3 objects · Mixed fill` |
| Help của tool | Cách bắt đầu, 3–5 bước, thông số chính, sửa lại, hủy và giới hạn | Mở từ Help/F1 nếu binding đã được triển khai; có đường menu thay thế |
| Lỗi/unsupported | Lý do và cách tiếp tục, gần control hoặc vùng thông báo phù hợp | `Select a path to edit its stroke.` |

### 7.1. Quy chuẩn Tooltip và Botbar Status Hint (Guidance Contract)

Để người dùng luôn nắm bắt được cách sử dụng mà không làm rối mắt hoặc tràn màn hình, mọi chức năng phải tuân thủ 2 kênh hướng dẫn chính:

1. **Botbar (Status bar ở đáy màn hình) — Hướng dẫn ngữ cảnh theo thời gian thực**:
   - Là nơi duy nhất dành cho câu hướng dẫn thao tác theo từng bước và phím bổ trợ (Modifier keys: Shift/Ctrl/Alt/Esc/Enter). Tuyệt đối không đưa câu hướng dẫn dài lên Property bar.
   - Định dạng chuẩn 3 phần: `[Hành động hiện tại] · [Phím bổ trợ gợi ý] · [Hoàn tất / Hủy]`.
   - *Ví dụ chữ nhật*: `Kéo chuột để vẽ hình chữ nhật · Giữ Shift: vẽ hình vuông · Giữ Ctrl: vẽ từ tâm · Esc: hủy`.
   - *Ví dụ Node tool*: `Nhấp chọn node hoặc kéo quét nhiều node · Shift+Click: chọn thêm · Double-click đoạn thẳng: thêm node · Del: xóa node`.
   - *Ví dụ Contour*: `Kéo tay cầm trên canvas hoặc chỉnh thông số trên Properties · Enter: Áp dụng · Esc: Hủy bản nháp`.
   - Khi hover vào một nút bấm hoặc control: Botbar cập nhật gợi ý chi tiết cho nút đó; khi chuột rời đi, khôi phục lại gợi ý của tool hiện hành.

2. **Tooltip khi Hover chuột — Giải nghĩa tức thì cho từng nút bấm / ô nhập**:
   - Bắt buộc áp dụng cho 100% icon, nút bấm và ô nhập số trên Toolbox, Toolbar, Property bar và Bảng Properties.
   - Cấu trúc chuẩn 2 dòng:
     - **Dòng 1 (Tiêu đề & Phím tắt)**: In đậm Tên chức năng + `(Phím tắt)` nếu có. Ví dụ: `Rã viền Contour (Ctrl+K)` hoặc `Công cụ vẽ tự do Freehand (F5)`.
     - **Dòng 2 (Mô tả tác dụng)**: 1 câu súc tích giải thích rõ chức năng làm gì và ảnh hưởng thế nào đến đối tượng. Ví dụ: `Tách các lớp viền song song thành các đường path vector độc lập để can thiệp từng chi tiết.`
   - Tránh các mô tả sáo rỗng như: "Bấm vào đây để...", "Nút này dùng để...", "Click to perform action". Dùng động từ hành động trực tiếp.

Status bar ưu tiên lỗi đang cần xử lý → trạng thái thao tác đang chạy → hint tool/focus/hover
→ thông tin selection thường trực. Rời hover thì khôi phục hint/tool state trước đó; không để câu cũ treo mãi.
Giữ phần thông tin thiết yếu nhìn được; không nhồi cả hướng dẫn, mọi tọa độ, profile và log vào một dòng nhỏ.
Control quá hẹp dùng overflow/tooltip có thể gọi bằng focus; hướng dẫn chi tiết luôn có mục Help truy cập bằng bàn phím.
Không dùng status hint thay cho lỗi cần được người dùng xử lý hoặc xác nhận phá hủy.

Viết thông tin phục vụ thiết kế. Không đưa tên class, epoch/job ID, protocol, backend hoặc hướng dẫn build vào UI thường ngày.
Không quảng bá phím bổ trợ/phím tắt từ Corel/Illustrator nếu PikDraw chưa có handler và chưa kiểm chứng.
Help, tooltip và status hint được giao trong cùng task với tính năng, không để sau như phần trang trí.

## 8. Thao tác, phạm vi dữ liệu và accessibility

Menu, icon, Properties và shortcut đi đến cùng hành vi. Preview không sửa source;
Apply/gesture hoàn tất tạo transaction/Undo hợp lý; Escape/Cancel theo policy của feature và giữ dữ liệu người dùng.
Mở dock hoặc chọn tool không tạo bước Undo. Numeric field có caret, selection, copy/paste, unit và validation;
phím tắt canvas không chiếm phím khi đang nhập text/số hoặc IME. Không làm mất bản nháp chỉ vì chuyển focus.

English là UI mặc định, Vietnamese là ngôn ngữ thứ hai. Tên action, tooltip, hint và help dùng message ID ổn định;
tài liệu cho con người trong repo viết tiếng Việt. Đổi ngôn ngữ không đổi artwork, Unicode nguồn, ID, số liệu CNC,
đơn vị/geometry nguồn hoặc schema. Không tự bịa shortcut để lấp ô trống trong phiếu UI.

Reviewer kiểm tab order, focus nhìn được, thao tác bằng bàn phím, tên truy cập được, độ đọc ở light/dark
và scaling. Kích thước, khoảng cách theo [workspace](../architecture/workspace-layout.md); không thu nhỏ font
hoặc chỉ dùng màu để giải quyết tràn. Ảnh mockup không chứng minh keyboard/accessibility đã hoạt động.

## 9. Ví dụ placement để agent không đoán lại

| Tính năng | Đường menu chính | Trên property bar | Trong Properties / nơi chi tiết | Hướng dẫn |
| --- | --- | --- | --- | --- |
| Stroke width | Object > Properties → Appearance / Stroke | Ô width + unit, swatch; icon mở thêm | Width, color, cap/join; advanced nếu đã hỗ trợ | Tooltip width; hint về selection và đơn vị |
| Contour | Effects > Contour… | Offset/steps hay dùng nếu đã hỗ trợ | Section Contour hoặc effect editor đã có; không tự thêm panel riêng | Preview, hướng offset, Apply/Cancel, khả năng sửa lại |
| Bitmap adjust | Bitmaps > Adjust > Brightness/Contrast… | Chỉ quick action khi chọn bitmap | Điều khiển adjustment có preview; giữ nguồn theo capability | Nêu scope ảnh, preview và cách reset |
| Page size | Layout > Page Setup… | Preset, W/H, orientation khi không selection | Page/Document | Phân biệt khổ trang với kích thước selection |
| Export | File > Export… | Có thể có icon trên standard toolbar | Dialog file/format/options và progress thích hợp | Format subset/loss report theo chức năng đã nhận |
| Theme / language | Settings > Preferences… | Tối đa shortcut icon đã có, không chiếm property bar | Trang Interface của Preferences | Chỉ đổi UI/preferences, không đổi tài liệu |

Đây là ví dụ đặc tả, không công bố Contour, Bitmap adjust hay Help runtime đã được triển khai.
Một feature cụ thể vẫn phải ghi field/state/command ID chính xác trong phiếu và kiểm capability thực tế.

## 10. Checklist trước code và trước bàn giao

Trước code UI: có mục tiêu người dùng, menu chính, action ID, placement trên các bề mặt hoặc lý do không dùng,
field/scope/đơn vị, selection matrix, icon, tooltip/status/help, focus/shortcut và Apply/Cancel/Undo trong phiếu.
Với chức năng nhiều tùy chọn, kiểm bắt buộc có Properties đầy đủ, nhóm nhanh trên bar, đường mở/focus
và quy tắc đồng bộ hai bề mặt theo mục 5; không chấp nhận chỉ có bar/overflow.
Nếu có slider/tay nắm/preview, phiếu phải ghi cả hit rect, capture/drag/cancel, đồng bộ canvas và
giá trị, một bước Undo, busy/stale state cùng phép đo input-to-display theo mục 5.3.
Agent tự điền và tự kiểm các mục trên, không chờ người dùng duyệt phiếu để tiếp tục. Reviewer trả lại phần
còn câu “sẽ bố trí sau”, button dài, panel tự phát hoặc thiếu hướng dẫn thao tác khi review chất lượng.

Khi triển khai sau này: nghiệm thu trên chính source/exe nhận bàn giao, với context rỗng/một/nhiều/mixed/locked,
focus nhập số/IME, keyboard, cancel/undo và lỗi; ảnh native ở cửa sổ 1366×768 và 1920×1080 hoặc giới hạn có lý do,
scaling 100/125/150/200% theo thiết bị có sẵn; báo phần chưa đo. Ảnh/spec tham khảo ghi rõ là mockup.
Kiểm menu/control/hint không bị cắt và canvas vẫn có chỗ làm việc; không tuyên bố pass mọi DPI từ một ảnh.
Với chức năng nhiều tùy chọn, kiểm mở lại đúng bảng/section khi dock đóng hoặc bar hẹp, tìm và sửa được
thuộc tính phụ, giá trị trên bar/Properties đồng bộ và mở bảng không đổi dữ liệu/Undo.
Với slider/tay nắm, kiểm kéo chuột thật qua track và ra ngoài track, Esc/mất capture, preview chậm,
thả chuột, Undo/Redo và frame cuối trên canvas; báo p50/p95 đã đo hoặc ghi rõ chưa đo.

Đây là **gate review tài liệu và UX thủ công** nối với [quality gates](../quality/gates.md).
Chưa có checker tự động xác định một UI đã đúng bố cục hoặc dễ dùng. Task hiện tại chỉ đặt quy tắc;
không sửa app, harness, icon runtime, build hoặc nghiệm thu lại các tính năng cũ.
