# Đặc tả Thiết kế & Tương tác: Luồng Công cụ Pick, Thước Gióng và Phím tắt theo chuẩn CorelDRAW / SketchUp

## 1. Mục tiêu và Tổng quan
Đáp ứng trải nghiệm vẽ và biên tập đồ họa vector chuyên nghiệp chuẩn CorelDRAW & SketchUp:
1. **Tự động chọn (Auto-Pick) sau khi vẽ hình**: Khi hoàn thành thao tác vẽ một hình học (Rectangle, Ellipse, Polygon), hệ thống tự động chọn ngay đối tượng vừa tạo và chuyển sang công cụ Pick (Select Tool - Tool ID 0). Người dùng có thể di chuyển, co giãn (scale), xoay (rotate) hoặc đổi màu ngay lập tức mà không cần bấm đổi công cụ thủ công.
2. **Tự động khôi phục công cụ vẽ (Auto-Resume Drawing Tool)**: Khi người dùng nhấp chuột ra ngoài vùng trống (deselect / bỏ chọn), công cụ vẽ trước đó (`previous_drawing_tool_`) tự động kích hoạt trở lại để người dùng tiếp tục vẽ thêm các hình mới một cách liền mạch.
3. **Tương tác Thước gióng (Guidelines Interaction)**: Cho phép nhấp chọn (pick) và kéo di chuyển vị trí đường gióng ngang / dọc trực tiếp trên canvas. Nếu người dùng kéo đường gióng ra ngoài canvas (hoặc kéo về phía thước), đường gióng sẽ tự động được xóa an toàn.
4. **Phím tắt V và Space chuyên nghiệp**:
   - Phím `V`: Lập tức chuyển sang công cụ Pick (Select Tool - Tool ID 0) và ghi nhớ công cụ trước đó.
   - Phím `Space`: Toggle chuyển đổi qua lại giữa công cụ Pick và công cụ vẽ đang thao tác (CorelDRAW & SketchUp style). Nhấn Space lần 1 chuyển sang Pick; nhấn Space lần 2 chuyển trở lại công cụ vẽ trước đó. (Tự động bỏ qua khi đang gõ text hoặc nhập liệu số).

---

## 2. Đặc tả Giao diện & Tương tác UI/UX

### 2.1 Đường Menu & Action ID
- **Menu Bar**: View -> Guidelines (`FileToggleGuidelines`, `FileClearGuidelines`).
- **Phím tắt**:
  - `V`: Pick Tool (Tool 0).
  - `Space`: Toggle Pick Tool / Previous Drawing Tool.
  - `Delete` / `Backspace`: Xóa đối tượng hoặc đường gióng đang chọn.
  - `Esc`: Bỏ chọn hoặc thoát chế độ xoay.

### 2.2 Vị trí Toolbox & Con trỏ Chuột (Cursor Affordance)
- **Toolbox Row 0**: Công cụ Pick / Chọn (Icon SVG 2026 hiện đại, thanh thoát).
- **Con trỏ chuột**:
  - Khi hover vào đường gióng ngang: Con trỏ đổi thành `IDC_SIZENS` (32645, mũi tên lên/xuống).
  - Khi hover vào đường gióng dọc: Con trỏ đổi thành `IDC_SIZEWE` (32644, mũi tên trái/phải).
  - Khi hover vào tay nắm xoay: Con trỏ đổi thành `IDC_HAND` (32649).
  - Khi hover vào đối tượng được chọn: Con trỏ di chuyển `IDC_SIZEALL` (32646).

### 2.3 Status Bar Hint (Thông báo trạng thái song ngữ)
- **Sau khi tạo hình**:
  - Tiếng Việt: `"Đã tạo đối tượng và chọn ngay (Pick) · Kéo tay nắm để scale/xoay, click ngoài để tiếp tục vẽ"`
  - English: `"Created object and selected (Pick) · Drag handles to scale/rotate, click outside to resume drawing"`
- **Khi click ngoài khoảng trống (Deselect)**:
  - Tiếng Việt: `"Đã bỏ chọn · Khôi phục công cụ vẽ để tiếp tục tạo hình"`
  - English: `"Deselected · Resumed drawing tool"`
- **Khi kéo di chuyển đường gióng**:
  - Tiếng Việt: `"Dời đường gióng ngang Y: ... mm"` / `"Dời đường gióng dọc X: ... mm"`
  - English: `"Move horizontal guideline Y: ... mm"` / `"Move vertical guideline X: ... mm"`
- **Khi xóa đường gióng kéo ra ngoài**:
  - Tiếng Việt: `"Đã xóa thước gióng kéo ra ngoài bản vẽ"`
  - English: `"Deleted guideline dragged off-canvas"`

---

## 3. Kiến trúc Hiện thực & Luồng Xử lý Dữ liệu

### 3.1 Bảng biến trạng thái trong `NativeWindow`
- `previous_drawing_tool_` (`int`, mặc định 1 - Rectangle): Lưu lại công cụ vẽ gần nhất được sử dụng trước khi chuyển sang Pick tool.
- `selected_guideline_index_` (`int`, mặc định -1): Chỉ số của thước gióng đang được chọn hoặc kéo.
- `is_dragging_guideline_` (`bool`): Cờ xác định trạng thái kéo dời vị trí thước gióng.
- `drag_guideline_pos_mm_` (`double`): Tọa độ mm hiện thời của thước gióng trong quá trình kéo chuột.
- `drag_guideline_orientation_` (`GuidelineOrientation`): Hướng ngang (Horizontal) hoặc dọc (Vertical).

### 3.2 Quy trình Xử lý Sự kiện Chuột & Bàn phím
1. **Tạo hình hoàn tất (`NativeWindow::commit_created_shape`)**:
   - Thực thi `AddObjectCommand` qua Document Transaction.
   - Gọi `root().vector_controller().set_selection({object.id}, ...)`.
   - Lưu `previous_drawing_tool_ = active_tool_`.
   - Gọi `set_active_tool(0)` (chuyển sang Pick tool).
2. **Click ra ngoài vùng trống (`NativeWindow::handle_canvas_mouse_up`)**:
   - Khi `is_marquee_` và `clicked_without_drag` và `SelectionMode::Replace`:
   - Xóa vùng chọn: `root().vector_controller().set_selection({}, ...)`.
   - Tự động gọi `set_active_tool(previous_drawing_tool_)`.
3. **Tương tác Thước gióng (`NativeWindow::handle_canvas_mouse_down`)**:
   - Kiểm tra `guideline_at(x, y)` với dung sai 5px theo camera viewport.
   - Nếu trúng guideline và ở Tool 0: kích hoạt `is_dragging_guideline_ = true`.
   - Khi di chuột: cập nhật `position_mm` trực tiếp và hiển thị tọa độ trên status bar.
   - Khi thả chuột: nếu vị trí nằm trong canvas, áp dụng vị trí mới; nếu nằm ngoài canvas hoặc kéo về phía thanh thước, xóa guideline khỏi danh sách `guidelines_`.
4. **Phím tắt Space & V (`NativeWindow::handle_key_down`)**:
   - Bỏ qua khi `is_inline_text_editing_` hoặc `active_num_field_ != None`.
   - Phím `V`: Lưu `previous_drawing_tool_ = active_tool_` (nếu đang ở công cụ vẽ), sau đó chuyển `set_active_tool(0)`.
   - Phím `VK_SPACE`:
     - Nếu đang ở `active_tool_ == 0`: chuyển về `set_active_tool(previous_drawing_tool_)`.
     - Nếu đang ở công cụ khác: lưu `previous_drawing_tool_ = active_tool_`, chuyển về `set_active_tool(0)`.

---

## 4. Nghiệm thu & Kiểm thử (Feature Acceptance)

| Ca kiểm thử | Trạng thái | Ghi chú nghiệm thu |
| :--- | :--- | :--- |
| **AC-CP-01**: Vẽ hình học tự động chọn (Pick) và chuyển tool 0 | **PASS** | Kiểm chứng qua `test_desktop_shell`: `sel.count() == 1`, `active_tool() == 0`, `previous_drawing_tool() == 2`. |
| **AC-CP-02**: Click ngoài canvas bỏ chọn tự động khôi phục công cụ vẽ | **PASS** | Kiểm chứng qua `test_desktop_shell`: `sel.count() == 0`, `active_tool() == 2`. |
| **AC-CP-03**: Hover thước gióng hiển thị đúng con trỏ `IDC_SIZENS` / `IDC_SIZEWE` | **PASS** | Kiểm chứng qua `canvas_cursor_at` với dung sai 5px. |
| **AC-CP-04**: Pick chọn và kéo di chuyển thước gióng sang tọa độ mới | **PASS** | Thước gióng cập nhật chính xác giá trị mm trên canvas. |
| **AC-CP-05**: Kéo thước gióng ra ngoài canvas xóa thước gióng | **PASS** | Số lượng thước gióng giảm an toàn, không rò rỉ bộ nhớ. |
| **AC-CP-06**: Phím tắt `Space` toggle 2 chiều giữa Pick Tool và Drawing Tool | **PASS** | Toggle mượt mà, lưu vết công cụ chính xác. |
| **AC-CP-07**: Phím tắt `V` kích hoạt Pick Tool | **PASS** | Chuyển ngay sang Tool 0 từ bất kỳ công cụ vẽ nào. |
| **AC-CP-08**: Không gây hồi quy lên các tính năng Drag, Rotate, Palette, Pen | **PASS** | Toàn bộ 75/75 test suite CTest đều đạt 100% PASS. |
