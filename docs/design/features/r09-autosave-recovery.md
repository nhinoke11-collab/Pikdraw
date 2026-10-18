# R09 — Bản phục hồi tự động

Tái dùng File > Save/Save As (`FileSave/FileSaveAs`) và toolbar Save hiện có.
Timer tự động không có action sửa document. Không thêm toolbox, property bar,
Properties, field, icon SVG hoặc label mới; tooltip/focus help của Save giữ nguyên.
Settings > Preferences dùng phần hướng dẫn hiện có, cập nhật chu kỳ 60 giây.
Status chỉ báo lỗi ghi, không đổi trạng thái đã lưu khi checkpoint thành công.

Cứ 60 giây ghi snapshot của các tab đang bẩn, kể cả tab không active, vào
`%LOCALAPPDATA%/PikDraw/Recovery/<GUID>`. Mỗi phiên tài liệu/tab có thư mục riêng
vì nhiều tài liệu mới có thể dùng chung document ID. Không ghi lặp revision;
checkpoint cũ chỉ xóa sau khi bản thay thế được ghi an toàn. Save thành công dọn
checkpoint của đúng phiên/tab đó; save lỗi và thao tác hủy giữ bản phục hồi.
Đóng tab có bản phục hồi chưa lưu không tự xóa checkpoint.

Hướng dẫn: dùng File > Save để lưu chính thức; autosave không thay tệp gốc hoặc
đánh dấu sạch. Snapshot phục hồi được đọc qua SaveRecoveryService; màn hình tự
đề nghị phục hồi sau khi khởi động chưa nằm trong gói này.

AC-R09: timer trên hai tab bẩn tạo hai checkpoint khác nhau, giữ dirty/Undo;
lặp timer không nhân bản cùng revision, save một tab không dọn bản của tab khác.
Worker tests kiểm hạn mức Job Object thật, chặn spawn/allocation và kill-on-close;
không suy ra filesystem/network containment từ các hạn mức này.
Không đổi parser hoặc capability PDF/AI/CDR/EPS/SVG/DXF.
