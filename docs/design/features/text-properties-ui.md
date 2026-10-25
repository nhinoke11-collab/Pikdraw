# Thuộc tính văn bản và co giãn trực tiếp

Task: `text-properties-and-active-transform-20261018` · module `desktop`.

## Mục tiêu và vị trí

Soạn thảo chữ ngay trên canvas. Khi Text còn là công cụ đang dùng, người dùng kéo
thân chữ để di chuyển và kéo bất kỳ cạnh/góc nào để đổi cỡ; các góc giữ tỉ lệ ngang
và dọc. Tay nắm cạnh cũng đổi đồng đều kích thước chữ và giữ cạnh đối diện. Caret,
vùng chọn ký tự và phiên nhập vẫn còn hoạt động sau khi kéo. Text đã chọn nhưng chưa
đang nhập có cùng thao tác resize, không cần chuyển về Pick (`V`).

| Bề mặt | Quyết định |
| --- | --- |
| Menu chính | `Text > Edit Text…` mở phiên soạn trên canvas. `Text > Change Case ▸` mở menu con gồm năm kiểu đổi chữ; `Shift+F3` vẫn xoay lowercase → Title Case → UPPERCASE. `Text > Convert to Paragraph / Artistic Text` dùng `Ctrl+F8`. `Window > Properties` mở cùng bảng bên phải. |
| Toolbox | Giữ Text tool index 4, `F8` và icon Text hiện tại; không thêm tool/flyout. |
| Standard toolbar | Không áp dụng; text formatting là ngữ cảnh của Text/đối tượng đang chọn. |
| Property bar | Giữ các thuộc tính hay sửa nhanh: family, size, B/I/U/S, căn đoạn, line spacing và tracking; dùng chung giá trị với Properties. Không lặp các thuộc tính này thành lệnh riêng trong menu Text. |
| Properties | Section Text gồm Character, Paragraph & Text Frame; tất cả trường model đang hỗ trợ hiện trực tiếp, không có nút “Show more”. Nội dung chữ vẫn sửa trực tiếp trên canvas. |
| Dialog | Không thêm dialog riêng. Font family được chọn trong Property Bar/Properties; không giữ mục Font Picker lặp trong menu Text. |
| Context menu | Text đã uốn theo Path có `Reverse Text Direction` và `Straighten Text`; hai lệnh này chỉ bật khi chọn đúng text-on-path. Change Case ở context menu được ghi rõ là xoay chế độ bằng `Shift+F3`. |
| Shortcut | `Ctrl+T` mở Properties từ mọi ngữ cảnh, kể cả khi caret đang soạn; `Ctrl+B/I/U` và `Ctrl+Q` giữ nghĩa hiện tại. |

## Trường và phạm vi

| ID / label EN + VI | Kiểu và miền | Scope, trạng thái, commit |
| --- | --- | --- |
| `TextFontFamily` · Font family / Họ phông | Danh sách font đang cài trên Windows; nhóm All, Sans serif, Serif, Script và UTM/SVN khi có font tương ứng. | Draft Text hoặc toàn bộ Text được chọn. Hủy menu không đổi dữ liệu. Họ phông được thêm vào registry của phiên làm việc để đồng bộ với property bar. |
| `TextFontSize` · Size (pt) / Cỡ chữ (pt) | Số thập phân, 1–2880 pt. | Draft preview hoặc một command có Undo cho selection đồng nhất. Enter áp dụng, Tab chuyển field, Esc hủy. |
| `TextBold`, `TextItalic`, `TextUnderline`, `TextStrikethrough` · B/I/U/S | Toggle ký tự, trạng thái bật/tắt hoặc mixed rõ trên nút. | Draft preview hoặc một command cho toàn bộ Text selection. Toggle trên trạng thái mixed bật thuộc tính cho tất cả đối tượng; các thuộc tính khác giữ nguyên. `Ctrl+B/I/U` vẫn hoạt động khi đang soạn. |
| `TextAlignment` · Left/Center/Right/Justify | Căn đoạn trái, giữa, phải hoặc đều hai bên; dòng cuối đoạn Justify căn trái. | Draft preview hoặc một command cho toàn bộ Text selection; không đổi nội dung chữ. |
| `TextLetterSpacing` · Tracking / Giãn ký tự | Số thực trong khoảng −1000 đến +1000 phần nghìn em; model lưu mm. | Draft preview hoặc một command cho toàn bộ Text selection; chỉ đổi tracking, giữ cỡ chữ và kiểu chữ riêng từng đối tượng. |
| `TextCaseSentence`, `TextCaseLower`, `TextCaseUpper`, `TextCaseTitle`, `TextCaseToggle` · Change Case / Đổi hoa-thường | Sentence case, lowercase, UPPERCASE, Title Case, tOGGLE cASE. | Chọn một hoặc nhiều Text để đổi toàn bộ đối tượng; khi đang soạn, đổi phần bôi đen hoặc toàn bộ chữ. Các mục bị disable khi selection không phải Text. `Shift+F3` xoay lowercase → Title Case → UPPERCASE. |
| `TextLineSpacing` · Line spacing / Giãn dòng | Phần trăm 25–1000%; model lưu ratio 0.25–10, mặc định 120%. | Draft hoặc toàn bộ Text selection; Mixed được thể hiện rõ. Enter áp dụng, Tab chuyển field, Esc hủy. |
| `TextFrameWidth` · Text frame (mm) / Khung chữ (mm) | Số thập phân 0–1.000.000 mm; 0 là point text, số dương tạo khung tự xuống dòng. | Draft hoặc toàn bộ Text selection; Mixed được thể hiện rõ. Enter áp dụng, Tab chuyển field, Esc hủy. |
| `TextFontFromImage` · Find font from image / Tìm font bằng ảnh | Nút trạng thái “Coming soon / Sắp có”; chưa chạy phân tích ảnh. | Có thể bấm để nhận trạng thái tính năng sẽ phát triển sau; không báo thành công giả và không sửa document. |

## Thao tác chữ trên đường dẫn

- `Text > Fit Text to Path` (`TextFitToPath`) chỉ bật khi vùng chọn có đúng một
  Text và một Path chưa khóa. Chọn Text trước, giữ `Shift` và nhấp Path để thêm
  vào vùng chọn, sau đó chọn lệnh. Không có shortcut riêng.
- Khi gắn thành công, Text vẫn được chọn và status bar chỉ báo kết quả, cách mở
  menu chuột phải để `Reverse Text Direction` hoặc `Straighten Text`, và `Ctrl+Z`.
- Reverse/Straighten chỉ bật khi đã chọn Text gắn Path. Không thêm toolbox hay icon;
  dùng nhãn lệnh hiện có và tooltip điều kiện chọn. Property Bar/Properties chưa
  phơi bày các trường start offset hoặc baseline distance; không hiển thị như thể
  đã có tay nắm kéo hoặc field chỉnh hai thông số này.
- `Ctrl+F8` là `Convert to Paragraph / Artistic Text`, không phải Fit Text to Path.
  Khi đang soạn, chỉ áp dụng cho draft hiện tại; ngoài phiên soạn, chọn đúng một
  Text. Chuyển sang paragraph dùng bề rộng layout hiện tại thay vì bề rộng cố định.

Khi các Text được chọn có family, size hoặc style khác nhau, từng field hiển thị
Mixed bằng dấu gạch/trạng thái trung gian. Đổi family, size, B/I/U/S, alignment,
tracking, frame hoặc line spacing chỉ cập nhật đúng thuộc tính đã chọn; các size,
alignment và style riêng còn lại được giữ nguyên. Toggle trên một giá trị mixed bật
style cho toàn bộ vùng chọn. Selection có Text bị khóa/ẩn là read-only; selection
hỗn hợp Text và loại đối tượng khác không được sửa một phần. Selection đổi khi đang
nhập số sẽ làm commit bị từ chối.

Draft không tăng revision document. Thay đổi qua nhiều lần gõ/style/scale được lưu
thành một bước khi nhấn `Ctrl+Enter`; Escape hủy bản nháp. Resize draft dùng font size
làm tỉ lệ chuẩn; frame width dương cũng scale cùng tỉ lệ để giữ bố cục tương đối.
Resize Text đã lưu tạo giao dịch transform và hoàn tác bằng `Ctrl+Z`.

## Nhãn và hướng dẫn

- Label ngắn: `Font family`, `Size (pt)`, `B`, `I`, `U`, `S`, `aA`, `Left`, `Center`,
  `Right`, `Justify`, `Tracking`, `Line spacing`, `Text frame (mm)`,
  `Find font from image`. Nhãn VI tương ứng được
  hiển thị theo ngôn ngữ ứng dụng.
- Nhóm font lấy từ family cài đặt Windows; UTM/SVN chỉ xuất hiện nếu hệ thống có font
  mang tên phù hợp. Nhóm Script/Serif/Sans dùng family metadata và heuristic tên, nên
  chỉ là cách duyệt nhanh, không thay thế metadata font chuẩn.
- Không thêm asset SVG mới: toolbox giữ icon Text hiện có; các điều khiển owner-drawn
  mang nhãn chữ rõ. Nút tìm font bằng ảnh không giả vờ có kết quả.
- Tooltip font field giải thích họ font; size nêu miền pt; B/I/U/S nêu style; căn đoạn
  giải thích Justify và dòng cuối; tracking nêu đơn vị em; Change Case nêu năm kiểu;
  frame nêu nghĩa 0 và wrapping; giãn dòng nêu phần trăm; nút ảnh báo thuật toán
  chưa được triển khai.
- Status khi scale: `Font size: … pt`; khi sửa số nêu miền hợp lệ, Enter áp dụng,
  Tab chuyển trường và Esc hủy. Sau commit selection, status nhắc `Ctrl+Z`.
- Cách dùng: chọn Text tool, click để đặt caret và gõ ngay trên canvas; kéo thân để
  di chuyển; rê vào cạnh/góc rồi kéo để scale; không cần rời Text tool. Nhấn `Ctrl+T`
  để mở Properties, chọn họ phông hoặc chỉnh size/style/paragraph/tracking. Nhấn
  mở `Text > Change Case ▸` rồi chọn kiểu cụ thể, hoặc nhấn `Shift+F3` để xoay
  lowercase → Title Case → UPPERCASE. Nhấn `Ctrl+Enter`
  để lưu phiên, `Esc` để hủy.
- Không có dialog tìm font bằng ảnh, icon mới, toolbox mới, panel mới hay standard
  toolbar mới. Fill/stroke/opacity thuộc Appearance của đối tượng, không phải thuộc
  tính font Character/Paragraph trong phạm vi này.

## Nghiệm thu

- Given Text đang có caret, When kéo góc hoặc cạnh, Then cả hai kích thước thay đổi
  cùng tỉ lệ, cạnh đối diện không nhảy, caret vẫn hoạt động và có thể gõ tiếp.
- Given Text đã chọn khi công cụ Text vẫn active, When kéo cạnh/góc, Then Text scale
  trực tiếp không méo, không yêu cầu Pick và một Undo hoàn tác thao tác.
- Given font/paragraph settings đang mở bằng `Ctrl+T`, When mở panel hoặc chọn field,
  Then draft và selection hiện đúng giá trị; mở panel không tạo Undo hay rời phiên nhập.
- Given nhiều Text có size/alignment khác nhau, When bật B/I/U/S hoặc đổi family,
  Then mỗi object giữ nguyên các thuộc tính không được chỉnh; Undo/Redo khôi phục tất cả.
- Given selection Mixed, locked/hidden hoặc bị đổi khi nhập, When áp dụng field,
  Then mixed hiển thị rõ, chỉ field được chọn thay đổi, hoặc thao tác bị từ chối nếu
  selection không hợp lệ.
- Given nút tìm font bằng ảnh, When bấm, Then UI chỉ thông báo thuật toán sẽ phát
  triển sau, không chạy hay báo kết quả giả.
- Given Text menu đang mở, When xem các lệnh, Then menu chỉ có lệnh và chuyển đổi;
  family, size, B/I/U/S và alignment được chỉnh ở Property Bar/Properties.
- Given Text menu đang mở, When rê vào `Change Case`, Then năm kiểu đổi chữ hiện
  trong popup con riêng; Enter/mũi tên phải mở menu con, mũi tên trái/Esc quay về
  menu Text, và chọn kiểu chỉ kích hoạt khi selection phù hợp. `Shift+F3` xoay ba kiểu phổ biến.
- Given Fit Text to Path, When chưa chọn đúng một Text và một Path, Then lệnh bị
  disable và tooltip nêu điều kiện; sau khi áp dụng, status chỉ vị trí Reverse,
  Straighten và Undo.
- Build/test không chứng minh độ mượt khi kéo, DPI, focus/accessibility hay giao diện
  thật; các điểm này cần kiểm bằng app native sau build.
