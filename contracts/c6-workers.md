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

## 6. IPC v2 — đề xuất source capability và bounded proposal stream

Đây là **profile Proposed**; implementation spike hiện có một worker v2 và adapter SVG giới hạn,
nhưng host chưa semantic-validate toàn bộ record để tạo document transaction. Không phát request
v2 tới worker v1. Negotiation xác nhận major version và danh sách message/features trước khi mở
file; không downgrade request có source sang v1 raw bytes. Worker/host mismatch kết thúc job bằng
unsupported_protocol, không gọi parser trong host.

### 6.1 Nguồn đọc chỉ qua capability host

1. Host kiểm tra file đã được người dùng chọn: loại file thường, không theo reparse point khi
   broker mở nguồn, có size hữu hạn trong profile và disk budget còn đủ. Không để worker tự mở
   user path, UNC, archive member path, font/profile folder hoặc resource URI.
2. I/O executor host sao chép snapshot nguồn vào thư mục riêng của job với tên ngẫu nhiên; mở
   nguồn read-only/share mode ngăn writer trong lúc copy; tính SHA-256 trên bytes đã copy và xác
   minh size. Staging có quota, ACL tối thiểu, cleanup khi mọi kết thúc, và không chứa linked
   assets ngoài capability được cấp riêng.
3. Launcher truyền đúng một handle read-only tới snapshot bằng explicit inherited-handle list
   cùng ba handle I/O cố định: stdin pipe đọc, stdout pipe ghi và stderr sink tới `NUL`. Vì vậy
   allowlist có đúng bốn handle trong profile có source; không có source thì đúng ba. Command line
   chỉ được mang giá trị handle do host tạo; tuyệt đối không mang đường dẫn nguồn. Handle được bind
   vào process/job ID, source token ngẫu nhiên 128-bit, size, hash, document epoch/revision và
   generation. Worker không thể xin handle/path khác. Không đưa handle qua payload có thể chọn tùy ý
   từ worker.
4. Host/worker phải có live AppContainer positive/negative canaries chứng minh worker đọc được
   handle nguồn, không ghi được, không mở file khác, không dùng handle/pipe của process khác và
   bị chặn network/child. Nếu handle inheritance không hoạt động trong profile OS, job fail
   closed; không mở ACL rộng hoặc fallback sang user path.

### 6.2 Framing, proposal stream và limits

- Header PIKC 16-byte phải validate magic, negotiated major, message type, sequence và byte length.
  Payload v2 bắt đầu bằng identity prefix cố định 24-byte: source token 128-bit và generation
  64-bit. Host kiểm prefix này trước khi cấp buffer phần payload còn lại; negotiation dùng prefix
  zero. Payload có thêm document ID, epoch, base revision, token và generation đầy đủ để host so
  khớp identity sau khi đọc phần bounded còn lại. Proposal frames có payload tối đa 1 MiB; producer
  chỉ có tối đa hai frame đang chờ để tạo backpressure.
- ImportBegin xác nhận token/hash/size/format profile; worker hash lại chính handle nguồn trước khi
  gọi adapter. ProposalChunk chứa chunk ordinal, first-record ordinal và các record có header
  canonical `type:u16, flags:u16=0, length:u32, bytes`; mọi record có type khác zero, length tối đa
  512 KiB và tổng chunk tối đa 1 MiB. CRC32/IEEE tính trên chuỗi canonical records. ImportEnd mang
  tổng chunks/records/canonical bytes, cờ fidelity report đầy đủ và SHA-256 của phép nối canonical
  records theo thứ tự. Host kiểm record framing, CRC, ordinal, tổng count/byte và SHA trước khi chấp
  nhận terminal frame. Sequence/message sai, token hoặc job identity khác, trailing bytes,
  checksum/hash/count không khớp, record vượt range hoặc EOF giữa frame làm hủy toàn proposal.
  Final hash không thay thế semantic validation từng record host-side.
- Record ID và payload được version hóa tại
  [`import-record-v1.json`](../docs/quality/reference-fixtures/import-record-v1.json): `1=Page`,
  `2=Path`, `3=Text`, `4=FidelityReport`, `5=FidelityIssue`. Số nguyên, độ dài và IEEE-754 binary64
  dùng little-endian. Page, endpoint/control points, arc radii, stroke width và vị trí text dùng
  mm; font size dùng pt. Record ID của object chỉ ổn định trong một proposal; `source_id` giữ nguồn
  ổn định nếu có. Path verbs là `Move=1`, `Line=2`, `Quad=3`, `Cubic=4`, `Arc=5`, `Close=6`;
  trường không dùng phải zero. Text và tên font là UTF-8. Màu giữ model RGB/CMYK/Spot/Gray,
  alpha, spot name, alternate RGB và cờ overprint; host không đổi màu nguồn sang RGB âm thầm.
  FidelityReport tóm tắt coverage, appearance, editability, production assessment và tổng issue;
  host kiểm từng record, thứ tự, số issue và report trước khi nhận proposal.
- Record schema v2 tại [`import-record-v2.json`](../docs/quality/reference-fixtures/import-record-v2.json)
  giữ nguyên payload v1 cho Page/Path/Text/FidelityReport/FidelityIssue và thêm `6=Layer`, `7=Group`.
  Layer trỏ tới các object gốc; Group trỏ tới các object con. Host kiểm thứ tự, ID, UTF-8/source ID,
  mọi tham chiếu, đúng một owner cho mỗi object, đồ thị không chu trình, issue locator và report
  trước khi chấp nhận terminal frame. C6 host chỉ bật `format_id=SVG`, `profile_id=svg-hierarchy-v2`;
  profile cũ `svg-flat-v1` không được dispatch.
- **SVG profile `svg-hierarchy-v2`:** nguồn UTF-8 tĩnh tối đa 16 MiB, root có width/height rõ đơn vị,
  hỗ trợ `rect`, `path`, `text`, `<g>` lồng nhau và layer Inkscape ở cấp root qua namespace chính thức.
  Group giữ thứ tự/con trỏ con; layer giữ tên, source ID, visibility/lock hiện tại và danh sách object
  gốc. SVG không khai báo layer được tạo một layer mặc định. `viewBox` chỉ dùng mapping xMidYMid meet
  mặc định hoặc none đồng tỷ lệ. Path commands nhận M/L/H/V/C/S/Q/T/Z. Text giữ Unicode, family/weight/
  style/cỡ chữ; việc tìm font và glyph hiển thị chưa được xác minh. Profile từ chối transform, CSS/style,
  filter/effect, mask, image/link ngoài, script, DTD/entity, layer lồng nhau, arc command và phần tử lạ.
  Từ chối toàn nguồn bằng structured worker error, không phát chunk dở dang. Giới hạn: 10.000 phần tử
  gồm group, depth 128, 6.000 verb/path, 100.000 path elements cộng dồn, 64 thuộc tính/tag, 256 KiB
  text/object, 16 MiB canonical stream và 1.000.000 mm mỗi chiều trang. Đây là phạm vi subset, chưa
  phải claim hỗ trợ SVG tổng quát.
- Host đã kiểm semantic hierarchy cho SVG profile, nhưng document transaction/consumer apply và review
  tích hợp chưa hoàn tất. Vì vậy C6 vẫn Proposed; parser test pass không chứng minh import vào document,
  fidelity đầy đủ, lưu/mở document hoặc an toàn OS trên mọi máy.
- Host ghi chunks vào job staging riêng thay vì giữ toàn scene trong RAM, nhưng validate record
  ngay khi đọc. Chỉ một reader/writer có quyền consume stream; queue count/bytes và disk usage có
  trần. Hết quota, thiếu quyền hoặc staging I/O lỗi kết thúc job; không bỏ chunk hoặc giảm chất
  lượng âm thầm.
- Per-frame 1 MiB và hai frame queue là transport defaults đề xuất. Max source bytes, proposal
  bytes, objects, hierarchy depth, path segments, text/glyphs, pixels/assets, staging disk, đồng
  thời jobs và deadline thuộc versioned import profile; phải đo bằng corpus hợp lệ/negative trước
  bật profile. Không dùng unlimited hoặc nâng quota vì một mẫu thất bại.
- Proposal là untrusted data theo [C8](c8-import-fidelity.md), không được mutate document. Host
  kiểm full stream, capability, provenance, loss completeness, units/precision, finite values,
  graph refs và toàn bộ limits trước khi tạo commit proposal. Sau đó document writer áp strict
  epoch/revision/generation và atomic transaction như C8.

### 6.3 Cancel, timeout, cleanup

Cancel tăng generation trước khi đóng pipes/terminating job; receiver ngừng nhận frames ngay. EOF,
crash, deadline, cancel, worker restart, document edit/close, hash mismatch hoặc lỗi dọn đều đánh
dấu proposal stale, terminate process/job khi cần, xóa source/output staging và giữ nguyên base
document/history/dirty state. Retry là job/token mới và có giới hạn; không resume một stream dở.
Supervisor có deadline khi drain pipes/cleanup để worker hỏng không treo host.

V2 không truyền raw pointer, renderer type, user path, arbitrary handle, object blob không giới
hạn hoặc source bytes lớn trong một frame. Response của adapter luôn là import proposal C8. Mọi
limit/profile ở mục này vẫn là đề xuất đến khi implement, build và chạy live OS canaries trên
exact merged revision.

## 7. Evidence để bật adapter

Theo từng worker/OS/build ghi exact hash, flags, toolchain/dependencies, limits và corpus ids.
Kiểm tra containment bằng path/file/network/handle/child positive+negative canaries; protocol
bằng truncated, oversized, duplicate/out-of-order, nonfinite, deep, stale, fuzz và stdout flood;
lifecycle bằng cancel/crash/timeout/close/restart; soak leak temp/files/handles/commit bytes.
Các wire-level vectors đã version hóa tại
[`worker-ipc-v2-vectors.json`](../docs/quality/reference-fixtures/worker-ipc-v2-vectors.json).
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
