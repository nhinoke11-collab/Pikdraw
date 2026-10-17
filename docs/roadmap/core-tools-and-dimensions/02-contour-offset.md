# Plan 02 — Contour / Offset có tham số và kiểm topology

Ngày: 17/10/2026. Trạng thái: **Planned**; general offset chưa được source hiện tại hỗ trợ.
Đọc [master](../master-implementation-plan-core-tools-and-dimensions.md), nhất là baseline, leases và interop.
Mã gói `C`; thay phần Contour của master cũ và phần offset thuộc D16, không nhận toàn bộ Boolean/CAM.

## 1. Kết quả và phân kỳ capability

Người dùng chọn closed source path, nhập khoảng cách thật, hướng và số bước, xem preview đúng hình học,
Apply thành contour có thể sửa lại; sửa source làm contour cập nhật. Break Apart tạo paths độc lập,
Undo khôi phục liên kết và style. Kết quả phải có holes/components đúng, không dùng bbox hoặc stroke dày giả contour.

| Đợt | Phạm vi bắt buộc | Chưa mở trong đợt |
| --- | --- | --- |
| C-native-1 / C00–C04 | Closed simple/compound regions đã chuẩn hóa; Outside/Inside, 1–99 steps, Miter/Round/Bevel, source sống, native persistence | Open one-sided parallel curve, tự sửa self-intersection nguồn, To Center chưa có termination gate |
| C-native-2 / C05 | To Center bounded, Break Apart, edit-source lifecycle, topology/loss reporting đầy đủ | Color progression/acceleration, text live contour, multi-source group union và modifier chains là task mở rộng rõ ràng |
| C-open / C06 | Open source: Stroke Outline với caps; one-sided offset của line/circular arc có analytic oracle riêng | General Bézier one-sided offset chưa được chứng minh thì giữ unsupported |

Không gọi toàn bộ Contour hoàn tất từ C-native-1. Bản gốc không mô tả open path rõ; plan này phân biệt
offset vùng kín, stroke outline và đường song song một phía để agent không gọi cùng thuật toán sai nghĩa.
Không làm tự bù dao, CAM/G-code, kerf/material compensation, 3D Shell hoặc chứng nhận máy cắt.

## 2. Tham khảo chính thức và khác biệt cần giữ

| Nguồn đã tra | Điều dùng để thiết kế |
| --- | --- |
| [CorelDRAW 2026 Contour objects](https://help.coreldraw.com/CorelDRAW/540111192/Documentation-Windows/CorelDRAW-en/CorelDRAW-Contour-objects.html) | Contour vào/ra, khoảng cách, số bước, các kiểu góc và tách contour; PikDraw không nhận các tính năng màu/clone chưa triển khai |
| [AutoCAD 2026 OFFSET](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-Core/files/GUID-C0E4246D-C420-42BD-A6FC-8B1852EFD005.htm) | Offset distance/side/through cho geometry; phân biệt tạo bản offset với modifier sống |
| [Clipper2 InflatePaths](https://www.angusj.com/clipper2/Docs/Units/Clipper/Functions/InflatePaths.htm) | API polygon/polyline; không phải giải offset Bézier analytic |
| [ClipperOffset notes](https://www.angusj.com/clipper2/Docs/Units/Clipper.Offset/Classes/ClipperOffset/_Body.htm) | Winding/holes, self-intersection closed source, open-path inflation và order output cần xử lý tường minh |
| [Clipper2 Robustness](https://www.angusj.com/clipper2/Docs/Robustness.htm) | Integer rounding có thể loại chi tiết rất nhỏ; không được hứa tuyệt đối không sliver chỉ vì dùng thư viện |

Range/default/error budgets bên dưới là **đề xuất kỹ thuật PikDraw**, không phải chuẩn của Corel/AutoCAD/Clipper2.

## 3. Source cần đọc và reuse

- `core/geometry/src/operations/offset.cpp`: hiện chỉ exact axis-aligned rectangle, Miter; keep regression hiện có.
- `core/geometry/include/pikdraw/geometry/operations/offset.hpp`: OffsetResult chỉ một Path2D và lỗi đơn giản;
  không đủ phân loại empty-success, nhiều components, error bounds, provenance và work budget.
- `core/document/.../model/editable_path.hpp`, `.../object.hpp`, `.../commands/path_commands.hpp`:
  authoritative source/IDs đã có; chưa có persisted live ContourFeature trong ObjectVariant hiện đọc.
- C1/C2/C3/C5/C8/C9, geometry/document/vector/rendering/interop AGENTS và README; dependency theo registry.
  `Effects > Contour` trong native menu đang disabled. Existing Appearance có blur/shadow là pixel filters,
  không phải topology modifier để xuất đường sản xuất.

Không thêm Clipper2 trước C00: pin release/commit/hash, license/notices, build integration và API wrapper
trong ADR có consumer review. Candidate được nghiên cứu, không có dependency đã được phê duyệt chỉ từ plan này.

## 4. Semantics và pipeline

### 4.1. Hình học đích

1. Đọc immutable source snapshot, kiểm eligibility/finite/range/transform, bản đồ outer/hole từ winding của vùng filled.
   Khoảng cách tính bằng world mm **sau composite transform**, không offset local rồi scale không đều.
   Không lấy bounds/effects halo làm boundary; nguồn line/arc/Bézier vẫn giữ nguyên.
2. Closed region: Outside nới vùng vật liệu, nên outer nở và hole co; Inside ngược lại.
   Hướng không suy bằng clockwise của một path đơn vì reflection/EvenOdd có thể đổi orientation.
   Compound/hole classification phải có oracle; cả NonZero và EvenOdd map đúng filled set trước kernel.
3. Bước k lấy từ **source gốc** ở khoảng `signed_distance = k * spacing`, k=1..steps;
   không offset kết quả bước trước gây tích lũy error/artifacts. Miter limit = chiều dài miter / |distance|,
   dùng fallback Bevel khi vượt limit. Round là cung join radius |distance| tại convex polygon corners,
   không phải lời hứa mọi đoạn đường là cung dao phay.
4. Offset có thể tách/ghép components, mất hole hoặc hết vùng khi inward. Trả `Success`, `Empty`,
   `InvalidInput`, `Unsupported`, `TopologyAmbiguous`, `WorkBudgetExceeded`, `Cancelled`; Empty là kết quả hợp lệ,
   không tạo zero-area fake path. Preview báo bước dừng/collapse; source vẫn còn.
5. Self-intersecting closed source: ban đầu reject với diagnostic, không tự Union nguồn cho người dùng.
   Gói bổ sung có thể normalize trên **bản dẫn xuất** theo fill rule, có report và bounded oracle;
   không làm thay đổi source hay gọi đã giữ topology gốc.
6. To Center là chuỗi inward `k*d` đến Empty; không tính medial axis hoặc skeleton. Có trần 99 bước,
   output/CPU budget và cờ `Truncated`; nếu hết trần trước empty thì hiện chưa tới tâm, không ghi complete.
   Plateau/degenerate width phải dừng có lý do. Requested steps disabled khi dùng To Center.

### 4.2. Source-preserving approximation

Ưu tiên analytic line/circular arc cases có oracle; general curves được derive polygon có sai số công bố.
Không gọi đường offset Bézier là một cubic exact. Source cubic/ellipse vẫn analytic/editable;
Break Apart tạo polyline/arc theo representation thực và báo approximation đã áp dụng.

Đề xuất profile C00 cho tài liệu signage: tổng error mục tiêu ≤0.005 mm trên **catalog đã gate**;
flatten ≤0.001 mm, join tessellation ≤0.001 mm, quantization grid q=0.00001 mm,
phần kernel/conditioning còn lại phải đo và kiểm, không suy tổng bound bằng cộng q tùy tiện.
Adaptive flatten theo world curvature, depth/segment cap; không dùng zoom-dependent LOD.
Đây không phải manufacturing tolerance hoặc chứng nhận đúng trên mọi shape. Gần cusp, near-tangent,
chi tiết hẹp hoặc topology chưa phân giải trong budget: reject/report, không giảm quality để pass.

Wrapper chuyển sang integer bằng checked scale, kiểm bounds sau mở rộng và miter trước allocate;
đề xuất conservative `|scaled coordinate| ≤ 10^12`, target tối đa 1,000,000 segments toàn request,
99 steps và 128 MiB scratch dự kiến, validate admission trước compute. C00 đo peak và cập nhật nếu cần bằng evidence;
không hứa trần RAM ứng dụng bằng riêng scratch. Library result coordinates/order không cấp stable source IDs.
Mỗi output loop có key dẫn xuất theo feature/step/component, không lấy thứ tự vector kernel làm identity lâu dài.

### 4.3. APIs và persisted feature (đề xuất, C02 chốt)

```text
OffsetRequest: canonical curves/filled-region + world transform + side/distance/steps/join
               + tolerance profile + budgets + cancellation token
OffsetResult: status + regions/components/holes + approximation report + algorithm version
              + work stats + source-to-output attribution where provable
ContourFeature: stable feature ID + schema + source attachment + typed parameters + output paint
                + tolerance profile/algorithm version; source geometry remains authoritative
```

Đợt đầu một feature gắn PathObject source, không reference tới contour khác; graph cycle không có đường tạo.
Nếu mở source links đa object/chain phải có cycle/depth validation và task schema riêng.
Data/parameters ở document, kernel ở geometry. Một evaluator read-only trung lập trong document
gọi geometry để renderer/vector/interop dùng được mà không phụ thuộc ngược `modules/vector` từ rendering.
Vị trí API cụ thể chốt C02/ADR theo registry; không để Clipper types rò vào model/storage.
Contour không được nhét vào blur/shadow stack để che mất geometry output; thứ tự source → geometry contour
→ paint/appearance phải rõ, derived paths có own paint; source paint giữ nguyên. C9 kiểm scope/order/loss.

Cache key: document epoch/revision, source IDs/source hash/composite transform, feature params,
tolerance, algorithm/build version. Pan/zoom không đổi source-derived geometry; chỉ cache raster phụ thuộc camera.
Đổi source/style/transform/undo/delete → invalidate dependency đúng phạm vi; stale preview không được publish.
Worker compute không sửa document. Apply chỉ nhận proposal đúng epoch/base revision/job/target set;
cancel/new job tăng generation trước cleanup; lỗi output quota không giữ kết quả một phần.

Save native giữ source+parameters+IDs+version và output representation policy được C5 review.
Không tự lấy version `v5`: đọc writer hiện tại trước chọn migration. Unknown required feature/version
phải reject toàn load/save hoặc preserve opaque theo capability được chứng minh, không drop contour.
Khi reopen, cache có thể rebuild nhưng engine/version không có phải báo unsupported, không đổi contour ngầm.

Break Apart: một atomic command materialize output hợp lệ thành PathObjects IDs mới, giữ source object/style;
bỏ live feature khỏi source, paths độc lập có layer/order/paint rõ; Undo phục hồi feature và đúng IDs đã cấp.
Không đọc output từ screenshot. Export/Break không dùng preview hạ chất lượng.

### 4.4. Open path không được làm giả

`Stroke Outline`: open centerline + positive half-width tạo region hai phía, caps Butt/Round/Square;
negative inflation không có nghĩa inward open curve. UI phải gọi đúng `Outline`, không gọi `Left offset`.
`Parallel Curve`: side Left/Right theo traversal, giữ open path và endpoints; C06 chỉ analytic line/circular arc
trước, general Bézier chờ kernel spike riêng. Closed region Inside/Outside không áp cho open path.

## 5. Phiếu UI

Menu chính `Effects > Contour…`, proposed `action.effects.contour.focus` mở Properties > Contour.
Tool tương tác `action.tool.contour` nằm trong **Appearance flyout hiện có**, chỉ thêm entry đủ capability;
không dựng group Extrude/3D mới. Không thêm standard toolbar hoặc floating form trên canvas.
Break Apart trong `Object > Break Contour Apart`, `action.object.contour.break_apart` là mutation khác action focus.
Chưa gán Ctrl+F9/Ctrl+K mặc định; chỉ dùng menu/Properties cho đến audit binding và tests.

| Field ID / EN — VI | Vị trí | Kiểu/range/default, scope | State / commit |
| --- | --- | --- | --- |
| `contour.direction` / Direction — Hướng | Bar + Properties > Geometry | Outside/Inside/To Center; default Outside; draft/selected feature | To Center chỉ C05; source open dùng mode khác |
| `contour.spacing` / Distance — Khoảng cách | Bar + Geometry | Finite 0<d≤100000 mm và expanded bounds hợp C1; default 1 mm; draft/feature | Nhập unit theo parser hiện có; preview; Apply một command |
| `contour.steps` / Steps — Số bước | Bar + Geometry | Integer 1..99, default 1 | To Center read-only count thực; requested truncation báo rõ |
| `contour.join` / Corners — Góc | Bar + Corners | Miter/Round/Bevel; default Miter | Same presenter; miter limit chỉ enable Miter |
| `contour.miter_limit` / Miter limit — Giới hạn góc | Properties > Corners | Finite 1..100, default 4 | Invalid không clamp ngầm; fallback Bevel đúng ratio |
| `contour.quality` / Precision — Độ chính xác | Properties > Precision | Named profile C00; default 0.005 mm target catalog | Không thay bằng preview quality; report nguồn/kết quả/read-only residual |
| `contour.paint` / Fill, Stroke — Tô, Viền | Properties > Appearance | Typed source colors/width hiện hỗ trợ; default copy eligible source | New output paint riêng; mixed multi-selection không lấy object đầu |
| `contour.preview` / Preview — Xem trước | Properties | Bool true; interaction preference | Off chỉ tắt transient result; không đổi feature đã commit |
| `contour.open_mode/cap` / Open path — Đường hở | Properties > Geometry | Outline/Parallel và cap chỉ C06; default theo tool mode đã chọn | Unsupported side/curve disable, không áp closed semantics |

Chưa selection: Properties ghi `New contour defaults`, không giả sửa tài liệu. Chọn feature: chỉnh tham số hiện có,
không append contour mới mỗi lần focus. Nhiều features: common/mixed fields qua batch; phase đầu không hỗ trợ thì
disable có lý do và yêu cầu chọn một source. Locked/read-only/reference/bitmap/text live disable.
Bar giữ direction/distance/steps/join; toàn bộ tham số hỗ trợ có tại Properties, sections Geometry/Corners/Appearance/Precision.
Nút `Contour properties` mở lại đúng section khi panel đóng/bar hẹp. Apply/Cancel/Esc có thật, same validation/Undo;
selection/tool/tab đổi trong draft thì hủy transient preview, không commit. Focus sang panel giữ draft.

SVG đề xuất `contour.svg`, `contour-inside.svg`, `contour-outside.svg`, `contour-break-apart.svg`;
reuse icon registry nếu có, assets dưới `apps/desktop/resources/icons/`, nét/theme/license theo hệ hiện tại.
Message IDs `ui.contour.*`; nhãn `Contour/Đường bao`, `Distance/Khoảng cách`, `Steps/Số bước`, `Apply/Áp dụng`.
Tooltip/focus `Contour — Create editable parallel outlines / Đường bao — Tạo đường bao có tham số`.
Status theo phase: `Select a closed path / Chọn đường kín` → `Adjust distance; Apply or Esc / Chỉnh khoảng cách; Áp dụng hoặc Esc`.
Busy có Cancel, error có lý do gần field; không hiện job IDs/kernel names trong product flow.

Help `Help > Contour`, `action.help.contour`: chọn source → chọn hướng/distance/steps → preview holes/collapse
→ Apply và sửa source → Break Apart/Undo; nêu khác biệt outline/parallel, approximation và CNC chưa chứng nhận.
Tab order/focus, numeric caret/copy/IME, light/dark, DIP và overflow theo chuẩn; không ép panel luôn mở.

## 6. Task và owned paths

Mọi task **planned**. Path mới là đề xuất. Shared CMake/contracts/storage/shell cần lease riêng tuần tự theo master.

| Task / phụ thuộc | Owned paths candidates | Đầu ra và gate |
| --- | --- | --- |
| C00 / baseline | New `core/geometry/tests/operations/test_offset_corpus.cpp`; `docs/decisions/` đúng ADR mới; plan này | Spike kernel/version/license/range, independent oracle và profile; không enable UI từ spike |
| C01 / C00 | `core/geometry/include/pikdraw/geometry/operations/offset.hpp`; `core/geometry/src/operations/offset.cpp`; new offset helper files cùng thư mục; geometry tests/CMake/dependency config đã scope | Closed regions/holes, structured results/limits/cancel, no bbox fallback; existing rectangle tests giữ |
| C02 / C00 | New document model/commands/evaluator `contour*`; shared object/validation/clone/snapshots/storage/CMake; C2/C3/C5/C9 theo delta cụ thể | Native source feature, schema migration, neutral evaluation, consumer review; không overlap D00/D01 |
| C03 / C01+C02 | New `modules/vector/include/pikdraw/vector/tools/contour/`, `modules/vector/src/tools/contour/`, tests; rendering source adapters/evaluation integration theo exact paths | Controller preview/Apply/lifecycle, renderer derived bounds/hit-test/culling; new regression target |
| C04 / C03 | New desktop contour controller; shared native_window/menu/actions/icons/localization/properties và desktop tests/CMake | Native author/edit/cancel UI, Properties/help, no duplicate feature on focus |
| C05 / C04 | Feature paths, contour commands/storage/renderer tests; plan evidence | To Center bounded, Break Apart, native persistence, original star +20 mm gate, stale/delete/Undo |
| C06 / C01+C05 | Geometry open-offset/outline helpers + tests, contour UI controls đã scope | Open outline và analytic one-sided offset gate; label đúng loại kết quả; general one-sided cubic vẫn chưa bật nếu chưa đạt |
| C07 / C05+C06 | Plan, capability evidence; state sau lease riêng | Review catalog/production limitations, source/exe hash và handoff |

C00 có stop condition: kernel không đạt fixture topology/range/error hoặc license/build chưa chốt thì báo outcome,
không chuyển sang C04 với một bản chỉ phồng bbox. C02 schema phải review trước UI live-feature.

## 7. Nghiệm thu theo luồng dùng thật

| ID | Given | When | Then | Verify |
| --- | --- | --- | --- | --- |
| AC-C01 | Rectangle 100×60 mm, reflected/nonuniform transform fixture riêng | Outside 10 mm Miter; Inside 30 mm | World dimensions đúng; inward có thể Empty hợp lệ, source giữ | Analytic rectangle oracle, không chỉ bounds với hình cong |
| AC-C02 | Star đơn 10 đỉnh, outer radius 100/inner 40 mm | Outside 20 mm Round | Filled region/components đúng, convex join R=20 theo profile; không có self-crossing ngoài rule | Independent distance/segment-intersection + raster topology oracle; hình native để review |
| AC-C03 | Donut outer R50, hole R10, NonZero/EvenOdd và reflected variants | Outside 2 mm; Inside 2 mm | Outer/hole đổi đúng phía, winding/holes giữ nghĩa | Circle analytic measurements + region membership samples |
| AC-C04 | Acute corner và self-crossing bow-tie | Miter limit 2; áp trên source không supported | Acute bevel đúng ratio; bow-tie reject/report, không sửa nguồn | Geometry status, no revision/output half-result |
| AC-C05 | Bézier/ellipse nguồn + source gần giới hạn C1 | 3 bước d=2 mm rồi zoom 100→800% | Mỗi bước từ source; error trong profile catalog; output world không đổi theo zoom | Independent dense/high-precision distance oracle, source segment types giữ; admission range cases |
| AC-C06 | Narrow regions và To Center d=1 mm | Đến Empty hoặc hết 99 bước | Empty đúng; trần trước empty hiện Truncated, không infinite loop | Bounded counts/time/cancel trace, no final-step fake path |
| AC-C07 | Live contour đã Apply | Kéo node/scale source, Undo, delete source, Undo rồi save/reopen | Derived result theo revision; feature/source/IDs/params/style khôi phục | Document/renderer/storage integration, no stale cache |
| AC-C08 | Preview chậm và thay distance/source/tab | Cancel hoặc result cũ trả về | Không publish preview cũ/commit; Apply chỉ đúng current parameters | Worker/command lifecycle tests; source revision unchanged khi cancel |
| AC-C09 | Selected live feature và Properties đóng/bar hẹp | Mở panel, sửa Miter, Apply, Break Apart rồi Undo | Không thêm feature khi focus; bar đồng bộ; materialize paths IDs đúng và restore live link | Native shell + storage tests, source snapshot |
| AC-C10 | Open L polyline và analytic arc | Outline với Round caps, rồi Parallel Left/Right | Outline closed region khác parallel open curve; negative inflate không giả inward | Independent cap/side oracle, unsupported cubic branch có diagnostic |
| AC-C11 | Large request vượt segment/scratch cap | Preview và export/materialize | Reject/cancel bounded; không giảm error target hay ghi output một phần | Resource/fault tests; destination preservation nếu có export |

Lệnh hiện có cho geometry (CTest name khác target):

```powershell
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_geometry -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^pikdraw_geometry_tests$'
# Các target sau là đề xuất, chỉ chạy sau đăng ký CMake:
./tooling/native/build.ps1 -Configuration Debug -Test -TestTarget test_pikdraw_document_contour,test_pikdraw_vector_contour,test_pikdraw_desktop_shell -BuildOnly
./tooling/native/build.ps1 -Configuration Debug -TestsOnly -TestRegex '^(test_pikdraw_document_contour|test_pikdraw_vector_contour|test_pikdraw_desktop_shell)$'
./tooling/native/build.ps1 -Configuration Debug
```

Thay C2/C3/C5/C9 cần thêm storage/appearance/render/culling/interop regressions đúng consumers. Khi kernel/dependency mới,
verify Debug và Release trên source mới; không dùng binary có từ trước. Đo CPU compute/query, peak scratch,
cancel latency và UI stalls riêng; mục tiêu interactive preview p95 ≤33.3 ms trên fixture khai báo là gate cần đo,
không là FPS. Nếu chưa đạt, cho cancellable preview có Busy và giữ performance gate mở.
Manual native: focus, drag, narrow window/DPI/themes như master. Máy thật/CNC ngoài nhận native; không gọi star gate là cut acceptance.

## 8. Ma trận format/representation/feature và bàn giao

| Format | Xem/render | Edit và metadata | Export / sản xuất |
| --- | --- | --- | --- |
| Native version mới được C02 review | Source + derived contour qua evaluator đã gate | Live params/source IDs/styles/algorithm version; save/reopen | Break Apart có approximation report; chưa chứng nhận machining |
| SVG static 1.0/1.1 subset geometry | Adapter riêng normalize source trước contour; filter không tự map | Imported source editable chỉ subset; live SVG filter không được gọi native contour | Export derived paths chỉ sau explicit expand/loss report và writer fixture; không live-contour round-trip |
| ASCII DXF `dxf-entities-v1` geometry | LINE/ARC/CIRCLE/LWPOLYLINE profile; không nhận hatch/block/3D ngoài profile | Units/source map và analytic forms theo fixtures | Chưa DXF writer; offset paths không tự unlock CNC |
| PDF, AI authoring, AI PDF-compatible, CDR versions, EPS | Hiện reject/no adapters; native authoring không cần các format này | Không có claim import/effect/provenance đủ | Export PDF/CDR/AI/EPS ngoài scope; mỗi adapter/version phải có catalog C8/C9 riêng |

Chỉ share normalized geometry/commands/render semantics; không dùng converter chung làm bằng chứng fidelity.
Appearance/effect scope theo C9; không flatten paint/metadata ngầm để contour được. Handoff ghi kernel pin/license,
error/profile/quotas đo được, fixtures, source/exe hashes, commands/results và các rows chưa nhận.
