# Document core

Document core hiện có model/path source, command transactions, snapshots, undo/redo, stale-job checks và native storage/recovery. Đây là nền runtime đang dùng trong app; chưa có nghĩa toàn bộ yêu cầu M1, C5 hoặc durability đã được nghiệm thu.

## Cấu trúc

- include/pikdraw/document/model: object, editable path, topology và validation.
- include/pikdraw/document/commands và history: command, transaction writer và undo history.
- include/pikdraw/document/snapshots và async: snapshot, persistent map và proposal metadata.
- include/pikdraw/document/storage: native format, recovery draft và save/recovery API.
- src/document.cpp, src/commands, src/snapshots, src/async và src/storage: implementation.
- tests/: regression theo commands, snapshots, async và storage.

## Invariants

Tất cả ghi tài liệu đi qua command/transaction. Reader dùng snapshot có ownership rõ; không đọc mutable facade. Revision tăng qua commit và undo/redo. Async result phải khớp document, epoch, revision và job generation; close/cancel/edit làm kết quả cũ mất hiệu lực.

## Giới hạn hiện tại

Page/layer arrays còn có thể copy khi commit; byte budget/history retention và large-document performance chưa được chứng minh đầy đủ. Group/PowerClip và spatial indices đã có ở các consumer; coverage và chi phí invalidation cần đo theo workload. File `.pr` dùng schema v23 khi không có encoded raster source và schema v24 với bảng source/sample dedup; container v24 giữ đường chunk cũ, v25 mang schema mới, reader nhận v2–v25. Không còn trần tổng 256 MiB; mở file được giới hạn theo bộ nhớ còn trống. Atomic save stream payload vào file tạm bằng staging tối đa một chunk 1 MiB; `NativeFormat::serialize()` vẫn giữ API vector cho caller cần buffer. Load vẫn giữ container và payload giải nén trong memory, chưa streaming/lazy; xem [PR format](../../docs/architecture/native-pr-format.md). Native storage có bounded reader, nhưng migration/durability/recovery vẫn cần evidence riêng. Worker host tồn tại riêng; không nên hiểu các API lifecycle là một hệ thống import worker đã hoàn tất.

Dùng tooling/native/build.ps1 cho build/test phù hợp. Kiểm tra source snapshot và report hiện tại trước khi khẳng định một suite đã pass.
