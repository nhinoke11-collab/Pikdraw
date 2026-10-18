# Phiếu UI: Contour Properties

## 1. Mục tiêu và phạm vi

- **Task:** contour-properties-ui-20261018 · module desktop.
- **Mục tiêu:** nối spacing, steps, join và miter limit vào Inspector; giữ preview trong Contour tool và chỉ ghi document khi Apply. Bổ sung đường gọi Remove đã có ở controller.
- **Logic có sẵn:** ContourToolController giữ direction/spacing/steps/join/miter, tính preview, apply(), remove(), break_apart() và cancel(). Apply/Remove/Break dùng command document hiện có.
- **Phạm vi:** một Path đích, ID giữ nguyên. Path không tồn tại/không phải Path/bị khóa làm các trường và thao tác ghi bị disable. Không sửa semantics hình học hoặc chuyển đổi contour thành ảnh.
- **Giới hạn:** preview được xác nhận bởi engine hiện có; hình học lỗi hoặc Path mở không thể Apply. Không khẳng định khả năng sản xuất hay độ trung thực của định dạng ngoài PikDraw.
- **Tham chiếu:** [chuẩn UI/UX](../ui-ux-standard.md), [bố cục workspace](../../architecture/workspace-layout.md), [audit B3](../../reviews/ui-logic-gap-audit-20261018.md).

## 2. Đường vào và vị trí

| Bề mặt | Quyết định |
| --- | --- |
| Menu chính | Effects > Contour; gọi action.effects.contour.focus và chuyển sang Contour tool. |
| Toolbox | Appearance > Contour, cùng tool/controller với menu; action ID action.tool.contour. |
| Property bar | Giữ hướng Outside/Inside/To Center, spacing −/+0,5 mm, steps −/+, Apply và Break. Spacing giữ miền 0,1–100000 mm để khớp độ chính xác một số lẻ đang hiển thị; steps giữ miền 1–99. |
| Properties | Inspector > Contour, tự focus tab Inspector khi mở tool. Có hướng, spacing, steps, join, miter limit, trạng thái target/preview và Apply/Remove/Break/Cancel. |
| Action IDs | Menu/tool/Break dùng ID registry hiện có: action.effects.contour.focus, action.tool.contour, action.object.contour.break_apart. Các nút Apply/Remove/Break/Cancel trong Inspector có control ID contour.properties.apply, .remove, .break, .cancel; không tạo shortcut toàn cục mới. |
| Standard toolbar / dialog / panel riêng | Không áp dụng. Dùng property bar và dock hiện tại; không mở cửa sổ phụ. |

- **Icon:** toolbox giữ glyph Contour do draw_tool_icon vẽ bằng GDI+/GDI. Không có SVG Contour trong resource hiện tại và NativeWindow chưa nạp SVG icon; không thêm asset SVG rời chưa được runtime sử dụng.
- **Tooltip / focus help:** tên tool là Contour; mô tả “Preview and edit a parametric path contour” / “Xem trước và chỉnh viền Path có tham số”. Nhãn từng field/nút luôn hiển thị; hint trong Properties nêu miền, điểm Apply và cách khôi phục. Không quảng bá shortcut chưa đăng ký.
- **Status hint:** khi mở tool, hướng dẫn sửa trong Properties; khi nhập số, Enter cập nhật preview, Tab chuyển trường, Esc bỏ field; Apply/Remove/Break báo kết quả và Ctrl+Z khi command đã ghi.

## 3. Trường và trạng thái

| Nhãn EN · VI | Kiểu / miền | Phạm vi và trạng thái | Commit |
| --- | --- | --- | --- |
| Target · Đối tượng | Chỉ đọc: Path, số bước preview, đã lưu/chưa lưu | Path đích của controller; không tự lấy Path khác khi selection đổi | Chỉ đọc; đổi tool không ghi dữ liệu |
| Direction · Hướng viền | Outside / Inside / To center; mặc định Outside | Path không khóa; nút đang chọn có trạng thái active | Cập nhật preview, Apply mới ghi |
| Spacing · Khoảng cách | Số thập phân mm, 0,1–100000; mặc định 1 mm | Path không khóa; property bar hiển thị cùng giá trị đến một số lẻ | Enter/Tab xác nhận vào preview; dấu chấm hoặc phẩy thập phân |
| Steps · Số bước | Số nguyên, 1–99; mặc định 1 | Path không khóa; không nhận dấu thập phân | Như Spacing |
| Corner join · Góc nối | Miter / Round / Bevel; mặc định Miter | Path không khóa; lựa chọn hiện tại được đánh dấu | Cập nhật preview |
| Miter limit · Giới hạn Miter | Số thập phân, 1–100; mặc định 4 | Chỉ bật khi join=Miter; disabled với Round/Bevel | Enter/Tab cập nhật preview |
| Apply · Áp dụng | Nút | Bật khi Path không khóa và preview thành công | Ghi/cập nhật một ApplyContourCommand, một bước Undo |
| Remove · Xóa | Nút | Bật khi Path đích có Contour đã lưu | Gọi ContourToolController::remove(); một RemoveContourCommand, Undo khôi phục |
| Break · Tách | Nút | Bật khi Path đích có Contour đã lưu | Tạo các Path độc lập qua command hiện tại; Ctrl+Z hoàn tác |
| Cancel · Hủy | Nút | Bật với Path đích hợp lệ | Bỏ field đang nhập; nạp tham số đã lưu, hoặc khôi phục mặc định khi chưa có Contour. Không ghi document |

- Enter xác nhận field đang focus và tính lại preview; Tab xác nhận rồi chuyển theo thứ tự Spacing → Steps → Miter limit → Spacing. Esc trong field bỏ nội dung field đó.
- Giá trị không hợp lệ hoặc ngoài miền giữ field mở để sửa; controller không nhận giá trị một phần. Preview không hợp lệ làm Apply disabled và báo chọn Path kín hợp lệ.
- Properties dùng cùng controller với property bar. Chỉnh bất kỳ mặt nào cập nhật cùng preview; mở Inspector không tạo command/Undo.
- Cancel khi đã có Contour khôi phục thông số đã lưu; khi chưa có Contour thì trả về Outside / 1 mm / 1 step / Miter / limit 4.
- Break/Remove chỉ thao tác trên Path đích. Effects > Clear vẫn xóa effect stack thông thường, không thay thế Remove Contour.

## 4. Cách dùng và trạng thái trợ giúp

| Trạng thái | Hint EN · VI |
| --- | --- |
| Tool mở, chưa có target hợp lệ | Select an unlocked Path to edit Contour settings · Chọn Path không khóa để sửa Contour |
| Nhập số | Enter updates preview · Tab next · Esc cancels field · Enter cập nhật xem trước · Tab chuyển trường · Esc bỏ nhập |
| Preview đã sẵn sàng | Apply saves one undo step · Cancel restores saved settings · Apply lưu một bước Undo · Cancel khôi phục tham số đã lưu |
| Không có Contour để Remove/Break | This Path has no saved Contour · Path này chưa có Contour đã lưu |
| Apply không khả dụng | Choose an unlocked Path with a valid closed-path preview · Chọn Path kín không khóa có preview hợp lệ |

Hướng dẫn đầy đủ: chọn Path, mở Effects > Contour, đặt hướng và khoảng cách, nhập Steps/Join/Miter limit trong Properties, xem số bước preview, rồi Apply. Remove xóa feature còn gắn với Path; Break biến các loop thành Path độc lập; Ctrl+Z hoàn tác command.

## 5. Kiểm tra

- test_pikdraw_desktop_shell mở Contour từ flyout, kiểm tra Inspector được focus, nhập ba trường chính xác, xác nhận draft không đổi document revision, Apply một transaction, Cancel khôi phục draft, Remove gọi đúng operation và Undo phục hồi feature.
- Chưa tuyên bố nghiệm thu screen reader hoặc focus tuần tự cho các nút vẽ custom trong Inspector; thao tác số có Enter/Tab/Esc. Kiểm tra hình ảnh theo harness chỉ xác nhận paint path tại kích thước được chụp.
