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
| Menu chính | `Window > Properties` (`MenuActionId::WindowProperties`, `Ctrl+T`) mở cùng Inspector bên phải. `Text > Choose Font…` (`MenuActionId::TextFont`) mở danh mục font đã cài, không dùng hộp thoại “Show more fonts”. |
| Toolbox | Giữ Text tool index 4, `F8` và icon Text hiện tại; không thêm tool/flyout. |
| Standard toolbar | Không áp dụng; text formatting là ngữ cảnh của Text/đối tượng đang chọn. |
| Property bar | Giữ family, size, B/I/U, alignment và thao tác Text hiện có; cùng dữ liệu với Properties. |
| Properties | Section Text gồm Character, Paragraph & Text Frame; tất cả trường model đang hỗ trợ hiện trực tiếp, không có nút “Show more”. Nội dung chữ vẫn sửa trực tiếp trên canvas. |
| Dialog | Không thêm dialog riêng. Nút chọn họ phông trong Properties mở menu font đã cài theo nhóm; `Text > Choose Font…` và font picker trên Property bar cùng mở danh mục font đã cài. |
| Context menu | Không thêm menu chuột phải cho lần này. |
| Shortcut | `Ctrl+T` mở Properties từ mọi ngữ cảnh, kể cả khi caret đang soạn; `Ctrl+B/I/U` và `Ctrl+Q` giữ nghĩa hiện tại. |

## Trường và phạm vi

| ID / label EN + VI | Kiểu và miền | Scope, trạng thái, commit |
| --- | --- | --- |
| `TextFontFamily` · Font family / Họ phông | Danh sách font đang cài trên Windows; nhóm All, Sans serif, Serif, Script và UTM/SVN khi có font tương ứng. | Draft Text hoặc toàn bộ Text được chọn. Hủy menu không đổi dữ liệu. Họ phông được thêm vào registry của phiên làm việc để đồng bộ với property bar. |
| `TextFontSize` · Size (pt) / Cỡ chữ (pt) | Số thập phân, 1–2880 pt. | Draft preview hoặc một command có Undo cho selection đồng nhất. Enter áp dụng, Tab chuyển field, Esc hủy. |
| `TextBold`, `TextItalic`, `TextUnderline` · B/I/U | Toggle ký tự, trạng thái bật/tắt rõ trên nút. | Draft preview hoặc một command cho Text selection đồng nhất. `Ctrl+B/I/U` vẫn hoạt động khi đang soạn. |
| `TextAlignment` · Left/Center/Right | Căn đoạn trái, giữa, phải. | Draft preview hoặc một command cho Text selection đồng nhất; không đổi nội dung chữ. |
| `TextLineSpacing` · Line spacing / Giãn dòng | Phần trăm 25–1000%; model lưu ratio 0.25–10, mặc định 120%. | Draft hoặc toàn bộ Text selection; Mixed được thể hiện rõ. Enter áp dụng, Tab chuyển field, Esc hủy. |
| `TextFrameWidth` · Text frame (mm) / Khung chữ (mm) | Số thập phân 0–1.000.000 mm; 0 là point text, số dương tạo khung tự xuống dòng. | Draft hoặc toàn bộ Text selection; Mixed được thể hiện rõ. Enter áp dụng, Tab chuyển field, Esc hủy. |
| `TextFontFromImage` · Find font from image / Tìm font bằng ảnh | Nút trạng thái “Coming soon / Sắp có”; chưa chạy phân tích ảnh. | Có thể bấm để nhận trạng thái tính năng sẽ phát triển sau; không báo thành công giả và không sửa document. |

Khi thuộc tính font của nhiều Text khác nhau, nhóm Character hiển thị Mixed và không
cho áp dụng một style lên cả vùng chọn để tránh ghi đè giá trị riêng của từng chữ.
Paragraph frame và line spacing vẫn nhập được theo giá trị chung. Selection có
Text bị khóa/ẩn là read-only; selection hỗn hợp Text và loại đối tượng khác không
được sửa một phần. Selection đổi khi đang nhập số sẽ làm commit bị từ chối.

Draft không tăng revision document. Thay đổi qua nhiều lần gõ/style/scale được lưu
thành một bước khi nhấn `Ctrl+Enter`; Escape hủy bản nháp. Resize draft dùng font size
làm tỉ lệ chuẩn; frame width dương cũng scale cùng tỉ lệ để giữ bố cục tương đối.
Resize Text đã lưu tạo giao dịch transform và hoàn tác bằng `Ctrl+Z`.

## Nhãn và hướng dẫn

- Label ngắn: `Font family`, `Size (pt)`, `B`, `I`, `U`, `Left`, `Center`, `Right`,
  `Line spacing`, `Text frame (mm)`, `Find font from image`. Nhãn VI tương ứng được
  hiển thị theo ngôn ngữ ứng dụng.
- Nhóm font lấy từ family cài đặt Windows; UTM/SVN chỉ xuất hiện nếu hệ thống có font
  mang tên phù hợp. Nhóm Script/Serif/Sans dùng family metadata và heuristic tên, nên
  chỉ là cách duyệt nhanh, không thay thế metadata font chuẩn.
- Không thêm asset SVG mới: toolbox giữ icon Text hiện có; các điều khiển owner-drawn
  mang nhãn chữ rõ. Nút tìm font bằng ảnh không giả vờ có kết quả.
- Tooltip font menu giải thích nhóm font; size nêu miền pt; B/I/U nêu style; căn đoạn
  giải thích hướng căn; frame nêu nghĩa 0 và wrapping; giãn dòng nêu phần trăm; nút
  ảnh báo thuật toán chưa được triển khai.
- Status khi scale: `Font size: … pt`; khi sửa số nêu miền hợp lệ, Enter áp dụng,
  Tab chuyển trường và Esc hủy. Sau commit selection, status nhắc `Ctrl+Z`.
- Cách dùng: chọn Text tool, click để đặt caret và gõ ngay trên canvas; kéo thân để
  di chuyển; rê vào cạnh/góc rồi kéo để scale; không cần rời Text tool. Nhấn `Ctrl+T`
  để mở Properties, chọn họ phông hoặc chỉnh size/style/paragraph. Nhấn `Ctrl+Enter`
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
- Given một Text được chọn đồng nhất, When đổi size/style/alignment/frame/spacing,
  Then model và property bar đồng bộ; Undo/Redo khôi phục giá trị.
- Given selection Mixed, locked/hidden hoặc bị đổi khi nhập, When áp dụng field,
  Then không ghi một phần hoặc chọn ngầm thuộc tính của object đầu.
- Given nút tìm font bằng ảnh, When bấm, Then UI chỉ thông báo thuật toán sẽ phát
  triển sau, không chạy hay báo kết quả giả.
- Build/test không chứng minh độ mượt khi kéo, DPI, focus/accessibility hay giao diện
  thật; các điểm này cần kiểm bằng app native sau build.
