# Soạn chữ trực tiếp trên canvas

Task: `text-canvas-editing-20261018`, desktop, `codex:text-canvas`.

## Mục tiêu và quyết định UI

Chữ đang nhập được vẽ bằng cùng layout/glyph với chữ đã lưu, tại đúng vị trí trên
canvas. Không có nền ô nhập, placeholder, hộp thoại hay hướng dẫn đè lên artwork.
Nhấp để tạo chữ; nhấp chữ để đặt caret; kéo chọn ký tự; nhấp đúp chọn từ.
Enter xuống dòng, Ctrl+Enter kết thúc, Escape hủy cả phiên và khôi phục nguồn.
Nhấp ngoài chữ hoặc chuyển công cụ kết thúc phiên. Đổi tab/tài liệu phải kết thúc
trên tài liệu cũ trước khi đổi; Save nhận bản nháp trước khi ghi.

Trong Text tool, kéo thân một text object đã có để di chuyển; nhấp thân để đặt
caret và sửa nội dung. Sau Ctrl+Enter, object vẫn được chọn: kéo tay nắm để scale;
click-drag thân để di chuyển. Text frame của đoạn văn không vẽ khung xanh trong
phiên nhập; caret và vùng chọn ký tự là phản hồi nhập duy nhất. Drag ký tự sau khi
đã vào chế độ sửa vẫn chọn văn bản, không di chuyển object.

Tham khảo hành vi, không sao chép tài sản:
- [Adobe Illustrator: point/area text](https://helpx.adobe.com/illustrator/desktop/design-with-text/add-manage-text/add-text-to-vector-artwork.html).
- [Corel: thêm/chọn chữ](https://kb.corel.com/en/127995).
- [Microsoft Word: chọn chữ và phím tắt](https://support.microsoft.com/en-us/accessibility/word/keyboard-shortcuts-in-word?wt.mc_id=otc_word).

Phạm vi lần này: chữ tự do nhiều dòng và đoạn văn có khung tự xuống dòng (kéo
trên canvas để tạo khung); chọn/chèn/xóa, điều hướng, clipboard, Unicode/IME,
định dạng toàn đối tượng và lưu/Undo nguyên tử. `Ctrl+F8` chuyển giữa chữ điểm và
khung đoạn văn. Giữ khoảng cách dòng đã có trong tài liệu; lần này chưa có ô
chỉnh khoảng cách dòng. Chưa có định dạng khác nhau trong từng span, liên kết
khung, text-on-path, bullets/columns hoặc bộ kiểm tra chính tả. Không dựng lệnh giả.

## Vị trí và điều khiển

| Bề mặt | Quyết định |
| --- | --- |
| Menu | Giữ `Text > Edit Text` (`TextEdit`) và các lệnh chuyển đổi/chỉnh nội dung. Font, B/I/U/S và alignment chỉ ở Property Bar/Properties; không lặp trong menu. |
| Toolbox | Giữ công cụ Text (index 4), icon SVG Text hiện có; F8 chọn Text. |
| Standard toolbar | Không thêm nút; dùng thanh thuộc tính Text hiện có. |
| Property bar / Properties | Font, size (pt), B/I/U, Left/Center/Right; tái dùng giá trị và command hiện có; khi nhập chỉ sửa draft. Properties hiện có không mở hộp nhập nội dung. Độ rộng khung đặt bằng cách kéo khung trên canvas. |
| Scope | Phiên nhập: toàn đối tượng draft; ngoài phiên: các text object được chọn. Không giả định hỗ trợ style từng ký tự. |
| Font | Giữ family/PostScript/weight nguồn khi không thay; không thay font lạ bằng font đầu danh sách. |
| Size | Giá trị nguồn hợp lệ được giữ; tăng/giảm theo control hiện có. |
| Color | Giữ màu nguồn/appearance khi sửa; chữ mới dùng drawing defaults, không ép màu neon. |
| Dialog/panel mới | Không áp dụng; canvas là vùng soạn thảo. |
| Help | Tài liệu này; hint ngắn ở status: Enter xuống dòng, Ctrl+Enter kết thúc, Esc hủy. |

Không tài liệu/locked/hidden/read-only: không bắt đầu phiên. Nhiều selection:
định dạng toàn text object như control hiện hành; một phiên chỉ sửa một object.
Mixed character style không áp dụng vì model hiện chỉ có một font mỗi object.
Tool/section opening không tự ghi document. Focus rời app giữ draft và tắt caret.
IME composition chỉ hiện preview; kết thúc composition mới nhập Unicode nguồn.
Revision/epoch/page thay đổi ngoài phiên: từ chối commit stale, không ghi đè.

## Hành vi và nghiệm thu

- Draft dùng immutable preview snapshot, không thêm lịch sử hay sửa nguồn.
  Sửa nội dung + font/style + tạo mới trong group là một command/Undo.
- Giữ ID, transform, fill/CMYK, appearance và Unicode NFC/NFD; xóa hết chữ
  của object cũ phải lưu chuỗi rỗng, draft rỗng mới không tạo object.
- Caret/selection lấy glyph advance từ layout thật; Backspace/Delete đi theo
  grapheme, không cắt byte UTF-8 hay dấu tổ hợp. Cặp surrogate được ghép đúng.
- Ctrl+A/C/X/V/Z/Y; Shift+mũi tên, Ctrl+mũi tên, Home/End, Up/Down;
  Ctrl+B/I/U đổi style toàn draft. Không kích hoạt shortcut công cụ khi đang gõ.
- Preview và commit cùng vị trí baseline/zoom/transform. Chữ cũ được thay trong
  snapshot preview; không vẽ hai bản. Alignment và underline phải có hiệu lực
  sau commit, render và native reopen.

Ca tự động mở rộng `test_desktop_shell_text_settings.cpp` và suite desktop shell:
gõ tiếng Việt có dấu tổ hợp, chèn giữa chuỗi, chọn/xóa/dán, Enter nhiều dòng,
cancel/no-op, local Undo/Redo, một bước document Undo/Redo, font ngoài danh sách,
giữ màu/transform, stale revision, Save/reopen, kích hoạt bằng chuột/phím.
Native screenshot chỉ xác nhận hình được chụp; không suy ra nghiệm thu IME ngoài
máy, DPI đa màn hình, hiệu năng hay đầy đủ Unicode/script từ test pass.
