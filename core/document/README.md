# Document — bản nháp spike B

Prototype C++20 có một writer, transaction rollback, công bố immutable snapshot/root và spatial index cùng revision. Reader lấy shared root bằng atomic shared_ptr operations; không copy graph mỗi lần đọc. Không cam kết lock-free. Các API khác trên Document dành cho writer; reader chỉ dùng `create_snapshot()` và snapshot bất biến.

Insert giữ bản clone riêng để chặn alias mutable; proposal kiểm document ID, lifecycle epoch, base revision, generation, cancel, payload null/ID trùng và ID không tồn tại. Batch update đi qua một transaction và một undo record. Undo/redo tăng revision; lifetime epoch phân biệt cả object Document mới mang cùng ID. Writer không tự chuyển sang thread khác.

History giữ tối đa 500 root tổng cộng giữa undo/redo, được kiểm thử qua 600 edits rồi undo/redo đến cạn. Chunk directory và spatial index dùng private paged COW map (256 buckets); snapshot/history copy shared root O(1), sửa chỉ detach directory/bucket liên quan. Iterator giữ root riêng và chỉ cho đọc; chunk rỗng được xóa. SourcePath chia sẻ segment storage có API const và detach khi sửa; transform/style không clone toàn bộ điểm. Không cam kết lock-free, worst-case bucket latency hay thread safety cho cùng một instance mutable. Reader vẫn phải dùng snapshot bất biến.

Có benchmark Release cục bộ: 20.000 PathObject rỗng để đo overhead metadata/index, một path thật 100.001 segment, 64 transforms và 65 retained roots. P95 command từ 25,3186 ms xuống 0,0636 ms; private bytes lúc giữ root từ 582.148.096 xuống 20.619.264. Cleanup từ 181,184 ms xuống 7,7696 ms. Xem [artifact](reports/cow-measurement-20261015.json) và `tests/benchmark_document.cpp`. Một run baseline/final candidate, chưa renderer/UI/corpus sản xuất hoặc threshold nghiệm thu. API metrics đo prepare, operation, source-validation (nằm trong operation), snapshot, history cleanup và publish; không cộng double-count các spans.

Dirty state hiện bảo thủ theo epoch/revision đã lưu; Undo về nội dung giống bản lưu vẫn có thể dirty. Không dùng dirty flag để chấp nhận proposal cũ. Loader công bố một snapshot duy nhất ở revision phục hồi, không đi qua command revision trung gian.

Native container thử nghiệm private version 1 giữ curve, UTF-8/font fields, RGB/CMYK/spot, tên ICC và overprint. Tên ICC không thay dữ liệu profile/embedded font. Text bounds chỉ là ước lượng theo byte, chưa shaping. Group/custom object bị từ chối cho tới khi có bounds/topology/hierarchy đúng.

Save dùng một snapshot, sibling temp riêng, flush stream và FlushFileBuffers trên Windows rồi atomic replace. Save r không làm sạch r+1. Loader kiểm size/checksum/truncation, phục hồi revision và tránh cấp lại ID đã dùng; epoch mới thuộc lifetime mới. Chưa có journal/checkpoint, kill-at-every-phase hoặc power-loss proof. Format chưa khóa cho sản phẩm.

26 native tests đã qua, gồm 10.000 commit seed 42 có oracle riêng (tối đa 64 object sống, 10 snapshot giữ lại), reader/writer stress, late proposals, rollback giữa batch, Unicode path, round-trip metadata, fault BeforeReplace và malformed native bytes. Có kiểm tra envelope/CRC bằng Python zlib độc lập. Chưa có sanitizer và independent acceptance review trên integrated SHA; gate B còn partial.
