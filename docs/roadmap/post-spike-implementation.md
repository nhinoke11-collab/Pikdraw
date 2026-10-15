# Kế hoạch triển khai PikDraw sau ba spike

Ngày: 2026-10-15. Trạng thái: **đặc tả triển khai, chưa thực thi**. Native editor và native build pipeline chưa có. Ba spike đã có draft evidence; independent PS00 audit ghi A `partial`, B `partial`, C `fail`, nên foundation chưa được nhận và M1 vẫn đóng. Xem [báo cáo nghiệm thu](../reports/foundation/ps00-acceptance-20261015.md) và [baseline hiện tại](../quality/baseline.json). Việc viết kế hoạch không khởi chạy agent, không tạo task implementation hàng loạt và không thay thế nghiệm thu.

Đọc cùng [ba spike foundation](foundation-spikes.md), [catalog 26 task](post-spike-tasks.json), [document/runtime](../architecture/document-runtime.md), [quality gates](../quality/gates.md) và [giao thức agent](../agents/PROTOCOL.md).

## 1. Đích đến và điều kiện bắt đầu

Ưu tiên một editor dùng được xuyên suốt: **tạo/nhập → sửa vector và chữ → undo/redo → lưu native → mở lại/phục hồi → xuất một format đã kiểm chứng**. Sau đó hoàn thiện Vector Alpha, rồi In ấn/Thi công theo một phạm vi production hẹp. Bốn workspace dùng chung document; tab xuất hiện không có nghĩa workspace đã triển khai.

Đường đi dự kiến là C++20 + Qt Widgets + Skia. Chỉ khóa sau khi evidence A/B/C được nhận. Nếu spike bác bỏ stack, ps00 phải sửa catalog/ADR trước khi giao code phụ thuộc; không giữ Qt hoặc Skia vì kế hoạch đã viết.

| Điều kiện | Bằng chứng cần có | Khi thiếu |
| --- | --- | --- |
| Spike A | Backend/shell/present, GPU và software trên máy tham chiếu, precision render, cache/invalidation và device-loss theo gate | Sửa prototype hoặc đổi candidate; không mở rộng renderer |
| Spike B | Geometry oracle, transactions/snapshots/stale rejection, history, save/recovery và retention | Chặn document/storage/production implementation phụ thuộc |
| Spike C | Ma trận subset format, fidelity/editability/output, IPC validation, OS permission denial và licenses | Chặn tích hợp parser untrusted; trusted-only không được ghi là containment đã pass |
| Baseline được review | Commit tích hợp, toolchain/dependencies, fixture checksums/rights, budgets số cụ thể, raw measurements, reviewer | ps00 còn review/blocked, ps01 chưa ready |
| Scope đã đồng bộ | Worktrees/tasks cũ được kiểm kê, cùng contract revision, không có lease chồng lấn | Coordinator giải quyết trước dispatch |

ps00 có thể nhận reports chưa đầy đủ để rà soát và lập task sửa spike. Chỉ đánh dấu ps00 done khi tất cả gate bắt buộc đã được nhận. Kết quả `partial` hoặc `not_measured` của **gate bắt buộc** không được coi là pass. Một CDR version được phân loại unsupported có report là kết quả hợp lệ nếu ngoài subset được chọn; kế hoạch không yêu cầu đọc mọi CDR/AI trước khi đi tiếp.

Memory limits, regression tolerance, source coordinate range, storage durability và format subset phải có giá trị/phạm vi cụ thể trong baseline. Không điền số giả để mở task. Trường hợp cần đổi yêu cầu phải có quyết định ADR được review và một lượt đo theo gate mới.

## 2. Các mốc và tiêu chí đóng mốc

| Mốc | Task | Sản phẩm nghiệm thu | Điều kiện chuyển mốc |
| --- | --- | --- | --- |
| M0 — khóa nền tảng | ps00–ps02 | Evidence được nhận, contracts/vectors, build và gate tối thiểu | Dependency pin, ownership, consumer review và build workflow dùng được |
| M1 — editor xuyên suốt | ps03–ps13 | Shell thật, document/render/tools/storage/worker/interop ghép thành một app | Demo mục 6 đạt với services thật; không chỉ screenshot/mock |
| M2 — Vector Alpha | ps14–ps18 | UI/locale, vector tools, ảnh cơ bản, độ bền và gói Windows nội bộ | Full-app checks, 8 giờ replay, package/canary và review độc lập |
| M3 — Production Beta | ps19, ps19a, ps20–ps24 | Preflight, đường thi công, PDF/DXF theo preset, pilot xưởng | Oracle độc lập, migration, kích thước/màu/output và operator evidence |
| M4 — mở rộng có khảo sát riêng | Chưa dispatch trong catalog | 3D liên kết; sau đó mới plugin ngoài/CAM nếu có scope | Spike và kế hoạch riêng cho rủi ro mới |

Mốc là kết quả kiểm chứng, không phải số tuần đã trôi qua. Chỉ ước lượng lịch sau ps00 khi biết code spike tái sử dụng được bao nhiêu, máy và công cụ nào sẵn có. Task lớn nên chia thành checkpoint/commit review được; nếu cần chia task thực tế, coordinator ghi mapping về ID catalog và giữ nguyên gate cuối.

Không viết lại prototype đã đạt chỉ để đổi bố cục. Mỗi task nhận phần code phù hợp từ spike, làm sạch boundary, bổ sung lỗi/lifecycle/tests còn thiếu. Prototype đo nhanh không tự được nâng thành production code.

## 3. Backlog và cách đọc catalog

`post-spike-tasks.json` là nguồn chuẩn cho **ID, dependencies, owned_paths, deliverables, checks, contracts và reviewers**. Mỗi task áp dụng thêm Definition of Done tại mục 7. Đường dẫn chưa tồn tại là vị trí dự kiến cần tạo, không phải tính năng đã có. `depends_on` là quan hệ điều phối task; dependency mã nguồn vẫn theo `registry/modules.json`.

Bảng dưới là chỉ mục cho người đọc; JSON quyết định thứ tự nếu có khác biệt và coordinator phải sửa bất nhất trước dispatch.

| ID | Công việc cụ thể | Kết quả chính |
| --- | --- | --- |
| ps00-accept-spikes | Nghiệm thu A/B/C, chọn stack/budgets/coverage | ADR và baseline có evidence, audit các task/worktree cũ |
| ps01-contract-pack | Khóa semantics và vectors dùng chung | Contracts C1–C7, consumer review và oracle chuẩn |
| ps02-build-gates | CMake/Ninja, dependency pin, CI/gate, dev runner | Build tăng dần và kiểm tra kiến trúc có negative tests |
| ps03-geometry | Double mm, curves, transforms, Boolean/offset subset | Geometry API với bounds/error budgets/topology diagnostics |
| ps04-color | RGB/CMYK/spot/ICC/overprint và preview transform | Giữ màu nguồn, lỗi profile có báo cáo |
| ps05-native-preview | Qt shell, forms/QSS, Reload UI | UI preview dùng lại cho app, chưa gắn service thì ghi mock rõ |
| ps06-typography | Unicode, shaping, caret, font identity/fallback | Chữ tiếng Việt chỉnh sửa được; source khác outline/cache |
| ps07-document | Single writer, transactions, history, snapshots/index | Stable IDs, atomic commits, stale rejection và retention |
| ps08-storage | Native format, migration, autosave/recovery | Save đúng revision, fault injection, không mất source data |
| ps09-renderer | Snapshot facade, culling/cache, GPU/software | Renderer đo trên workload thật, không sửa source vì LOD |
| ps10-vector-basics | Select/shapes/text/transform/style/delete | Commands và gesture preview có undo/cancel |
| ps11-interop-core | Import subset, codec worker-side, output đầu tiên | Fidelity report, host validation và parser isolation boundary |
| ps12-worker-host | Launch/IPC/OS containment/quotas/cancel | Worker lỗi không làm hỏng host/document |
| ps13-vertical-slice | Bind services vào shell, E2E | Editor mở/sửa/lưu/phục hồi/xuất dùng được |
| ps14-desktop-workflows | Layers/inspector/settings/en/vi/keyboard | Workflow nhất quán, localization cũ được tiếp nhận an toàn |
| ps15-vector-editing | Pen/nodes/snap/group/align/Boolean | Chỉnh tiếp được source, không full CAD/CAM |
| ps16-raster-basics | Place/crop/opacity/relink ảnh | Nguồn ảnh/profile/DPI giữ nguyên; decoder qua worker |
| ps17-vector-alpha-integration | Ghép tools, stress/soak/regressions | Vector Alpha candidate có report từng máy và known limits |
| ps18-windows-package | Gói Windows, clean-machine checks | Internal Alpha accepted; chưa phát hành công khai |
| ps19-production-contracts | Chốt print/DXF preset, C8 và pilot corpus | Consumer impact, migration và output oracle trước code |
| ps19a-production-document | Bổ sung production metadata/commands/storage | File Alpha mở/sửa/lưu lại không mất dữ liệu |
| ps20-preflight | Kiểm khổ/tỷ lệ/DPI/font/profile/features | Lỗi gắn object và revision, export eligibility rõ |
| ps21-manufacturing | Cut/engrave/drill, dimensions, contour/offset | Source-linked output có tolerance, stale/cancel và validation |
| ps22-production-formats | PDF và DXF writer theo subset | Output oracle khác implementation, revision/preset report |
| ps23-production-workspaces | Bind In ấn/Thi công vào document chung | Preflight → sửa → recheck → xuất, shared history |
| ps24-production-beta | Pilot xưởng và nghiệm thu | Actual measurements, operator review, release scope đúng bằng chứng |

### Các hợp đồng phải khóa trước song song

| Contract | Semantics tối thiểu | Producer/consumer cần review |
| --- | --- | --- |
| C1 — geometry | mm/binary64, axes/handedness, transform order, curve/fill/winding, finite/range validation, error budget/topology failure | geometry, document, typography, vector, rendering, interop |
| C2 — document/commands | IDs/object versions, monotonic revision, epoch/job generation, atomic error/undo, snapshot/index lifetime, single-writer commit | document và mọi module đọc/ghi |
| C3 — view/render | Camera-relative conversion, viewport/DPI, snapshot revision, invalidation bounds, render resource ownership, pick/selection result | document, rendering, desktop |
| C4 — text/color/assets | Unicode/font identity/shaping map, RGB/CMYK/spot/ICC/overprint, original asset/provenance, missing resource policy | typography, color, document, rendering, interop, raster |
| C5 — storage | Schema version, limits/checksum, unknown field policy, migration, dirty revision, checkpoint/durability | document, interop, desktop |
| C6 — worker/conversion | Version negotiation, counts/bytes/depth/time budgets, handle grants, cancel/generation, proposed changes, loss report/errors | interop, workers, document, desktop |
| C7 — actions/UI/settings | Stable command/action IDs, capabilities/selection kinds, progress/cancel, workspace/view state, settings version, locale keys | vector/raster, desktop; production sau C8 |
| C8 — production | Preset/units/error budgets, source-linked derived result, source revision, cut classification, preflight severity, export projection | document, production, interop, desktop |

Contracts không chỉ là JSON shape. Ví dụ: cùng matrix sáu số nhưng nhân transform ngược thứ tự vẫn là breaking semantics. Có positive/negative vectors và expected result độc lập. Public C++ APIs thuộc module cung cấp; spec/fixture ở `contracts/` và `docs/quality/` không biến thành một runtime module nối vòng toàn hệ thống. Không lưu `SkPath`, `QObject*`, GPU handle hoặc ABI layout vào file/IPC.

Module implementation có thể thêm private helper và test trong scope. Đổi API/semantics dùng chung, dependency pin hoặc oracle phải qua task contract/gate riêng trước khi consumer tiếp tục. Module producer không tự sửa expected results của consumer để làm xanh kiểm thử.

### Luồng phụ thuộc mã nguồn cần giữ

- Desktop điều phối document, rendering, vector/raster/production và worker host.
- Rendering đọc document/snapshots; document không gọi renderer hoặc desktop.
- Worker host phụ thuộc converter interface; interop không gọi ngược worker host. Parser targets chỉ đi vào worker executable, không transitive-link vào host.
- Production đọc document/geometry, tạo report/projection; interop nhận projection thuộc contract/document, không include production implementation. Nếu cần dependency mới, sửa registry bằng task riêng.
- UI reload đổi view/presenter binding; không tạo document model thứ hai.
- Shared command/snapshot/types có owner duy nhất. Không để từng agent tạo một bản `Document`/DTO tiện dụng riêng rồi chuyển đổi bằng cast.

### Ownership của runtime khi ghép app

ps01 khóa executor/lifetime contract trong C2/C3/C5; ps07/ps08 chuẩn bị API có thể kiểm bằng executor
được inject. ps13 là composition root tạo các executor thật, gắn services và shutdown. Module không
include desktop để lấy thread pool; host truyền interface/callback theo contract. Implementation
của executor dùng lại từ spike, nằm trong scope đã giao; nếu cần module nền tảng mới thì phải tạo
task registry/contract riêng trước, không giấu dependency trong header tiện ích.

| Vai trò | Trách nhiệm và gate |
| --- | --- |
| UI và logical document writer | Nhận input, validate/commit atomic; prepare nặng không chiếm frame; writer không bị nhân bản theo số worker |
| Render owner | Context/surface/resource đúng thread theo backend đã chọn; chỉ đọc snapshot/index cùng revision |
| Compute executor | Queue bounded, cancellation/generation, ưu tiên tương tác; không một thread vô hạn cho mỗi job |
| I/O executor | Save/export đọc snapshot ổn định, giới hạn jobs/bytes; completion không xóa dirty của revision mới |
| Converter processes | OS containment, IPC/resource validation, crash/restart và cleanup; process không phải một thread thứ tư |
| Shutdown/document close | Ngừng nhận job, hủy/invalidate theo epoch, release resources đúng owner; callback muộn không truy cập document/widget đã hủy |

Budget tổng phải tính cả workers, undo/snapshots, assets và caches. Job queue đầy cần backpressure/
coalescing theo semantics, không bỏ command document đã nhận hoặc tạo retention không giới hạn.
ps13 kiểm pan/zoom trong lúc compute/save/import, đóng tài liệu và thoát app khi jobs đang chạy.

## 4. Vòng lặp UI nhanh và dùng được cho sản phẩm

**Mặc định:** Qt Widgets cho shell; `.ui` cho forms/panels, QSS cho style. C++ giữ window lifecycle, viewport host, services và command routing. Build host lần đầu; sau đó chỉnh layout/style rồi dùng **Reload UI** trong dev. Khi sửa C++/custom widget/API/dependency vẫn build tăng dần.

Qt hỗ trợ đọc forms lúc chạy qua [QUiLoader](https://doc.qt.io/qt-6/quiloader.html); [tài liệu .ui](https://doc.qt.io/qt-6/designer-using-a-ui-file.html) phân biệt runtime loading và tạo code khi build. [QSS](https://doc.qt.io/qt-6/stylesheet.html) dùng để chỉnh style. Version Qt cụ thể do spike/ps00 pin; không mặc định chọn bản mới nhất theo URL tài liệu.

| Thay đổi | Vòng lặp dev dự kiến | Có build C++ không? |
| --- | --- | --- |
| Spacing, màu, typography UI, panel layout, icon file tin cậy | Lưu file → Reload UI → thao tác kiểm tra | Không, khi file được load external trong dev |
| Text/locale catalog | Nạp lại catalog/translation bằng tooling đã chọn | Không đổi C++; có thể cần bước compile resource/translation |
| C++ handler, custom widget, engine/document | Incremental build → chạy lại/smoke test | Có |
| Backend/ABI/dependency/schema runtime | Build các consumer và chạy contract/migration checks | Có |
| Gói release dùng resources đóng gói | Build/package/test từ nguồn đã duyệt | Có bước tạo lại resources/package |

Bố trí dự kiến trong `apps/desktop/`: `src/`, `ui/`, `themes/`, `icons/`, `tests/`, `fixtures/`. ps01 xác nhận naming trước code. `locales/` dùng catalog en/vi theo C7; desktop adapter triển khai sau, không ép thêm module core cho một nhu cầu UI.

Reload phải làm đủ:

1. Đọc file dev từ thư mục được chỉ định, validate object/action IDs và widget allowlist. Không mở native plugin ngoài từ form hoặc môi trường plugin path không tin cậy.
2. Tạo panel tree mới trước; nếu lỗi thì giữ UI cũ, báo lỗi trong dev diagnostics. Không hủy document/viewport/service để thử một form.
3. Giữ view-model state, camera, selection, dock state và field draft hợp lệ. Không reload giữa active drag/IME composition; chờ kết thúc hoặc hủy gesture theo policy rõ.
4. Gỡ connections/bindings cũ, swap trên UI thread, bind đúng một lần; callback cũ được guard lifetime/generation. Không tạo command sửa document trong quá trình swap.
5. Kiểm lại focus/shortcut/dock placement. Không reload toàn main window hoặc GPU surface chỉ vì đổi panel.
6. Release tắt loose-file reload, mock và watcher; dùng resources tin cậy. Cơ chế UI dev không là cửa hậu nạp plugin.

ps05 chạy với fixture presenters cho selection rỗng/có nhiều object, loading/error/cancel/missing asset. ps13 thay bằng services thật và kiểm lại lifecycle. ps05 không được claim đã đạt persistence/render/performance của app.

Ưu tiên nút/manual shortcut Reload trước file watcher để giảm lỗi và code phụ. Chỉ thêm watcher/debounce khi có nhu cầu đo được. Kiểm 50 lần reload không nhân đôi action handler hoặc giữ widget cũ; khi đã ghép services, document/undo/jobs phải còn đúng.

Bản thiết kế tương tác [ui_design_2026.html](../architecture/ui-reference/ui_design_2026.html) (và các biến thể `ui_light_mode.html`, `ui_english_2026.html`) tại `docs/architecture/ui-reference/` là **nguồn tham chiếu giao diện chuẩn bắt buộc** cho ps05 và ps13. Toàn bộ mã màu (Cyber Teal `#00f2c3`, nền đen sâu `#070b10` / `#0c131d`, hairline borders), kích thước chuẩn (Topbar 38px, Context ribbon 34px, Tool strip 48–56px, Dock phải 280–320px, Status bar 26px) và components (Logo gradient PK, Spotlight search Ctrl+K, Pill workspace switcher, quick theme toggle) phải được ánh xạ chính xác sang Qt Widgets (`.ui`) và QSS (`themes/theme-dark.qss`, `themes/theme-light.qss`). Kế hoạch không tạo thêm một editor HTML phải chuyển lại sang Qt, không hứa C++ chạy trong HTML khi chưa biên dịch, và không đưa WebAssembly vào critical path.

## 5. Điều phối 2–3 agent và dispatch task

Catalog không phải scheduler. Harness hiện chưa đọc `depends_on`, chưa tự gọi Claude/Gemini/OpenAI và chưa tự kiểm gate artifact. Coordinator thực hiện lựa chọn; một agent đang có phiên chạy có thể tiếp tục task đủ điều kiện trong phạm vi đã giao. Không cần xin lại phép cho thao tác routine đã được giao, nhưng không tự mở rộng sang milestone/feature ngoài scope.

**Ready** nghĩa là: tất cả dependencies đã tích hợp và được nhận; gate milestone có evidence; contract revision khớp; tooling/fixture có sẵn; owned paths không giao nhau với lease đang giữ. Có report hoặc branch xanh chưa đủ. Provider là nhãn phiên, không phải chủ sở hữu cố định của một module.

### Cách chạy song song an toàn

| Sau khi | Các task có thể chạy đồng thời, tối đa ba implementer |
| --- | --- |
| ps02 | ps03 geometry + ps04 color + ps05 preview |
| ps03 | ps06 typography có thể chạy cùng ps04/ps05 còn lại |
| ps03 + ps04 + ps06 | ps07 document; ps05 có thể tiếp tục nếu chưa xong |
| ps07 | Chọn ba trong ps08 storage, ps09 renderer, ps10 vector, ps11 interop |
| ps11 | ps12 workers có thể chạy với các module M1 còn lại |
| Các prerequisite ps13 đã merge | ps13 tích hợp desktop; không chạy task desktop khác cùng lúc |
| ps13 | ps14 desktop + ps15 vector + ps16 raster |
| ps17 → ps18 | Nghiệm thu/package nối tiếp; fixes quay về task module sở hữu |
| ps19 → ps19a | Khóa production contracts rồi migrate document trước consumer |
| ps19a | ps20 → ps21 trong production; ps22 interop có thể chạy song song |
| ps21 + ps22 | ps23 tích hợp → ps24 nghiệm thu xưởng |

Không bắt task phụ thuộc phải đợi task không liên quan để “đủ một đợt”. Ví dụ ps09 có thể bắt đầu khi ps07 xong dù preview chưa xong. Bảng chỉ minh họa; dependency JSON và lease là nguồn quyết định.

Coordinator không sửa root CMake/registry/contracts cùng lúc với agent sở hữu chúng. Mỗi module tự quản CMake/tests/README dưới thư mục của mình theo interface build ps02. Root build/gates/oracle/state thuộc task điều phối riêng. Tối đa 2–3 implementer còn cần thời gian review/integration; không tính reviewer như công việc miễn phí.

### Worktree cũ và localization

`desktop-layout`, `document-spike`, `interop-spike` và `localization-foundation` đã từng được chuẩn bị từ base foundation cũ. Trước tiếp tục: kiểm `task show`, Git status và active owner; đối chiếu code/commits với catalog; giữ dữ liệu chưa commit. Không chạy chúng như thể đã chứa ADR/contracts mới.

Task localization được người dùng xác nhận đã dừng và lease đã giải phóng ở phiên thiết kế trước; điều đó không cho phép xóa phần chưa commit hoặc bỏ qua một lease mới nếu task được tiếp tục. ps00 kiểm kê; ps01 chốt boundary/keys; ps14 tiếp nhận phần hợp lệ qua commit được review. Nếu phạm vi cũ không khớp, coordinator tạo task thay thế có scope chính xác, ghi liên kết/supersession trong board/handoff; không sửa record JSON trực tiếp để mở rộng quyền.

Các ADR mới chọn số chưa dùng tại thời điểm dispatch. Không tạo lại `0002-localization.md` theo task cũ khi ADR 0002 đã dùng cho runtime/hardware.

### Thủ tục cho coordinator/agent mới

1. Đọc START/state, catalog entry được giao và task board. Nếu A/B/C chưa đủ: tiếp tục task spike/remediation được giao, không bắt đầu ps03.
2. Kiểm gate và dependent **integrated SHA**, reviewer, version; pin base có toàn bộ prerequisites. Ghi contract/fixture/baseline revisions vào goal/context/handoff.
3. Chọn một ready entry; create task với đúng module và owned paths. Xem task cũ cùng ID trước để tránh tạo trùng.
4. Claim bằng owner/session duy nhất; tạo worktree từ pinned base và chuyển vào path trả về. Đọc context/module instructions/skill cần thiết.
5. Implement theo checkpoints; cập nhật board khi có blocker. Nếu phải sửa shared contract hoặc đường dẫn ngoài scope, tạo task phối hợp riêng; không tự ghi sang module bên cạnh.
6. Commit, chạy checks đúng phạm vi, handoff; reviewer độc lập kiểm actual diff và evidence. Integrator kiểm merged SHA rồi cập nhật project-state qua task integration có ownership riêng.
7. Chỉ sau integration mới đánh dấu done. Coordinator chọn task ready kế tiếp; nếu không có thì ghi blocker/next action, không tự hạ gate hoặc claim task chưa ready.

Mẫu **tạo ps00**, dùng tại checkout điều phối sau khi đã kiểm reports và task chưa tồn tại. Đây là các lệnh harness đang có; block không tự xác nhận gate:

```powershell
$planCatalog = Get-Content -Raw docs/roadmap/post-spike-tasks.json | ConvertFrom-Json
$planTask = $planCatalog.tasks | Where-Object id -eq 'ps00-accept-spikes'
$planBase = (git rev-parse HEAD).Trim()
$planOwner = 'codex-post-spike-' + [guid]::NewGuid().ToString('N')
$planArgs = @('task', 'create', $planTask.id, '--title', $planTask.title,
  '--module', $planTask.module, '--goal', ($planTask.deliverables -join ' '),
  '--provider', 'codex', '--base', $planBase)
foreach ($planPath in $planTask.owned_paths) { $planArgs += @('--path', $planPath) }
& .\tooling\pikdraw.ps1 @planArgs
if ($LASTEXITCODE -ne 0) { throw 'Task create failed; inspect the board.' }
& .\tooling\pikdraw.ps1 task claim $planTask.id --owner $planOwner --provider codex
if ($LASTEXITCODE -ne 0) { throw 'Claim failed; do not create or edit a worktree.' }
$planWorktreeJson = & .\tooling\pikdraw.ps1 worktree create --task-id $planTask.id --provider codex --owner $planOwner --base $planBase
if ($LASTEXITCODE -ne 0) { throw 'Inspect worktree status before retrying.' }
$planWorktree = $planWorktreeJson | ConvertFrom-Json
Set-Location -LiteralPath $planWorktree.path
& .\tooling\pikdraw.ps1 context --task-id $planTask.id
```

Không chạy block này cho toàn bộ catalog một lần. Các entry sau phải được kiểm gate/base/scope lại khi dispatch. Owner/provider trong ví dụ thay theo phiên thực tế; không gán nhãn nhà cung cấp như bằng chứng họ đã được gọi.

## 6. Demo bắt buộc khi đóng M1

Một fixture tổng hợp, có quyền dùng, khổ **20.000 × 1.000 mm** với shapes, đường Bézier, chữ “PikDraw — Biển hiệu Ánh Sáng”, font fixture, ảnh nhỏ và metadata màu. Fixture correctness có analytic references; bộ stress 100.000+ nodes riêng theo A, không dùng một file nhỏ để chứng minh hiệu năng.

1. Mở app native, tạo/nhập tài liệu subset đã nhận, đặt đơn vị mm; kiểm page extent và provenance/report.
2. Tạo shape và chữ; chọn, kéo, nhập tọa độ/kích thước, đổi fill/stroke; thao tác không làm lệch đơn vị hoặc mất Unicode.
3. Undo/redo chuỗi trên; so geometry/style/hierarchy/source bằng semantic oracle, không chỉ ảnh.
4. Pan/zoom trong lúc save/compute/import chạy; hủy hoặc sửa source trước khi job trả về; kết quả cũ bị từ chối.
5. Save r rồi edit r+1; UI vẫn dirty khi save r hoàn tất. Mở lại native và kiểm IDs, curves/text/fonts/colors/assets.
6. Inject crash/fault ở checkpoint đã định; mở bản phục hồi riêng, ghi rõ cửa sổ edits có thể mất. Không ghi đè file gốc.
7. Xuất một format đã chọn từ C/ps00 (mặc định SVG subset nếu phù hợp); mở bằng oracle độc lập, kiểm extents/units và loss report.
8. Chạy forced software và lỗi GPU init/recoverable loss; UI cho biết preview mode, dữ liệu nguồn không đổi.
9. Đổi workspace/theme, dev reload panel; document/history/camera không bị khởi tạo lại. Workspace chưa có service vẫn là placeholder được ghi rõ.
10. Lặp workload stress đã nhận trên Intel/AMD 8 GB và RTX; đo lại full app gồm UI/history/assets/workers, không lấy số của spike thay thế.

M1 chưa phải release cho xưởng. M2 thêm usability, nodes/Boolean/raster, soak và package; M3 mới kiểm output production với preset/operator cụ thể.

## 7. Definition of Done và xử lý lỗi

Một task chỉ done khi đáp ứng đồng thời:

- Deliverables của entry có implementation/artifact thực tế; README ghi build/run và limits. Code dùng lại từ spike có provenance.
- Diff chỉ nằm trong owned paths, actual dependencies đúng registry; không đổi tests/oracle/gate chuẩn ngoài scope. C++ module không lén link parser vào host.
- Contract vectors của producer/consumer liên quan qua; command/undo/cancel/source preservation được kiểm theo rủi ro. Test không chỉ gọi rồi kiểm không crash.
- Validation đúng revision, reviewer độc lập ghi findings và cách xử lý; khi merge đổi tree phải chạy lại các checks liên quan trên merged SHA.
- Evidence/handoff có base/head/integrated SHA, commands, kết quả, artifact paths/hashes, machine/toolchain/dependency/fixture metadata và known limits.
- Integrator cập nhật state theo năng lực thực sự đã tích hợp; `planned` không chuyển `implemented` chỉ vì có class header hoặc screenshot.
- Board ghi next action đủ để phiên mới tiếp tục; lease được xử lý theo protocol, không takeover theo timeout.

**Gate chặn ngay:** data loss, sai đơn vị/geometry không report, stale commit, half-transaction, save/recovery corruption, migration phá file cũ, permission escape, parser chạy nhầm host hoặc benchmark bắt buộc thiếu/fail. Missing hardware là `not_measured`, không phải pass.

**Fix loop:** reviewer ghi minimal reproducer/fixture và gate fail → coordinator tạo task fix trong module sở hữu → implementer sửa → rerun checks bị ảnh hưởng → review/integrate → cập nhật evidence. Không sửa budget/expected fixture cùng feature fix để che regression. Cần sửa oracle thật thì có task riêng và reviewer khác.

Artifact nhỏ/manifest/report được lưu ở vị trí task sở hữu; raw data lớn, build artifacts và private files ở storage ngoài Git hoặc `.agent/` phù hợp, kèm checksum và cách truy cập cho reviewer. Handoff cần phân biệt local-only artifact với artifact đã chia sẻ. Không commit binary/customer data/secrets/full chat logs.

### Kiểm thử theo lớp

| Lớp | Nơi dự kiến | Ví dụ kiểm chứng |
| --- | --- | --- |
| Module | `<module>/tests/` | Geometry analytic, transaction/history, parser limits, typography/color |
| Contract consumer | Module tests đọc fixture chuẩn | Cùng transform/revision/errors/cancel vectors, không clone oracle |
| Boundary/build | `tests/architecture/` | Include/link graph, scope/paths, parser host-link, version/coverage |
| E2E | `tests/integration/` | UI thật → services → storage/worker/output; không dùng mock để đóng mốc |
| Hardware/stability | Runner + report đã pin | Present/input percentiles, memory retention, 8 giờ replay, device/process faults |
| Production acceptance | `tests/acceptance/production/` | PDF/RIP/DXF độc lập và đo/operator ở xưởng |

Sanitizer chỉ dùng ở toolchain hỗ trợ, ghi coverage còn thiếu. Không ép một sanitizer không khả dụng trên Windows thành lệnh giả pass; dùng cấu hình bổ sung được review và stress/lifetime tests. Hardware gates có thể chạy trên máy tham chiếu riêng thay CI hosted; vẫn phải gắn revision/artifact và chặn milestone nếu thiếu.

## 8. Build/run/test mà ps02 phải cung cấp

**Các lệnh native dưới đây là giao diện tooling cần triển khai, chưa chạy được ở foundation hiện tại.** ps02 phải viết và kiểm runner/presets; cập nhật `docs/development/native-build.md` nếu tên/flags thay đổi. Toolchain prerequisites được khóa từ spike, không tự cài ngẫu nhiên dependency mới.

```powershell
# Sau khi ps02 triển khai presets; task tương ứng đã có native target.
cmake --preset win-dev
cmake --build --preset win-dev
ctest --preset win-dev --output-on-failure

# Sau ps05: chạy shell với fixture presenters và external UI assets.
.\tooling\native\dev.ps1 -Mode Preview -Configuration Debug -Scenario basic

# Sau ps13: app dùng services thật; phần mềm raster được kiểm riêng.
.\tooling\native\dev.ps1 -Mode Editor -Configuration Debug
.\tooling\native\dev.ps1 -Mode Editor -Configuration Release -ForceSoftware

# Các suite phải fail rõ khi thiếu target/tests bắt buộc.
.\tooling\native\check.ps1 -Suite architecture -Configuration Debug
.\tooling\native\check.ps1 -Suite contract -Configuration Debug
.\tooling\native\check.ps1 -Suite e2e -Configuration Release
```

`win-dev`/Release preset dùng dependency versions đã pin, output/cache tách theo worktree/config. Dev runner không build lại dependency toàn bộ mỗi lần thay QSS. Không dùng số đo Debug/sanitizer để chứng minh FPS Release.

Native suite phải có danh sách test bắt buộc cho milestone và báo test count/skips; “không tìm thấy test” không là pass. Benchmark report tách từng máy/scenario/run, có present/input/raw data. Không thêm fallback runner chỉ in “OK” khi executable chưa tồn tại.

Harness baseline vẫn là các lệnh **đang có**:

```powershell
.\tooling\pikdraw.ps1 doctor
# Dùng Python runtime của môi trường; launcher pikdraw.ps1 không có subcommand unittest.
python -m unittest discover -s tests/harness -v
```

Nếu alias `python` không hoạt động, dùng executable Python đã tìm thấy ở môi trường. Harness pass chỉ chứng minh harness, không chứng minh app.

## 9. Ranh giới sau Production Beta

M4 mở bằng một task thiết kế/spike riêng cho 3D: extrusion từ source 2D, holes, winding/normals, đơn vị/độ dày, invalidation khi source đổi, GPU/software capability và ngân sách RAM/viewport. Dữ liệu linked source được giữ trong document chung; mesh là derived data. Chỉ code workspace 3D rộng hơn sau khi spike này được nhận.

Plugin ngoài cần SDK versioning, host-enforced capabilities, compatibility/migration và containment thực tế trước mở untrusted code. UI plugin đóng góp bằng host components/commands; không dùng native Qt Designer plugin để đi vòng process isolation.

CAM/G-code/postprocessors, nesting tối ưu, full CAD constraints, mô phỏng quang học, native CDR export và AI authoring round-trip tiếp tục deferred. Không suy ra những năng lực này từ việc đã xuất DXF hoặc dựng một hình extrusion.

**Bước đầu tiên khi A/B/C hoàn tất:** coordinator nhận `ps00-accept-spikes`, kiểm evidence và chốt baseline. Sau ps00 → ps01 → ps02, mở ngay geometry, color và native preview theo lease; không cần viết lại một kế hoạch triển khai mới.
