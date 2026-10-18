# Đặc tả UI phục hồi tài liệu — 18/10/2026

## Mục tiêu và phạm vi

Task `implement-recovery-ui-20261018`, desktop. Khi PikDraw tìm thấy autosave checkpoint hợp lệ sau lần chạy trước, người dùng xem được thời điểm, trang, số object và revision; có thể mở thành tab cần lưu, xóa riêng checkpoint hoặc để lại xử lý sau. Tài liệu phục hồi không ghi đè tệp gốc. Đường mở lại là `File > Recover Autosave…`.

Phạm vi hiện tại là checkpoint của toàn tài liệu. Pen tool draft có schema/controller riêng nhưng chưa có writer, đường dẫn hay hợp đồng khôi phục chạy trong shell; không giả lập nó như checkpoint tài liệu. Hệ thống liệt kê và khôi phục các file `.pkdr` hợp lệ trong thư mục Recovery, giới hạn số candidate và bỏ qua symlink/record sai.

## Placement và action

| Mục | Quyết định |
| --- | --- |
| Menu | `File > Recover Autosave…`; mở lại danh sách sau khi người dùng chọn Later |
| Action ID | `action.file.recover_autosave`, một action mở review; không tạo Undo |
| Startup | Mở review một lần sau khi cửa sổ đã vẽ frame đầu tiên nếu có candidate |
| Toolbar / toolbox | Không áp dụng: đây là thao tác khôi phục tài liệu, không phải lệnh vẽ thường dùng hay tool |
| Property bar / Properties | Không áp dụng: candidate chưa phải selection/tài liệu đang được chỉnh |
| Dialog | Dùng modal native theo visual tokens của shell hiện có; không thêm dock/panel |
| Icon | Không có icon runtime mới; mục menu có nhãn rõ và accessible name từ label |
| Shortcut | Chưa gán; không quảng bá shortcut chưa đăng ký |

## Candidate fields và state

| Field | Hiển thị | Scope / trạng thái | Khi thực hiện |
| --- | --- | --- | --- |
| Tài liệu phục hồi | ID ngắn ổn định lấy từ document ID | Read-only; selection chỉ đổi hàng đang focus | Không ghi tài liệu |
| Thời điểm | Local date/time từ sửa đổi file checkpoint | Read-only | Không ghi tài liệu |
| Trang | W × H mm | Read-only; nguồn từ snapshot đã xác thực | Không ghi tài liệu |
| Đối tượng / revision | Count và revision đã checkpoint | Read-only | Không ghi tài liệu |
| Recover | Mở candidate thành tab mới, còn dirty và không gắn file path | Enable khi candidate còn hợp lệ, tab capacity chưa cạn | Giữ checkpoint cho tới lần save thành công hoặc checkpoint mới thay an toàn |
| Discard | Xóa đúng candidate được chọn sau xác nhận | Disable khi danh sách rỗng hoặc candidate đã được khôi phục trong phiên hiện tại | Xóa file `.pkdr` hợp lệ nằm bên trong Recovery root |
| Later | Đóng review, giữ nguyên checkpoint | Luôn enable | Không ghi tài liệu/không tạo Undo |

Danh sách cho phép chọn bằng chuột hoặc Up/Down; Tab luân phiên danh sách/Recover/Discard/Later; Enter thực hiện control focus; Escape chọn Later. Khi nội dung lỗi/không hỗ trợ, giữ candidate và chỉ hiển thị lý do ngắn. Dialog có cùng nguồn candidate/validation; không có bản sao giá trị trên bar.

## Copy, hints và trợ giúp

English mặc định, Vietnamese theo lựa chọn ngôn ngữ hiện có. Nhãn ngắn: `Recover`, `Discard…`, `Later`; `Phục hồi`, `Xóa…`, `Để sau`. Tooltip/focus help cho menu: “Review autosaved documents from an earlier session.” / “Xem các bản tự lưu từ phiên trước.” Status hint mô tả trạng thái: tìm thấy candidate, đã mở thành tab chưa lưu, hoặc lỗi đọc/xóa. Dialog nêu rõ “Recovery opens a new unsaved tab. Save it to choose a permanent file.” / “Bản phục hồi mở ở tab mới chưa lưu. Hãy lưu để chọn vị trí tệp chính thức.”

Help theo hành động 3 bước: chọn checkpoint; chọn Recover; Save As để lưu tài liệu. Discard chỉ xóa checkpoint đã xác thực, có hộp xác nhận. Click ngoài không đóng dialog và không xóa dữ liệu. Không áp dụng progress/cancel job vì thao tác hiện đồng bộ, hữu hạn 128 file; nếu vượt ngưỡng thì báo còn candidate chưa được liệt kê thay vì quét vô hạn.

## Kiểm chứng

- Storage test: scan root qua một cấp, đọc metadata từ snapshot, bỏ qua symlink/record không hợp lệ, giới hạn list và chỉ discard file nằm trong root.
- Desktop test: startup hiển thị review khi tìm thấy candidate; Recover tạo tab mới với object/page chính xác, document dirty, không có file path; checkpoint vẫn còn cho tới save; Later không thay đổi candidate; Discard xác nhận và xóa đúng một checkpoint.
- UI runtime evidence: cần kiểm native build ở kích thước 1366×768 và 1920×1080, scale thiết bị có sẵn, light/dark, keyboard focus, Recover/Discard/Later và lỗi. Unit/integration test không thay thế nghiệm thu trực quan.
