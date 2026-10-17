# C10 — Agent automation service

Version: `0.2.0 Implementation in progress`, 17/10/2026. The source now includes the
typed automation service, authenticated desktop pipe, MCP stdio sidecar, opt-in Settings
grant/revoke, and per-user COM local-server adapter. The contract records the target API
and safety semantics; features absent from the current MCP tool list remain proposed.
See [current MCP capabilities](../apps/mcp-server/README.md) and the
[implementation roadmap](../docs/roadmap/ai-agent-mcp-com-20261017.md). Full end-to-end and
release acceptance remain open.
Consumers: desktop composition, MCP sidecar, optional Windows COM adapter, integration tests.
Depends on [C2](c2-document.md), [C5](c5-storage.md), [C6](c6-workers.md),
[C7](c7-actions-ui.md), [C8](c8-import-fidelity.md), [C9](c9-appearance-effects.md).
Decision: [ADR 0010](../docs/decisions/0010-agent-automation-gateway.md).

## 1. Service và phiên làm việc của ứng dụng

`instance_id` nhận diện một process editor cụ thể; không tự chọn cửa sổ foreground.
`session_handle` là handle **cấp ứng dụng PikDraw**, được trả bởi `pikdraw.connect`,
gắn principal, instance, granted scopes, expiry và policy generation. Nó không phải
MCP protocol session và không phải bằng chứng ủy quyền độc lập. Host kiểm caller/token
và quyền trên mọi call. Không suy danh tính từ `clientInfo` hoặc PID client tự khai.

`connect` mặc định attach instance đã chọn, không reset tài liệu. Khi chưa có instance,
trả `APP_NOT_RUNNING`; chỉ launch bằng tùy chọn explicit và tạo tài liệu riêng khi được cấp.
Nhiều instance trả danh sách có tên đã lọc thông tin nhạy cảm để chọn. Handle không
chuyển sang document/instance khác. Restart process hết hiệu lực mọi handle cũ.

## 2. Mô hình lời gọi

DTO chung cho document write (shape chuẩn đề xuất, chưa phải JSON Schema đã khóa):

```json
{
  "api_version": "0.1",
  "session_handle": "ses_A",
  "document_id": "doc_A",
  "document_epoch": "7",
  "expected_revision": "42",
  "request_id": "req_A",
  "idempotency_key": "batch_A",
  "deadline_ms": 10000,
  "operations": [
    {
      "op": "object.create_rectangle",
      "client_ref": "panel",
      "page_id": "page_A",
      "layer_id": "layer_A",
      "x_mm": 10,
      "y_mm": 10,
      "width_mm": 600,
      "height_mm": 300,
      "fill": {"model": "rgb", "channels": [0.1, 0.2, 0.8], "alpha": 1}
    }
  ]
}
```

IDs/uint64 revisions/epochs là chuỗi opaque trên wire, tránh làm tròn qua JSON number.
Geometry dùng finite binary64, mm trong document space theo C1: origin góc dưới trái
trang, +Y lên, góc dương ngược chiều kim đồng hồ từ +X; transform parent × child.
Schema lặp tường minh các convention này. Text size dùng pt tường minh;
không có DPI/camera-dependent coordinates. Không đổi chuẩn tọa độ core trong C10.
Source color model/channels/profile và Unicode nguyên nghĩa theo C4; không ép CMYK
qua hex RGB. Text trả resolved font identity, fallback/missing glyph và layout diagnostics.

Mỗi operation có schema, defaults, required capabilities, allowed target types,
range/units, read/write scope, cost class, undo semantics và error codes. Reject unknown
field/op, NaN/Infinity, reference cycle và mixed unsupported target; không silent skip.
`client_ref` chỉ trong batch, host cấp ID thật khi commit và trả mapping. Cross-batch dùng ID thật.

## 3. Atomicity, concurrency và retry

- Validate toàn batch trên candidate snapshot, prepare ngoài writer khi tốn kém,
  kiểm lại document/epoch/revision/job generation + policy generation trong writer queue.
  Một commit thành công tăng revision một lần và tạo một Undo unit. Lỗi không đổi
  root, history, dirty state, UI selection hoặc phát success event.
- Không giữ transaction mở giữa nhiều round trip. `dry_run` trả diagnostics, cost,
  predicted bounds và base identity; không giữ khóa hay cấp revision/ID thật.
  Khi apply vẫn validate lại, không coi dry run là reservation.
- Mọi write có explicit targets; không dựa selection chung hoặc active tool. Adapter
  không tạm đổi global selection rồi gọi các hàm `*_selected` nhiều lần. UI có thể
  gọi cùng command builder với selection đã resolve thành IDs.
- UI draft/modal đang hoạt động: read committed snapshot được phép; write trả
  `BUSY_INTERACTION` cho bản đầu. Không tự finish/cancel draft của người dùng.
  UI edit sau khi agent đọc làm apply trả `REVISION_CONFLICT`, không implicit rebase.
- Undo/redo là write có expected revision, tăng revision đơn điệu. Bản đầu agent chỉ
  undo transaction mới nhất nếu receipt khớp đỉnh history và thuộc phiên được cấp;
  không vô tình undo thao tác xen giữa của người dùng. Redo có điều kiện tương tự.
- Dedup key scoped theo principal + instance + document epoch; payload hash khác cùng
  key trả `IDEMPOTENCY_CONFLICT`. Receipt được công bố nguyên tử cùng commit trước
  trả response; retry trả cùng IDs/revision, không lặp write. Retention/budget được
  công bố; hết window trả `IDEMPOTENCY_EXPIRED`, không tự coi là request mới.
- `request.get` tra receipt/job. Disconnect/timeout không chứng minh write chưa commit.
  Cancel trước commit thì bỏ candidate; sau commit trả `ALREADY_COMMITTED` + receipt.
  Restart làm epoch/instance cũ invalid; client kiểm file/snapshot, không tự retry write
  trên instance mới. Không hứa exactly-once xuyên crash/restart khi chưa có durable journal.

## 4. Queries, jobs và bằng chứng

`document.inspect` trả document/page/layer IDs, revision, units, object counts, dirty,
history head receipt và capabilities. `objects.query/get` phân trang theo immutable
snapshot handle gắn epoch/revision; hết hạn trả `SNAPSHOT_EXPIRED`, không trộn hai revision.
Fields gồm type/hierarchy/z-order, local/world transform, geometric/paint bounds tách biệt,
editable geometry, text/font, source colors/effects/provenance và lock/visibility.
Filter theo type/layer/region/tags có budget; không chỉ trả tọa độ bounding box.

`document.validate` chạy checker đã đăng ký, trả pass/fail/unsupported/not_assessed theo
check ID/version, target IDs, actual/expected/tolerance và revision/hash. Missing checker
không được thành pass. Test oracle phải có phép đo/expected độc lập với code được test.
`render.inspect` tùy chọn đọc renderer buffer/diagnostics từ snapshot đã chốt, không chụp
cửa sổ. Pixel hash chỉ tái lập trong cùng font/profile/renderer build; không là chuẩn
thẩm mỹ, production hoặc fidelity tổng quát.

Job dài trả application `job_handle`, status/progress/diagnostics và base identity;
`job.get/cancel` là tools thường, không phụ thuộc extension task của MCP. Worker proposal
phải qua C6/C8; writer kiểm generation tại commit. Progress có thể coalesce, receipt
không bị drop. Event có sequence, revision và cursor; mất đoạn buộc resync snapshot.
Bản đầu poll là đủ, subscriptions chỉ khi binding/client hỗ trợ và có test tương thích.

## 5. Kết quả và lỗi

Domain result dùng `structuredContent` có `api_version`, `status`, `request_id`,
document identity, `revision_before/after`, `transaction_id`, created/changed/deleted IDs,
client-ref mapping, diagnostics, `content_hash` với canonicalization version khi có.
MCP wrapper tuân revision protocol đã pin; domain error có `isError: true` và typed code.
Malformed JSON-RPC/method là protocol error, khác domain failure của tool hợp lệ.

Các code tối thiểu: `UNAUTHORIZED`, `SCOPE_DENIED`, `SESSION_EXPIRED`, `APP_NOT_RUNNING`,
`INSTANCE_AMBIGUOUS`, `DOCUMENT_CLOSED`, `EPOCH_MISMATCH`, `REVISION_CONFLICT`,
`BUSY_INTERACTION`, `INVALID_ARGUMENT`, `UNSUPPORTED_CAPABILITY`, `TARGET_NOT_FOUND`,
`TARGET_LOCKED`, `RESOURCE_LIMIT`, `FIDELITY_BLOCKED`, `PATH_DENIED`, `CANCELLED`,
`ALREADY_COMMITTED`, `IDEMPOTENCY_CONFLICT`, `IDEMPOTENCY_EXPIRED`, `SNAPSHOT_EXPIRED`.
Diagnostic có path tới operation/field, lý do và retryability; không đổ private file/text/token.

## 6. Quyền và IO

Scopes: `document.read`, `document.create`, `document.edit`, `history.edit`, `file.read`,
`file.write`, `render.read`, `validation.run`. Grant do host quản lý và có thể revoke;
annotations MCP chỉ mô tả, không cấp quyền. Session read-only không thể batch/save/undo.
Được cấp edit thì không hỏi lại từng rectangle. New/open/close tài liệu dirty cần lựa
chọn save/keep/cancel cụ thể hoặc tài liệu automation riêng; không discard mặc định.

IO dùng file/asset handles và thư mục cấp phép. Host resolve final target, kiểm traversal,
symlink/junction/reparse point và TOCTOU bằng handle policy. Save/export gắn snapshot revision,
ghi temp + atomic replace trong cùng volume; overwrite cần grant cụ thể. Save revision R
không đánh dấu saved revision R+1. Mạng, shell, arbitrary script/eval, macro và device output
không phải scope của bản đầu. Tên/text/metadata tài liệu là dữ liệu không tin cậy, không
là instruction để host/agent thực thi. Import/export tuân C8/C9, không cấp capability mới.

## 7. Transport và COM

Target MCP `2026-07-28` theo tài liệu chính thức ngày khảo sát; pin SDK sau spike
kiểm client thực. Không áp handshake `initialize` đời cũ cho binding mới; nếu hỗ trợ
`2025-11-25`, đặt compatibility adapter riêng và test cả hai. API version của PikDraw
độc lập revision MCP. Domain handles luôn explicit dù binding có hay không có session.
stdout chỉ MCP frames; logs đã redaction đi stderr. Không đọc credential môi trường ngoài nhu cầu.

Pipe nội bộ versioned length-prefixed UTF-8 frames, quotas trước allocation, ACL theo user/logon,
deny remote clients, xác minh peer và token capability ngắn hạn qua kênh bootstrap được bảo vệ.
Không tái dùng nguyên pipe worker dành cho parser. Discovery không chứa bearer secret;
không endpoint toàn máy không xác thực. Lỗi bridge không được bypass service validation.

COM local server hiện thực là `PikDraw.Automation.1`, CLSID
`{9E3F8D47-F078-43B4-9B61-5C188B6243A1}`, với ProgID version-independent
`PikDraw.Automation`. `IDispatch` late-binding có `GetInstances()` (DISPID 1),
`GetTools()` (DISPID 2), và `CallTool(name, argumentsJson)` (DISPID 3). JSON trả trong
BSTR UTF-16: discovery trả structured result, tool list trả definitions, còn `CallTool`
trả MCP tool result đầy đủ gồm `structuredContent`, `content` và `isError`. Tên, schema,
scopes và domain errors giống `apps/mcp-server`; tool không có handler không xuất hiện.
Không có type library, event callback, shell/eval hay arbitrary dispatch.

COM đăng ký explicit qua `/RegServer` và `/UnregServer` trong
`HKCU\\Software\\Classes`; `LocalServer32` chạy executable với `-Embedding`. Local server
chỉ đăng ký class trong process, đặt COM access DACL cho SID user hiện tại, packet privacy,
local execution/activation. Client STA được marshal tới local-server MTA; mỗi object khóa
tuần tự `McpServer` state để bảo toàn session. Adapter không gọi callback vào client trong
lúc writer commit. Call trả HRESULT cho lỗi dispatch/transport; lỗi tool hợp lệ giữ domain
result theo MCP JSON. Không tạo AppID/DCOM. Named pipe của desktop vẫn xác minh cùng user,
session scope, revision và capability như MCP; COM không phải đường vượt quyền.

Build/test đã được nối dưới `apps/com-server`; dispatch unit test không chạm registry.
Independent STA và MTA client activation smoke qua per-user registration đã pass; test gỡ
các ProgID/CLSID keys và chờ local server tự thoát. Chưa claim full COM parity, installer,
policy enterprise Registry, 32-bit client hoặc app-side revoke cho tới khi các gate đó có
evidence riêng.
