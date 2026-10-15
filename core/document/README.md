# Document — bản nháp spike B

Prototype C++20 có một writer, transaction rollback, công bố immutable snapshot/root và spatial index cùng revision. Reader lấy shared root bằng atomic shared_ptr operations; không copy graph mỗi lần đọc. Không cam kết lock-free. Các API khác trên Document dành cho writer; reader chỉ dùng `create_snapshot()` và snapshot bất biến.

Insert giữ bản clone riêng để chặn alias mutable; proposal kiểm document ID, lifecycle epoch, base revision, generation, cancel, payload null/ID trùng và ID không tồn tại. Batch update đi qua một transaction và một undo record. Undo/redo tăng revision; lifetime epoch phân biệt cả object Document mới mang cùng ID. Writer không tự chuyển sang thread khác.

History giữ tối đa 500 root. COW mới theo chunk; map/index vẫn copy O(n) khi writer chuẩn bị/commit và path clone vẫn copy vector. Chưa đo ngân sách prepare/validation/commit/retention/cleanup trên tài liệu sản xuất; chưa có persistent asset tree hoặc memory budget theo bytes. Đây là giới hạn cần xử lý trước production, không xem atomic publication là gate hiệu năng đã qua.

Native container thử nghiệm private version 1 giữ curve, UTF-8/font fields, RGB/CMYK/spot, tên ICC và overprint. Tên ICC không thay dữ liệu profile/embedded font. Text bounds chỉ là ước lượng theo byte, chưa shaping. Group/custom object bị từ chối cho tới khi có bounds/topology/hierarchy đúng.

Save dùng một snapshot, sibling temp riêng, flush stream và FlushFileBuffers trên Windows rồi atomic replace. Save r không làm sạch r+1. Loader kiểm size/checksum/truncation, phục hồi revision và tránh cấp lại ID đã dùng; epoch mới thuộc lifetime mới. Chưa có journal/checkpoint, kill-at-every-phase hoặc power-loss proof. Format chưa khóa cho sản phẩm.

21 native tests đã qua, gồm 10.000 commit seed 42 có oracle riêng (tối đa 64 object sống, 10 snapshot giữ lại), reader/writer stress, late proposals, rollback giữa batch, Unicode path, round-trip metadata, fault BeforeReplace và malformed native bytes. Có kiểm tra envelope/CRC bằng Python zlib độc lập. Chưa có sanitizer và independent acceptance review trên integrated SHA; gate B còn partial.
