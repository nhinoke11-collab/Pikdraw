# Contract C6: Worker lifecycle, containment và IPC

Version: 1.0.0 (wire compatibility)
Status: Locked for the current v1 wire; future file-handle profile is Proposed
Producer: `platform/workers`
Consumers: `modules/interop`, `modules/plugin_host`, `modules/desktop`

---

## 1. Phạm vi và bảo đảm

Parser và decoder bên thứ ba là đầu vào không tin cậy. Chúng chạy ở process worker ngoài
editor. Worker chỉ tạo kết quả đề xuất; host xác thực rồi writer mới được commit. Crash,
timeout, cancel, lỗi quota, response sai version hoặc payload sai không áp dụng một phần
kết quả vào document đang mở.

Process riêng không tự cô lập filesystem, network, handles, child processes, CPU hoặc RAM.
Mỗi bảo đảm cần OS policy và canary dương/âm đo được trên đúng worker build. Sandbox init lỗi
thì host fail closed; không tự khởi chạy adapter không cô lập.

## 2. Wire v1 đã có trong code

Header hiện hành là `IpcHeader`, packed 16 bytes, các trường little-endian trên Windows:

| Thứ tự | Kiểu | Ý nghĩa |
| --- | --- | --- |
| 0 | uint32 | Magic `0x50494B43` (bytes `43 4B 49 50`, `PIKC`) |
| 4 | uint16 | Wire version `1` |
| 6 | uint16 | Message type |
| 8 | uint32 | Sequence ID |
| 12 | uint32 | Payload bytes |

Message types hiện có: Ping/Pong 1/2, ImportRequest/ImportResponse 10/11,
CanaryRequest/CanaryResponse 20/21 và ErrorResponse 99. Sequence ID ghép request/response.
Packet gồm header + payload. C++ serializer/deserializer hiện không gắn CRC32 ở cuối.
CRC32 trong storage container không hàm ý CRC32 cho IPC. Host vẫn phải kiểm tra độ dài chính
xác, version, loại message, sequence, trường bắt buộc và toàn bộ payload trước khi dùng.

Giới hạn v1 trong code: payload tối đa 16 MiB; import request mang raw source bytes trong
payload; tối đa 100.000 objects; depth tối đa 64; timeout truyền vào launcher lớn hơn 0 và
không quá 60 giây. Files lớn hơn 16 MiB bị từ chối trước khi serialize. Không nâng trần v1
để xử lý file lớn.

Wire v1 không chứa source-file handle/token, chunk/backpressure, capability matrix đáng tin,
font/profile blob protocol, transaction report đầy đủ hay IPC cancel message đã triển khai.
Không ghép nhầm bảng PKWP/64 MiB/CRC32 trong bản đặc tả cũ với packet PIKC hiện tại.

## 3. Ranh giới AppContainer và file broker

- Tắt network capability; kiểm read, write, outbound, bind/listen và protocol handles bằng
  positive controls lẫn denial đúng OS. Một kết nối bị timeout không tính là denial.
- Kế hoạch cấp tối thiểu executable/runtime của worker và pipe handles trong allowlist.
  Không tin đường dẫn, tên file hoặc handle do payload worker chọn.
- Wire v1 nhận bytes qua IPC; worker không tự mở đường dẫn user. V1 chưa hỗ trợ stream file lớn.
- Import file lớn cần protocol revision được chấp thuận. Host broker sao chép bytes bất biến
  vào vùng staging/job riêng sau khi kiểm tra path/handle, size, snapshot/hash và quota disk.
  Worker chỉ được đọc đúng nguồn/job qua capability tạm thời do host cấp; không được duyệt
  thư mục user, link UNC, registry, font folder hay linked asset tự do.
- Broker chỉ giải quyết nguồn được chọn. Font, ICC, linked images và resource URI đều cần
  capability riêng; cấm worker tự tải mạng hoặc chạy script/macro từ file nguồn.
- Dọn vùng staging theo lifetime job dù job hoàn thành, timeout, worker crash, app đóng hoặc
  host restart. Log không chứa nội dung file, data URL, full document path hay font bytes.

Vùng private bin, path-specific file access và pipe handle inheritance cần được chứng minh
riêng trên worker process đang chạy. Canary C6 cho network/file/handle/child chỉ chứng minh
đúng canary, policy và build đã chạy; không tự mở quyền import cho mọi parser.

## 4. Job Object, RAM, CPU

Code `JobLimitsConfig` đang mặc định 256 MiB, active process 1, `KILL_ON_JOB_CLOSE` và đặt
`JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY`. Trên Windows các memory
limits này giới hạn **virtual memory commit**, không phải lượng RAM vật lý/working set. Quá
limit có thể làm allocation/commit thất bại; contract không suy ra rằng Windows luôn terminate
worker gọn gàng hoặc host chắc chắn sống. Host giám sát process, I/O, peak usage và exit state,
chủ động terminate job có deadline, rồi bỏ toàn bộ proposal lỗi.

Không được đặt quota 512 MiB làm số runtime chỉ vì nó xuất hiện trong bản đặc tả trước: code
mặc định 256 MiB, app tổng đang cần đo. Từng mức quota là profile versioned, benchmark trên máy
8 GB và được review trước đổi. Admission control tính staging+native allocations+decoded images+
font/profile+preview+undo/index toàn app, không chỉ commit memory trong child.

Code hiện chặn child process bằng `JOB_OBJECT_LIMIT_ACTIVE_PROCESS = 1` và giữ process trong
Job Object. Hàm JobObject đang khảo sát không đặt CPU hard cap hoặc process priority. “Below
normal”/CPU rate control chỉ là target nếu chưa đo code path và scheduler Windows cụ thể; UI
phải giữ input/render budget trước khi bật quota production.

## 5. Deadline, cancel, crash

Host đặt absolute monotonic deadline từ lúc admission. Thời gian I/O, write/read pipe, parse,
validation, staging và phần chuẩn bị tính về deadline thích hợp; heartbeat không gia hạn job.
Launcher v1 chặn timeout trên một phút; preview 5 s/full import 30 s mới là target khảo sát,
không phải latency đã đo và không phù hợp với mọi file.

Khi user cancel, timeout, worker crash, pipe đóng, response framing sai hoặc quota/canary fail:

1. đánh dấu generation đã hủy trước khi dọn;
2. terminate Job Object/process khi cần và đóng/drain cả hai pipe trong khoảng thời gian hữu hạn;
3. xóa staging output và bỏ proposal/preview thuộc generation đó;
4. giữ document source/revision/undo/dirty state nguyên trạng;
5. chỉ retry sau lỗi transient theo policy có giới hạn hoặc theo hành động rõ ràng của user,
   luôn dùng job ID/generation mới; không tự vòng lặp mở lại file lỗi.

Response đến muộn hoặc worker cũ được khởi động lại không có quyền commit. Host đối chiếu
document ID, lifecycle epoch, strict base revision, job ID và generation mới nhất ngay tại
atomic commit. Đóng/mở, undo, cancel hoặc sửa document làm proposal cũ stale.

## 6. Import file lớn — profile tương lai, chưa được wire v1 hỗ trợ

Nâng IPC lên wire major v2 hoặc một contract transport được review trước khi đổi shape. V2 phải
có version negotiation/fail-closed, source capability token khó đoán và gắn job, chunk offsets/
sequences, total byte limit, backpressure, checksum per chunk/final stream, bounded queue, staging
lifetime và fault tests. Không truyền raw pointer, renderer type, absolute user path hoặc một
blob nhiều GB không giới hạn. Chunk thiếu/trùng/sai thứ tự/hash/length, worker stale/cancel đều
phải bỏ toàn bộ staged document.

Max file bytes, workspace disk quota, count/depth/segments/glyphs/pixels và deadlines cần được
đo theo corpus hợp lệ; contract v2 không tự cấp quyền nâng quota. Response của adapter là import
proposal có [C8](c8-import-fidelity.md), không là document mutation.

## 7. Evidence để bật adapter

Theo từng worker/OS/build ghi exact hash, flags, toolchain/dependencies, limits và corpus ids.
Kiểm tra containment bằng path/file/network/handle/child positive+negative canaries; protocol
bằng truncated, oversized, duplicate/out-of-order, nonfinite, deep, stale, fuzz và stdout flood;
lifecycle bằng cancel/crash/timeout/close/restart; soak leak temp/files/handles/commit bytes.
Kiểm thử trên source user được chạy cục bộ; chỉ lưu corpus IDs/hash trong report có phân quyền,
không commit sample, tên khách hàng hoặc nội dung file.

Không công bố “không bao giờ crash/treo”. Điều kiện công bố: đường lỗi đã thử kết thúc có deadline,
không commit tài liệu dở dang và không giữ UI callback chờ parser; giới hạn cùng chính sách recovery
phải được nói rõ theo build/profile đã đo.

## 8. Nguồn code cần giữ đồng bộ

- `modules/interop/include/pikdraw/interop/ipc_dto.hpp`
- `modules/interop/src/ipc_dto.cpp`
- `modules/interop/include/pikdraw/interop/payload_validator.hpp`
- `modules/interop/src/payload_validator.cpp`
- `platform/workers/include/pikdraw/workers/job_object.hpp`
- `platform/workers/src/job_object.cpp`
- `platform/workers/src/process_launcher.cpp`

C6 là đặc tả cho consumer; code changes phải theo task module workers/interop riêng và review
chung trên merged SHA. Phần v2 chưa implement trong contract-only checkpoint này.
