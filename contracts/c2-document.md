# Contract C2: Document, commands và snapshot

Version: 1.2.0 (đồng bộ tài liệu với API hiện có; không đổi API/wire).
Audited: 2026-10-24, source đang làm việc.
Producer: `core/document`. Consumers: renderer, desktop, vector, raster, interop, production, automation.
Status: writer, snapshot, command/Undo và async proposal đã có trong source; giới hạn được ghi riêng dưới đây.

## 1. Một logical writer

`Document` ghi nhận thread tạo nó và kiểm tra quyền writer/reentrancy. Desktop đưa thay đổi về
thread sở hữu document. Worker, renderer và MCP/COM không được sửa tree trực tiếp; các lệnh
automation phải đi qua cùng command/transaction và kiểm tra phiên làm việc như UI.

`current_snapshot()` công bố `shared_ptr<const DocumentSnapshot>` bằng atomic load/store.
Reader giữ snapshot bất biến trong suốt công việc. Không suy ra atomic `shared_ptr` là lock-free
trên mọi standard library. `PersistentObjectMap` chia sẻ dữ liệu giữa các phiên bản; page collection
và một số bước prepare vẫn sao chép. Chưa có bảo đảm mọi edit chỉ tốn O(log n) hoặc mọi asset đều
không copy. Reader/history còn giữ snapshot thì tài nguyên liên quan còn sống.

## 2. Identity, revision và trạng thái lưu

| Trường | Hành vi hiện tại |
| --- | --- |
| `document_id` | String định danh document; không thay thế lifecycle epoch. |
| `epoch` | uint64, cấp cho lifecycle mới/reset; proposal của lifecycle cũ bị từ chối. |
| `revision` | uint64, tăng một khi publish command có thay đổi, kể cả Undo/Redo; không quay về revision cũ. |
| `job_id`, `job_generation` | Job ID khác 0 và generation do `begin_job` cấp; bảng job quyết định còn hợp lệ hay đã hủy. |
| `active_page_id`, `page_navigation_generation` | Điều hướng trang có thể publish snapshot cùng revision và tăng navigation generation. |

Command trả `no_change` không thêm history hoặc revision. Hết miền uint64 phải báo lỗi, không wrap.
`mark_saved(epoch, revision)` chỉ làm sạch đúng snapshot đã lưu. Dirty còn phụ thuộc checkpoint
được phục hồi chưa lưu. Save snapshot R hoàn tất khi đã có R+1 không được đánh dấu R+1 là sạch.

Chế độ `PdfPrintLocked` giữ PDF nguồn để xem/in và chặn command sửa nội dung/Undo/Redo.
Việc mở/thay document là workflow lifecycle riêng. Cache trang PDF không phải vector editable.

## 3. IDs và mô hình hiện hành

`ObjectId` là uint64, 0 không hợp lệ. Allocator dùng counter chung trong process; nhập/mở file
phải reserve các ID đã có, không tái sử dụng hoặc wrap. Subpath/Node/Segment có typed uint64 IDs.
Move/style/reorder giữ ID; Undo/Redo khôi phục đúng identity. Source ID của importer chỉ là metadata,
không được tự trở thành ID đáng tin của document.

`ObjectVariant` hiện gồm Path, Text, Group và Image. Group có child IDs và optional geometric clip.
Document có nhiều trang/artboard, layer và assets. Một số feature như dimension/contour hiện dùng
path feature specifications; không suy ra có variant riêng chỉ vì tồn tại kiểu/helper tương ứng.

Path có `Path2D path` và optional `shared_ptr<const EditablePath> authoring_source`.
Khi source tồn tại, sửa source rồi tái tạo adapter bằng `set_authoring_source`; không sửa hai bản
độc lập. Nhánh chưa có source phải migrate/validate trước thao tác node. Tagged union tổng quát
`EditablePath | ParametricShape | ConstructionGeometry` là hướng thiết kế, chưa phải API hiện tại.

## 4. Command, transaction và history

API thực tế:

```cpp
DocumentCommand::execute(Document&) -> CommandResult
DocumentCommand::undo(Document&) -> CommandResult
Document::execute_command(CommandPtr) -> CommandResult
Document::undo() -> bool
Document::redo() -> bool
```

`CommandResult` phân biệt success, failure và no-change; có thể kèm summary của command màu/TAC.
Document giữ state trước prepare, khôi phục khi lỗi/exception/no-change, chỉ publish snapshot
sau khi command thành công. `CompositeCommand` gom edit thành một Undo unit. Command không được
gây side effect ngoài document rồi dựa vào rollback snapshot để hoàn tác filesystem/network.

History hiện giới hạn **số bước**, mặc định 100; command mới xóa redo branch. Chưa có byte budget
cho history/asset giữ lại. Giới hạn RAM theo bytes, gộp edit liên tục và đo retained assets theo
[plan hiệu năng](../docs/roadmap/performance-storage-automation.md).

## 5. Async proposal và kiểm tra stale

`AsyncProposal` gồm metadata, description và danh sách `CommandPtr`. Metadata hiện có:
`document_id`, `epoch`, `base_revision`, `job_id`, `job_generation`, `target_object_ids`,
`target_page_id`, `page_navigation_generation`.

`validate_and_commit_proposal` kiểm writer/read-only, document ID/epoch, strict base revision,
trang đích còn tồn tại, job còn active đúng generation, target objects còn trong map hiện hành
và command khác null. Lệnh được gom thành composite. Proposal rỗng có thể được nhận mà không tạo
revision. `replace_from_proposal` kiểm lifecycle/revision/job rồi reset document; đây là thay
lifecycle, không phải một edit thông thường.

**Giới hạn hiện tại:** metadata có navigation generation nhưng core validator chưa so sánh nó;
target-object lookup dùng map của trang active. Caller phải kiểm context/trang tương ứng và
đóng gói command có scope rõ; chưa có bảo vệ đầy đủ mọi race chuyển trang ở core validator.
Không tự rebase/merge proposal stale. Job bị cancel/close không có quyền commit muộn.

## 6. Draft và yêu cầu cho feature mới

Pointer previews/draft thuộc interaction state. Finish áp command nguyên tử; cancel không ghi
geometry/history. Tool phải giữ document ID/epoch/base revision và generation cần thiết, chặn
commit khi context đổi. Pan/zoom không được sửa tọa độ source đã xác nhận. Kiểm chứng lifecycle
qua controller và đường UI thực tế; hợp đồng không xác nhận mọi tool đã có draft recovery,
local Undo hoặc đầy đủ mọi modifier.

## 7. Nguồn để cập nhật và phạm vi kiểm chứng

- [Document API](../core/document/include/pikdraw/document/document.hpp), [writer/publication](../core/document/src/document.cpp).
- [CommandResult](../core/document/include/pikdraw/document/commands/command.hpp), [history](../core/document/include/pikdraw/document/history/undo_history.hpp).
- [Object model](../core/document/include/pikdraw/document/model/object.hpp), [ID allocator](../core/document/include/pikdraw/document/model/object_id.hpp).
- [Proposal metadata](../core/document/include/pikdraw/document/async/async_proposal.hpp), [validator](../core/document/src/async/proposal_validator.cpp).
- Regression liên quan: [snapshot](../core/document/tests/snapshots/test_snapshots.cpp), [stale proposal](../core/document/tests/async/test_stale_proposals.cpp).

Các link test chỉ vị trí coverage. Lượt đồng bộ tài liệu này không chạy lại native test và không
xác nhận FPS, peak RAM hoặc toàn bộ lifecycle UI.
