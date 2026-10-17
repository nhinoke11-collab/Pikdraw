# Phiếu thiết kế UI: Hệ sinh thái Sao chép, Clipboard và Mảng Nhân/Chia (Copy, Paste, Duplicate, SketchUp Array & Step-Repeat)

Ngày lập: 17/10/2026.
Tài liệu tham chiếu chuẩn: [Chuẩn UX/UI](../ui-ux-standard.md), [Bố cục Workspace](../../architecture/workspace-layout.md), [Hướng dẫn tương thích tài nguyên vector](../../architecture/vector-resource-interoperability.md).
Đối chiếu chuẩn công nghiệp: CorelDRAW Technical Suite 2024/2025, Adobe Illustrator CC 2025/2026, Trimble SketchUp Pro 2024/2026 (Linear Array & Dynamic Spacing Engine).

---

## 1. Mục tiêu và trạng thái

- **Tên tính năng:** Hệ sinh thái Sao chép, Dán, Nhân bản và Mảng Nhân/Chia khoảng cách (Copy, Cut, Paste, Paste-in-Place, Duplicate, SketchUp-Style Multiplier/Divider Array `*N` & `/N`, Step & Repeat, Copy Properties).
- **Module:** `desktop`, `document`, `interop`.
- **Người dùng cần hoàn thành:**
  - Sao chép (`Ctrl+C`), Cắt (`Ctrl+X`) và Dán (`Ctrl+V`) các đối tượng đồ họa trong cùng bản vẽ hoặc giữa các tab tài liệu khác nhau.
  - Dán chính xác tọa độ gốc (`Ctrl+Shift+V` Paste in Place), dán đè lên trên (`Ctrl+F` Paste in Front), dán ra phía sau (`Ctrl+B` Paste in Back).
  - Trao đổi vector hai chiều qua Windows Clipboard với các phần mềm chuyên nghiệp (Adobe Illustrator, CorelDRAW, Inkscape, Figma, CAD) thông qua chuẩn SVG (`image/svg+xml`) và bitmap preview (`CF_DIBV5`).
  - Nhân bản trực quan nhanh chóng: `Alt + Drag` (nhân bản bằng kéo chuột), phím `+` NumPad (nhân bản tại chỗ $dx=0, dy=0$ kinh điển của Corel), và "đóng dấu" (Right-click / Spacebar stamping khi đang kéo).
  - **Mảng Nhân/Chia theo khoảng ban đầu kiểu SketchUp (SketchUp-Style Linear Array):**
    - Sau khi kéo sao chép một đối tượng đi một khoảng $\vec{d} = (\Delta x, \Delta y)$, người dùng có thể gõ trực tiếp từ bàn phím:
      - `*N` hoặc `xN` (ví dụ `*5`, `5x`): **Nhân chuỗi bản sao** — tạo ra $N$ bản sao liên tiếp cách đều nhau đúng bước nhảy $\vec{d}$ ban đầu.
      - `/N` (ví dụ `/5`): **Chia đều khoảng cách** — xem vị trí thả chuột là mốc đích cuối cùng, tự động chèn thêm $N-1$ bản sao phân bổ đều tăm tắp giữa mốc đầu và mốc cuối.
      - **Live Re-adjustment:** Cho phép gõ tiếp `*8` hoặc `/3` trong phiên đang active để thay đổi số lượng bản sao tức thì mà không làm rác Undo history.
  - Nhân bản tham số và chuỗi lặp: `Ctrl+D` (Duplicate theo offset cấu hình được hoặc lặp lại biến đổi cuối cùng), `Ctrl+Shift+D` (Step and Repeat cho việc rải LED, tem nhãn, hoa văn CNC).
  - Sao chép thuộc tính đồ họa (`Edit > Copy Properties From…` / Eyedropper).
- **Capability hiện có:**
  - Prototype `duplicate_selected(double offset_mm)` cơ bản trong `DocumentWorkflow` (đang hardcode offset $15.0\,\text{mm}$, chưa hỗ trợ remap `EditablePath` subpath/node/segment ID).
  - Menu `Edit` đã có các placeholder: `EditCut`, `EditCopy`, `EditPaste` nhưng đang bị disable (`is_disabled = true`) và chưa gán phím tắt.
  - Module `interop` đã có `SvgExporter` và `SvgImporter` hoàn chỉnh với schema validation và sandbox broker.
  - Document Engine đã có `CompositeCommand`, `AddObjectCommand`, `DeleteObjectCommand`, `TransformCommand`.
- **Phần thiết kế và cam kết trong tài liệu này:**
  - Toàn bộ đặc tả UI/UX, bảng phân loại menu, phím tắt, contextual property bar, bảng Properties chi tiết, status hints, cú pháp lệnh mảng nhân chia và quy tắc re-mapping ID bất biến.

---

## 2. Tìm thấy tính năng ở đâu

| Bề mặt | Quyết định cụ thể | Lý do / Đối chiếu |
| --- | --- | --- |
| **Menu chính** | `Edit > Cut` (`Ctrl+X`)<br>`Edit > Copy` (`Ctrl+C`)<br>`Edit > Paste` (`Ctrl+V`)<br>`Edit > Paste in Place` (`Ctrl+Shift+V`)<br>`Edit > Paste in Front` (`Ctrl+F`)<br>`Edit > Paste in Back` (`Ctrl+B`)<br>`Edit > Duplicate` (`Ctrl+D`)<br>`Edit > Step and Repeat…` (`Ctrl+Shift+D`)<br>`Edit > Copy Properties From…` | Menu `Edit` là nơi quy chuẩn theo mục 3 Chuẩn UX/UI dành riêng cho thao tác Clipboard, Duplicate và Selection. |
| **Action / Command ID** | `action.edit.cut`<br>`action.edit.copy`<br>`action.edit.paste`<br>`action.edit.paste_in_place`<br>`action.edit.paste_in_front`<br>`action.edit.paste_in_back`<br>`action.edit.duplicate`<br>`action.edit.step_and_repeat`<br>`action.edit.array_multiply`<br>`action.edit.array_divide`<br>`action.edit.copy_properties_from` | Định danh ổn định, tuân thủ tiền tố `action.edit.*` trong `action_id.hpp`. |
| **Loại thao tác** | - Action một lần: Cut, Copy, Paste, Paste-in-Place, Duplicate, NumPad `+`.<br>- Dynamic Keyboard Command: `*N` (nhân bước nhảy) và `/N` (chia đều tổng khoảng cách) ngay sau khi kéo sao chép.<br>- Modal/Panel tương tác: Step & Repeat (`Ctrl+Shift+D`), Copy Properties From…<br>- Interactive gesture: `Alt + Drag`, Right-click / Spacebar stamping trong lúc kéo. | Phân định rõ ràng giữa lệnh tức thì, lệnh phiên động (dynamic session) và panel chuyên dụng. |
| **Standard Toolbar** | Cụm nút icon sau cụm File và trước cụm Undo/Redo:<br>`[Cut] [Copy] [Paste] [Duplicate]` | Thao tác clipboard tần suất cao được đặt trên thanh công cụ chính với icon SVG tối giản chuẩn 2026. |
| **Toolbox / Flyout** | Không đặt lệnh clipboard vào Toolbox (Toolbox chỉ chứa công cụ vẽ). Bổ sung `Eyedropper Tool` (`I`) vào nhóm công cụ Utility/Measure trên Toolbox để phục vụ hút và đổ thuộc tính. | Tuân thủ mục 4 Chuẩn UX/UI: Toolbar chứa lệnh tài liệu/thao tác, Toolbox chỉ chứa công cụ tương tác canvas. |
| **Context Property Bar** | - Khi **không có selection**: Hiển thị cặp ô nhập `Duplicate Offset X` và `Duplicate Offset Y` (đơn vị mm, mặc định `5.0 mm`, `5.0 mm`).<br>- Khi **có selection**: Hiển thị nút nhanh `Duplicate` kèm icon nhỏ.<br>- Khi **vừa hoàn thành kéo sao chép (Active Array Session)**: Ô nhập hiển thị `Array: [ *1 ]` cho phép gõ số lượng nhân/chia hoặc cuộn chuột để tăng giảm bản sao. | Chuẩn xác theo trải nghiệm CorelDRAW & SketchUp VCB, giúp người dùng bảng hiệu/in ấn tùy chỉnh bước nhảy và số lượng nhân chia ngay tức thì. |
| **Properties (Dock phải)** | Section **"Duplicate & Array"** trong Properties panel:<br>- Chế độ tạo mảng: `Linear Offset (Step)` hoặc `Linear Span (Divide)`.<br>- Số lượng bản sao ($N$: $1 \dots 500$).<br>- Khoảng cách $X, Y$ (mm) và hướng (Trái/Phải, Trên/Dưới).<br>- Nút `Apply` để tạo mảng bản sao. | Tuân thủ mục 5 Chuẩn UX/UI: Thiết lập nhanh ở Property Bar, toàn bộ tham số chi tiết chuyên sâu đặt ở Properties dock. |
| **Context Menu (Chuột phải)** | Nhấp chuột phải vào selection trên Canvas hiển thị:<br>- `Cut` (`Ctrl+X`)<br>- `Copy` (`Ctrl+C`)<br>- `Paste` (`Ctrl+V`)<br>- `Paste in Place` (`Ctrl+Shift+V`)<br>- `Duplicate` (`Ctrl+D`)<br>- `Copy Properties From…` | Lối tắt ngữ cảnh quen thuộc cho người dùng chuột. |

---

## 3. Bảng thuộc tính và trường dữ liệu (Field Specifications)

| ID / Label (EN + VI) | Bề mặt | Kiểu / Đơn vị | Miền giá trị / Mặc định | Scope & Binding | Khi nào Enable / Mixed | Commit / Validation |
| --- | --- | --- | --- | --- | --- | --- |
| `dup_offset_x`<br>Offset X / Độ dời X | Property Bar & Properties | Number (`mm`) | $-10000.0 \dots 10000.0$<br>Mặc định: `5.0 mm` | Document / App Setting | Enable khi không chọn đối tượng hoặc đang chọn Pick tool | Commit khi Enter hoặc mất focus. Kiểm tra số thực hợp lệ. |
| `dup_offset_y`<br>Offset Y / Độ dời Y | Property Bar & Properties | Number (`mm`) | $-10000.0 \dots 10000.0$<br>Mặc định: `5.0 mm` | Document / App Setting | Enable khi không chọn đối tượng hoặc đang chọn Pick tool | Commit khi Enter hoặc mất focus. Kiểm tra số thực hợp lệ. |
| `array_expression`<br>Array Cmd / Lệnh mảng | Property Bar / Keyboard HUD | String | Cú pháp `*N`, `xN`, `/N`<br>($N \in [1, 500]$) | Active Array Session | Kích hoạt ngay sau khi hoàn thành kéo sao chép đối tượng | Commit ngay khi nhấn Enter. Tự động cập nhật mảng bản sao trực quan. |
| `step_repeat_count`<br>Copies / Số bản sao | Properties & Dialog | Integer | $1 \dots 500$<br>Mặc định: `1` | Modal Draft / Workflow Param | Enable khi đang chọn $\ge 1$ đối tượng | Commit khi nhấn `Apply`. Giá trị nguyên dương. |
| `step_repeat_mode_x`<br>Mode X / Chế độ X | Properties & Dialog | Enum | `None`, `Offset (Multiply)`, `Between (Divide)`<br>Mặc định: `Offset` | Modal Draft | Enable khi đang chọn $\ge 1$ đối tượng | Thay đổi tức thì hiển thị ô nhập khoảng cách tương ứng. |
| `step_repeat_distance_x`<br>Distance X / Khoảng cách X | Properties & Dialog | Number (`mm`) | $0.0 \dots 50000.0$<br>Mặc định: `10.0 mm` | Modal Draft | Enable khi Mode X $\ne$ `None` | Kiểm tra khoảng cách $\ge 0$. |
| `step_repeat_dir_x`<br>Direction X / Hướng X | Properties & Dialog | Enum | `Right` (+X), `Left` (-X)<br>Mặc định: `Right` | Modal Draft | Enable khi Mode X $\ne$ `None` | Xác định dấu âm/dương của vector tịnh tiến. |
| `step_repeat_mode_y`<br>Mode Y / Chế độ Y | Properties & Dialog | Enum | `None`, `Offset (Multiply)`, `Between (Divide)`<br>Mặc định: `None` | Modal Draft | Enable khi đang chọn $\ge 1$ đối tượng | Thay đổi tức thì hiển thị ô nhập khoảng cách tương ứng. |
| `step_repeat_distance_y`<br>Distance Y / Khoảng cách Y | Properties & Dialog | Number (`mm`) | $0.0 \dots 50000.0$<br>Mặc định: `10.0 mm` | Modal Draft | Enable khi Mode Y $\ne$ `None` | Kiểm tra khoảng cách $\ge 0$. |
| `step_repeat_dir_y`<br>Direction Y / Hướng Y | Properties & Dialog | Enum | `Up` (+Y), `Down` (-Y)<br>Mặc định: `Up` | Modal Draft | Enable khi Mode Y $\ne$ `None` | Xác định dấu âm/dương của vector tịnh tiến. |
| `copy_prop_fill`<br>Fill / Màu tô | Dialog / Bar | Checkbox (Boolean) | `true` / `false`<br>Mặc định: `true` | Dialog Draft | Enable khi đang có đối tượng đích | Xác định thuộc tính fill được chép. |
| `copy_prop_outline`<br>Outline / Đường viền | Dialog / Bar | Checkbox (Boolean) | `true` / `false`<br>Mặc định: `true` | Dialog Draft | Enable khi đang có đối tượng đích | Xác định thuộc tính stroke và stroke_width được chép. |
| `copy_prop_text`<br>Text / Thuộc tính chữ | Dialog / Bar | Checkbox (Boolean) | `true` / `false`<br>Mặc định: `false` | Dialog Draft | Enable khi cả 2 đối tượng là TextObject | Xác định font, font_size, alignment được chép. |

---

## 4. Ma trận ngữ cảnh thao tác (Contextual Matrix)

| Ngữ cảnh | Menu Edit (Cut, Copy, Paste, Duplicate) | Property Bar | Properties (Dock) | Status Hint (Gợi ý đáy màn hình) |
| --- | --- | --- | --- | --- |
| **Chưa mở tài liệu** | Tất cả đều Disable | Ẩn hoặc thông báo rỗng | Ẩn | `Sẵn sàng` |
| **Có tài liệu, không chọn đối tượng** | Cut, Copy, Duplicate: **Disable**.<br>Paste: **Enable** (nếu clipboard có dữ liệu). | Hiện ô nhập `Duplicate Offset X, Y` | Tab Document / Page setup | `Sao chép: Chọn đối tượng để thao tác hoặc nhấn Ctrl+V để dán dữ liệu từ clipboard.` |
| **Chọn 1 đối tượng** | Cut, Copy, Duplicate, Copy Properties: **Enable**.<br>Paste, Paste in Front/Back: **Enable** (nếu clipboard có data). | Hiện kích thước đối tượng + nút Duplicate nhanh | Hiện Transform, Appearance, Section Duplicate & Array | `Ctrl+C: Sao chép · Ctrl+D: Nhân bản · Giữ Alt kéo chuột để copy · Phím +: Nhân bản tại chỗ.` |
| **Chọn nhiều đối tượng** | Cut, Copy, Duplicate, Step & Repeat: **Enable**.<br>Copy Properties: **Disable** (chỉ sao chép thuộc tính vào 1 đối tượng). | Hiện Bounding Box + nút Duplicate | Hiện Shared Properties & Section Duplicate & Array | `Đã chọn N đối tượng · Ctrl+C: Sao chép toàn bộ · Ctrl+D: Nhân bản nhóm đối tượng.` |
| **Vừa thả chuột sao chép (Active Array Session)** | Enable các lệnh cơ bản | Ô nhập chuyển sang `Array: [ *1 ]` (nhận phím gõ trực tiếp) | Cập nhật số lượng đối tượng trong mảng | `Mảng sao chép: Gõ *N (ví dụ *5) để nhân chuỗi, hoặc /N (ví dụ /5) để chia đều khoảng cách · Esc để kết thúc.` |
| **Đối tượng bị khóa (Locked)** | Cut: Disable.<br>Copy, Duplicate: **Enable** (nhân bản tạo ra bản sao không bị khóa). | Báo `Locked` | Các trường hình học bị khóa (read-only) | `Đối tượng đang bị khóa. Có thể sao chép hoặc nhân bản bản sao mới.` |
| **Đang kéo chuột di chuyển (Pick Tool Dragging)** | Không áp dụng qua menu | Cập nhật tọa độ real-time | Cập nhật tọa độ | `Kéo để di chuyển · Giữ Alt để tạo bản sao · Nhấp chuột phải hoặc nhấn Spacebar để đóng dấu thả bản sao · Shift để khóa trục.` |

---

## 5. Danh mục Icon SVG, Nhãn ngắn và Trợ giúp truy cập

| Thành phần | Icon ID / Thiết kế | Nhãn ngắn EN | Nhãn ngắn VI | Phím tắt | Tooltip & Accessible Name |
| --- | --- | --- | --- | --- | --- |
| **Cut** | `icon_edit_cut`<br>(Cây kéo vector thanh thoát) | Cut | Cắt | `Ctrl+X` | Cut selection to clipboard — Cắt vùng chọn vào khay nhớ tạm |
| **Copy** | `icon_edit_copy`<br>(Hai trang giấy xếp chồng góc chéo) | Copy | Sao chép | `Ctrl+C` | Copy selection to clipboard — Sao chép vùng chọn vào khay nhớ tạm |
| **Paste** | `icon_edit_paste`<br>(Bảng kẹp tài liệu và tờ giấy) | Paste | Dán | `Ctrl+V` | Paste clipboard content to center of viewport — Dán nội dung vào giữa màn hình |
| **Paste in Place** | `icon_edit_paste_in_place`<br>(Tờ giấy với tọa độ ghim $x, y$) | Paste in Place | Dán tại vị trí gốc | `Ctrl+Shift+V` | Paste clipboard content at original coordinates — Dán giữ nguyên tọa độ gốc |
| **Paste in Front** | `icon_edit_paste_in_front`<br>(Tờ giấy trên lớp z-order nổi) | Paste in Front | Dán lên trên | `Ctrl+F` | Paste directly in front of selection — Dán ngay phía trên đối tượng gốc |
| **Paste in Back** | `icon_edit_paste_in_back`<br>(Tờ giấy dưới lớp z-order chìm) | Paste in Back | Dán ra sau | `Ctrl+B` | Paste directly behind selection — Dán ngay phía dưới đối tượng gốc |
| **Duplicate** | `icon_edit_duplicate`<br>(Hai hình vuông lồng nhau kèm dấu cộng) | Duplicate | Nhân bản | `Ctrl+D` | Duplicate selection with configured offset — Nhân bản đối tượng theo bước nhảy |
| **Instant Duplicate** | N/A (Phím tắt trực tiếp) | Duplicate at Origin | Nhân bản tại chỗ | `+` (NumPad) | Duplicate selection at identical position ($dx=0, dy=0$) — Nhân bản trùng khớp vị trí |
| **Linear Multiply Array** | N/A (Cú pháp SketchUp) | Multiply Array | Nhân chuỗi bản sao | `*N` hoặc `xN` | Create N copies along the initial distance vector — Tạo N bản sao theo bước nhảy ban đầu |
| **Linear Divide Array** | N/A (Cú pháp SketchUp) | Divide Array | Chia đều khoảng cách | `/N` | Divide the total span into N equal intervals — Chia đều tổng khoảng cách thành N phần bằng nhau |
| **Step & Repeat** | `icon_edit_step_repeat`<br>(Dãy $3 \times 3$ ma trận ô vuông tịnh tiến) | Step and Repeat… | Mảng nhân bản… | `Ctrl+Shift+D` | Create multiple copies with precise spacing — Tạo chuỗi bản sao theo khoảng cách |
| **Copy Properties** | `icon_edit_copy_properties`<br>(Cây bút hút thuộc tính kèm tia sáng) | Copy Properties From… | Sao chép thuộc tính từ… | Chưa gán | Copy fill, outline or text styling from another object — Sao chép kiểu dáng từ đối tượng khác |

---

## 6. Quy tắc nghiệp vụ và Kịch bản nghiệm thu (Acceptance Criteria)

### A. Quy tắc Re-mapping ID bất biến (Invariant ID Allocation)
1. **ObjectId**: Mọi đối tượng được dán, nhân bản hoặc sinh ra trong mảng `*N`, `/N` đều phải nhận `generate_object_id()` mới duy nhất.
2. **GroupObject**: Khi nhân bản một nhóm, phải tạo ID mới cho nhóm và đệ quy nhân bản mọi đối tượng con (`children_ids`), cập nhật danh sách con của nhóm mới với các ID mới sinh.
3. **EditablePath (Authoring Curves)**:
   - Nếu `PathObject` có `authoring_source`: **BẮT BUỘC** deep-clone toàn bộ `EditablePath`.
   - Sinh mới toàn bộ `SubpathId`, `NodeId`, và `SegmentId` bằng `generate_path_element_id<Id>()`.
   - Cấm sao chép nguyên trạng ID các node/segment cũ để tránh xung đột định danh trong các thuật toán Node Editing sau này.
4. **Transaction Integrity trong Active Array Session**:
   - Khi người dùng gõ `*5` rồi đổi thành `*8` hoặc `/4`, hệ thống tự động thu hồi các bản sao của lệnh trước đó và sinh mảng mới thay thế **trong cùng một Session Transaction**.
   - Khi người dùng bấm `Ctrl+Z`, toàn bộ mảng đối tượng vừa tạo được hoàn tác sạch sẽ chỉ trong **1 bước duy nhất**.

### B. Ca nghiệm thu (Given / When / Then)

#### Ca 1: Dán đối tượng với tọa độ gốc (Paste in Place)
- **Given**: Người dùng chọn một hình chữ nhật tại tọa độ $(100.0, 200.0)\,\text{mm}$ và nhấn `Ctrl+C`. Sau đó chuyển sang một tài liệu/tab khác hoặc di chuyển viewport đi nơi khác.
- **When**: Người dùng nhấn `Ctrl+Shift+V` (Paste in Place).
- **Then**:
  - Đối tượng mới được tạo ra với `ObjectId` mới hoàn toàn.
  - Tọa độ chính xác là $(100.0, 200.0)\,\text{mm}$ (không bị dịch chuyển vào giữa màn hình).
  - Đối tượng mới được chọn tự động.
  - Lịch sử Undo ghi nhận lệnh `"Paste in Place"`.

#### Ca 2: Nhân bản tức thì tại chỗ với phím `+` (CorelDRAW Workflow)
- **Given**: Người dùng chọn một hình tròn có bán kính $50\,\text{mm}$ tại $(300.0, 300.0)\,\text{mm}$.
- **When**: Người dùng nhấn phím `+` trên bàn phím số (NumPad Plus).
- **Then**:
  - Một bản sao hình tròn được tạo ngay tại $(300.0, 300.0)\,\text{mm}$ ($dx=0, dy=0$).
  - Đối tượng mới nằm ngay trên đối tượng gốc trong z-order của Layer.
  - Vùng chọn lập tức chuyển sang bản sao mới, cho phép người dùng kéo scale hoặc đổi màu ngay.
  - Nhấn `Ctrl+Z` hoàn tác, bản sao biến mất và hình tròn gốc vẫn nguyên vẹn.

#### Ca 3: Nhân bản kéo chuột (`Alt + Drag`) và Đóng dấu (`Spacebar Stamping`)
- **Given**: Người dùng dùng Pick Tool kéo một ngôi sao từ vị trí $A$.
- **When**: Trong lúc đang giữ chuột trái kéo, người dùng giữ phím `Alt` và nhả chuột trái tại vị trí $B$.
- **Then**:
  - Ngôi sao gốc tại vị trí $A$ vẫn tồn tại.
  - Một ngôi sao mới giống hệt xuất hiện tại vị trí $B$ với `ObjectId` mới.
  - Vùng chọn chuyển sang ngôi sao mới tại $B$.
- **When (Stamping)**: Trong lúc kéo từ $A$ đến $B$, người dùng nhấn phím `Spacebar` tại vị trí $C$.
- **Then**: Một bản sao được "đóng dấu" thả ngay tại vị trí $C$, con trỏ chuột vẫn tiếp tục kéo đối tượng đến vị trí $B$.

#### Ca 4: Nhân chuỗi bản sao theo khoảng ban đầu kiểu SketchUp (`*N` hoặc `xN`)
- **Given**: Người dùng chọn một thanh lam gỗ tại $X = 0\,\text{mm}$, giữ `Alt` kéo sang phải một khoảng $100\,\text{mm}$ ($B$ tại $X = 100\,\text{mm}$).
- **When**: Ngay sau khi nhả chuột, người dùng gõ `*5` (hoặc `5x`) trên bàn phím rồi nhấn `Enter`.
- **Then**:
  - Hệ thống tự động tạo thêm 4 bản sao tại các tọa độ $X = 200\,\text{mm}$, $X = 300\,\text{mm}$, $X = 400\,\text{mm}$, $X = 500\,\text{mm}$ (tổng cộng 5 bản sao cách đều nhau $100\,\text{mm}$).
  - Tất cả 5 bản sao nhận `ObjectId` mới độc lập.
  - Nhấn `Ctrl+Z` hoàn tác toàn bộ 5 bản sao trong 1 lần bấm duy nhất.

#### Ca 5: Chia đều khoảng cách tổng thể kiểu SketchUp (`/N`)
- **Given**: Người dùng muốn rải 5 chấn song cửa sổ vào một khung rộng $1000\,\text{mm}$. Người dùng giữ `Alt` kéo chấn song từ mép trái ($X = 0\,\text{mm}$) đặt sang mép phải ($X = 1000\,\text{mm}$).
- **When**: Người dùng gõ `/5` rồi nhấn `Enter`.
- **Then**:
  - Hệ thống tính toán bước chia $s = 1000\,\text{mm} / 5 = 200\,\text{mm}$.
  - Giữ nguyên bản sao ở $X = 1000\,\text{mm}$, đồng thời tự động chèn thêm 4 bản sao nằm tại $X = 200\,\text{mm}$, $X = 400\,\text{mm}$, $X = 600\,\text{mm}$, $X = 800\,\text{mm}$.
  - Khung cửa được chia thành 5 khoảng bằng nhau tuyệt đối.

#### Ca 6: Thay đổi số lượng mảng linh hoạt trong phiên (Live Re-adjustment)
- **Given**: Người dùng vừa gõ `*5` để tạo 5 thanh lam ở Ca 4.
- **When**: Thấy số lượng chưa đủ che kín bảng hiệu, người dùng gõ tiếp `*8` (Enter) mà không cần Undo hay thao tác lại từ đầu.
- **Then**:
  - Hệ thống tự động mở rộng mảng lên 8 thanh lam tại $X = 100, 200, \dots, 800\,\text{mm}$.
  - Undo stack vẫn chỉ chứa duy nhất 1 unit thao tác mảng.

#### Ca 7: Trao đổi dữ liệu Clipboard ngoài hệ thống (Interop SVG)
- **Given**: Người dùng chọn đối tượng vector trong PikDraw và nhấn `Ctrl+C`.
- **When**: Người dùng mở ứng dụng bên ngoài (Adobe Illustrator hoặc trình duyệt web) và nhấn `Ctrl+V`.
- **Then**: Ứng dụng ngoài nhận được dữ liệu chuẩn `image/svg+xml` và hiển thị chính xác hình học vector và màu sắc.
- **Ngược lại**: Khi copy một đoạn mã vector SVG từ trình duyệt hoặc Illustrator, chuyển sang PikDraw và nhấn `Ctrl+V`, PikDraw đưa qua `SvgImporter` sandbox, kiểm tra quota an toàn, tạo thành các `PathObject` native có thể chỉnh sửa tự do.
