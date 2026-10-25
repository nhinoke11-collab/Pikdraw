# Contract C6: Worker lifecycle, containment và IPC

Version: 1.1.0 (đồng bộ tài liệu; wire v1/v2 không đổi trong lượt này).
Audited: 2026-10-24, source đang làm việc.
Producer: `platform/workers`, `modules/interop`. Consumer: `apps/desktop` và host import.
Status: IPC v1 legacy và v2 source-handle/proposal-stream đã có; không còn là spike chỉ hỗ trợ SVG.

## 1. Ranh giới writer và process

Parser/decoder bên thứ ba chạy ngoài editor. Worker trả dữ liệu chưa tin cậy; host kiểm framing,
identity, hash, cấu trúc và semantics trước khi document writer áp command theo C2/C8.
Worker không được mutate document hoặc tự cấp capability. Crash/cancel/timeout/payload lỗi phải
bỏ candidate, không publish một phần tài liệu. Không fallback sang parser trong UI khi sandbox lỗi.

Process riêng không tự chứng minh cô lập filesystem/network/handles/CPU/RAM. AppContainer,
restricted handle inheritance và Job Object là các lớp triển khai; canary đúng OS/build là gate
riêng. Audit source này không chạy lại canary và không chứng nhận mọi adapter/OS đã an toàn.

## 2. Header chung và nhánh legacy v1

`IpcHeader` packed 16 bytes, scalar little-endian:

| Offset | Kiểu | Giá trị/ý nghĩa |
| --- | --- | --- |
| 0 | u32 | Magic `0x50494B43`, bytes `43 4B 49 50`; dùng giá trị số/byte làm authority, không suy từ comment ASCII. |
| 4 | u16 | `wire_version`: 1 hoặc 2 theo nhánh giao thức. |
| 6 | u16 | Message type. |
| 8 | u32 | Sequence ID. |
| 12 | u32 | Payload byte count. |

V1 còn serializer/launcher tương thích: Ping/Pong 1/2, ImportRequest/Response 10/11,
CanaryRequest/Response 20/21, ErrorResponse 99. Payload tối đa 16 MiB; request mang raw bytes.
Object/depth constants là 100.000/64. Nhánh `Transact` v1 nhận timeout >0 và <=60 giây.
Không có CRC trailer chung cho packet v1. Không lấy CRC của storage hoặc v2 chunk gán cho v1.
Không nâng trần v1 hoặc downgrade âm thầm request v2 để né lỗi source/capability.

## 3. V2 đang tích hợp

V2 giữ header 16 bytes, payload tối đa 1 MiB và identity prefix 24 bytes:
source token 128-bit + generation u64. Negotiation dùng zero identity. Full job identity gồm
document ID, epoch, base revision, generation, source token; host phải so khớp xuyên suốt job.

| Message | ID | Vai trò |
| --- | --- | --- |
| Negotiate request/response | 30/31 | Major/minor và required/accepted feature bits. |
| ImportBegin | 32 | Job identity, source size/SHA-256, format/profile, optional source page index. |
| ProposalChunk | 33 | Chunk/record ordinals, record count, CRC32 và canonical record bytes. |
| ImportEnd | 34 | Tổng chunk/record/bytes, canonical SHA-256, fidelity-report-complete. |
| ErrorResponse | 35 | Structured failure. |
| MemoryWait | 36 | Thông báo chờ RAM của worker; host kiểm identity và allowance. |

Feature bits hiện có: SOURCE_HANDLE, CHECKED_PROPOSAL_STREAM, SOURCE_PAGE_SELECTION,
RASTER_SOURCE_ASSETS. Caller/worker phải thống nhất features cần dùng trước import.
Transport có constant tối đa hai queued frames; không suy ra toàn import giữ đúng hai buffers.
Mỗi chunk tối đa 4096 records; record payload tối đa 512 KiB. Canonical record header gồm
`type:u16, reserved:u16=0, payload_bytes:u32`. Không được parse cast raw struct/pointer.

Record IDs hiện có trong source:

| IDs | Loại |
| --- | --- |
| 1–7 | Page, Path, Text, FidelityReport, FidelityIssue, Layer, Group. |
| 8–10 | RasterImage, RasterImageChunk, RasterSourceChunk. |
| 11–12 | RasterSourceChunk64, RasterImage64. |

Codec và host consumer có nhánh U64, nhưng desktop raster admission vẫn dùng
`kRasterRecordByteLimit = UINT32_MAX` cho nguồn; native storage còn bound riêng. Chưa được
công bố import/save ảnh >4 GiB chỉ vì có record 11/12. Giữ khả năng đọc record legacy; thay
layout/feature negotiation cần migration và review consumer, không tái dùng ID với nghĩa khác.

Host hiện validate chunk/end checksum, ordinals, count, graph/layer membership và record semantics.
Desktop materialize proposal thành object/page/asset rồi commit/review theo C8. SVG, DXF, raster,
PDF/AI PDF-compatible, CDR và EPS có adapter theo subset; không suy ra fidelity đầy đủ từ v2.

## 4. Nguồn chỉ đọc và resources

`SourceSnapshot` có hai đường: `Create` sao chép vào staging riêng và `PinReadOnly` giữ handle
nguồn, chặn concurrent write/delete opens, hash chính handle đó để tránh copy nguồn lần hai.
Không mô tả mọi import như luôn spool toàn file. Cả hai đường giữ size, SHA-256, token và lifetime
RAII. Host chọn nguồn, kiểm loại file/reparse/size và cấp handle chỉ đọc; worker không chọn user path.

Launcher dùng explicit inherited-handle list: stdin, stdout, stderr sink và source handle khi có.
Không truyền arbitrary handles/pointers hoặc đường dẫn nguồn do parser chỉ định. Font/ICC/linked
assets/URI ngoài nguồn cần capability riêng; worker không tự tải mạng, chạy macro hoặc mở thư mục
của người dùng. Nguồn/output tạm có owner và dọn theo lifetime job; log không chứa bytes/tokens
bí mật hoặc đường dẫn/nội dung khách hàng.

## 5. RAM, deadline và cancellation hiện tại

`JobLimitsConfig` mặc định 256 MiB, active process 1, kill-on-close và chặn process con.
Memory limit của Job Object là commit-memory limit, không phải working-set/RSS guarantee.
Giá trị `max_memory_bytes=0` tắt hard cap; các đường desktop import hiện dùng giá trị này.
Vì vậy không được ghi mọi import worker bị giới hạn cứng 256 MiB.

Import hiện dùng `SystemMemoryPressureWaiter`: headroom dưới ngưỡng 95% RAM hệ thống thì chờ,
tự tiếp tục khi đủ, allowance tối đa năm phút cộng dồn mỗi waiter và vẫn nhận cancel. Đây là
cooperative admission tại các điểm có probe, không phải reservation toàn app. Parser, host
materialization, full-frame pixels, source bytes, codec trung gian và Undo còn có thể cùng resident.
Không có bảo đảm mọi allocation trong thư viện đều qua waiter hoặc không thể OOM/UI stall.

Desktop hiện chọn active timeout 120 giây cho rich import và 30 giây cho nhánh còn lại;
thời gian chờ RAM có cơ chế điều chỉnh deadline riêng. Trần 60 giây chỉ thuộc nhánh v1 nêu trên.
Không lấy các giá trị này làm thời gian mở file đã đo. Job Object chưa tự tạo CPU hard-cap policy.

Cancel/timeout/crash/EOF/hash mismatch làm candidate bị loại; terminate job/đóng pipe và thu hồi
resources theo host. Retry phải có identity/generation mới. Tại commit vẫn kiểm C2 lifecycle,
revision và job generation; IPC identity không thay thế document guard.

## 6. Hướng cần triển khai và evidence

Admission theo retained bytes toàn app, asset/tile storage, bounded staging và queue/cancel xuyên
suốt codec/UI vẫn theo [plan hiệu năng](../docs/roadmap/performance-storage-automation.md).
Không đặt nhãn đã hoàn thành cho các mục này từ kiểm tra checksum hoặc pressure waiter.

Adapter release cần evidence riêng: malformed/truncated/oversize/deep/cyclic payload; stale/cancel/
crash/deadline; source/hash mismatch; leak handles/temp/commit bytes; OS positive/negative canaries;
và import-edit-Undo-save/reopen cùng fidelity oracle. Contract fixtures là vectors, không phải
kết quả đã chạy trên build hiện tại.

## 7. Nguồn authority

- [Wire DTO/constants](../modules/interop/include/pikdraw/interop/ipc_dto.hpp), [v2 codec](../modules/interop/src/ipc_v2_dto.cpp).
- [Record types/codecs API](../modules/interop/include/pikdraw/interop/adapters/svg_worker_adapter.hpp), [raster bound](../modules/interop/include/pikdraw/interop/adapters/raster_image_decoder.hpp).
- [Source snapshot](../platform/workers/include/pikdraw/workers/source_snapshot.hpp), [launcher](../platform/workers/src/process_launcher.cpp).
- [Stream validator](../platform/workers/src/v2_proposal_validator.cpp), [semantic validator](../platform/workers/src/v2_proposal_semantics.cpp).
- [Job/memory waiter](../platform/workers/include/pikdraw/workers/job_object.hpp), [desktop import](../apps/desktop/src/workflows/document_workflow_import.cpp).
- [Wire vectors](fixtures/worker-import-v2-vectors.json), [integrity regression](../platform/workers/tests/v2_integrity_tests.cpp).

Lượt này chỉ đồng bộ tài liệu với source, không thay protocol và không chạy native/canary tests.
