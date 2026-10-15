# PikDraw document

Trạng thái: runtime thư viện đang được sửa và kiểm chứng; chưa nghiệm thu ps07/ps08 hoặc M1.
Dependencies: geometry, color, typography; không phụ thuộc UI.

| Nhóm | API / implementation | Kiểm tra |
| --- | --- | --- |
| Writer / snapshots | document.hpp, src/document.cpp, snapshots/document_snapshot.hpp | commands, snapshots, async/test_writer_safety.cpp |
| Async lifecycle | async/async_proposal.hpp, src/async/proposal_validator.cpp | async/test_stale_proposals.cpp, async/test_writer_safety.cpp |
| Commands / history | commands/, history/undo_history.hpp | commands/test_commands.cpp, independent audit regressions |
| Storage prototype | storage/, src/storage/ | storage/test_storage.cpp, test_storage_fidelity.cpp; chưa nghiệm thu C5/durability |

Document gắn với thread tạo nó. Chỉ `current_snapshot()` được gọi đồng thời từ reader;
reader giữ shared ownership snapshot và không truy cập mutable facade. Publication dùng
atomic shared_ptr load/store; không cam kết lock-free. Callback notification không được
reenter writer; lỗi callback tăng `listener_failure_count`, không đảo commit đã công bố.
Command lỗi hoặc throw phục hồi toàn bộ page/object root; undo/redo phục hồi committed
roots, tránh phụ thuộc rollback từng bước của command lỗi. Revision tăng cả undo/redo.
History giới hạn 100 bước; **chưa có memory-byte budget**.

Trước dispatch worker, owner gọi `begin_job(id)` để lấy metadata. Hủy qua `cancel_job(id)`;
khởi động lại cùng ID tạo generation mới; proposal kiểm tra document ID, lifecycle epoch,
strict base revision, job ID/generation, source targets còn tồn tại và commands không null.
Edit/undo/redo vô hiệu outstanding jobs. `close()` chặn mọi commit muộn. Chưa có worker
executor/IPC runtime nối các API này. `mark_saved` chỉ nhận completion đúng revision hiện tại.
IDs nhập vào command được reserve để generator không đè object đã có.

Known limits cần xử lý trước M1:
- PersistentObjectMap dùng AVL bất biến, chia sẻ node/object không đổi; `all()` trả view
  giữ root sống, `lookup_ref()` mượn object theo lifetime snapshot. Page/layer ID arrays vẫn copy
  khi commit; chưa chứng minh budget trên tài liệu lớn bằng benchmark.
- Chưa có immutable spatial index, asset/image model, group edit services hoặc byte budget.
- Native codec v2 giữ các thuộc tính nguồn đang có trong model, bounded parsing và full-container
  CRC32; xem ADR 0005. V1 lossy bị từ chối. Chưa đủ schema/metadata/migrations/recovery index C5.
- Atomic save dùng OS flush/replace, không xóa target sau lỗi; API UTF-8. Fault injection không
  thay bằng chứng mất điện. File I/O/codec tests cần chạy trên bản tích hợp cuối.
- Parser có giới hạn file/count/string và malformed fixtures; chưa nghiệm thu budget trên máy xưởng.
- Chưa chứng minh durability mất điện, recovery index hoặc migrations.
- Chưa có desktop để nghiệm thu thao tác người dùng.

Build: CMake root với PIKDRAW_BUILD_TESTS=ON; chạy CTest regex `test_pikdraw_document`.
Báo cáo audit độc lập chứa lệnh, revision và kết quả thật; việc có API không chứng minh editor hoàn chỉnh.
